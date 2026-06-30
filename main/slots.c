/* ESPINK slot store — pure C implementation (see slots.h). */
#include "slots.h"
#include <string.h>

/* Attribute ID for each slot index, in order. Index N corresponds to
 * value[N]. Keep ESPINK_SLOT_COUNT in slots.h in sync with this table. */
static const uint16_t k_attr_table[ESPINK_SLOT_COUNT] = {
    ESPINK_ATTR_HDR_LEFT,
    ESPINK_ATTR_HDR_RIGHT,
    ESPINK_ATTR_NOW_VALUE,
    ESPINK_ATTR_NOW_LABEL,
    ESPINK_ATTR_FC1,
    ESPINK_ATTR_FC2,
    ESPINK_ATTR_FC3,
    ESPINK_ATTR_CAL1,
    ESPINK_ATTR_CAL2,
    ESPINK_ATTR_CAL3,
    ESPINK_ATTR_CAL4,
    ESPINK_ATTR_SNS1,
    ESPINK_ATTR_SNS2,
    ESPINK_ATTR_SNS3,
    ESPINK_ATTR_SNS4,
    ESPINK_ATTR_ALERT_TEXT,
    ESPINK_ATTR_ALERT_ACTIVE,
};

/* Icon keywords recognised as a "icon:" prefix by espink_split_icon. These map
 * to U8g2 open-iconic-weather glyphs in the renderer. */
static const char *k_icon_tokens[] = {
    "sun", "clear", "clear-night", "moon", "cloud", "cloudy", "partly",
    "rain", "rainy", "drizzle", "storm", "thunder", "snow", "snowy",
    "fog", "mist", "wind", "windy",
};

void espink_slots_init(espink_slots_t *s)
{
    memset(s, 0, sizeof(*s));
}

int espink_slot_index_for_attr(uint16_t attr_id)
{
    for (int i = 0; i < ESPINK_SLOT_COUNT; i++) {
        if (k_attr_table[i] == attr_id) {
            return i;
        }
    }
    return -1;
}

const char *espink_slots_get(const espink_slots_t *s, int index)
{
    if (index < 0 || index >= ESPINK_SLOT_COUNT) {
        return "";
    }
    return s->value[index];
}

bool espink_slots_set_attr(espink_slots_t *s, uint16_t attr_id, const char *value)
{
    int index = espink_slot_index_for_attr(attr_id);
    if (index < 0) {
        return false;
    }
    char buf[ESPINK_SLOT_MAXLEN + 1];
    if (value == NULL) {
        value = "";
    }
    /* Truncate to the slot's capacity. */
    strncpy(buf, value, ESPINK_SLOT_MAXLEN);
    buf[ESPINK_SLOT_MAXLEN] = '\0';

    if (strcmp(s->value[index], buf) == 0) {
        return false; /* unchanged */
    }
    strcpy(s->value[index], buf);
    return true;
}

static bool is_icon_token(const char *tok, size_t len)
{
    for (size_t i = 0; i < sizeof(k_icon_tokens) / sizeof(k_icon_tokens[0]); i++) {
        if (strlen(k_icon_tokens[i]) == len && strncmp(k_icon_tokens[i], tok, len) == 0) {
            return true;
        }
    }
    return false;
}

void espink_split_icon(const char *value, char *token_out, size_t token_sz,
                       const char **text_out)
{
    token_out[0] = '\0';
    *text_out = value;

    const char *colon = strchr(value, ':');
    if (colon == NULL) {
        return; /* no separator: whole string is text */
    }
    size_t prefix_len = (size_t)(colon - value);
    if (prefix_len == 0 || !is_icon_token(value, prefix_len)) {
        return; /* prefix is not a known icon keyword: keep whole string as text */
    }
    if (prefix_len >= token_sz) {
        prefix_len = token_sz - 1; /* defensive: never overflow token_out */
    }
    memcpy(token_out, value, prefix_len);
    token_out[prefix_len] = '\0';
    *text_out = colon + 1;
}

bool espink_coalesce_due(int64_t last_dirty_us, int64_t now_us, int64_t debounce_us)
{
    return (now_us - last_dirty_us) >= debounce_us;
}
