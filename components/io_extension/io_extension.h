/*****************************************************************************
 * | File         :   io_extension.h
 * | Author       :   Waveshare team
 * | Function     :   PCA9557/PCA9554 IO extension support
 * ----------------
 * | This version :   V3.0 (PCA9557 Standard support)
 * | Date         :   2024-11-20
 * | Info         :   Corrected register mapping for standard PCA parts
 *
 ******************************************************************************/

#ifndef __IO_EXTENSION_H
#define __IO_EXTENSION_H

#include "i2c.h" 

#define IO_EXTENSION_ADDR      0x24 

/* Standard PCA9557 / PCA9554 Register Map */
#define PCA9557_REG_INPUT      0x00
#define PCA9557_REG_OUTPUT     0x01
#define PCA9557_REG_POLARITY   0x02
#define PCA9557_REG_CONFIG     0x03 // 0=Output, 1=Input

/* Legacy aliases for the rest of the code */
#define IO_EXTENSION_Mode           PCA9557_REG_CONFIG
#define IO_EXTENSION_IO_OUTPUT_ADDR PCA9557_REG_OUTPUT
#define IO_EXTENSION_IO_INPUT_ADDR  PCA9557_REG_INPUT

#define IO_EXTENSION_IO_0 0x00 
#define IO_EXTENSION_IO_1 0x01 // TOUCH_RST
#define IO_EXTENSION_IO_2 0x02 // BL_PWM
#define IO_EXTENSION_IO_3 0x03 // LCD_RST
#define IO_EXTENSION_IO_4 0x04 // SD_CS
#define IO_EXTENSION_IO_5 0x05 // USB/CAN Select
#define IO_EXTENSION_IO_6 0x06
#define IO_EXTENSION_IO_7 0x07

typedef struct _io_extension_obj_t {
  uint8_t addr;
  i2c_master_dev_handle_t dev;
  uint8_t Last_io_value;
} io_extension_obj_t;

void IO_EXTENSION_Init();
void IO_EXTENSION_Output(uint8_t pin, uint8_t value);
uint8_t IO_EXTENSION_Input(uint8_t pin);
void IO_EXTENSION_Pwm_Output(uint8_t Value);
uint16_t IO_EXTENSION_Adc_Input();
void IO_EXTENSION_ScanBus();

#endif
