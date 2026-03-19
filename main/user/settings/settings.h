#ifndef _SETTINGS_H_
#define _SETTINGS_H_

#include <stdbool.h>
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

extern const double depth_convert[2];
extern const char   depth_unit_str[2][4];

typedef enum {
    WIND_KNOTS = 0,
    WIND_MPH   = 1,
    WIND_MS    = 2,
    WIND_KPH   = 3,
} wind_unit_t;

extern const double wind_convert[4];
extern const char   wind_unit_str[4][4];

/* -----------------------------------------------------------------------
 * Settings structure
 * -------------------------------------------------------------------- */

typedef struct {
    uint8_t      brightness;   /* Screen backlight duty cycle, 0-100        */
    depth_unit_t depth_unit;   /* DEPTH_METRES or DEPTH_FEET                */
    wind_unit_t  wind_unit;    /* WIND_KNOTS / WIND_MPH / WIND_MS / WIND_KPH */
    uint8_t      autodepth_value; /* Auto depth value, 0-100                 */
    bool         use_transducer_offset;
} app_settings_t;

/* -----------------------------------------------------------------------
 * Default values applied on first boot (no NVS key found)
 * -------------------------------------------------------------------- */
#define SETTINGS_DEFAULT_BRIGHTNESS  75
#define SETTINGS_DEFAULT_DEPTH_UNIT  DEPTH_METRES
#define SETTINGS_DEFAULT_WIND_UNIT   WIND_KNOTS
#define SETTINGS_DEFAULT_AUTODEPTH_VALUE  0
#define SETTINGS_DEFAULT_USE_TRANSDUCER_OFFSET true

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

void settings_set_autodepth_value(uint8_t value);

void settings_set_use_transducer_offset(bool value);

#ifdef __cplusplus
}
#endif

#endif /* _SETTINGS_H_ */
