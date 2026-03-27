/* user/events/ui_events.c
 *
 * Implementations of ALL UI event callback functions declared in ui_events.h.
 *
 * SquareLine Studio generates ui_events.h (declarations) inside the ui/ folder
 * and may overwrite it on each export — that is fine.  This file lives outside
 * that directory so it is never touched by SquareLine.
 *
 * Add new event handlers here as you wire up new screen elements in
 * SquareLine Studio.
 */
#include "ui.h"          /* pulls in lvgl and all screen headers */
#include "settings.h"    /* settings_set_brightness() etc.       */
#include "esp_log.h"     /* ESP_LOGI()                           */
#include "ui_datapicker_ext.h"
#include "screens/ui_Wind.h"
#include "screens/ui_DatapickerScreen.h"
#include "screens/ui_Settings.h"
#include "wifi.h"

static const char *TAG       = "UI_EVENTS";
/* ---------------------------------------------------------------------------
 * Brightness slider  (Settings screen)
 *
 * Triggered by: BrightnessSlider → LV_EVENT_VALUE_CHANGED → set_brightness
 * Registered in SquareLine as the "Call function" action.
 * --------------------------------------------------------------------------*/
void set_brightness(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t   value  = lv_slider_get_value(slider);

    /* Clamp just in case the slider range ever changes */
    if (value < 0)   value = 0;
    if (value > 100) value = 100;

    /* Applies brightness immediately via IO_EXTENSION_Pwm_Output()
     * and persists to NVS so the value survives a reboot. */
    ESP_LOGI(TAG, "Brightness -> %d (saved)", (int)value);
    settings_set_brightness((uint8_t)value);
}

void set_depth_unit(lv_event_t * e)
{
    lv_obj_t *dropdown = lv_event_get_target(e);
    int32_t   value  = lv_dropdown_get_selected(dropdown);

    /* Clamp just in case the dropdown range ever changes */
    if (value < 0)   value = 0;
    if (value > 1) value = 1;

    ESP_LOGI(TAG, "Depth unit -> %d (saved)", (int)value);
    settings_set_depth_unit((uint8_t)value);
}

void set_wind_unit(lv_event_t * e)
{
    lv_obj_t *dropdown = lv_event_get_target(e);
    int32_t   value  = lv_dropdown_get_selected(dropdown);

    /* Clamp just in case the dropdown range ever changes */
    if (value < 0)   value = 0;
    if (value > 1) value = 1;

    /* Applies wind unit immediately via IO_EXTENSION_Pwm_Output()
     * and persists to NVS so the value survives a reboot. */
    ESP_LOGI(TAG, "Wind unit -> %d (saved)", (int)value);
    settings_set_wind_unit((uint8_t)value);
}

void set_autodepth_value(lv_event_t * e)
{
    lv_obj_t *roller = lv_event_get_target(e);
    int32_t   value  = lv_roller_get_selected(roller);
    
    /* persists to NVS so the value survives a reboot. */
    ESP_LOGI(TAG, "Auto depth value -> %d (saved)", (int)value);
    settings_set_autodepth_value((uint8_t)value);
}

void use_transducer_offset_value(lv_event_t * e)
{
    lv_obj_t *toggle = lv_event_get_target(e);
    bool   value  = lv_obj_has_state(toggle, LV_STATE_CHECKED);

    ESP_LOGI(TAG, "Use transducer offset -> %d (saved)", (int)value);
    settings_set_use_transducer_offset(value);
}

static void wifi_timer_cb(lv_timer_t * t)
{
    if (ui_Label1) {
        lv_label_set_text(ui_Label1, wifi_get_status_str());
    }
}

void wifi_credentials_changed(lv_event_t * e)
{
    const char * ssid = lv_textarea_get_text(ui_SSIDInput);
    const char * pass = lv_textarea_get_text(ui_PasswordInput);
    
    ESP_LOGI(TAG, "WiFi credentials changed: SSID=%s", ssid);
    settings_set_wifi_credentials(ssid, pass);
    wifi_connect(ssid, pass);
}

void settings_screen_loaded(lv_event_t * e)
{
    /* Populate widgets from persisted settings */
    lv_slider_set_value(uic_BrightnessSlider, (int32_t)settings_get().brightness, LV_ANIM_OFF);
    lv_dropdown_set_selected(uic_DepthUnitChoice, (int32_t)settings_get().depth_unit);
    lv_dropdown_set_selected(uic_WindUnitsChoice, (int32_t)settings_get().wind_unit);
    lv_roller_set_selected(uic_AutoDepthValue, (uint16_t)settings_get().autodepth_value, LV_ANIM_OFF);

    /* Populate WiFi credentials */
    app_settings_t settings = settings_get();
    lv_textarea_set_text(ui_SSIDInput, settings.wifi_ssid);
    lv_textarea_set_text(ui_PasswordInput, settings.wifi_pass);

    /* Start status timer */
    static lv_timer_t * wifi_timer = NULL;
    if (!wifi_timer) {
        wifi_timer = lv_timer_create(wifi_timer_cb, 500, NULL);
    }

    /* Add events to SSID and Password inputs if not already added */
    /* Note: SquareLine doesn't always handle multiple events well, so we add them here */
    lv_obj_add_event_cb(ui_SSIDInput, wifi_credentials_changed, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(ui_PasswordInput, wifi_credentials_changed, LV_EVENT_READY, NULL);
}

void ui_event_databox_long_pressed(lv_event_t * e)
{
    lv_obj_t *target = lv_event_get_target(e);
    int index = -1;

    /* Identify which databox was pressed */
    if      (target == ui_LHS1) index = 0;
    else if (target == ui_LHS2) index = 1;
    else if (target == ui_LHS3) index = 2;
    else if (target == ui_LHS4) index = 3;
    else if (target == ui_LHS5) index = 4;
    else if (target == ui_RHS1) index = 5;
    else if (target == ui_RHS2) index = 6;
    else if (target == ui_RHS3) index = 7;
    else if (target == ui_RHS4) index = 8;
    else if (target == ui_RHS5) index = 9;

    if (index >= 0) {
        ESP_LOGI(TAG, "Databox %d long pressed, opening picker", index);
        ui_datapicker_ext_set_edit_index(index);
        _ui_screen_change(&ui_DatapickerScreen, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_DatapickerScreen_screen_init);
    }
}

void ui_event_datapicker_load(lv_event_t * e)
{
    ui_datapicker_ext_load();
}

void ui_event_datapicker_save(lv_event_t * e)
{
    ui_datapicker_ext_save();
    _ui_screen_change(&ui_Wind, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_Wind_screen_init);
}

void ui_FirmwareButtonPressed(lv_event_t * e)
{
    ESP_LOGI(TAG, "Firmware button pressed");
}

