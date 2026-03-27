#ifndef OTA_H
#define OTA_H

#include "esp_err.h"

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_CHECKING,
    OTA_STATE_UPDATE_AVAILABLE,
    OTA_STATE_UP_TO_DATE,
    OTA_STATE_UPDATING,
    OTA_STATE_SUCCESS,
    OTA_STATE_FAILED
} ota_state_t;

/**
 * @brief Initialize the OTA manager
 */
void ota_init(void);

/**
 * @brief Check for a new version on the remote server
 * 
 * Fetches version.txt from the web.
 * Results are updated in global state and message.
 */
void ota_check_for_update(void);

/**
 * @brief Start the firmware upgrade process
 * 
 * Downloads and installs the .bin file using HTTPS OTA.
 * Reboots on success.
 */
void ota_start_upgrade(void);

/**
 * @brief Get the current OTA state
 */
ota_state_t ota_get_state(void);

/**
 * @brief Get the last OTA result message (for UI display)
 */
const char* ota_get_result_message(void);

/**
 * @brief Get the remote version string if found
 */
const char* ota_get_remote_version_str(void);

#endif // OTA_H
