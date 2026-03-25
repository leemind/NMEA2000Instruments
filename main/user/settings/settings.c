#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "settings.h"
#include "io_extension.h"   /* IO_EXTENSION_Pwm_Output() */

static const char *TAG       = "SETTINGS";
static const char *NVS_NS    = "app_settings";  /* NVS namespace           */
static const char *KEY_BRIGHT = "brightness";
static const char *KEY_DEPTH  = "depth_unit";
static const char *KEY_WIND   = "wind_unit";
static const char *KEY_AUTODEPTH = "autodepth_value";
static const char *KEY_USE_TRANSDUCER_OFFSET = "use_transducer_offset";

/* In-memory copy — written once on init, updated by every setter */
static app_settings_t s_settings = {
    .brightness = SETTINGS_DEFAULT_BRIGHTNESS,
    .depth_unit = SETTINGS_DEFAULT_DEPTH_UNIT,
    .wind_unit  = SETTINGS_DEFAULT_WIND_UNIT,
    .autodepth_value = SETTINGS_DEFAULT_AUTODEPTH_VALUE,
    .use_transducer_offset = SETTINGS_DEFAULT_USE_TRANSDUCER_OFFSET
};

const double wind_convert[4]     = {1.94384, 2.23694, 1.0, 3.6};
const char   wind_unit_str[4][4] = {"kts", "mph", "m/s", "kph"};
const double depth_convert[2]    = {1, 3.28084};
const char   depth_unit_str[2][4] = {"m", "ft"};

/* -----------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------- */

static nvs_handle_t open_nvs(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return 0;
    }
    return h;
}

static void apply_brightness(uint8_t v)
{
    /* PWM is active-low: 100% duty = off, 0% duty = full brightness.
     * Invert so that the stored value is intuitive (100 = bright). */
    IO_EXTENSION_Pwm_Output(100 - v);
}

/* -----------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------- */

void settings_init(void)
{
    /* Initialise NVS flash partition.  If the partition was truncated or has
     * an incompatible version, erase it and re-initialise. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition problem (%s) — erasing and reinitialising",
                 esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    nvs_handle_t h = open_nvs();
    if (!h) {
        ESP_LOGW(TAG, "Using default settings (NVS unavailable)");
        apply_brightness(s_settings.brightness);
        return;
    }

    /* brightness */
    uint8_t bv = SETTINGS_DEFAULT_BRIGHTNESS;
    err = nvs_get_u8(h, KEY_BRIGHT, &bv);
    if (err == ESP_OK) {
        s_settings.brightness = bv;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Error reading brightness: %s", esp_err_to_name(err));
    }

    /* depth_unit */
    uint8_t dv = (uint8_t)SETTINGS_DEFAULT_DEPTH_UNIT;
    err = nvs_get_u8(h, KEY_DEPTH, &dv);
    if (err == ESP_OK) {
        s_settings.depth_unit = (depth_unit_t)dv;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Error reading depth_unit: %s", esp_err_to_name(err));
    }

    /* wind_unit */
    uint8_t wv = (uint8_t)SETTINGS_DEFAULT_WIND_UNIT;
    err = nvs_get_u8(h, KEY_WIND, &wv);
    if (err == ESP_OK) {
        s_settings.wind_unit = (wind_unit_t)wv;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Error reading wind_unit: %s", esp_err_to_name(err));
    }

    /* autodepth_value */
    uint8_t av = (uint8_t)SETTINGS_DEFAULT_AUTODEPTH_VALUE;
    err = nvs_get_u8(h, KEY_AUTODEPTH, &av);
    if (err == ESP_OK) {
        s_settings.autodepth_value = av;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Error reading autodepth_value: %s", esp_err_to_name(err));
    }

    nvs_close(h);

    ESP_LOGI(TAG, "Settings loaded — brightness=%d  depth=%s  wind=%s",
             s_settings.brightness,
             s_settings.depth_unit == DEPTH_METRES ? "m" : "ft",
             s_settings.wind_unit  == WIND_KNOTS   ? "kts" :
             s_settings.wind_unit  == WIND_MPH     ? "mph" :
             s_settings.wind_unit  == WIND_MS      ? "m/s" : "kph");

    /* NOTE: brightness is NOT applied here — I2C / IO expander are not yet
     * initialised at this point in app_main.  Call settings_apply_brightness()
     * from main.c after pwm_init(). */
}

app_settings_t settings_get(void)
{
    return s_settings;
}

void settings_set_brightness(uint8_t value)
{
    if (value > 100) value = 100;
    s_settings.brightness = value;
    apply_brightness(value);

    nvs_handle_t h = open_nvs();
    if (!h) return;
    nvs_set_u8(h, KEY_BRIGHT, value);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGD(TAG, "brightness -> %d (saved)", value);
}

void settings_set_depth_unit(depth_unit_t unit)
{
    s_settings.depth_unit = unit;

    nvs_handle_t h = open_nvs();
    if (!h) return;
    nvs_set_u8(h, KEY_DEPTH, (uint8_t)unit);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGD(TAG, "depth_unit -> %d (saved)", (int)unit);
}

void settings_set_wind_unit(wind_unit_t unit)
{
    s_settings.wind_unit = unit;

    nvs_handle_t h = open_nvs();
    if (!h) return;
    nvs_set_u8(h, KEY_WIND, (uint8_t)unit);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGD(TAG, "wind_unit -> %d (saved)", (int)unit);
}

void settings_set_autodepth_value(uint8_t value)
{
    s_settings.autodepth_value = value;

    nvs_handle_t h = open_nvs();
    if (!h) return;
    nvs_set_u8(h, KEY_AUTODEPTH, value);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGD(TAG, "autodepth_value -> %d (saved)", value);
}

void settings_set_use_transducer_offset(bool value) {
    s_settings.use_transducer_offset = value;

    nvs_handle_t h = open_nvs();
    if (!h) return;
    nvs_set_u8(h, KEY_USE_TRANSDUCER_OFFSET, (uint8_t)value);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGD(TAG, "use_transducer_offset -> %d (saved)", (int)value);
}

