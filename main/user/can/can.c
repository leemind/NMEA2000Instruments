#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <esp_log.h>
#include <cJSON.h>

#include "ui.h"
#include "can.h"          // Include the CAN driver for communication
#include "pgn_json_parser.h"  // Include PGN parser for decoding NMEA2000 messages
#include "lvgl_port.h"        // LVGL mutex for thread-safe UI access from CAN task
#include "screens/ui_Wind.h"  // UI objects (ui_Heading, ui_AWS, etc.)

TaskHandle_t can_TaskHandle;

static char can_data[2 * 1024] = {0};  // Buffer for receiving data
static cJSON *pgn_database = NULL;  // Global PGN database
static const char *TAG = "CAN_DECODER";

/**
 * @brief Extract PGN number from NMEA2000 CAN identifier
 * 
 * NMEA2000 CAN ID structure (29-bit):
 * - Bits 28-26: Priority (3 bits)
 * - Bit 25:     Reserved (1 bit)
 * - Bit 24:     Data Page (1 bit)
 * - Bits 23-16: PDU Format / PF (8 bits)
 * - Bits 15-8:  PDU Specific / PS (8 bits)
 * - Bits 7-0:   Source Address (8 bits)
 * 
 * PGN is 18 bits = Reserved(1) + DataPage(1) + PDUFormat(8) + PDUSpecific(8)
 * extracted from bits [25:8] of the 29-bit CAN ID:
 * - PDU1 (PF < 240):  PS is a destination address, zeroed out in PGN
 * - PDU2 (PF >= 240): PS is a group extension, included in PGN
 */
static uint32_t extract_pgn_from_can_id(uint32_t can_id)
{
    uint8_t  pdu_format  = (can_id >> 16) & 0xFF;   // bits 23:16
    uint8_t  pdu_specific = (can_id >> 8) & 0xFF;   // bits 15:8
    uint32_t dp_r        = (can_id >> 24) & 0x03;   // Reserved + Data Page, bits 25:24

    if (pdu_format < 240) {
        // PDU1: PS is destination address — not part of the PGN
        return (dp_r << 16) | ((uint32_t)pdu_format << 8);
    } else {
        // PDU2: PS is group extension — included in the PGN
        return (dp_r << 16) | ((uint32_t)pdu_format << 8) | pdu_specific;
    }
}

/**
 * @brief Extract priority from NMEA2000 CAN identifier
 */
static uint8_t extract_priority_from_can_id(uint32_t can_id)
{
    return (can_id >> 26) & 0x07;
}

/**
 * @brief Extract source address from NMEA2000 CAN identifier
 */
static uint8_t extract_source_from_can_id(uint32_t can_id)
{
    return can_id & 0xFF;
}

/**
 * @brief LVGL animation executor callback for compass rose rotation.
 *
 * The animated value may temporarily exceed 0-3599 when crossing the North
 * boundary, so we normalise with modulo before setting the angle.
 */
static void img_angle_anim_cb(void *obj, int32_t angle)
{
    angle = ((angle % 3600) + 3600) % 3600;
    lv_img_set_angle((lv_obj_t *)obj, (int16_t)angle);
}

/**
 * @brief Dispatch a decoded PGN to the appropriate fixed UI element.
 *
 * This function handles the special-case PGNs whose destination UI element
 * is fixed/hardwired in the screen design.  Add a new case here for each
 * additional fixed PGN you want to display.
 *
 * All LVGL writes are wrapped in lvgl_port_lock() because this runs in the
 * CAN task, not in the LVGL task.
 *
 * @param pgn      18-bit NMEA2000 PGN
 * @param data     Raw CAN data payload bytes
 * @param data_len Number of payload bytes
 */
