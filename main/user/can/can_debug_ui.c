#include "can_debug_ui.h"
#include "esp_twai_onchip.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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

static lv_obj_t *s_log_cont = NULL;  // scrollable container
static lv_obj_t *s_log_label = NULL; // log text (a label, NOT a textarea)
static lv_obj_t *s_lbl_status = NULL;

/* Rolling log buffer. The CAN task only appends into this RAM buffer (cheap,
   no LVGL); an lv_timer flushes it to the textarea a few times per second on
   the LVGL task. This keeps expensive textarea re-layout off the CAN task and
   bounds it to a fixed rate over fixed-size content. */
#define LOG_BUF_CAP 8192
static char  s_log_buf[LOG_BUF_CAP];
static size_t s_log_len = 0;
static volatile bool s_log_dirty = false;
static SemaphoreHandle_t s_log_mux = NULL;
static lv_timer_t *s_flush_timer = NULL;

/* Runs on the LVGL task under the LVGL lock (lv_timer_handler holds it). */
static void can_debug_ui_flush_cb(lv_timer_t *t) {
  (void)t;
  if (!s_log_label || !s_log_dirty)
    return;
  if (lv_disp_get_scr_act(NULL) != ui_CANDebugScreen)
    return; // Only pay the re-layout cost while the screen is visible.
  if (!s_log_mux || xSemaphoreTake(s_log_mux, 0) != pdTRUE)
    return; // Briefly busy in add_log; try again next tick.

  /* A label lays the whole text out in one pass (O(n)); a textarea would insert
     it char-by-char with a re-layout each time (O(n^2)) and hang the LVGL task. */
  lv_label_set_text(s_log_label, s_log_buf);
  s_log_dirty = false;
  xSemaphoreGive(s_log_mux);

  /* Scroll the container to the bottom to show the latest lines. */
  lv_obj_update_layout(s_log_cont);
  lv_obj_scroll_to_y(s_log_cont, LV_COORD_MAX, LV_ANIM_OFF);
}

void can_debug_ui_init(void) {
  if (!ui_CANDebugScreen)
    return;

  /* Create status label at the top */
  s_lbl_status = lv_label_create(ui_CANDebugScreen);
  lv_label_set_text(s_lbl_status, "CAN Status: OK");
  lv_obj_align(s_lbl_status, LV_ALIGN_TOP_MID, 0, 5);
  lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0xFFFFFF), 0);

  /* Scrollable container holding a label for terminal-style logging. A label
     (not a textarea) is used because setting large text into a textarea inserts
     it character-by-character with a full re-layout per char (O(n^2)) and hangs
     the LVGL task; a label lays the whole string out in a single pass. */
  s_log_cont = lv_obj_create(ui_CANDebugScreen);
  lv_obj_set_width(s_log_cont, lv_pct(95));
  lv_obj_set_height(s_log_cont, lv_pct(80));
  lv_obj_align(s_log_cont, LV_ALIGN_TOP_MID, 0, 35);
  lv_obj_set_style_bg_color(s_log_cont, lv_color_hex(0x000000), 0);
  lv_obj_set_style_pad_all(s_log_cont, 4, 0);
  lv_obj_set_scroll_dir(s_log_cont, LV_DIR_VER);

  s_log_label = lv_label_create(s_log_cont);
  lv_obj_set_width(s_log_label, lv_pct(100));
  lv_label_set_long_mode(s_log_label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(s_log_label, "");
  lv_obj_set_style_text_color(s_log_label, lv_color_hex(0x00FF00), 0); // Green text
  lv_obj_set_style_text_font(s_log_label, &lv_font_montserrat_12, 0);

  /* Set up the rolling-log buffer and its periodic flush timer. */
  if (!s_log_mux)
    s_log_mux = xSemaphoreCreateMutex();
  s_log_len = 0;
  s_log_buf[0] = '\0';
  s_log_dirty = false;
  if (!s_flush_timer)
    s_flush_timer = lv_timer_create(can_debug_ui_flush_cb, 250, NULL); // ~4 Hz
}

/* Called from the CAN task. Only appends to the RAM rolling buffer — no LVGL
   calls — so it is cheap and safe to run at the CAN message rate. The textarea
   is updated separately by can_debug_ui_flush_cb on the LVGL task. */
void can_debug_ui_add_log(const char *text) {
  if (!s_log_mux || !text)
    return;
  size_t tlen = strlen(text);
  if (tlen == 0)
    return;

  if (xSemaphoreTake(s_log_mux, pdMS_TO_TICKS(10)) != pdTRUE)
    return; // Flush in progress; drop this chunk rather than block the CAN task.

  if (tlen >= LOG_BUF_CAP) {
    /* Single chunk bigger than the buffer: keep only its tail. */
    size_t keep = LOG_BUF_CAP - 1;
    memcpy(s_log_buf, text + (tlen - keep), keep);
    s_log_len = keep;
  } else {
    /* Make room by dropping whole oldest lines from the front. */
    if (s_log_len + tlen >= LOG_BUF_CAP) {
      size_t drop = (s_log_len + tlen) - (LOG_BUF_CAP - 1);
      while (drop < s_log_len && s_log_buf[drop] != '\n')
        drop++;
      if (drop < s_log_len && s_log_buf[drop] == '\n')
        drop++; // include the newline so we start on a clean line
      memmove(s_log_buf, s_log_buf + drop, s_log_len - drop);
      s_log_len -= drop;
    }
    memcpy(s_log_buf + s_log_len, text, tlen);
    s_log_len += tlen;
  }
  s_log_buf[s_log_len] = '\0';
  s_log_dirty = true;

  xSemaphoreGive(s_log_mux);
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
