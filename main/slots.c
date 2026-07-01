/* ESPINK slot store — pure C implementation (see slots.h). */
#include "slots.h"
#include <stdlib.h>
#include <string.h>

/* Attribute ID for each slot index, in order. Index N corresponds to
 * value[N]. Keep ESPINK_SLOT_COUNT in slots.h in sync with this table. */
static const uint16_t k_attr_table[ESPINK_SLOT_COUNT] = {
    ESPINK_ATTR_HDR_LEFT,
    ESPINK_ATTR_HDR_RIGHT,
    ESPINK_ATTR_NOW_VALUE,
    ESPINK_ATTR_NOW_LABEL,
    ESPINK_ATTR_NOW_HILO,
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
    ESPINK_ATTR_HEADACHE,
    ESPINK_ATTR_SHOP1,
    ESPINK_ATTR_SHOP2,
    ESPINK_ATTR_SHOP3,
    ESPINK_ATTR_SHOP4,
    ESPINK_ATTR_SHOP5,
    ESPINK_ATTR_SHOP6,
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

bool espink_celsius_at_least(const char *s, double threshold)
{
    size_t len = strlen(s);
    while (len > 0 && s[len - 1] == ' ') {
        len--;
    }
    if (len == 0 || s[len - 1] != 'C') {
        return false; /* must end in 'C' */
    }
    len--;
    while (len > 0 && s[len - 1] == ' ') {
        len--;
    }
    /* optional degree sign (UTF-8 0xC2 0xB0) */
    if (len >= 2 && (unsigned char)s[len - 1] == 0xB0 && (unsigned char)s[len - 2] == 0xC2) {
        len -= 2;
    }
    while (len > 0 && s[len - 1] == ' ') {
        len--;
    }
    /* scan back over the number */
    size_t end = len;
    while (len > 0) {
        char c = s[len - 1];
        if ((c >= '0' && c <= '9') || c == '.' || c == '-') {
            len--;
        } else {
            break;
        }
    }
    bool has_digit = false;
    for (size_t i = len; i < end; i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            has_digit = true;
        }
    }
    if (!has_digit) {
        return false;
    }
    char buf[16];
    size_t n = end - len;
    if (n > sizeof(buf) - 1) {
        n = sizeof(buf) - 1;
    }
    memcpy(buf, s + len, n);
    buf[n] = '\0';
    return atof(buf) >= threshold;
}

int espink_headache_level(int value)
{
    if (value < 20) return 1;
    if (value < 40) return 2;
    if (value < 60) return 3;
    if (value < 80) return 4;
    return 5;
}