static void handle_pgn_fixed(uint32_t pgn, const uint8_t *data, int data_len)
{
    switch (pgn) {

    case 127250: {
        /* Vessel Heading
         * Byte 0     : SID (sequence ID)
         * Bytes 1-2  : Heading, uint16 LE, resolution = 0.0001 rad/LSB
         * Bytes 3-4  : Deviation  (ignored here)
         * Bytes 5-6  : Variation  (ignored here)
         * Byte  7    : Reference  (ignored here)
         */
        if (data_len < 3) break;

        uint16_t raw = (uint16_t)data[1] | ((uint16_t)data[2] << 8);

        /* 0xFFFF is the NMEA2000 "not available" sentinel */
        if (raw == 0xFFFFU) break;

        float heading_deg = (float)raw * 0.0001f * (180.0f / (float)M_PI);

        /* Clamp to 0-360 */
        while (heading_deg >= 360.0f) heading_deg -= 360.0f;
        while (heading_deg <    0.0f) heading_deg += 360.0f;

        char buf[8];
        snprintf(buf, sizeof(buf), "%03.0f", heading_deg);

        /* Rose target: (360 - heading) degrees clockwise in LVGL tenths */
        int32_t target = (int32_t)((360.0f - heading_deg) * 10.0f);
        target = ((target % 3600) + 3600) % 3600;

        if (lvgl_port_lock(100)) {
            if (ui_Heading) {
                lv_label_set_text(ui_Heading, buf);
            }

            if (ui_CompassRose) {
                /* Read where the rose currently sits (may be mid-animation) */
                int32_t current = (int32_t)lv_img_get_angle(ui_CompassRose);

                /* Choose the shortest arc — never rotate more than 180° */
                int32_t delta = target - current;
                if (delta >  1800) delta -= 3600;
                if (delta < -1800) delta += 3600;

                /* Cancel any in-progress animation on this object */
                lv_anim_del(ui_CompassRose, NULL);

                /* Scale duration to angular distance at ~60°/sec so small
                 * changes animate quickly and large sweeps are not too slow.
                 * abs(delta) is in tenths of degrees; 600 tenths/sec → ÷6 gives ms. */
                uint32_t anim_ms = (uint32_t)(abs(delta) * 1000 / 600);
                if (anim_ms < 150) anim_ms = 150;   /* floor: always visible */
                if (anim_ms > 600) anim_ms = 600;   /* cap:   never sluggish   */

                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, ui_CompassRose);
                lv_anim_set_exec_cb(&a, img_angle_anim_cb);
                lv_anim_set_values(&a, current, current + delta);
                lv_anim_set_time(&a, anim_ms);
                lv_anim_set_path_cb(&a, lv_anim_path_linear);
                lv_anim_start(&a);
            }

            lvgl_port_unlock();
        }
        break;
    }

    case 130306: {
        /* Wind Data
         * Byte 0     : SID
         * Bytes 1-2  : Wind Speed, uint16 LE, resolution = 0.01 m/s/LSB
         * Bytes 3-4  : Wind Direction, uint16 LE, resolution = 0.0001 rad/LSB
         * Byte  5    : Wind Reference (bits 0-2); ignored here
         */
        if (data_len < 5) break;

        uint16_t spd_raw = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
        uint16_t dir_raw = (uint16_t)data[3] | ((uint16_t)data[4] << 8);

        /* 0xFFFF = "not available" sentinel for both fields */
        if (spd_raw == 0xFFFFU || dir_raw == 0xFFFFU) break;

        /* Speed: 0.01 m/s per LSB → knots (1 m/s = 1.94384 kts) */
        float speed_kts = (float)spd_raw * 0.01f * 1.94384f;

        /* Angle: 0.0001 rad per LSB → degrees */
        float angle_deg = (float)dir_raw * 0.0001f * (180.0f / (float)M_PI);
        while (angle_deg >= 360.0f) angle_deg -= 360.0f;

        /* Speed string: 1 decimal place */
        char spd_buf[8];
        snprintf(spd_buf, sizeof(spd_buf), "%.1f", speed_kts);

        /* Angle string: P xxx (port, < 180°) or S yyy (starboard, ≥ 180°)
         * For starboard the displayed angle is the mirror: 360 - angle */
        char ang_buf[10];
        if (angle_deg < 180.0f) {
            snprintf(ang_buf, sizeof(ang_buf), "P %03.0f", angle_deg);
        } else {
            snprintf(ang_buf, sizeof(ang_buf), "S %03.0f", 360.0f - angle_deg);
        }

        if (lvgl_port_lock(100)) {
            if (ui_AWS) {
                lv_label_set_text(ui_AWS, spd_buf);
            }
            if (ui_AWA) {
                lv_label_set_text(ui_AWA, ang_buf);
            }

            if (ui_ApparantPointer) {
                /* Pointer target: same formula as compass rose —
                 * (360 - angle) degrees clockwise puts dead-ahead at 12 o'clock,
                 * port wind swings the pointer left, starboard swings it right. */
                int32_t ptr_target = (int32_t)((360.0f - angle_deg) * 10.0f);
                ptr_target = ((ptr_target % 3600) + 3600) % 3600;

                int32_t ptr_current = (int32_t)lv_img_get_angle(ui_ApparantPointer);

                int32_t ptr_delta = ptr_target - ptr_current;
                if (ptr_delta >  1800) ptr_delta -= 3600;
                if (ptr_delta < -1800) ptr_delta += 3600;

                lv_anim_del(ui_ApparantPointer, NULL);

                uint32_t ptr_ms = (uint32_t)(abs(ptr_delta) * 1000 / 600);
                if (ptr_ms < 150) ptr_ms = 150;
                if (ptr_ms > 600) ptr_ms = 600;

                lv_anim_t p;
                lv_anim_init(&p);
                lv_anim_set_var(&p, ui_ApparantPointer);
                lv_anim_set_exec_cb(&p, img_angle_anim_cb);
                lv_anim_set_values(&p, ptr_current, ptr_current + ptr_delta);
                lv_anim_set_time(&p, ptr_ms);
                lv_anim_set_path_cb(&p, lv_anim_path_linear);
                lv_anim_start(&p);
            }

            lvgl_port_unlock();
        }
        break;
    }

    /* ---------------------------------------------------------------
     * Future fixed PGNs — add cases here, e.g.:
     *   case 127251:  // Rate of Turn
     * --------------------------------------------------------------- */

    default:
        break;
    }
}

