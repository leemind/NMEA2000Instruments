#include "ota.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_crt_bundle.h"

static const char *TAG = "OTA_MANAGER";

// Default base URL for firmware updates
// User can override this in their build or settings later
#define FW_BASE_URL "https://firmware.sailstudio.tech/WindInstruments/"
#define VERSION_FILE_URL FW_BASE_URL "version.txt"
#define BINARY_FILE_URL  FW_BASE_URL "NMEA2000Instruments.bin"

static ota_state_t current_state = OTA_STATE_IDLE;
static char result_message[128] = "Ready";
static char remote_version_str[32] = "";

// Forward declarations
static void check_update_task(void *pvParameter);
static void upgrade_task(void *pvParameter);

void ota_init(void) {
    current_state = OTA_STATE_IDLE;
    strlcpy(result_message, "Ready", sizeof(result_message));
}

ota_state_t ota_get_state(void) {
    return current_state;
}

const char* ota_get_result_message(void) {
    return result_message;
}

const char* ota_get_remote_version_str(void) {
    return remote_version_str;
}

/**
 * @brief Simple version comparison (supports major.minor.patch)
 * Returns true if remote > current
 */
static bool is_newer_version(const char* current, const char* remote) {
    int cur_maj, cur_min, cur_pat;
    int rem_maj, rem_min, rem_pat;

    if (sscanf(current, "%d.%d.%d", &cur_maj, &cur_min, &cur_pat) < 1) return false;
    if (sscanf(remote, "%d.%d.%d", &rem_maj, &rem_min, &rem_pat) < 1) return false;

    if (rem_maj > cur_maj) return true;
    if (rem_maj < cur_maj) return false;
    
    if (rem_min > cur_min) return true;
    if (rem_min < cur_min) return false;

    return rem_pat > cur_pat;
}

void ota_check_for_update(void) {
    if (current_state == OTA_STATE_CHECKING || current_state == OTA_STATE_UPDATING) {
        return;
    }
    xTaskCreate(&check_update_task, "check_update_task", 8192, NULL, 5, NULL);
}

void ota_start_upgrade(void) {
    if (current_state != OTA_STATE_UPDATE_AVAILABLE) {
        ESP_LOGW(TAG, "Upgrade called but no update available or already in progress");
        return;
    }
    xTaskCreate(&upgrade_task, "upgrade_task", 8192, NULL, 5, NULL);
}

static void check_update_task(void *pvParameter) {
    current_state = OTA_STATE_CHECKING;
    strlcpy(result_message, "Checking for updates...", sizeof(result_message));
    ESP_LOGI(TAG, "Starting version check from %s", VERSION_FILE_URL);

    esp_http_client_config_t config = {
        .url = VERSION_FILE_URL,
        .timeout_ms = 10000,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_open(client, 0);

    if (err == ESP_OK) {
        int content_length = esp_http_client_fetch_headers(client);
        if (content_length > 0 && content_length < sizeof(remote_version_str)) {
            int read = esp_http_client_read(client, remote_version_str, content_length);
            remote_version_str[read] = '\0';
            
            // Trim whitespace/newlines
            char *end = remote_version_str + strlen(remote_version_str) - 1;
            while(end >= remote_version_str && (*end == '\n' || *end == '\r' || *end == ' ')) {
                *end-- = '\0';
            }

            const esp_app_desc_t *app_desc = esp_app_get_description();
            ESP_LOGI(TAG, "Local version: %s, Remote version: %s", app_desc->version, remote_version_str);

            if (is_newer_version(app_desc->version, remote_version_str)) {
                current_state = OTA_STATE_UPDATE_AVAILABLE;
                snprintf(result_message, sizeof(result_message), "New version %s available!", remote_version_str);
            } else {
                current_state = OTA_STATE_UP_TO_DATE;
                strlcpy(result_message, "Firmware is up to date.", sizeof(result_message));
            }
        } else {
            current_state = OTA_STATE_FAILED;
            strlcpy(result_message, "Error reading remote version.", sizeof(result_message));
        }
    } else {
        current_state = OTA_STATE_FAILED;
        snprintf(result_message, sizeof(result_message), "Failed to connect to server: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

static void upgrade_task(void *pvParameter) {
    current_state = OTA_STATE_UPDATING;
    strlcpy(result_message, "Downloading update...", sizeof(result_message));
    ESP_LOGI(TAG, "Starting upgrade from %s", BINARY_FILE_URL);

    esp_http_client_config_t config = {
        .url = BINARY_FILE_URL,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        current_state = OTA_STATE_SUCCESS;
        strlcpy(result_message, "Update successful! Rebooting...", sizeof(result_message));
        ESP_LOGI(TAG, "OTA Success. Rebooting in 2s...");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        esp_restart();
    } else {
        current_state = OTA_STATE_FAILED;
        snprintf(result_message, sizeof(result_message), "Update failed: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Firmware upgrade failed!");
    }

    vTaskDelete(NULL);
}
