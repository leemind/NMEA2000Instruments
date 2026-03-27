/*****************************************************************************
 * | File         :   i2c.c
 * | Author       :   Waveshare team
 * | Function     :   Hardware underlying interface
 * | Info         :
 * |                 I2C driver code for I2C communication.
 * ----------------
 * | This version :   V1.1 (Improved for Stability - No ESP_ERROR_CHECK)
 * | Date         :   2026-03-27
 * | Info         :   Modified by Antigravity to prevent crashes on timeouts.
 *
 ******************************************************************************/

#include "i2c.h"  // Include I2C driver header for I2C functions
static const char *TAG = "i2c";  // Define a tag for logging

DEV_I2C_Port handle;

DEV_I2C_Port DEV_I2C_Init()
{
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = EXAMPLE_I2C_MASTER_NUM,
        .scl_io_num = EXAMPLE_I2C_MASTER_SCL,
        .sda_io_num = EXAMPLE_I2C_MASTER_SDA,
        .glitch_ignore_cnt = 7,
    };

    // Use ESP_ERROR_CHECK ONLY during initialization as it's critical
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &handle.bus));
    
    i2c_device_config_t i2c_dev_conf = {
        .scl_speed_hz = EXAMPLE_I2C_MASTER_FREQUENCY,
    };
    
    if (i2c_master_bus_add_device(handle.bus, &i2c_dev_conf, &handle.dev) != ESP_OK) {
        ESP_LOGE(TAG, "I2C device creation failed");
    }

    return handle;
}

void DEV_I2C_Set_Slave_Addr(i2c_master_dev_handle_t *dev_handle, uint8_t Addr)
{
    i2c_device_config_t i2c_dev_conf = {
        .scl_speed_hz = EXAMPLE_I2C_MASTER_FREQUENCY,
        .device_address = Addr,
    };
    
    if (i2c_master_bus_add_device(handle.bus, &i2c_dev_conf, dev_handle) != ESP_OK) {
        ESP_LOGE(TAG, "I2C address modification failed");
    }
}

void DEV_I2C_Write_Byte(i2c_master_dev_handle_t dev_handle, uint8_t Cmd, uint8_t value)
{
    uint8_t data[2] = {Cmd, value};
    esp_err_t err = i2c_master_transmit(dev_handle, data, sizeof(data), 100);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Write Byte failed: %s (non-fatal)", esp_err_to_name(err));
    }
}

uint8_t DEV_I2C_Read_Byte(i2c_master_dev_handle_t dev_handle)
{
    uint8_t data[1] = {0};
    esp_err_t err = i2c_master_receive(dev_handle, data, 1, 100);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Read Byte failed: %s (non-fatal)", esp_err_to_name(err));
    }
    return data[0];
}

uint16_t DEV_I2C_Read_Word(i2c_master_dev_handle_t dev_handle, uint8_t Cmd)
{
    uint8_t data[2] = {Cmd, 0};
    esp_err_t err = i2c_master_transmit_receive(dev_handle, data, 1, data, 2, 100);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Read Word (CMD=0x%02X) failed: %s (non-fatal)", Cmd, esp_err_to_name(err));
        return 0; // Return zero on error
    }
    return data[1] << 8 | data[0];
}

void DEV_I2C_Write_Nbyte(i2c_master_dev_handle_t dev_handle, uint8_t *pdata, uint8_t len)
{
    esp_err_t err = i2c_master_transmit(dev_handle, pdata, len, 100);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Write Nbyte failed: %s (non-fatal)", esp_err_to_name(err));
    }
}

void DEV_I2C_Read_Nbyte(i2c_master_dev_handle_t dev_handle, uint8_t Cmd, uint8_t *pdata, uint8_t len)
{
    esp_err_t err = i2c_master_transmit_receive(dev_handle, &Cmd, 1, pdata, len, 100);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Read Nbyte failed: %s (non-fatal)", esp_err_to_name(err));
    }
}
