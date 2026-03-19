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
    lv_obj_t *dropdown = lv_event_get_target(e);
    int32_t   value  = lv_dropdown_get_selected(dropdown);

    /* Clamp just in case the dropdown range ever changes */
    if (value < 0)   value = 0;
    if (value > 1) value = 1;

    
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

void settings_screen_loaded(lv_event_t * e)
{
    /* Populate widgets from persisted settings */
    lv_slider_set_value(uic_BrightnessSlider, (int32_t)settings_get().brightness, LV_ANIM_OFF);
    lv_dropdown_set_selected(uic_DepthUnitChoice, (int32_t)settings_get().depth_unit);
    lv_dropdown_set_selected(uic_WindUnitsChoice, (int32_t)settings_get().wind_unit);
    lv_dropdown_set_selected(uic_AutoDepthValue, (int32_t)settings_get().autodepth_value);
}