/**
 * @brief Decode and extract a value from raw CAN data based on field definition
 * 
 * Handles bit extraction with proper byte ordering
 */
static double decode_field_value(const uint8_t *data, int data_len, cJSON *field)
{
    cJSON *bit_length = cJSON_GetObjectItem(field, "BitLength");
    cJSON *bit_offset = cJSON_GetObjectItem(field, "BitOffset");
    cJSON *resolution = cJSON_GetObjectItem(field, "Resolution");
    /* Note: "Signed" field available in PGN DB but sign-extension not yet implemented */
    
    if (!bit_length || !bit_offset) return 0.0;
    
    int bits = bit_length->valueint;
    int offset = bit_offset->valueint;
    int byte_pos = offset / 8;
    int bit_pos = offset % 8;
    
    // Extract raw value from bytes
    uint64_t raw_value = 0;
    
    for (int i = 0; i < bits && byte_pos < data_len; i++) {
        uint8_t byte = data[byte_pos];
        uint8_t bit = (byte >> bit_pos) & 0x01;
        raw_value |= ((uint64_t)bit << i);
        
        bit_pos++;
        if (bit_pos >= 8) {
            bit_pos = 0;
            byte_pos++;
        }
    }
    
    // Apply resolution
    double value = (double)raw_value;
    if (resolution) {
        value *= resolution->valuedouble;
    }
    
    return value;
}

/**
 * @brief Print decoded PGN message to terminal
 */
