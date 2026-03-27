#include <cJSON.h>
#include <ctype.h>
#include <esp_log.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"

#include "can.h"          // Include the CAN driver for communication
#include "can_debug_ui.h" // CAN debug screen updates
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "io_extension.h"
#include "lvgl_port.h" // LVGL mutex for thread-safe UI access from CAN task
#include "pgn_json_parser.h" // Include PGN parser for decoding NMEA2000 messages
#include "screens/ui_Wind.h" // UI objects (ui_Heading, ui_AWS, etc.)
#include "screens/ui_Depth.h"
#include "screens/ui_Settings.h"
#include "screens/ui_CANDebugScreen.h"
#include "ui.h"
#include "settings.h"        // settings_get_depth_unit() etc.
#include "esp_timer.h"
#include "esp_task_wdt.h"

TaskHandle_t can_TaskHandle;

// Define a struct to hold the message we want to pass to our task
typedef struct {
  uint32_t identifier;
  uint8_t data_length;
  uint8_t data[8]; // Max NMEA2000/CAN payload size
} can_msg_t;

// Define a global queue handle
static QueueHandle_t can_rx_queue = NULL;

cJSON *pgn_database = NULL;    // Global PGN database
static const char *TAG = "CAN_DECODER";
static bool g_can_paused = false;

/* Forward declaration — defined later in this file */
static void img_angle_anim_cb(void *obj, int32_t angle);

/* -----------------------------------------------------------------------
 * Cached values for True Wind computation.
 * Written only from the CAN task (single-threaded), so no extra mutex.
 * -------------------------------------------------------------------- */
static float g_aws_ms = 0.0f;  /* Apparent Wind Speed in m/s       */
static float g_awa_deg = 0.0f; /* Apparent Wind Angle in degrees 0-360 */
static float g_sog_kts = 0.0f; /* Speed Over Ground in knots         */
static bool g_aws_valid = false;
static bool g_awa_valid = false;
static bool g_sog_valid = false;

typedef struct {
    lv_obj_t *value;
    lv_obj_t *label;
    lv_obj_t *unit;
} databox_ui_t;

static databox_ui_t g_databox_ui[10];
static bool g_ui_initialized = false;

static void init_databox_ui_pointers(void) {
    if (g_ui_initialized) return;
    g_databox_ui[0] = (databox_ui_t){uic_LHS1_DBValue, uic_LHS1_DBLabel, uic_LHS1_DBUnit};
    g_databox_ui[1] = (databox_ui_t){uic_LHS2_DBValue, uic_LHS2_DBLabel, uic_LHS2_DBUnit};
    g_databox_ui[2] = (databox_ui_t){uic_LHS3_DBValue, uic_LHS3_DBLabel, uic_LHS3_DBUnit};
    g_databox_ui[3] = (databox_ui_t){uic_LHS4_DBValue, uic_LHS4_DBLabel, uic_LHS4_DBUnit};
    g_databox_ui[4] = (databox_ui_t){uic_LHS5_DBValue, uic_LHS5_DBLabel, uic_LHS5_DBUnit};
    g_databox_ui[5] = (databox_ui_t){uic_DBValue,      uic_RHS1_DBLabel, uic_RHS1_DBUnit};
    g_databox_ui[6] = (databox_ui_t){uic_RHS2_DBValue, uic_RHS2_DBLabel, uic_RHS2_DBUnit};
    g_databox_ui[7] = (databox_ui_t){uic_RHS3_DBValue, uic_RHS3_DBLabel, uic_RHS3_DBUnit};
    g_databox_ui[8] = (databox_ui_t){uic_RHS4_DBValue, uic_RHS4_DBLabel, uic_RHS4_DBUnit};
    g_databox_ui[9] = (databox_ui_t){uic_RHS5_DBValue, uic_RHS5_DBLabel, uic_RHS5_DBUnit};
    g_ui_initialized = true;
}

/**
 * @brief Compute True Wind Speed/Angle from cached AWS, AWA and SOG,
 *        then animate the TruePointer to the new TWA.
 *
 * Vector decomposition (boat frame, x=stbd+, y=fwd+):
 *   TWx = AWS * sin(AWA)
 *   TWy = AWS * cos(AWA) - SOG
 *   TWS = sqrt(TWx^2 + TWy^2)
 *   TWA = atan2(TWx, TWy)   (negative = starboard, positive = port)
 *
 * MUST be called while already holding lvgl_port_lock.
 */
