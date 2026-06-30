/* U8g2 set up as an 800x480 RAM-only full-frame buffer (no real device).
 *
 * Draw with the normal u8g2_* API, then transpose the buffer into the e-paper
 * row-major plane with espink_u8g2_to_plane() (see main/render_fb.h). Render the
 * black content and the red content as two separate passes into two buffers. */
#ifndef U8G2_ESPINK_H
#define U8G2_ESPINK_H

#include "u8g2.h"

#define U8G2_ESPINK_WIDTH  800
#define U8G2_ESPINK_HEIGHT 480
/* Full-frame U8g2 buffer size (vertical_top_lsb tile layout). */
#define U8G2_ESPINK_BUF_BYTES (U8G2_ESPINK_WIDTH * (U8G2_ESPINK_HEIGHT / 8)) /* 48000 */

/* Bind `buf` (>= U8G2_ESPINK_BUF_BYTES) as the full-screen drawing buffer. */
void u8g2_SetupEspink(u8g2_t *u8g2, uint8_t *buf);

#endif /* U8G2_ESPINK_H */