static void print_pgn_message(uint32_t can_id, const uint8_t *data, int data_len)
{
    uint32_t pgn = extract_pgn_from_can_id(can_id);
    uint8_t priority = extract_priority_from_can_id(can_id);
    uint8_t source = extract_source_from_can_id(can_id);
    
    // Look up PGN definition
    cJSON *pgn_def = pgn_get_definition(pgn_database, pgn);
    
    if (!pgn_def) {
        // PGN not found, print raw data
        ESP_LOGI(TAG, "=== UNKNOWN PGN ===");
        ESP_LOGI(TAG, "PGN: %d (0x%X) | Priority: %d | Source: %d", pgn, pgn, priority, source);
        ESP_LOGI(TAG, "Data (%d bytes):", data_len);
        
        char hex_str[256] = {0};
        for (int i = 0; i < data_len; i++) {
            snprintf(hex_str + strlen(hex_str), sizeof(hex_str) - strlen(hex_str),
                     "%02X ", data[i]);
        }
        ESP_LOGI(TAG, "%s", hex_str);
        return;
    }
    
    // Print header
    cJSON *pgn_id = cJSON_GetObjectItem(pgn_def, "Id");
    cJSON *description = cJSON_GetObjectItem(pgn_def, "Description");
    
    ESP_LOGI(TAG, "=== PGN MESSAGE ===");
    ESP_LOGI(TAG, "PGN: %d | Name: %s", pgn, 
             pgn_id && pgn_id->valuestring ? pgn_id->valuestring : "unknown");
    ESP_LOGI(TAG, "Description: %s",
             description && description->valuestring ? description->valuestring : "N/A");
    ESP_LOGI(TAG, "Priority: %d | Source: %d | Length: %d bytes", priority, source, data_len);
    
    // Decode and print fields
    cJSON *fields = cJSON_GetObjectItem(pgn_def, "Fields");
    if (fields && fields->child) {
        ESP_LOGI(TAG, "Fields:");
        
        cJSON *field = NULL;
        cJSON_ArrayForEach(field, fields) {
            cJSON *field_id = cJSON_GetObjectItem(field, "Id");
            cJSON *field_name = cJSON_GetObjectItem(field, "Name");
            cJSON *field_type = cJSON_GetObjectItem(field, "FieldType");
            cJSON *unit = cJSON_GetObjectItem(field, "Unit");
            
            if (!field_id) continue;
            
            // Skip reserved fields
            if (field_type && field_type->valuestring && 
                strcmp(field_type->valuestring, "RESERVED") == 0) {
                continue;
            }
            
            double value = decode_field_value(data, data_len, field);
            
            ESP_LOGI(TAG, "  %s (%s): %.4f%s",
                     field_name && field_name->valuestring ? field_name->valuestring : field_id->valuestring,
                     field_id->valuestring,
                     value,
                     unit && unit->valuestring ? unit->valuestring : "");
        }
    }
    ESP_LOGI(TAG, "");  // Blank line for clarity

    /* Dispatch to the fixed UI element handler */
    handle_pgn_fixed(pgn, data, data_len);
}


void can_update_textarea_cb(lv_timer_t * timer) {
/*     if (CAN_Clear) {
        memset(can_data, 0, sizeof(can_data));  // Clear the buffer when flag is set
        CAN_Clear = false;
    } */
    //lv_textarea_set_text(ui_CAN_Read_Area, can_data);  // Update the UI with the new CAN data
}

/**
 * @brief Task for handling CAN communication.
 *
 * This task is responsible for initializing the CAN interface, reading CAN 
 * messages, and decoding them using the PGN database. Messages are printed
 * to the terminal with full field information.
 *
 * @param arg Task argument, not used in this function.
 */
