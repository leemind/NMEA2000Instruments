#include "can_debug_ui.h"
#include "esp_twai_onchip.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

#define MAX_CAN_MSGS 50

typedef struct {
  uint32_t can_id;
  uint8_t len;
  uint8_t data[8];
} can_msg_slot_t;

static can_msg_slot_t s_slots[MAX_CAN_MSGS];
static int s_num_slots = 0;

static lv_obj_t *s_textarea = NULL;
static lv_obj_t *s_lbl_status = NULL;

void can_debug_ui_init(void) {
  if (!ui_CANDebugScreen)
    return;

  /* Create status label at the top */
  s_lbl_status = lv_label_create(ui_CANDebugScreen);
  lv_label_set_text(s_lbl_status, "CAN Status: OK");
  lv_obj_align(s_lbl_status, LV_ALIGN_TOP_MID, 0, 5);
  lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0xFFFFFF), 0);

  /* Create textarea for terminal-style logging */
  s_textarea = lv_textarea_create(ui_CANDebugScreen);
  lv_obj_set_width(s_textarea, lv_pct(95));
  lv_obj_set_height(s_textarea, lv_pct(80));
  lv_obj_align(s_textarea, LV_ALIGN_TOP_MID, 0, 35);
  lv_textarea_set_cursor_click_pos(s_textarea, false); // Read-only feel
  lv_obj_set_style_bg_color(s_textarea, lv_color_hex(0x000000), 0);
  lv_obj_set_style_text_color(s_textarea, lv_color_hex(0x00FF00), 0); // Green text
  lv_obj_set_style_text_font(s_textarea, &lv_font_montserrat_12, 0);
  lv_textarea_set_max_length(s_textarea, 10000); // Prevent memory explosion
}

void can_debug_ui_add_log(const char *text) {
  if (!s_textarea)
    return;

  if (lvgl_port_lock(0)) {
    lv_textarea_add_text(s_textarea, text);
    /* Scroll to the bottom to show latest messages */
    lv_obj_scroll_to_y(s_textarea, LV_COORD_MAX, LV_ANIM_OFF);
    lvgl_port_unlock();
  }
}

void can_debug_ui_update_msg(uint32_t can_id, const uint8_t *data,
                             uint8_t len) {
  /* This function previously updated a table. Now that we use a textarea
     for comprehensive logging, this is effectively replaced by add_log.
     However, we keep the slots update for potential future use. */
  int found_idx = -1;
  for (int i = 0; i < s_num_slots; i++) {
    if (s_slots[i].can_id == can_id) {
      found_idx = i;
      break;
    }
  }

  if (found_idx == -1) {
    if (s_num_slots < MAX_CAN_MSGS) {
      found_idx = s_num_slots++;
    } else {
      return;
    }
  }

  s_slots[found_idx].can_id = can_id;
  s_slots[found_idx].len = len > 8 ? 8 : len;
  memcpy(s_slots[found_idx].data, data, s_slots[found_idx].len);
}

void can_debug_ui_update_status(uint32_t alerts) {
  if (!alerts)
    return;

  if (lvgl_port_lock(0)) {
    if (s_lbl_status && lv_disp_get_scr_act(NULL) == ui_CANDebugScreen) {
      char buf[128] = "Alerts: ";
      /*       if (alerts & TWAI_ALERT_BUS_OFF)
              strncat(buf, "BUS_OFF ", sizeof(buf) - strlen(buf) - 1);
            if (alerts & TWAI_ALERT_BUS_RECOVERED)
              strncat(buf, "RECOVERED ", sizeof(buf) - strlen(buf) - 1);
            if (alerts & TWAI_ALERT_RX_QUEUE_FULL)
              strncat(buf, "RX_Q_FULL ", sizeof(buf) - strlen(buf) - 1);
            if (alerts & TWAI_ALERT_ERR_PASS)
              strncat(buf, "ERR_PASS ", sizeof(buf) - strlen(buf) - 1);
            if (alerts & TWAI_ALERT_BUS_ERROR)
              strncat(buf, "BUS_ERR ", sizeof(buf) - strlen(buf) - 1); */

      lv_label_set_text(s_lbl_status, buf);
      lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0xFF0000), 0);
    }
    lvgl_port_unlock();
  }
}
