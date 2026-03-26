/*****************************************************************************
 * | File       :   main.c
 * | Author     :   Waveshare team
 * | Function   :   Main function
 * | Info       :
 * |                UI Design：
 *                          1. User Login and Creation: Users can log in or
 *create new accounts, and the created users are saved to NVS, so data is not
 *lost after power-down.
 *                          2. Wi-Fi: Can connect to Wi-Fi and start an access
 *point (hotspot).
 *                          3. RS485: Can send and receive data, with data
 *displayed on the screen.
 *                          4. PWM: Can modify PWM output in multiple ways to
 *control screen brightness. Additionally, it can display information from a
 *Micro SD card.
 *                          5. CAN: Can send and receive data, with data
 *displayed on the screen.
 *----------------
 * | Version    :   V1.0
 * | Date       :   2025-05-08
 * | Info       :   Basic version
 *
 ******************************************************************************/
#include "can.h"          // Header for CAN communication
#include "can_debug_ui.h" // Header for dynamic CAN Debug UI init
#include "esp_littlefs.h" // Header for LittleFS file system operations
#include "lvgl_port.h"    // Header for LVGL port initialization and locking
#include "pwm.h" // Header for PWM initialization (used for backlight control)
#include "rgb_lcd_port.h" // Header for Waveshare RGB LCD driver
#include "settings.h"     // Header for persistent settings (NVS)
#include "gt911.h"  // Header for touch screen operations (GT911)
#include "ui.h"           // Header for user interface initialization
#include "wifi.h"         // WiFi driver

#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main"; // Tag used for ESP log output

/**
 * @brief Main application function.
 *
 * This function initializes the necessary hardware components such as the touch
 * screen and RGB LCD display, sets up the LVGL library for graphics rendering,
 * and runs the LVGL demo UI.
 *
 * - Initializes the GT911 touch screen controller.
 * - Initializes the Waveshare ESP32-S3 RGB LCD display.
 * - Initializes the LVGL library for graphics rendering.
 * - Runs the LVGL demo UI.
 *
 * @return None
 */

void little_fs_init(void) {
  esp_vfs_littlefs_conf_t conf = {.base_path = "/littlefs",
                                  .partition_label = "littlefs",
                                  .format_if_mount_failed = false,
                                  .dont_mount = false};

  esp_err_t ret = esp_vfs_littlefs_register(&conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
    return;
  }

  size_t total = 0, used = 0;
  ret = esp_littlefs_info(conf.partition_label, &total, &used);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "LittleFS initialized. Total: %d bytes, Used: %d bytes",
             total, used);
  } else {
    ESP_LOGE(TAG, "Failed to get LittleFS info (%s)", esp_err_to_name(ret));
  }
}

void app_main() {
  // Initialise NVS and load persisted settings (brightness, units, etc.)
  // Must run before ui_init() so the UI widgets start with correct values.
  settings_init();

  little_fs_init(); // Initialize LittleFS for file system operations on the
                    // flash memory

  static esp_lcd_panel_handle_t panel_handle = NULL; // Handle for the LCD panel
  static esp_lcd_touch_handle_t tp_handle = NULL; // Handle for the touch panel

  // Initialize the GT911 touch screen controller
  // This sets up the touch functionality of the screen.
  tp_handle = touch_gt911_init();

  // Initialize the Waveshare ESP32-S3 RGB LCD hardware
  // This prepares the LCD panel for display operations.
  panel_handle = waveshare_esp32_s3_rgb_lcd_init();

  // Turn on the LCD backlight
  // This ensures the display is visible.
  wavesahre_rgb_lcd_bl_on();

  // Initialize the LVGL library, linking it to the LCD and touch panel handles
  // LVGL is a lightweight graphics library used for rendering UI elements.
  ESP_ERROR_CHECK(lvgl_port_init(panel_handle, tp_handle));

  // Lock the LVGL port to ensure thread safety during API calls
  // This prevents concurrent access issues when using LVGL functions.
  if (lvgl_port_lock(-1)) {

    // Initialize the UI components with LVGL (e.g., demo screens, sliders)
    // This sets up the user interface elements using the LVGL library.
    ui_init();

    /* Initialize dynamic UI components NOT drawn in SquareLine */
    can_debug_ui_init();

    // Release the mutex after LVGL operations are complete
    // This allows other tasks to access the LVGL port.
    lvgl_port_unlock();
  }
  vTaskDelay(100); // Delay for a short period to ensure stable initialization

  // Initialize PWM for controlling backlight brightness (if applicable)
  // PWM is used to adjust the brightness of the LCD backlight.
  pwm_init();

  // Apply persisted brightness now that the I2C / IO expander is ready.
  // pwm_init() sets an initial 50% duty; this overrides it with the saved
  // value.
  IO_EXTENSION_Pwm_Output(100 - settings_get().brightness);

  // Initialize SD card operations
  // This sets up the Micro SD card for data storage and retrieval.
  // sd_init();

  // Start the WiFi system
  wifi_init();

  // Start the CAN task
  xTaskCreatePinnedToCore(can_task, "can_task", 6 * 1024, NULL, 15, &can_TaskHandle, 0);
}