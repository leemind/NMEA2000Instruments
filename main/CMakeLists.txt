file(GLOB_RECURSE srcs *.c
                   ui/*.c
                   ui/components/*.c
                   ui/screens/*.c
                   user/can/*.c
                   user/events/*.c
                   user/pwm/*.c
                   user/settings/*.c
                   user/ui/*.c
                   user/wifi/*.c
                   user/ota/*.c
                   )
set(include_dirs 
   . 
   ui
   ui/screens
   user/can
   user/events
   user/pwm
   user/settings
   user/ui
   user/wifi
   user/ota
   )
idf_component_register( SRCS ${srcs}
                       INCLUDE_DIRS ${include_dirs}
                       REQUIRES lvgl cjson littlefs esp_lcd esp_lcd_touch_gt911 
                                esp_driver_twai esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_driver_uart
                                gpio i2c io_extension lvgl_port pgn_parser rgb_lcd_port touch usart nvs_flash
                                esp_wifi esp_event esp_netif esp_http_client esp_https_ota
                       )
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
