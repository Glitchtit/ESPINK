/* Framebuffer transpose (see render_fb.h). */
#include "render_fb.h"

#define TILE_ROWS    (ESPINK_FB_HEIGHT / 8)   /* 60 */
#define BYTES_PER_ROW (ESPINK_FB_WIDTH / 8)   /* 100 */

void espink_u8g2_to_plane(const uint8_t *u8g2buf, uint8_t *plane)
{
    /* Start white; clear a bit wherever a U8g2 pixel is set (drawn -> ink). */
    for (int i = 0; i < ESPINK_FB_PLANE_BYTES; i++) {
        plane[i] = 0xFF;
    }

    for (int ty = 0; ty < TILE_ROWS; ty++) {
        const uint8_t *tile = &u8g2buf[ty * ESPINK_FB_WIDTH];
        for (int x = 0; x < ESPINK_FB_WIDTH; x++) {
            uint8_t col = tile[x];
            if (col == 0) {
                continue; /* no pixels in this 8-row column slice */
            }
            for (int b = 0; b < 8; b++) {
                if (col & (1u << b)) {
                    int y = ty * 8 + b;
                    int idx = y * BYTES_PER_ROW + (x >> 3);
                    plane[idx] &= (uint8_t)~(1u << (7 - (x & 7)));
                }
            }
        }
    }
}
