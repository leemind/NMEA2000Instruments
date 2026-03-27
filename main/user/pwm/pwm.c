#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui.h"
#include "pwm.h"

static const char *TAG = "PWM";

/**
 * @brief Initialize the PWM module for LCD backlight control.
 * 
 * This function sets up the hardware PWM on the ESP32 and initializes
 * the IO extension chip's PWM output for the display brightness.
 */
void pwm_init()
{
    ESP_LOGI(TAG, "Initializing LCD Backlight PWM");

    // Initialize the GPIO pin for PWM with a 1 kHz frequency
    DEV_GPIO_PWM(LED_GPIO_PIN, 1000);

    // Set the initial PWM duty cycle to 50%
    // This value (0-100) controls the brightness relay/transistor
    IO_EXTENSION_Pwm_Output(50);
}