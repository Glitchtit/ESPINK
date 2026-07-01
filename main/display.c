/* Dashboard renderer (see display.h).
 *
 * Two U8g2 passes share one drawing buffer: the black pass draws all monochrome
 * content and is transposed into the black plane; the red pass draws only the
 * alert highlight and is transposed into the red plane. The UC8179 driver then
 * does a single full refresh. */
#include "display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "u8g2.h"
#include "u8g2_espink.h"
#include "render_fb.h"
#include "headache_icons.h"

static const char *TAG = "display";

/* Layout constants (pixels). Two columns: left = now / sensors / agenda,
 * right = forecast / headache / shopping. */
#define M           12    /* outer margin */
#define COL_R       410   /* right column x */
#define HDR_BASE    28
#define RULE_Y      40

/* LEFT: now */
#define NOW_VAL_X   M
#define NOW_VAL_BASE 92
#define NOW_HILO_BASE 118
#define NOW_ICON_X  250
#define NOW_ICON_BASE 100
#define NOW_LBL_BASE 146

/* LEFT: sensors (vertical stack) */
#define SNS_X       M
#define SNS_TOP     192
#define SNS_ROW_H   26

/* LEFT: agenda */
#define CAL_TITLE_BASE 260
#define CAL_BASE    286
#define CAL_ROW_H   26

/* RIGHT: forecast */
#define FC_X        COL_R
#define FC_COL_W    126
#define FC_BASE     72
#define FC_ROW_H    56

/* RIGHT: headache */
#define HDC_ICON_X  COL_R
#define HDC_ICON_Y  164
#define HDC_TEXT_X  (COL_R + 84)

/* RIGHT: shopping */
#define SHOP_TITLE_BASE 260
#define SHOP_BASE   286
#define SHOP_ROW_H  25

#define ALERT_BASE  474

static const char *const k_headache_words[5] = {
    "Low", "Slight", "Moderate", "High", "Severe",
};

/* Headache index (0-100) from the slot, or -1 if the slot is empty. */
static int headache_value(const espink_slots_t *s)
{
    const char *v = espink_slots_get(s, espink_slot_index_for_attr(ESPINK_ATTR_HEADACHE));
    return v[0] == '\0' ? -1 : atoi(v);
}

struct espink_display {
    epd_uc8179_dev_t *epd;
    uint8_t *u8g2buf;   /* U8G2_ESPINK_BUF_BYTES */
    uint8_t *black;     /* EPD_UC8179_PLANE_BYTES */
    uint8_t *red;       /* EPD_UC8179_PLANE_BYTES */
    u8g2_t u8g2;
};

/* Map a weather token to an open_iconic_weather glyph code. NOTE: these codes
 * must be verified against the open-iconic-weather chart on the real panel and
 * tuned here; the set encodes glyphs from 0x45. Unknown -> 0 (draw nothing). */
static uint8_t icon_glyph(const char *token)
{
    struct { const char *t; uint8_t g; } map[] = {
        {"sun", 0x45}, {"clear", 0x45}, {"clear-night", 0x47}, {"moon", 0x47},
        {"cloud", 0x42}, {"cloudy", 0x42}, {"partly", 0x41},
        {"rain", 0x43}, {"rainy", 0x43}, {"drizzle", 0x43},
        {"storm", 0x48}, {"thunder", 0x48}, {"snow", 0x44}, {"snowy", 0x44},
        {"fog", 0x46}, {"mist", 0x46}, {"wind", 0x49}, {"windy", 0x49},
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (strcmp(map[i].t, token) == 0) {
            return map[i].g;
        }
    }
    return 0;
}

#define GET(slots, attr) espink_slots_get((slots), espink_slot_index_for_attr(attr))

/* A high >= this renders red. */
#define RED_HI_THRESHOLD 25

/* Sensor tile i position (vertical stack, left column). */
static void sns_xy(int i, int *x, int *y)
{
    *x = SNS_X;
    *y = SNS_TOP + i * SNS_ROW_H;
}

/* Draw a "HH/LL" hi/lo string. The high renders red (in the red pass) when it is
 * >= RED_HI_THRESHOLD; the low and separator are always black. Both passes lay
 * out identically so the red high sits exactly where the black one would. */
