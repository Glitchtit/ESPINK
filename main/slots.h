/* ESPINK slot store — pure C, no ESP-IDF dependencies (host-testable).
 *
 * A "slot" is one short text field of the fixed dashboard layout, addressed by a
 * manufacturer-specific Zigbee attribute ID on custom cluster 0xFC00. Home
 * Assistant writes these via Z2M; the firmware renders them. Each value is kept
 * short (<= ESPINK_SLOT_MAXLEN) because esp-zigbee-sdk has no APS fragmentation. */
#ifndef ESPINK_SLOTS_H
#define ESPINK_SLOTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Max characters stored per slot. Kept within the single-APS-frame string limit
 * (~60 usable chars) so a value always fits one unfragmented ZCL write. */
#define ESPINK_SLOT_MAXLEN 60

/* Manufacturer-specific attribute IDs on custom cluster 0xFC00. */
enum {
    ESPINK_ATTR_HDR_LEFT    = 0x0000, /* date, e.g. "Tue 1 Jul" */
    ESPINK_ATTR_HDR_RIGHT   = 0x0001, /* status, e.g. "Updated 14:05" */
    ESPINK_ATTR_NOW_VALUE   = 0x0010, /* big value, e.g. "23°" */
    ESPINK_ATTR_NOW_LABEL   = 0x0011, /* "icon:text", e.g. "sun:Partly cloudy" */
    ESPINK_ATTR_FC1         = 0x0020, /* forecast line 1 */
    ESPINK_ATTR_FC2         = 0x0021,
    ESPINK_ATTR_FC3         = 0x0022,
    ESPINK_ATTR_CAL1        = 0x0030, /* agenda line 1 */
    ESPINK_ATTR_CAL2        = 0x0031,
    ESPINK_ATTR_CAL3        = 0x0032,
    ESPINK_ATTR_CAL4        = 0x0033,
    ESPINK_ATTR_SNS1        = 0x0040, /* sensor tile 1 */
    ESPINK_ATTR_SNS2        = 0x0041,
    ESPINK_ATTR_SNS3        = 0x0042,
    ESPINK_ATTR_SNS4        = 0x0043,
    ESPINK_ATTR_ALERT_TEXT  = 0x0050, /* red alert ribbon text */
    ESPINK_ATTR_ALERT_ACTIVE= 0x0051, /* "0"/"1": drives the red plane */
};

/* Number of addressable slots (must match the table in slots.c). */
#define ESPINK_SLOT_COUNT 17

typedef struct {
    char value[ESPINK_SLOT_COUNT][ESPINK_SLOT_MAXLEN + 1];
} espink_slots_t;

/* Zero-initialise all slots to empty strings. */
void espink_slots_init(espink_slots_t *s);

/* Map a Zigbee attribute ID to a slot index, or -1 if unknown. */
int espink_slot_index_for_attr(uint16_t attr_id);

/* Read a slot by index. Always returns a valid C string ("" for out-of-range). */
const char *espink_slots_get(const espink_slots_t *s, int index);

/* Write a slot addressed by attribute ID. The value is truncated to
 * ESPINK_SLOT_MAXLEN. Returns true if the stored value actually changed (so the
 * caller can decide whether a repaint is needed); false for unknown attr or
 * an identical value. */
bool espink_slots_set_attr(espink_slots_t *s, uint16_t attr_id, const char *value);

/* Split a "icon:text" value. If the prefix before the first ':' is a known icon
 * keyword, token_out receives it and *text_out points at the remainder.
 * Otherwise token_out is "" and *text_out points at the whole value (so a time
 * like "09:00 Standup" is not misparsed). token_out is always NUL-terminated. */
void espink_split_icon(const char *value, char *token_out, size_t token_sz,
                       const char **text_out);

/* True once `debounce_us` has elapsed since the last dirtying write — i.e. the
 * coalescing window has closed and it is time to repaint. */
bool espink_coalesce_due(int64_t last_dirty_us, int64_t now_us, int64_t debounce_us);

#endif /* ESPINK_SLOTS_H */
