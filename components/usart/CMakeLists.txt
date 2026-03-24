idf_component_register(SRCS "usart.c" 
                        INCLUDE_DIRS "."
                        REQUIRES driver esp_driver_uart gpio
                    )