static void draw_hilo(u8g2_t *u8g2, int x, int y, const uint8_t *font,
                      const char *hilo, bool red_pass)
{
    if (hilo[0] == '\0') {
        return;
    }
    const char *slash = strchr(hilo, '/');
    char hi[8];
    size_t n = slash ? (size_t)(slash - hilo) : strlen(hilo);
    if (n > 7) {
        n = 7;
    }
    memcpy(hi, hilo, n);
    hi[n] = '\0';
    bool hi_is_red = atoi(hi) >= RED_HI_THRESHOLD;

    u8g2_SetFont(u8g2, font);
    if (red_pass) {
        if (hi_is_red) {
            u8g2_DrawUTF8(u8g2, x, y, hi);
        }
        return;
    }
    if (!hi_is_red) {
        u8g2_DrawUTF8(u8g2, x, y, hi);
    }
    if (slash) {
        u8g2_DrawUTF8(u8g2, x + u8g2_GetUTF8Width(u8g2, hi), y, slash); /* "/LL" */
    }
}

/* Draw a value that may be a Celsius temperature. A temperature >= 25 renders in
 * the red plane, everything else in black. Passing the same call in both passes
 * draws it exactly once, in the right colour (red_pass == is_red). */
static void draw_temp_text(u8g2_t *u8g2, int x, int y, const uint8_t *font,
                           const char *text, bool red_pass)
{
    if (text[0] == '\0') {
        return;
    }
    if (red_pass == espink_celsius_at_least(text, RED_HI_THRESHOLD)) {
        u8g2_SetFont(u8g2, font);
        u8g2_DrawUTF8(u8g2, x, y, text);
    }
}

/* Extract the hi/lo (3rd colon field) of forecast slot i; "" if absent. */
static void forecast_hilo(const espink_slots_t *s, int i, char *out, size_t n)
{
    static const uint16_t fc[] = {ESPINK_ATTR_FC1, ESPINK_ATTR_FC2, ESPINK_ATTR_FC3};
    out[0] = '\0';
    const char *c1 = strchr(GET(s, fc[i]), ':');
    if (!c1) {
        return;
    }
    const char *c2 = strchr(c1 + 1, ':');
    if (!c2) {
        return;
    }
    strncpy(out, c2 + 1, n - 1);
    out[n - 1] = '\0';
}

