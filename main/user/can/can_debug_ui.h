#ifndef CAN_DEBUG_UI_H
#define CAN_DEBUG_UI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the CAN Debug UI on the CanDebugScreen
 */
void can_debug_ui_init(void);

/**
 * @brief Update the UI with a new CAN message
 * @param can_id 29-bit CAN ID
 * @param data CAN payload
 * @param len length of the payload
 */
void can_debug_ui_update_msg(uint32_t can_id, const uint8_t *data, uint8_t len);

/**
 * @brief Update the UI with CAN error states
 * @param alerts the TWAI alerts triggered
 */
void can_debug_ui_update_status(uint32_t alerts);

/**
 * @brief Add a comprehensive log message to the UI
 * @param text The log text to append
 */
void can_debug_ui_add_log(const char *text);

#ifdef __cplusplus
}
#endif

#endif // CAN_DEBUG_UI_H
