/* Zigbee end-device transport: joins the network, exposes custom cluster 0xFC00
 * with one attribute per dashboard slot, and turns inbound attribute writes into
 * slot-store updates. Writes are coalesced; once they settle, repaint_cb fires.
 *
 * repaint_cb runs in a timer/stack context — keep it short (e.g. notify a render
 * task). The slots pointer it receives is owned by this module. */
#ifndef ESPINK_ZIGBEE_H
#define ESPINK_ZIGBEE_H

#include "slots.h"

typedef void (*espink_repaint_cb_t)(const espink_slots_t *slots, void *ctx);

/* Start the Zigbee stack + endpoint on a dedicated task. */
void espink_zigbee_start(espink_repaint_cb_t repaint_cb, void *ctx);

#endif /* ESPINK_ZIGBEE_H */