static void draw_black_pass(u8g2_t *u8g2, const espink_slots_t *s)
{
    char token[16];
    const char *text;

    /* Header. */
    u8g2_SetFont(u8g2, u8g2_font_helvR14_tf);
    u8g2_DrawUTF8(u8g2, M, HDR_BASE, GET(s, ESPINK_ATTR_HDR_LEFT));
    const char *hr = GET(s, ESPINK_ATTR_HDR_RIGHT);
    int hrw = u8g2_GetUTF8Width(u8g2, hr);
    u8g2_DrawUTF8(u8g2, ESPINK_FB_WIDTH - M - hrw, HDR_BASE, hr);
    u8g2_DrawHLine(u8g2, M, RULE_Y, ESPINK_FB_WIDTH - 2 * M);

    /* "Now" big value (red when >= 25) + condition. */
    draw_temp_text(u8g2, NOW_VAL_X, NOW_VAL_BASE, u8g2_font_helvB24_tf,
                   GET(s, ESPINK_ATTR_NOW_VALUE), false);

    espink_split_icon(GET(s, ESPINK_ATTR_NOW_LABEL), token, sizeof(token), &text);
    uint8_t g = icon_glyph(token);
    if (g) {
        u8g2_SetFont(u8g2, u8g2_font_open_iconic_weather_6x_t);
        u8g2_DrawGlyph(u8g2, NOW_ICON_X, NOW_ICON_BASE, g);
    }
    u8g2_SetFont(u8g2, u8g2_font_helvR14_tf);
    u8g2_DrawUTF8(u8g2, NOW_VAL_X, NOW_LBL_BASE, text);
    /* Today's high/low, small, under the big value (high red if >= 25). */
    draw_hilo(u8g2, NOW_VAL_X, NOW_HILO_BASE, u8g2_font_helvR14_tf,
              GET(s, ESPINK_ATTR_NOW_HILO), false);

    /* Forecast: three columns. */
    const uint16_t fc[] = {ESPINK_ATTR_FC1, ESPINK_ATTR_FC2, ESPINK_ATTR_FC3};
    for (int i = 0; i < 3; i++) {
        int x = FC_X + i * FC_COL_W;
        const char *raw = GET(s, fc[i]);
        if (raw[0] == '\0') {
            continue;
        }
        /* Format "day:icon:hilo" -> day on top, icon middle, hilo bottom. */
        char day[16] = "", ic[16] = "", hilo[16] = "";
        const char *p = raw;
        const char *c1 = strchr(p, ':');
        if (c1) {
            size_t n = (size_t)(c1 - p); if (n > 15) n = 15;
            memcpy(day, p, n); day[n] = '\0';
            const char *c2 = strchr(c1 + 1, ':');
            if (c2) {
                size_t m = (size_t)(c2 - (c1 + 1)); if (m > 15) m = 15;
                memcpy(ic, c1 + 1, m); ic[m] = '\0';
                strncpy(hilo, c2 + 1, 15);
            } else {
                strncpy(ic, c1 + 1, 15);
            }
        } else {
            strncpy(day, raw, 15);
        }
        u8g2_SetFont(u8g2, u8g2_font_helvR14_tf);
        u8g2_DrawUTF8(u8g2, x, FC_BASE, day);
        uint8_t fg = icon_glyph(ic);
        if (fg) {
            u8g2_SetFont(u8g2, u8g2_font_open_iconic_weather_4x_t);
            u8g2_DrawGlyph(u8g2, x, FC_BASE + 42, fg);
        }
        draw_hilo(u8g2, x, FC_BASE + FC_ROW_H + 8, u8g2_font_helvR14_tf, hilo, false);
    }

    /* Headache indicator (right half, under the forecast). Text + level 1-4 icon
     * render in black; the Severe (level 5) icon renders red in draw_red_pass. */
    int hv = headache_value(s);
    if (hv >= 0) {
        int lvl = espink_headache_level(hv);
        u8g2_SetFont(u8g2, u8g2_font_helvR14_tf);
        u8g2_DrawUTF8(u8g2, HDC_TEXT_X, HDC_ICON_Y + 22, "Headache");
        u8g2_SetFont(u8g2, u8g2_font_helvB18_tf);
        u8g2_DrawUTF8(u8g2, HDC_TEXT_X, HDC_ICON_Y + 50, k_headache_words[lvl - 1]);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", hv);
        u8g2_SetFont(u8g2, u8g2_font_helvR14_tf);
        u8g2_DrawUTF8(u8g2, HDC_TEXT_X, HDC_ICON_Y + 72, buf);
        if (lvl < 5) {
            u8g2_DrawXBM(u8g2, HDC_ICON_X, HDC_ICON_Y, HEADACHE_ICON_W,
                         HEADACHE_ICON_H, headache_icons[lvl - 1]);
        }
    }

    /* Sensors (left column, vertical stack; a temperature tile >= 25 is red). */
    const uint16_t sns[] = {ESPINK_ATTR_SNS1, ESPINK_ATTR_SNS2,
                            ESPINK_ATTR_SNS3, ESPINK_ATTR_SNS4};
    for (int i = 0; i < 4; i++) {
        int x, y;
        sns_xy(i, &x, &y);
        draw_temp_text(u8g2, x, y, u8g2_font_helvR14_tf, GET(s, sns[i]), false);
    }

    /* Agenda (left column, under the sensors). */
    u8g2_SetFont(u8g2, u8g2_font_helvB14_tf);
    u8g2_DrawUTF8(u8g2, M, CAL_TITLE_BASE, "Agenda");
    u8g2_SetFont(u8g2, u8g2_font_helvR14_tf);
    const uint16_t cal[] = {ESPINK_ATTR_CAL1, ESPINK_ATTR_CAL2,
                            ESPINK_ATTR_CAL3, ESPINK_ATTR_CAL4};
    for (int i = 0; i < 4; i++) {
        u8g2_DrawUTF8(u8g2, M, CAL_BASE + i * CAL_ROW_H, GET(s, cal[i]));
    }

    /* Shopping list (right column, under the headache). */
    u8g2_SetFont(u8g2, u8g2_font_helvB14_tf);
    u8g2_DrawUTF8(u8g2, COL_R, SHOP_TITLE_BASE, "Shopping");
    u8g2_SetFont(u8g2, u8g2_font_helvR14_tf);
    const uint16_t shop[] = {ESPINK_ATTR_SHOP1, ESPINK_ATTR_SHOP2, ESPINK_ATTR_SHOP3,
                             ESPINK_ATTR_SHOP4, ESPINK_ATTR_SHOP5, ESPINK_ATTR_SHOP6};
    for (int i = 0; i < 6; i++) {
        const char *item = GET(s, shop[i]);
        if (item[0] != '\0') {
            u8g2_DrawUTF8(u8g2, COL_R, SHOP_BASE + i * SHOP_ROW_H, item);
        }
    }
}

