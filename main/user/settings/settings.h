#ifndef _SETTINGS_H_
#define _SETTINGS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Settings enumerations
 * -------------------------------------------------------------------- */

typedef enum {
    DEPTH_METRES = 0,
    DEPTH_FEET   = 1,
} depth_unit_t;

typedef enum {
    WIND_KNOTS = 0,
    WIND_MPH   = 1,
    WIND_MS    = 2,
    WIND_KPH   = 3,
} wind_unit_t;

/* -----------------------------------------------------------------------
 * Settings structure
 * -------------------------------------------------------------------- */

typedef struct {
    uint8_t      brightness;   /* Screen backlight duty cycle, 0-100        */
    depth_unit_t depth_unit;   /* DEPTH_METRES or DEPTH_FEET                */
    wind_unit_t  wind_unit;    /* WIND_KNOTS / WIND_MPH / WIND_MS / WIND_KPH */
} app_settings_t;

/* -----------------------------------------------------------------------
 * Default values applied on first boot (no NVS key found)
 * -------------------------------------------------------------------- */
#define SETTINGS_DEFAULT_BRIGHTNESS  75
#define SETTINGS_DEFAULT_DEPTH_UNIT  DEPTH_METRES
#define SETTINGS_DEFAULT_WIND_UNIT   WIND_KNOTS

/* -----------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------- */

/**
 * @brief Initialise NVS and load persisted settings.
 *
 * Must be called once from app_main() before ui_init() so the UI can
 * read current values when it builds its widgets.
 * Applies the loaded brightness immediately via IO_EXTENSION_Pwm_Output().
 */
void settings_init(void);

/**
 * @brief Return a copy of the current settings.
 *
 * Safe to call from any task.
 */
app_settings_t settings_get(void);

/**
 * @brief Set screen brightness (0-100) and persist to NVS.
 */
void settings_set_brightness(uint8_t value);

/**
 * @brief Set depth display unit and persist to NVS.
 */
void settings_set_depth_unit(depth_unit_t unit);

/**
 * @brief Set wind speed display unit and persist to NVS.
 */
void settings_set_wind_unit(wind_unit_t unit);

#ifdef __cplusplus
}
#endif

#endif /* _SETTINGS_H_ */
