/* RAM-only 800x480 U8g2 display backend (see u8g2_espink.h). */
#include "u8g2_espink.h"

/* Geometry-only display info. Timing fields are irrelevant (no real bus). */
static const u8x8_display_info_t espink_display_info = {
    .chip_enable_level = 0,
    .chip_disable_level = 1,
    .post_chip_enable_wait_ns = 1,
    .pre_chip_disable_wait_ns = 1,
    .reset_pulse_width_ms = 1,
    .post_reset_wait_ms = 1,
    .sda_setup_time_ns = 1,
    .sck_pulse_width_ns = 1,
    .sck_clock_hz = 4000000UL,
    .spi_mode = 0,
    .i2c_bus_clock_100kHz = 4,
    .data_setup_time_ns = 1,
    .write_pulse_width_ns = 1,
    .tile_width = U8G2_ESPINK_WIDTH / 8,   /* 100 */
    .tile_height = U8G2_ESPINK_HEIGHT / 8, /* 60 */
    .default_x_offset = 0,
    .flipmode_x_offset = 0,
    .pixel_width = U8G2_ESPINK_WIDTH,
    .pixel_height = U8G2_ESPINK_HEIGHT,
};

/* Minimal display callback: only the geometry messages matter; everything else
 * is acked (there is no hardware behind this buffer). */
static uint8_t u8x8_d_espink(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)arg_int;
    (void)arg_ptr;
    switch (msg) {
    case U8X8_MSG_DISPLAY_SETUP_MEMORY:
        u8x8_d_helper_display_setup_memory(u8x8, &espink_display_info);
        break;
    case U8X8_MSG_DISPLAY_INIT:
        u8x8_d_helper_display_init(u8x8);
        break;
    default:
        break;
    }
    return 1;
}

void u8g2_SetupEspink(u8g2_t *u8g2, uint8_t *buf)
{
    u8x8_Setup(u8g2_GetU8x8(u8g2), u8x8_d_espink, u8x8_cad_empty,
               u8x8_byte_empty, u8x8_dummy_cb);
    /* Full-screen single page: tile_buf_height == display tile height (60). */
    u8g2_SetupBuffer(u8g2, buf, U8G2_ESPINK_HEIGHT / 8,
                     u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
}
