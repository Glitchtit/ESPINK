/* Host tests for the U8g2-tile -> e-paper-plane transpose in main/render_fb.c. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "render_fb.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg) do {                                  \
    g_checks++;                                                \
    if (!(cond)) {                                             \
        g_failures++;                                          \
        printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    }                                                          \
} while (0)

/* Set pixel (x,y) in a U8g2 full-frame vertical_top_lsb buffer. */
static void u8g2_set(uint8_t *buf, int x, int y)
{
    int tile_row = y / 8;
    int idx = tile_row * ESPINK_FB_WIDTH + x;
    buf[idx] |= (uint8_t)(1u << (y % 8));
}

static uint8_t g_u8g2[ESPINK_FB_U8G2_BYTES];
static uint8_t g_plane[ESPINK_FB_PLANE_BYTES];

static void reset(void) {
    memset(g_u8g2, 0, sizeof(g_u8g2));
    memset(g_plane, 0, sizeof(g_plane));
}

static void test_blank_is_white(void) {
    printf("test_blank_is_white\n");
    reset();
    espink_u8g2_to_plane(g_u8g2, g_plane);
    int all_white = 1;
    for (size_t i = 0; i < sizeof(g_plane); i++) {
        if (g_plane[i] != 0xFF) { all_white = 0; break; }
    }
    CHECK(all_white, "no drawn pixels -> plane all 0xFF (white)");
}

static void test_top_left_pixel(void) {
    printf("test_top_left_pixel\n");
    reset();
    u8g2_set(g_u8g2, 0, 0);
    espink_u8g2_to_plane(g_u8g2, g_plane);
    /* (0,0) is the leftmost pixel of row 0 -> MSB of plane byte 0 cleared. */
    CHECK(g_plane[0] == 0x7F, "pixel (0,0) clears MSB of byte 0");
}

static void test_eighth_pixel_same_byte(void) {
    printf("test_eighth_pixel_same_byte\n");
    reset();
    u8g2_set(g_u8g2, 7, 0);
    espink_u8g2_to_plane(g_u8g2, g_plane);
    /* (7,0) is the rightmost pixel of plane byte 0 -> LSB cleared. */
    CHECK(g_plane[0] == 0xFE, "pixel (7,0) clears LSB of byte 0");
}

static void test_ninth_pixel_next_byte(void) {
    printf("test_ninth_pixel_next_byte\n");
    reset();
    u8g2_set(g_u8g2, 8, 0);
    espink_u8g2_to_plane(g_u8g2, g_plane);
    CHECK(g_plane[0] == 0xFF, "byte 0 untouched");
    CHECK(g_plane[1] == 0x7F, "pixel (8,0) clears MSB of byte 1");
}

static void test_second_row(void) {
    printf("test_second_row\n");
    reset();
    u8g2_set(g_u8g2, 0, 1);
    espink_u8g2_to_plane(g_u8g2, g_plane);
    /* Row 1 starts at plane byte WIDTH/8 = 100. */
    CHECK(g_plane[ESPINK_FB_WIDTH / 8] == 0x7F, "pixel (0,1) clears MSB of row-1 byte");
    CHECK(g_plane[0] == 0xFF, "row 0 untouched");
}

static void test_bottom_right_pixel(void) {
    printf("test_bottom_right_pixel\n");
    reset();
    u8g2_set(g_u8g2, ESPINK_FB_WIDTH - 1, ESPINK_FB_HEIGHT - 1);
    espink_u8g2_to_plane(g_u8g2, g_plane);
    /* Last pixel -> LSB of the last plane byte. */
    CHECK(g_plane[ESPINK_FB_PLANE_BYTES - 1] == 0xFE,
          "bottom-right pixel clears LSB of last byte");
}

int main(void) {
    test_blank_is_white();
    test_top_left_pixel();
    test_eighth_pixel_same_byte();
    test_ninth_pixel_next_byte();
    test_second_row();
    test_bottom_right_pixel();
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