static void draw_red_pass(u8g2_t *u8g2, const espink_slots_t *s)
{
    if (strcmp(GET(s, ESPINK_ATTR_ALERT_ACTIVE), "1") != 0) {
        return;
    }
    /* Red ribbon: a rule plus the alert text, both rendered into the red plane. */
    u8g2_DrawBox(u8g2, M, ALERT_BASE - 26, 8, 26);
    u8g2_SetFont(u8g2, u8g2_font_helvB18_tf);
    u8g2_DrawUTF8(u8g2, M + 16, ALERT_BASE, GET(s, ESPINK_ATTR_ALERT_TEXT));
}

/* Hot current temperatures (>= 25) drawn red: the big now value + any sensor
 * tile that is itself a Celsius reading. Layout mirrors draw_black_pass exactly. */
static void draw_temps_red(u8g2_t *u8g2, const espink_slots_t *s)
{
    draw_temp_text(u8g2, NOW_VAL_X, NOW_VAL_BASE, u8g2_font_helvB24_tf,
                   GET(s, ESPINK_ATTR_NOW_VALUE), true);
    const uint16_t sns[] = {ESPINK_ATTR_SNS1, ESPINK_ATTR_SNS2,
                            ESPINK_ATTR_SNS3, ESPINK_ATTR_SNS4};
    for (int i = 0; i < 4; i++) {
        int x, y;
        sns_xy(i, &x, &y);
        draw_temp_text(u8g2, x, y, u8g2_font_helvR14_tf, GET(s, sns[i]), true);
    }
}

/* Red highs (>= 25) for today's hi/lo and the forecast columns. */
static void draw_hilo_red(u8g2_t *u8g2, const espink_slots_t *s)
{
    draw_hilo(u8g2, NOW_VAL_X, NOW_HILO_BASE, u8g2_font_helvR14_tf,
              GET(s, ESPINK_ATTR_NOW_HILO), true);
    for (int i = 0; i < 3; i++) {
        char hilo[16];
        forecast_hilo(s, i, hilo, sizeof(hilo));
        draw_hilo(u8g2, FC_X + i * FC_COL_W, FC_BASE + FC_ROW_H + 8,
                  u8g2_font_helvR14_tf, hilo, true);
    }
}

/* The Severe headache icon is drawn red for emphasis (all other levels are
 * black, drawn in draw_black_pass). */
static void draw_headache_red(u8g2_t *u8g2, const espink_slots_t *s)
{
    int hv = headache_value(s);
    if (hv >= 0 && espink_headache_level(hv) == 5) {
        u8g2_DrawXBM(u8g2, HDC_ICON_X, HDC_ICON_Y, HEADACHE_ICON_W,
                     HEADACHE_ICON_H, headache_icons[4]);
    }
}

esp_err_t espink_display_create(epd_uc8179_dev_t *epd, espink_display_t **out)
{
    espink_display_t *d = calloc(1, sizeof(*d));
    if (!d) {
        return ESP_ERR_NO_MEM;
    }
    d->epd = epd;
    d->u8g2buf = heap_caps_malloc(U8G2_ESPINK_BUF_BYTES, MALLOC_CAP_8BIT);
    d->black = heap_caps_malloc(EPD_UC8179_PLANE_BYTES, MALLOC_CAP_8BIT);
    d->red = heap_caps_malloc(EPD_UC8179_PLANE_BYTES, MALLOC_CAP_8BIT);
    if (!d->u8g2buf || !d->black || !d->red) {
        free(d->u8g2buf); free(d->black); free(d->red); free(d);
        return ESP_ERR_NO_MEM;
    }
    u8g2_SetupEspink(&d->u8g2, d->u8g2buf);
    *out = d;
    return ESP_OK;
}

esp_err_t espink_display_render(espink_display_t *d, const espink_slots_t *slots)
{
    u8g2_ClearBuffer(&d->u8g2);
    draw_black_pass(&d->u8g2, slots);
    espink_u8g2_to_plane(d->u8g2buf, d->black);

    u8g2_ClearBuffer(&d->u8g2);
    draw_red_pass(&d->u8g2, slots);
    draw_headache_red(&d->u8g2, slots);
    draw_hilo_red(&d->u8g2, slots);
    draw_temps_red(&d->u8g2, slots);
    espink_u8g2_to_plane(d->u8g2buf, d->red);

    ESP_LOGI(TAG, "rendering frame (full refresh)");
    return epd_uc8179_display(d->epd, d->black, d->red);
}
