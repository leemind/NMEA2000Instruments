file(GLOB_RECURSE srcs *.c
                   ui/*.c
                   ui/components/*.c
                   ui/screens/*.c
                   user/can/*.c
                   user/pwm/*.c
                   user/sd_card/*.c
                   )
set(include_dirs 
   . 
   ui
   user/can
   user/pwm
   user/sd_card
   )
idf_component_register( SRCS ${srcs}
                       INCLUDE_DIRS ${include_dirs}
                       )
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
littlefs_create_partition_image(data FLASH_IN_PROJECT)   