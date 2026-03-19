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
    settings_set_brightness((uint8_t)value);
}