void can_task(void *arg)
{
    // Load PGN database on startup
    const char *db_path = "/littlefs/PGNS/NMEA_database_1_300.json";
    pgn_database = pgn_json_load(db_path);
    
    if (!pgn_database) {
        ESP_LOGW(TAG, "Failed to load PGN database from %s", db_path);
        ESP_LOGW(TAG, "Will display raw CAN messages only");
    } else {
        ESP_LOGI(TAG, "PGN database loaded successfully");
        pgn_print_all_ids(pgn_database);  // Log all available PGNs
    }
    
    // TWAI configuration settings for the CAN bus
    static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();  // Set CAN bus speed to 500 kbps
    static const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();  // Accept all incoming CAN messages
    static const twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, TWAI_MODE_NORMAL);  // General configuration, set TX/RX GPIOs and mode
    //lv_roller_set_selected(ui_CAN_Roller, 5, LV_ANIM_OFF);
    // Initialize the CAN communication interface
    IO_EXTENSION_Output(IO_EXTENSION_IO_5, 1);  // Select CAN communication interface (0 for USB, 1 for CAN)
    
    // Initialize the CAN communication system
    can_init(t_config, f_config, g_config);  // Initialize CAN with specified configurations

    uint32_t alerts_triggered;  // Variable to store triggered CAN bus alerts
    static twai_message_t message;  // Variable to store received CAN message
    char message_str[256];  // Buffer to store formatted message string

    while (1)
    {
        alerts_triggered = can_read_alerts();  // Check for any triggered CAN bus alerts

        // If new CAN data is received, process and display it
        if (alerts_triggered & TWAI_ALERT_RX_DATA) {
            message = can_read_Byte();  // Read the received CAN message

            // Decode and print the PGN message to terminal
            if (pgn_database) {
                print_pgn_message(message.identifier, message.data, message.data_length_code);
            } 
                // Fallback: print raw hex if database not loaded
                snprintf(message_str, sizeof(message_str),
                         "ID: 0x%03lX | Length: %d | Data: ",
                         message.identifier, message.data_length_code);

                for (int i = 0; i < message.data_length_code; i++) {
                    snprintf(message_str + strlen(message_str), sizeof(message_str) - strlen(message_str),
                             "%02X ", message.data[i]);
                }
                
                ESP_LOGI(TAG, "%s", message_str);
            

            // Append the formatted message to the global CAN data buffer (for UI)
            snprintf(message_str, sizeof(message_str),
                     "ID: 0x%03lX | Len: %d\n",
                     message.identifier, message.data_length_code);
            strncat(can_data, message_str, sizeof(can_data) - strlen(can_data) - 1);   

            // Create a timer to update the textarea every 100ms
            lv_timer_t * t = lv_timer_create(can_update_textarea_cb, 100, NULL);
            lv_timer_set_repeat_count(t, 1);  // Execute once
        }

    }
}

/**
 * @brief Converts a hexadecimal string to an array of bytes.
 *
 * This function removes spaces from the input string and converts each pair
 * of hexadecimal characters to a byte, storing the result in the output array.
 * If the string contains invalid hexadecimal characters or cannot be fully 
 * converted, an error code is returned.
 *
 * @param input The input string to be converted.
 * @param output The output array to store the converted bytes.
 * @param max_output_size The maximum size of the output array.
 *
 * @return The number of bytes successfully converted, or a negative error code.
 */
int string_to_hex(const char *input, uint8_t *output, size_t max_output_size) {
    size_t len = 0;

    // Traverse the input string
    for (size_t i = 0; input[i] != '\0'; i++) {
        if (input[i] == ' ') continue;  // Skip spaces

        // Check if the character is a valid hexadecimal digit
        if (!isxdigit((int)input[i])) return -2;  // Invalid hexadecimal character
        if (len / 2 >= max_output_size) return -1;  // Output buffer is full

        // Convert the character to its hexadecimal value and store it in the output array
        output[len / 2] = (output[len / 2] << 4) | (uint8_t)strtol((char[]){input[i], '\0'}, NULL, 16);
        len++;
    }

    return (len % 2 == 0) ? len / 2 : -1;  // Return the number of bytes if valid, or an error code if invalid
}
