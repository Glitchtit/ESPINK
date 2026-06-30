/* Dashboard renderer: composes the fixed 800x480 layout from the slot store and
 * pushes it to the e-paper via the UC8179 driver. */
#ifndef ESPINK_DISPLAY_H
#define ESPINK_DISPLAY_H

#include "esp_err.h"
#include "epd_uc8179.h"
#include "slots.h"

typedef struct espink_display espink_display_t;

/* Allocate framebuffers (U8g2 buffer + two planes, ~144 KB) and bind the panel. */
esp_err_t espink_display_create(epd_uc8179_dev_t *epd, espink_display_t **out);

/* Render the current slots and do a full panel refresh (blocks ~26 s on red). */
esp_err_t espink_display_render(espink_display_t *d, const espink_slots_t *slots);

#endif /* ESPINK_DISPLAY_H */
