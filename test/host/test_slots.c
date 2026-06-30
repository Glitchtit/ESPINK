/* Host-side unit tests for the pure slot logic in main/slots.c.
 * Compiled with plain gcc (no ESP-IDF) — see test/host/Makefile.
 *
 * slots.c must stay free of ESP-IDF headers so this builds on the host. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "slots.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg) do {                                  \
    g_checks++;                                                \
    if (!(cond)) {                                             \
        g_failures++;                                          \
        printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    }                                                          \
} while (0)

static void test_attr_index_mapping(void) {
    printf("test_attr_index_mapping\n");
    /* Known attribute IDs map to distinct, valid slot indices. */
    int hdr_left = espink_slot_index_for_attr(ESPINK_ATTR_HDR_LEFT);
    int now_value = espink_slot_index_for_attr(ESPINK_ATTR_NOW_VALUE);
    CHECK(hdr_left >= 0, "hdr_left maps to a valid index");
    CHECK(now_value >= 0, "now_value maps to a valid index");
    CHECK(hdr_left != now_value, "distinct attrs map to distinct indices");
    /* Unknown attribute IDs are rejected. */
    CHECK(espink_slot_index_for_attr(0xBEEF) < 0, "unknown attr rejected");
}

static void test_set_and_get(void) {
    printf("test_set_and_get\n");
    espink_slots_t s;
    espink_slots_init(&s);
    /* A fresh store returns empty strings, never NULL. */
    int idx = espink_slot_index_for_attr(ESPINK_ATTR_HDR_LEFT);
    CHECK(espink_slots_get(&s, idx) != NULL, "get never returns NULL");
    CHECK(espink_slots_get(&s, idx)[0] == '\0', "fresh slot is empty");
    /* Writing a value by attr ID round-trips. */
    bool changed = espink_slots_set_attr(&s, ESPINK_ATTR_HDR_LEFT, "Tue 1 Jul");
    CHECK(changed, "first write reports changed");
    CHECK(strcmp(espink_slots_get(&s, idx), "Tue 1 Jul") == 0, "value round-trips");
    /* Writing the identical value reports no change (so we can skip repaint). */
    CHECK(!espink_slots_set_attr(&s, ESPINK_ATTR_HDR_LEFT, "Tue 1 Jul"),
          "identical write reports no change");
    /* Writing a different value reports change. */
    CHECK(espink_slots_set_attr(&s, ESPINK_ATTR_HDR_LEFT, "Wed 2 Jul"),
          "different write reports change");
}

static void test_set_unknown_attr(void) {
    printf("test_set_unknown_attr\n");
    espink_slots_t s;
    espink_slots_init(&s);
    CHECK(!espink_slots_set_attr(&s, 0xBEEF, "x"), "unknown attr write rejected");
}

static void test_set_truncates_overlong(void) {
    printf("test_set_truncates_overlong\n");
    espink_slots_t s;
    espink_slots_init(&s);
    char big[256];
    memset(big, 'A', sizeof(big));
    big[sizeof(big) - 1] = '\0';
    espink_slots_set_attr(&s, ESPINK_ATTR_NOW_LABEL, big);
    int idx = espink_slot_index_for_attr(ESPINK_ATTR_NOW_LABEL);
    size_t got = strlen(espink_slots_get(&s, idx));
    CHECK(got == ESPINK_SLOT_MAXLEN, "overlong value truncated to max len");
}

static void test_split_icon_with_token(void) {
    printf("test_split_icon_with_token\n");
    char token[16];
    const char *text = NULL;
    espink_split_icon("sun:Partly cloudy", token, sizeof(token), &text);
    CHECK(strcmp(token, "sun") == 0, "token extracted before first colon");
    CHECK(strcmp(text, "Partly cloudy") == 0, "text is remainder after colon");
}

static void test_split_icon_no_token(void) {
    printf("test_split_icon_no_token\n");
    char token[16];
    const char *text = NULL;
    espink_split_icon("09:00 Standup", token, sizeof(token), &text);
    /* A time like "09:00" must NOT be misread as an icon token: only treat the
     * prefix as a token if it is a known icon keyword. Unknown => no token,
     * whole string is text. */
    CHECK(token[0] == '\0', "non-icon prefix yields empty token");
    CHECK(strcmp(text, "09:00 Standup") == 0, "whole string kept as text");
}

static void test_split_icon_empty(void) {
    printf("test_split_icon_empty\n");
    char token[16];
    const char *text = NULL;
    espink_split_icon("", token, sizeof(token), &text);
    CHECK(token[0] == '\0', "empty input yields empty token");
    CHECK(text != NULL && text[0] == '\0', "empty input yields empty text");
}

static void test_coalesce_due(void) {
    printf("test_coalesce_due\n");
    int64_t debounce = 2000000; /* 2 s in microseconds */
    /* Not due until the debounce window has elapsed since the last write. */
    CHECK(!espink_coalesce_due(1000000, 1500000, debounce),
          "not due before debounce elapses");
    CHECK(espink_coalesce_due(1000000, 3000000, debounce),
          "due exactly at debounce boundary");
    CHECK(espink_coalesce_due(1000000, 5000000, debounce),
          "due well after debounce");
}

int main(void) {
    test_attr_index_mapping();
    test_set_and_get();
    test_set_unknown_attr();
    test_set_truncates_overlong();
    test_split_icon_with_token();
    test_split_icon_no_token();
    test_split_icon_empty();
    test_coalesce_due();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
