idf_component_register(
    SRCS
        "main.c"
        "slots.c"
        "render_fb.c"
        "display.c"
        "zigbee.c"
    INCLUDE_DIRS "."
    REQUIRES
        epd_uc8179
        u8g2
        esp_timer
        nvs_flash
        espressif__esp-zigbee-lib
)
