/* Framebuffer transpose: U8g2 full-frame tile buffer -> UC8179 row-major plane.
 * Pure logic, no ESP-IDF / U8g2 dependency (host-testable). */
#ifndef ESPINK_RENDER_FB_H
#define ESPINK_RENDER_FB_H

#include <stdint.h>

#define ESPINK_FB_WIDTH  800
#define ESPINK_FB_HEIGHT 480
/* U8g2 vertical_top_lsb full-frame buffer: WIDTH bytes per 8-row tile. */
#define ESPINK_FB_U8G2_BYTES  (ESPINK_FB_WIDTH * (ESPINK_FB_HEIGHT / 8)) /* 48000 */
/* UC8179 plane: row-major, 8 horizontal pixels per byte, MSB leftmost. */
#define ESPINK_FB_PLANE_BYTES ((ESPINK_FB_WIDTH * ESPINK_FB_HEIGHT) / 8)  /* 48000 */

/* Transpose a U8g2 full-frame buffer into a UC8179 plane. A *set* U8g2 pixel
 * (drawn) becomes *ink* in the plane, i.e. the corresponding plane bit is
 * cleared to 0; undrawn pixels stay 1 (white). The plane is fully written
 * (no need to pre-clear it). */
void espink_u8g2_to_plane(const uint8_t *u8g2buf, uint8_t *plane);

#endif /* ESPINK_RENDER_FB_H */
