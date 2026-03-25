/*****************************************************************************
 * | File         :   io_extension.c
 * | Author       :   Waveshare team
 * | Function     :   PCA9557 IO Driver
 * | Info         :   Refactored for standard register map at 0x24.
 ******************************************************************************/
 #include "io_extension.h"
 #include "freertos/FreeRTOS.h"
 #include "freertos/semphr.h"
 #include "esp_log.h"
 
 static const char *TAG = "IO_EXT";
 io_extension_obj_t IO_EXTENSION;
 static SemaphoreHandle_t io_mutex = NULL;
 
 static void io_lock() {
     if (io_mutex == NULL) io_mutex = xSemaphoreCreateMutex();
     xSemaphoreTake(io_mutex, portMAX_DELAY);
 }
 
 static void io_unlock() {
     if (io_mutex != NULL) xSemaphoreGive(io_mutex);
 }
 
 static void init_handle() {
     if (IO_EXTENSION.dev != NULL) return;
     extern DEV_I2C_Port handle;
     i2c_device_config_t conf = {
         .scl_speed_hz = 100000,
         .device_address = IO_EXTENSION_ADDR,
     };
     ESP_ERROR_CHECK(i2c_master_bus_add_device(handle.bus, &conf, &IO_EXTENSION.dev));
 }
 
 void IO_EXTENSION_ScanBus() {
     extern DEV_I2C_Port handle;
     ESP_LOGI(TAG, "Scanning I2C Bus...");
     for (int i = 0x01; i < 0x7F; i++) {
         if (i2c_master_probe(handle.bus, i, 100) == ESP_OK) {
             ESP_LOGI(TAG, "Found device at 0x%02X", i);
         }
     }
 }

 void IO_EXTENSION_Init() {
     io_lock();
     IO_EXTENSION.addr = IO_EXTENSION_ADDR;
     init_handle();
     IO_EXTENSION_ScanBus();
     
     // PCA9557 Config: 0=Output, 1=Input. Set all to Output.
     uint8_t config_data[2] = {PCA9557_REG_CONFIG, 0x00};
     i2c_master_transmit(IO_EXTENSION.dev, config_data, 2, 100);
     
     // Set all outputs high initially
     IO_EXTENSION.Last_io_value = 0xFF;
     uint8_t out_data[2] = {PCA9557_REG_OUTPUT, 0xFF};
     i2c_master_transmit(IO_EXTENSION.dev, out_data, 2, 100);
     
     io_unlock();
     ESP_LOGI(TAG, "PCA9557 Driver Initialized at 0x24");
 }
 
 void IO_EXTENSION_Output(uint8_t pin, uint8_t value) {
     io_lock();
     init_handle();
     if (value == 1) IO_EXTENSION.Last_io_value |= (1 << pin);
     else IO_EXTENSION.Last_io_value &= (~(1 << pin));
     
     uint8_t data[2] = {PCA9557_REG_OUTPUT, IO_EXTENSION.Last_io_value};
     i2c_master_transmit(IO_EXTENSION.dev, data, 2, 100);
     io_unlock();
 }
 
 uint8_t IO_EXTENSION_Input(uint8_t pin) {
     uint8_t reg = PCA9557_REG_INPUT;
     uint8_t val = 0xFF;
     io_lock();
     init_handle();
     i2c_master_transmit_receive(IO_EXTENSION.dev, &reg, 1, &val, 1, 100);
     io_unlock();
     return ((val & (1 << pin)) > 0);
 }
 
 void IO_EXTENSION_Pwm_Output(uint8_t Value) {
     // If BL_PWM is connected to IO2.
     IO_EXTENSION_Output(IO_EXTENSION_IO_2, (Value > 50)); // Simple toggle for now
 }
 
 uint16_t IO_EXTENSION_Adc_Input() {
     return 0; // PCA9557 has no ADC
 }
