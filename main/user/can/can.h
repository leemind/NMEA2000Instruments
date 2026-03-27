#ifndef _CAN_
#define _CAN_

#include "esp_twai_onchip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TX_GPIO_NUM GPIO_NUM_20 // Transmit GPIO number for CAN
#define RX_GPIO_NUM GPIO_NUM_19 // Receive GPIO number for CAN

// Define the maximum number of hexadecimal bytes that can be processed
#define MAX_HEX_DATA 32  // Maximum number of hexadecimal bytes

extern TaskHandle_t can_TaskHandle;  // Declare the task handle for the CAN task
extern struct cJSON *pgn_database;   // Global PGN database

/**
 * @brief Task for handling CAN communication.
 *
 * This task initializes the CAN interface, listens for CAN messages, 
 * processes the messages, and updates the user interface accordingly.
 * It continuously runs in a loop to read and process incoming CAN data.
 *
 * @param arg Argument passed to the task (not used in this function).
 */
void can_task(void *arg);

/**
 * @brief Pause/Resume the CAN processing task.
 * Used during OTA updates to reduce CPU load and prevent bus contention.
 */
void can_pause(void);
void can_resume(void);

/**
 * @brief Converts a hexadecimal string to an array of bytes.
 *
 * This function takes an input string representing hexadecimal values (with or without spaces)
 * and converts it to an array of bytes. The result is stored in the output array.
 *
 * @param input The input hexadecimal string to be converted.
 * @param output The output array where the resulting bytes will be stored.
 * @param max_output_size The maximum size of the output array.
 *
 * @return The number of bytes successfully converted, or a negative error code:
 *         -1 if the output buffer is too small,
 *         -2 if the input string contains non-hexadecimal characters.
 */
int string_to_hex(const char *input, uint8_t *output, size_t max_output_size);

#endif