static void compute_true_wind(void) {
  if (!g_aws_valid || !g_awa_valid || !g_sog_valid)
    return;

  /* Convert AWA to signed radians: port = +, starboard = - */
  float awa_signed_deg = g_awa_deg;
  if (awa_signed_deg > 180.0f)
    awa_signed_deg -= 360.0f;
  float awa_rad = awa_signed_deg * ((float)M_PI / 180.0f);

  float tw_x = g_aws_ms * sinf(awa_rad);
  float tw_y = g_aws_ms * cosf(awa_rad) - g_sog_kts;

  float tws = sqrtf(tw_x * tw_x + tw_y * tw_y);
  float twa_rad = atan2f(tw_x, tw_y); /* signed: + = port */
  float twa_deg = twa_rad * (180.0f / (float)M_PI);

  /* Normalise TWA to 0-360 (port side 0-180, starboard 180-360) */
  if (twa_deg < 0.0f)
    twa_deg += 360.0f;

  ESP_LOGD(TAG, "TWS=%.1f m/s  TWA=%.1f deg", tws, twa_deg);

  app_settings_t settings = settings_get();
  init_databox_ui_pointers();

  for (int i = 0; i < 10; i++) {
    databox_config_t *cfg = &settings.databoxes[i];
    if (cfg->pgn1 != 0xFFFFFFFF) continue;

    if (strcmp(cfg->field1_id, "TWS") == 0) {
      char tws_buf[8];
      snprintf(tws_buf, sizeof(tws_buf), "%.1f", tws * wind_convert[settings.wind_unit]);
      if (g_databox_ui[i].value) lv_label_set_text(g_databox_ui[i].value, tws_buf);
      if (g_databox_ui[i].label) lv_label_set_text(g_databox_ui[i].label, cfg->label);
      if (g_databox_ui[i].unit)  lv_label_set_text(g_databox_ui[i].unit,  wind_unit_str[settings.wind_unit]);
    } 
    else if (strcmp(cfg->field1_id, "TWA") == 0) {
      char twa_buf[10];
      if (twa_deg < 180.0f) {
        snprintf(twa_buf, sizeof(twa_buf), "P %03.0f", twa_deg);
      } else {
        snprintf(twa_buf, sizeof(twa_buf), "S %03.0f", 360.0f - twa_deg);
      }
      if (g_databox_ui[i].value) lv_label_set_text(g_databox_ui[i].value, twa_buf);
      if (g_databox_ui[i].label) lv_label_set_text(g_databox_ui[i].label, cfg->label);
      if (g_databox_ui[i].unit)  lv_label_set_text(g_databox_ui[i].unit,  cfg->unit);
    }
  }

  /* Rotate TruePointer — same formula and animation as ApparantPointer */
  if (ui_TruePointer) {
    int32_t tgt = (int32_t)((360.0f - twa_deg) * 10.0f);
    tgt = ((tgt % 3600) + 3600) % 3600;

    int32_t cur = (int32_t)lv_img_get_angle(ui_TruePointer);

    int32_t dlt = tgt - cur;
    if (dlt > 1800)
      dlt -= 3600;
    if (dlt < -1800)
      dlt += 3600;

    lv_anim_del(ui_TruePointer, NULL);

    uint32_t ms = (uint32_t)(abs(dlt) * 1000 / 600);
    if (ms < 150)
      ms = 150;
    if (ms > 600)
      ms = 600;

    lv_anim_t t;
    lv_anim_init(&t);
    lv_anim_set_var(&t, ui_TruePointer);
    lv_anim_set_exec_cb(&t, img_angle_anim_cb);
    lv_anim_set_values(&t, cur, cur + dlt);
    lv_anim_set_time(&t, ms);
    lv_anim_set_path_cb(&t, lv_anim_path_linear);
    lv_anim_start(&t);
  }
}

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
static uint32_t extract_pgn_from_can_id(uint32_t can_id) {
  uint8_t pdu_format = (can_id >> 16) & 0xFF;  // bits 23:16
  uint8_t pdu_specific = (can_id >> 8) & 0xFF; // bits 15:8
  uint32_t dp_r = (can_id >> 24) & 0x03; // Reserved + Data Page, bits 25:24

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
static uint8_t extract_priority_from_can_id(uint32_t can_id) {
  return (can_id >> 26) & 0x07;
}

/**
 * @brief Extract source address from NMEA2000 CAN identifier
 */
static uint8_t extract_source_from_can_id(uint32_t can_id) {
  return can_id & 0xFF;
}

/**
 * @brief LVGL animation executor callback for compass rose rotation.
 *
 * The animated value may temporarily exceed 0-3599 when crossing the North
 * boundary, so we normalise with modulo before setting the angle.
 */
static void img_angle_anim_cb(void *obj, int32_t angle) {
  angle = ((angle % 3600) + 3600) % 3600;
  lv_img_set_angle((lv_obj_t *)obj, (int16_t)angle);
}

/**
 * @brief Decode and extract a value from raw CAN data based on field definition
 *
 * Handles bit extraction with proper byte ordering
 */
static double decode_field_value(const uint8_t *data, int data_len,
                                 cJSON *field) {
  cJSON *bit_length = cJSON_GetObjectItem(field, "BitLength");
  cJSON *bit_offset = cJSON_GetObjectItem(field, "BitOffset");
  cJSON *resolution = cJSON_GetObjectItem(field, "Resolution");

  if (!bit_length || !bit_offset)
    return 0.0;

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
 * @brief Helper to get a decoded field value by its JSON ID.
 * Returns NAN if field not found.
 */
static double get_pgn_field_value(cJSON *pgn_def, const uint8_t *data,
                                  int data_len, const char *field_id) {
  cJSON *fields = cJSON_GetObjectItem(pgn_def, "Fields");
  cJSON *field = NULL;
  cJSON_ArrayForEach(field, fields) {
    cJSON *id = cJSON_GetObjectItem(field, "Id");
    if (id && id->valuestring && strcmp(id->valuestring, field_id) == 0) {
      return decode_field_value(data, data_len, field);
    }
  }
  return NAN;
}

/**
 * @brief Handle computed values (TWS/TWA) if configured
 */
static void handle_computed_databoxes(void) {
    compute_true_wind();
}

static double convert_unit(double value, const char *from, const char *to) {
    if (!from || !to || !from[0] || !to[0] || strcmp(from, to) == 0) return value;
    
    /* Speed: m/s to ... */
    if (strcmp(from, "m/s") == 0) {
        if (strcmp(to, "knots") == 0) return value * 1.94384;
        if (strcmp(to, "km/h") == 0) return value * 3.6;
        if (strcmp(to, "mph") == 0) return value * 2.23694;
    }
    /* Distance: m to ... */
    if (strcmp(from, "m") == 0) {
        if (strcmp(to, "feet") == 0) return value * 3.28084;
        if (strcmp(to, "fathoms") == 0) return value * 0.546807;
        if (strcmp(to, "nm") == 0) return value / 1852.0;
    }
    /* Temp: K to ... */
    if (strcmp(from, "K") == 0) {
        if (strcmp(to, "C") == 0) return value - 273.15;
        if (strcmp(to, "F") == 0) return (value - 273.15) * 1.8 + 32.0;
    }
    /* Pressure: Pa to ... */
    if (strcmp(from, "Pa") == 0) {
        if (strcmp(to, "bar") == 0) return value / 100000.0;
        if (strcmp(to, "psi") == 0) return value * 0.000145038;
        if (strcmp(to, "inHg") == 0) return value * 0.0002953;
    }
    /* Volume: L to ... */
    if (strcmp(from, "L") == 0) {
        if (strcmp(to, "gal (US)") == 0) return value * 0.264172;
        if (strcmp(to, "gal (Imp)") == 0) return value * 0.219969;
    }
    /* Flow: L/h to ... */
    if (strcmp(from, "L/h") == 0) {
        if (strcmp(to, "gph (US)") == 0) return value * 0.264172;
        if (strcmp(to, "gph (Imp)") == 0) return value * 0.219969;
    }
    /* Angle: rad to deg */
    if (strcmp(from, "rad") == 0 && strcmp(to, "deg") == 0) {
        return value * (180.0 / M_PI);
    }
    if (strcmp(from, "deg") == 0 && strcmp(to, "rad") == 0) {
        return value * (M_PI / 180.0);
    }

    return value;
}

/**
 * @brief Update dynamic databoxes based on incoming PGN
 */
static void handle_pgn_dynamic(cJSON *pgn_def, uint32_t pgn, const uint8_t *data, int data_len) {
    app_settings_t settings = settings_get();
    init_databox_ui_pointers();

    for (int i = 0; i < 10; i++) {
        databox_config_t *cfg = &settings.databoxes[i];
        if (cfg->pgn1 == 0) continue;

        double val = NAN;
        bool update = false;

        if (cfg->pgn1 == pgn) {
            val = get_pgn_field_value(pgn_def, data, data_len, cfg->field1_id);
            if (!isnan(val)) update = true;
        }

        /* Addition logic */
        if (cfg->pgn2 != 0 && cfg->pgn2 == pgn) {
            double val2 = get_pgn_field_value(pgn_def, data, data_len, cfg->field2_id);
            if (!isnan(val2)) {
                /* If we already have val1 from this same PGN, add them. 
                   Otherwise we'd need a cache for cross-PGN addition.
                   For now, same-PGN addition is supported. */
                if (!isnan(val)) val += val2;
                else val = val2; // Fallback if only f2 is in this PGN (unlikely for same PGN)
                update = true;
            }
        }

        if (update && !isnan(val)) {
            /* Apply unit conversion */
            val = convert_unit(val, cfg->unit, cfg->display_unit);

            char buf[16];
            snprintf(buf, sizeof(buf), "%.1f", val); // Default 1 decimal place

            if (lvgl_port_lock(100)) {
                if (g_databox_ui[i].value) lv_label_set_text(g_databox_ui[i].value, buf);
                if (g_databox_ui[i].label) lv_label_set_text(g_databox_ui[i].label, cfg->label);
                if (g_databox_ui[i].unit)  lv_label_set_text(g_databox_ui[i].unit,  cfg->display_unit[0] ? cfg->display_unit : cfg->unit);
                lvgl_port_unlock();
            }
        }
    }
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
 * @param pgn_def  PGN definition from JSON database
 * @param data     Raw CAN data payload bytes
 * @param data_len Number of payload bytes
 */
static void handle_pgn_fixed(cJSON *pgn_def, const uint8_t *data, int data_len) {
  if (!pgn_def)
    return;

  cJSON *pgn_item = cJSON_GetObjectItem(pgn_def, "PGN");
  if (!pgn_item)
    return;
  uint32_t pgn = pgn_item->valueint;

  /* Handle dynamic databoxes first */
  handle_pgn_dynamic(pgn_def, pgn, data, data_len);

  switch (pgn) {

  case 127250: {
    /* Vessel Heading */
    double heading_rad = get_pgn_field_value(pgn_def, data, data_len, "headingSensorReading");
    double deviation_rad = get_pgn_field_value(pgn_def, data, data_len, "deviation");
    double variation_rad = get_pgn_field_value(pgn_def, data, data_len, "variation");
    if (isnan(heading_rad))
      break;

    float heading_deg = (float)heading_rad * (180.0f / (float)M_PI);

    /* Clamp to 0-360 */
    while (heading_deg >= 360.0f)
      heading_deg -= 360.0f;
    while (heading_deg < 0.0f)
      heading_deg += 360.0f;

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
        if (delta > 1800)
          delta -= 3600;
        if (delta < -1800)
          delta += 3600;

        /* Cancel any in-progress animation on this object */
        lv_anim_del(ui_CompassRose, NULL);

        /* Scale duration to angular distance at ~60°/sec so small
         * changes animate quickly and large sweeps are not too slow.
         * abs(delta) is in tenths of degrees; 600 tenths/sec → ÷6 gives ms. */
        uint32_t anim_ms = (uint32_t)(abs(delta) * 1000 / 600);
        if (anim_ms < 150)
          anim_ms = 150; /* floor: always visible */
        if (anim_ms > 600)
          anim_ms = 600; /* cap:   never sluggish   */

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
    /* Wind Data */
    double speed_ms = get_pgn_field_value(pgn_def, data, data_len, "windSpeed");
    double angle_rad = get_pgn_field_value(pgn_def, data, data_len, "windDirection");

    if (isnan(speed_ms) || isnan(angle_rad))
      break;

    app_settings_t settings = settings_get();

    /* Angle: rad → degrees */
    float angle_deg = (float)angle_rad * (180.0f / (float)M_PI);
    while (angle_deg >= 360.0f)
      angle_deg -= 360.0f;

    /* Speed string: 1 decimal place */
    char spd_buf[8];
    snprintf(spd_buf, sizeof(spd_buf), "%.1f",
             speed_ms * wind_convert[settings.wind_unit]);

    /* Angle string: P xxx (port, < 180°) or S yyy (starboard, ≥ 180°)
     * For starboard the displayed angle is the mirror: 360 - angle */
    char ang_buf[10];
    if (angle_deg < 180.0f) {
      snprintf(ang_buf, sizeof(ang_buf), "P %03.0f", angle_deg);
    } else {
      snprintf(ang_buf, sizeof(ang_buf), "S %03.0f", 360.0f - angle_deg);
    }

    /* Cache apparent wind for True Wind computation */
    g_aws_ms = speed_ms;
    g_awa_deg = angle_deg;
    g_aws_valid = true;
    g_awa_valid = true;

    if (lvgl_port_lock(100)) {
      if (ui_AWS) {
        lv_label_set_text(ui_AWS, spd_buf);
        lv_label_set_text(ui_AWSUnit, wind_unit_str[settings.wind_unit]);
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
        if (ptr_delta > 1800)
          ptr_delta -= 3600;
        if (ptr_delta < -1800)
          ptr_delta += 3600;

        lv_anim_del(ui_ApparantPointer, NULL);

        uint32_t ptr_ms = (uint32_t)(abs(ptr_delta) * 1000 / 600);
        if (ptr_ms < 150)
          ptr_ms = 150;
        if (ptr_ms > 600)
          ptr_ms = 600;

        lv_anim_t p;
        lv_anim_init(&p);
        lv_anim_set_var(&p, ui_ApparantPointer);
        lv_anim_set_exec_cb(&p, img_angle_anim_cb);
        lv_anim_set_values(&p, ptr_current, ptr_current + ptr_delta);
        lv_anim_set_time(&p, ptr_ms);
        lv_anim_set_path_cb(&p, lv_anim_path_linear);
        lv_anim_start(&p);
      }

      /* Recompute true wind with updated apparent wind */
      compute_true_wind();

      lvgl_port_unlock();
    }
    break;
  }

  case 129026: {
    /* COG & SOG, Rapid Update */
    double cog_rad = get_pgn_field_value(pgn_def, data, data_len, "courseOverGround");
    double sog_ms = get_pgn_field_value(pgn_def, data, data_len, "speedOverGround");

    if (isnan(sog_ms))
      break;

    g_sog_kts = (float)sog_ms * 1.94384f;
    g_sog_valid = true;

    /* SOG string */
    char sog_buf[8];
    snprintf(sog_buf, sizeof(sog_buf), "%.1f", g_sog_kts);

    /* COG string (degrees, 0-360) */
    char cog_buf[8];
    if (!isnan(cog_rad)) {
      float cog_deg = (float)cog_rad * (180.0f / (float)M_PI);
      while (cog_deg >= 360.0f)
        cog_deg -= 360.0f;
      snprintf(cog_buf, sizeof(cog_buf), "%03.0f", cog_deg);
    } else {
      snprintf(cog_buf, sizeof(cog_buf), "---");
    }

    /* Recompute true wind with updated SOG, and update RHS1/RHS2 databoxes */
    if (lvgl_port_lock(100)) {
      compute_true_wind();
      lvgl_port_unlock();
    }
    break;
  }

  case 128267: {
    /* Water Depth */
    double depth_m = get_pgn_field_value(pgn_def, data, data_len, "waterDepthTransducer");
    double offset_m = get_pgn_field_value(pgn_def, data, data_len, "offset");

    if (isnan(depth_m))
      break;

    app_settings_t settings = settings_get();

    float total_depth_m = (float)depth_m + (settings.use_transducer_offset && !isnan(offset_m) ? (float)offset_m : 0.0f);

    float depth_user = (float)total_depth_m * depth_convert[settings.depth_unit];

    char dep_buf[8];
    snprintf(dep_buf, sizeof(dep_buf), "%.1f", depth_user);

    if (lvgl_port_lock(100)) {
      /* Auto-Depth Screen Populating */
      if (uic_DepthBig)
        lv_label_set_text(uic_DepthBig, dep_buf);
      if (uic_BigDepthUnits)
        lv_label_set_text(uic_BigDepthUnits,
                          depth_unit_str[settings.depth_unit]);

      /* Auto-Depth Screen Switch Logic */
      bool auto_depth_enabled = false;
      if (uic_AutoDepthToggle) {
        auto_depth_enabled =
            lv_obj_has_state(uic_AutoDepthToggle, LV_STATE_CHECKED);
      }

      if (auto_depth_enabled && settings.autodepth_value > 0 &&
          depth_user < (float)settings.autodepth_value) {
        if (lv_disp_get_scr_act(NULL) == ui_Wind) {
          _ui_screen_change(&ui_Depth, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0,
                            &ui_Depth_screen_init);
        }
      }
            if (auto_depth_enabled && settings.autodepth_value > 0 &&
          depth_user > (float)settings.autodepth_value) {
        if (lv_disp_get_scr_act(NULL) == ui_Depth) {
          _ui_screen_change(&ui_Wind, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0,
                            &ui_Wind_screen_init);
        }
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


static void print_pgn_message(uint32_t can_id, const uint8_t *data,
                              int data_len) {
  int64_t start_time = esp_timer_get_time();
  uint32_t pgn = extract_pgn_from_can_id(can_id);
  uint8_t priority = extract_priority_from_can_id(can_id);
  uint8_t source = extract_source_from_can_id(can_id);

  char log_buf[1024] = {0};
  char line[256];

  // Look up PGN definition
  cJSON *pgn_def = pgn_get_definition(pgn_database, pgn);

  if (!pgn_def) {
    snprintf(log_buf, sizeof(log_buf), "--- UNKNOWN PGN ---\nPGN: %d (0x%X) | Pri: %d | Src: %d\nData: ",
             (int)pgn, (unsigned int)pgn, priority, source);
    for (int i = 0; i < data_len; i++) {
        snprintf(line, sizeof(line), "%02X ", data[i]);
        strncat(log_buf, line, sizeof(log_buf) - strlen(log_buf) - 1);
    }
    strncat(log_buf, "\n\n", sizeof(log_buf) - strlen(log_buf) - 1);
    ESP_LOGI(TAG, "%s", log_buf);
    can_debug_ui_add_log(log_buf);
    return;
  }

  cJSON *pgn_id = cJSON_GetObjectItem(pgn_def, "Id");
  cJSON *description = cJSON_GetObjectItem(pgn_def, "Description");

  snprintf(log_buf, sizeof(log_buf), "PGN: %d | %s\n%s\nPri:%d | Src:%d | Len:%d\n",
           (int)pgn, pgn_id && pgn_id->valuestring ? pgn_id->valuestring : "unknown",
           description && description->valuestring ? description->valuestring : "",
           priority, source, data_len);

  // Decode and print fields
  cJSON *fields = cJSON_GetObjectItem(pgn_def, "Fields");
  if (fields && fields->child) {
    strncat(log_buf, "Fields:\n", sizeof(log_buf) - strlen(log_buf) - 1);
    cJSON *field = NULL;
    cJSON_ArrayForEach(field, fields) {
      cJSON *field_id = cJSON_GetObjectItem(field, "Id");
      cJSON *field_name = cJSON_GetObjectItem(field, "Name");
      cJSON *field_type = cJSON_GetObjectItem(field, "FieldType");
      cJSON *unit = cJSON_GetObjectItem(field, "Unit");

      if (!field_id) continue;
      if (field_type && field_type->valuestring && strcmp(field_type->valuestring, "RESERVED") == 0) continue;

      double value = decode_field_value(data, data_len, field);
      snprintf(line, sizeof(line), "  %s: %.2f %s\n",
               field_name && field_name->valuestring ? field_name->valuestring : field_id->valuestring,
               value, unit && unit->valuestring ? unit->valuestring : "");
      strncat(log_buf, line, sizeof(log_buf) - strlen(log_buf) - 1);
    }
  }

  int64_t end_time = esp_timer_get_time();
  snprintf(line, sizeof(line), "Decode Time: %lld us\n", (end_time - start_time));
  strncat(log_buf, line, sizeof(log_buf) - strlen(log_buf) - 1);
  strncat(log_buf, "\n", sizeof(log_buf) - strlen(log_buf) - 1);

  ESP_LOGI(TAG, "\n%s", log_buf);

  /* Dispatch to the fixed UI element handler */
  handle_pgn_fixed(pgn_def, data, data_len);
}

void can_update_textarea_cb(lv_timer_t *timer) {
  /*     if (CAN_Clear) {
          memset(can_data, 0, sizeof(can_data));  // Clear the buffer when flag
     is set CAN_Clear = false;
      } */
  // lv_textarea_set_text(ui_CAN_Read_Area, can_data);  // Update the UI with
  // the new CAN data
}

static bool twai_error_cb(twai_node_handle_t handle,
                          const twai_error_event_data_t *edata,
                          void *user_ctx) {
  ESP_LOGW(TAG, "CAN Error: arb_lost=%d, bit=%d, form=%d, stuff=%d, ack=%d",
           (int)edata->err_flags.arb_lost, (int)edata->err_flags.bit_err,
           (int)edata->err_flags.form_err, (int)edata->err_flags.stuff_err,
           (int)edata->err_flags.ack_err);
  return false;
}

static bool twai_state_cb(twai_node_handle_t handle,
                          const twai_state_change_event_data_t *edata,
                          void *user_ctx) {
  const char *st[] = {"Active", "Warning", "Passive", "Bus-Off"};
  ESP_LOGI(TAG, "CAN State: %s -> %s", st[edata->old_sta], st[edata->new_sta]);
  return false;
}

static bool twai_rx_cb(twai_node_handle_t handle,
                       const twai_rx_done_event_data_t *edata, void *user_ctx) {
  uint8_t recv_buff[8];
  twai_frame_t rx_frame = {
      .buffer = recv_buff,
      .buffer_len = sizeof(recv_buff),
  };

  bool need_yield = false;

  // In the new esp_driver_twai v6.0+, the driver's ISR already loops to drain
  // the hardware FIFO and calls this callback for each message.
  // We only need to parse the current message once.
  if (twai_node_receive_from_isr(handle, &rx_frame) == ESP_OK) {
    // Populate our custom message struct
    can_msg_t msg;
    msg.identifier = rx_frame.header.id;
    msg.data_length = rx_frame.header.dlc;
    if (rx_frame.header.dlc > 0 && rx_frame.header.dlc <= sizeof(msg.data)) {
      memcpy(msg.data, rx_frame.buffer, rx_frame.header.dlc);
    }

    // Safely send to FreeRTOS queue from ISR
    BaseType_t high_task_wakeup = pdFALSE;
    xQueueSendFromISR(can_rx_queue, &msg, &high_task_wakeup);
    if (high_task_wakeup == pdTRUE) {
      need_yield = true;
    }
  }

  return need_yield; // Returns true if a higher priority task was woken
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
void can_task(void *arg) {
  // Register this task to be monitored by the Task Watchdog (TWDT)
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_task_wdt_add(NULL));

  // Load PGN database on startup
  const char *db_path = "/littlefs/PGNS/NMEA_database_1_300.json";
  pgn_database = pgn_json_load(db_path);

  if (!pgn_database) {
    ESP_LOGW(TAG, "Failed to load PGN database from %s", db_path);
    ESP_LOGW(TAG, "Will display raw CAN messages only");
  } else {
    ESP_LOGI(TAG, "PGN database loaded successfully");
    // pgn_print_all_ids(pgn_database); // Log all available PGNs
  }

  // Create the FreeRTOS Queue for CAN messages before we enable TWAI
  // Increased size to 50 to better handle bursts of NMEA2000 messages.
  can_rx_queue = xQueueCreate(50, sizeof(can_msg_t));
  if (can_rx_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create CAN RX Queue!");
  }

  // Initialize the CAN communication interface
  IO_EXTENSION_Output(
      IO_EXTENSION_IO_5,
      1); // Select CAN communication interface (0 for USB, 1 for CAN)

  twai_event_callbacks_t user_cbs = {
      .on_rx_done = twai_rx_cb,
      .on_error = twai_error_cb,
      .on_state_change = twai_state_cb,
  };

  twai_node_handle_t node_hdl = NULL;
  twai_onchip_node_config_t node_config = {
      .io_cfg.tx = TX_GPIO_NUM,     // TWAI TX GPIO pin
      .io_cfg.rx = RX_GPIO_NUM,     // TWAI RX GPIO pin
      .io_cfg.quanta_clk_out = -1,  // Not used
      .io_cfg.bus_off_indicator = -1, // Not used
      .bit_timing.bitrate = 250000, // 250 kbps bitrate
      .bit_timing.sp_permill = 800, // Increased to 80% for NMEA2000 robustness
      .fail_retry_cnt = -1,         // Retry forever
      .tx_queue_depth = 5,          // Transmit queue depth set to 5
  };
  // Create a new TWAI controller driver instance
  ESP_ERROR_CHECK(twai_new_node_onchip(&node_config, &node_hdl));

  // Configure "Accept All" filter for Extended frames (NMEA2000 standard).
  // Note: ESP32-S3 supports only 1 mask filter per node in this driver.
  twai_mask_filter_config_t accept_all_ext = {.id = 0, .mask = 0, .is_ext = 1};
  ESP_ERROR_CHECK(twai_node_config_mask_filter(node_hdl, 0, &accept_all_ext));

   ESP_ERROR_CHECK(
       twai_node_register_event_callbacks(node_hdl, &user_cbs, NULL));
  
   // Explicitly select CAN communication interface (0 for USB, 1 for CAN)
   IO_EXTENSION_Output(IO_EXTENSION_IO_5, 1);
  
   // Start the TWAI controller
   ESP_ERROR_CHECK(twai_node_enable(node_hdl));

  can_msg_t message;     // Variable to store received CAN message
  char message_str[256]; // Buffer to store formatted message string
  uint32_t last_status_log_ms = 0;

  while (1) {
    if (g_can_paused) {
      // Drain queue to avoid overflow but don't process
      can_msg_t junk;
      while (xQueueReceive(can_rx_queue, &junk, 0) == pdTRUE);
      esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (now_ms - last_status_log_ms > 5000) {
      twai_node_status_t status;
      twai_node_record_t record;
      if (twai_node_get_info(node_hdl, &status, &record) == ESP_OK) {
        uint8_t io_in = IO_EXTENSION_Input(IO_EXTENSION_IO_5); // Read current state
        //IO_EXTENSION_ScanBus(); // Scan bus every 5s to find disappearing devices

        ESP_LOGI(TAG, "Status: St=%d | CAN_SEL=%d | TX_Err=%d RX_Err=%d Bus_Err=%lu",
                 (int)status.state, (int)io_in, 
                 (int)status.tx_error_count, (int)status.rx_error_count,
                 (unsigned long)record.bus_err_num);
        
        if (!io_in) {
            ESP_LOGW(TAG, "CAN Selection Lost! Re-asserting...");
            IO_EXTENSION_Init();
            IO_EXTENSION_Output(IO_EXTENSION_IO_5, 1);
        }
      }
      last_status_log_ms = now_ms;
    }

    // Wait for a message to arrive in the queue (block up to 100ms)
    if (xQueueReceive(can_rx_queue, &message, pdMS_TO_TICKS(100)) == pdTRUE) {

      can_debug_ui_update_msg(message.identifier, message.data,
                              message.data_length);

      // Decoder output handles both logging and UI update
      if (pgn_database) {
        print_pgn_message(message.identifier, message.data,
                          message.data_length);
      } else {
         // Fallback for raw messages if DB is missing
         snprintf(message_str, sizeof(message_str), "ID: 0x%03lX | Len: %d\n",
                  (unsigned long)message.identifier, message.data_length);
         ESP_LOGI(TAG, "%s", message_str);
      }

      // Appendix: Avoid creating lv_timers in a loop here as it leaks memory
      // and causes resource exhaustion. If UI updates are needed, trigger
      // them via a signal or a single persistent timer.
    }
    esp_task_wdt_reset();
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

void can_pause(void) {
    ESP_LOGI(TAG, "CAN Processing Paused");
    g_can_paused = true;
}

void can_resume(void) {
    ESP_LOGI(TAG, "CAN Processing Resumed");
    g_can_paused = false;
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
    if (input[i] == ' ')
      continue; // Skip spaces

    // Check if the character is a valid hexadecimal digit
    if (!isxdigit((int)input[i]))
      return -2; // Invalid hexadecimal character
    if (len / 2 >= max_output_size)
      return -1; // Output buffer is full

    // Convert the character to its hexadecimal value and store it in the output
    // array
    output[len / 2] = (output[len / 2] << 4) |
                      (uint8_t)strtol((char[]){input[i], '\0'}, NULL, 16);
    len++;
  }

  return (len % 2 == 0) ? len / 2 : -1; // Return the number of bytes if valid,
                                        // or an error code if invalid
}
