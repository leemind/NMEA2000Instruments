#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <esp_log.h>
#include <cJSON.h>

#include "ui.h"
#include "can.h"  // Include the CAN driver for communication
#include "pgn_json_parser.h"  // Include PGN parser for decoding NMEA2000 messages

TaskHandle_t can_TaskHandle;

static char can_data[2 * 1024] = {0};  // Buffer for receiving data
static cJSON *pgn_database = NULL;  // Global PGN database
static const char *TAG = "CAN_DECODER";

/**
 * @brief Extract PGN number from NMEA2000 CAN identifier
 * 
 * NMEA2000 CAN ID structure (29-bit):
 * - Bits 28-26: Priority
 * - Bit 25: Reserved
 * - Bit 24: Data Page
 * - Bits 23-16: PDU Format
 * - Bits 15-8: PDU Specific
 * - Bits 7-0: Source Address
 * 
 * PGN calculation:
 * - If PDU Format < 240: PGN = (PDU Format << 8)
 * - If PDU Format >= 240: PGN = (PDU Format << 8) | PDU Specific
 */
static uint32_t extract_pgn_from_can_id(uint32_t can_id)
{
    uint8_t pdu_format = (can_id >> 16) & 0xFF;
    uint8_t pdu_specific = (can_id >> 8) & 0xFF;
    
    if (pdu_format < 240) {
        return (pdu_format << 8);
    } else {
        return (pdu_format << 8) | pdu_specific;
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
 * @brief Decode and extract a value from raw CAN data based on field definition
 * 
 * Handles bit extraction with proper byte ordering
 */
static double decode_field_value(const uint8_t *data, int data_len, cJSON *field)
{
    cJSON *bit_length = cJSON_GetObjectItem(field, "BitLength");
    cJSON *bit_offset = cJSON_GetObjectItem(field, "BitOffset");
    cJSON *resolution = cJSON_GetObjectItem(field, "Resolution");
    cJSON *is_signed = cJSON_GetObjectItem(field, "Signed");
    
    if (!bit_length || !bit_offset) return 0.0;
    
    int bits = bit_length->valueint;
    int offset = bit_offset->valueint;
    int byte_pos = offset / 8;
    int bit_pos = offset % 8;
    
    // Extract raw value from bytes
    uint64_t raw_value = 0;
    int bits_remaining = bits;
    
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
    const char *db_path = "/sdcard/PGNS/NMEA_database_1_300.json";
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
            } else {
                // Fallback: print raw hex if database not loaded
                snprintf(message_str, sizeof(message_str),
                         "ID: 0x%03lX | Length: %d | Data: ",
                         message.identifier, message.data_length_code);

                for (int i = 0; i < message.data_length_code; i++) {
                    snprintf(message_str + strlen(message_str), sizeof(message_str) - strlen(message_str),
                             "%02X ", message.data[i]);
                }
                
                ESP_LOGI(TAG, "%s", message_str);
            }

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
