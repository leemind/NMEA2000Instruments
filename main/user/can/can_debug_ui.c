#include "can_debug_ui.h"
#include "ui.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "twai.h"
#include <stdio.h>
#include <string.h>

#define MAX_CAN_MSGS 50

typedef struct {
    uint32_t can_id;
    uint8_t  len;
    uint8_t  data[8];
} can_msg_slot_t;

static can_msg_slot_t s_slots[MAX_CAN_MSGS];
static int s_num_slots = 0;

static lv_obj_t * s_table = NULL;
static lv_obj_t * s_lbl_status = NULL;

static void update_row_ui(int row_idx)
{
    if (!s_table) return;

    char buf[64];
    can_msg_slot_t *slot = &s_slots[row_idx];

    /* ID Column (Row = row_idx + 1 because index 0 is header) */
    snprintf(buf, sizeof(buf), "0x%08X", (unsigned int)slot->can_id);
    lv_table_set_cell_value(s_table, row_idx + 1, 0, buf);

    /* Len Column */
    snprintf(buf, sizeof(buf), "%d", slot->len);
    lv_table_set_cell_value(s_table, row_idx + 1, 1, buf);

    /* Data Column */
    buf[0] = '\0';
    for (int i = 0; i < slot->len; i++) {
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%02X ", slot->data[i]);
    }
    lv_table_set_cell_value(s_table, row_idx + 1, 2, buf);
}

static void candebug_screen_event_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) == LV_EVENT_SCREEN_LOADED) {
        for (int i = 0; i < s_num_slots; i++) {
            update_row_ui(i);
        }
    }
}

void can_debug_ui_init(void)
{
    if (!ui_CANDebugScreen) return;

    /* Create status label at the top */
    s_lbl_status = lv_label_create(ui_CANDebugScreen);
    lv_label_set_text(s_lbl_status, "CAN Status: OK");
    lv_obj_align(s_lbl_status, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0xFFFFFF), 0);

    /* Create table */
    s_table = lv_table_create(ui_CANDebugScreen);
    lv_obj_set_width(s_table, lv_pct(95));
    lv_obj_set_height(s_table, lv_pct(70));
    lv_obj_align(s_table, LV_ALIGN_TOP_MID, 0, 40);

    /* Column titles */
    lv_table_set_col_cnt(s_table, 3);
    lv_table_set_col_width(s_table, 0, 100);  // ID
    lv_table_set_col_width(s_table, 1, 50);   // Len
    lv_table_set_col_width(s_table, 2, 230);  // Data

    lv_table_set_cell_value(s_table, 0, 0, "ID");
    lv_table_set_cell_value(s_table, 0, 1, "Len");
    lv_table_set_cell_value(s_table, 0, 2, "Data");

    /* Register callback so table updates correctly when we nav to it */
    lv_obj_add_event_cb(ui_CANDebugScreen, candebug_screen_event_cb, LV_EVENT_SCREEN_LOADED, NULL);
}

void can_debug_ui_update_msg(uint32_t can_id, const uint8_t *data, uint8_t len)
{
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
            return; /* Ignore if table is full */
        }
    }

    /* Update internal data */
    s_slots[found_idx].can_id = can_id;
    s_slots[found_idx].len = len > 8 ? 8 : len;
    memcpy(s_slots[found_idx].data, data, s_slots[found_idx].len);

    /* Only update ListView if current screen is CANDebug */
    if (lvgl_port_lock(0)) {
        if (lv_disp_get_scr_act(NULL) == ui_CANDebugScreen) {
            update_row_ui(found_idx);
        }
        lvgl_port_unlock();
    }
}

void can_debug_ui_update_status(uint32_t alerts)
{
    if (!alerts) return;

    if (lvgl_port_lock(0)) {
        if (s_lbl_status && lv_disp_get_scr_act(NULL) == ui_CANDebugScreen) {
            char buf[128] = "Alerts: ";
            if (alerts & TWAI_ALERT_BUS_OFF) strncat(buf, "BUS_OFF ", sizeof(buf) - strlen(buf) - 1);
            if (alerts & TWAI_ALERT_BUS_RECOVERED) strncat(buf, "RECOVERED ", sizeof(buf) - strlen(buf) - 1);
            if (alerts & TWAI_ALERT_RX_QUEUE_FULL) strncat(buf, "RX_Q_FULL ", sizeof(buf) - strlen(buf) - 1);
            if (alerts & TWAI_ALERT_ERR_PASS) strncat(buf, "ERR_PASS ", sizeof(buf) - strlen(buf) - 1);
            if (alerts & TWAI_ALERT_BUS_ERROR) strncat(buf, "BUS_ERR ", sizeof(buf) - strlen(buf) - 1);

            lv_label_set_text(s_lbl_status, buf);
            lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0xFF0000), 0);
        }
        lvgl_port_unlock();
    }
}
