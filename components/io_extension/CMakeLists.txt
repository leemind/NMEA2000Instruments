idf_component_register(SRCS "io_extension.c" 
                        INCLUDE_DIRS "."
                        REQUIRES driver esp_driver_i2c esp_driver_gpio i2c gpio
                    )
