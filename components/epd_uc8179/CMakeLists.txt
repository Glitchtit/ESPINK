idf_component_register(
    SRCS "epd_uc8179.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_spi esp_driver_gpio
)
