/* Dashboard renderer (see display.h).
 *
 * Framed "certificate" layout: a header, four double-bordered panels (weather,
 * headache, living-room + agenda, shopping), and a footer. Two U8g2 passes share
 * one buffer: the black pass draws everything, the red pass re-draws only the
 * elements that should be red (hot temps, high forecast values, holiday header,
 * severe headache, alert). The UC8179 driver does one full refresh. */
#include "display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "u8g2.h"
#include "u8g2_espink.h"
#include "render_fb.h"
#include "headache_icons.h"

static const char *TAG = "display";

/* ---- layout (pixels) ---- */
#define M            8
#define HDR_BASE     28
#define RULE_Y       44
/* panel grid: two columns, two rows */
#define PL_X         8      /* left column x, width 396 -> right edge 404 */
#define PL_W         396
#define PR_X         414    /* right column x, width 378 -> right edge 792 */
#define PR_W         378
#define PT_Y         52     /* top row */
#define PT_H         198    /* -> bottom 250 */
#define PB_Y         260    /* bottom row */
#define PB_H         192    /* -> bottom 452 */

/* weather panel */
#define W_ICON_X     22
#define W_ICON_BASE  116
#define NOW_VAL_X    112
#define NOW_VAL_BASE 112
#define W_COND_X     250
#define W_COND_BASE  100
#define NOW_HILO_X   250
#define NOW_HILO_BASE 128
#define W_DIV_Y      142
#define FC_BASE      168    /* forecast day label baseline */
static const int k_fc_cx[3] = {70, 196, 322};   /* forecast column centres */

/* headache panel */
#define HDC_ICON_X   428
#define HDC_ICON_Y   74
#define HDC_TEXT_X   524

/* living-room + agenda panel */
#define LR_TITLE_BASE 360
#define LR_L1_BASE    384   /* sns1 (temperature) */
#define LR_L2_BASE    404   /* sns2 (humidity) */
#define AG_X          160
#define AG_TITLE_BASE 288
#define AG_BASE       316   /* first agenda item */
#define AG_ROW_H      31

/* shopping panel */
#define SH_TITLE_BASE 294
#define SH_X          446
#define SH_BASE       320
#define SH_ROW_H      24

/* colour thresholds */
#define RED_HI_THRESHOLD 25
#define RED_MARK '~'   /* leading marker -> draw the value red (stripped) */

static const char *const k_headache_words[5] = {
    "Low", "Slight", "Moderate", "High", "Severe",
};

struct espink_display {
    epd_uc8179_dev_t *epd;
    uint8_t *u8g2buf;
    uint8_t *black;
    uint8_t *red;
    u8g2_t u8g2;
};

#define GET(slots, attr) espink_slots_get((slots), espink_slot_index_for_attr(attr))

static int headache_value(const espink_slots_t *s)
{
    const char *v = GET(s, ESPINK_ATTR_HEADACHE);
    return v[0] == '\0' ? -1 : atoi(v);
}

/* Map a weather token to an open_iconic_weather glyph code (tune on hardware). */
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

/* ---- small drawing helpers ---- */

/* Double rounded border with ornamental corner ticks. */
static void draw_panel_frame(u8g2_t *u8g2, int x, int y, int w, int h)
{
    u8g2_DrawRFrame(u8g2, x, y, w, h, 9);
    u8g2_DrawRFrame(u8g2, x + 4, y + 4, w - 8, h - 8, 6);
    const int t = 8;
    u8g2_DrawLine(u8g2, x + 10, y + 10 + t, x + 10 + t, y + 10);
    u8g2_DrawLine(u8g2, x + w - 11 - t, y + 10, x + w - 11, y + 10 + t);
    u8g2_DrawLine(u8g2, x + 10, y + h - 11 - t, x + 10 + t, y + h - 11);
    u8g2_DrawLine(u8g2, x + w - 11 - t, y + h - 11, x + w - 11, y + h - 11 - t);
}

static void draw_checkbox(u8g2_t *u8g2, int x, int baseline)
{
    u8g2_DrawFrame(u8g2, x, baseline - 12, 13, 13);
}

/* Simple house glyph (roof + body + door) in the box [x..x+42, y..y+46]. */
static void draw_house(u8g2_t *u8g2, int x, int y)
{
    u8g2_DrawTriangle(u8g2, x, y + 18, x + 21, y, x + 42, y + 18);
    u8g2_DrawFrame(u8g2, x + 6, y + 18, 30, 28);
    u8g2_DrawFrame(u8g2, x + 17, y + 32, 8, 14);
}

/* Simple shopping-cart glyph near (x, y). */
static void draw_cart(u8g2_t *u8g2, int x, int y)
{
    u8g2_DrawLine(u8g2, x, y, x + 5, y);
    u8g2_DrawLine(u8g2, x + 5, y, x + 9, y + 16);
    u8g2_DrawLine(u8g2, x + 9, y + 16, x + 26, y + 16);
    u8g2_DrawLine(u8g2, x + 6, y + 4, x + 30, y + 4);
    u8g2_DrawLine(u8g2, x + 30, y + 4, x + 26, y + 16);
    u8g2_DrawDisc(u8g2, x + 12, y + 21, 2, U8G2_DRAW_ALL);
    u8g2_DrawDisc(u8g2, x + 24, y + 21, 2, U8G2_DRAW_ALL);
}

/* Draw a "HH/LL" hi/lo; the high renders red (red pass) when >= threshold. */
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
        u8g2_DrawUTF8(u8g2, x + u8g2_GetUTF8Width(u8g2, hi), y, slash);
    }
}

/* A value ending in a Celsius temp >= 25 renders red; drawn once per pass. */
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

/* A value with a leading RED_MARK draws red (marker stripped); else black. */
static void draw_marked(u8g2_t *u8g2, int x, int y, const uint8_t *font,
                        const char *text, bool red_pass)
{
    bool marked = (text[0] == RED_MARK);
    const char *content = marked ? text + 1 : text;
    if (content[0] == '\0') {
        return;
    }
    if (red_pass == marked) {
        u8g2_SetFont(u8g2, font);
        u8g2_DrawUTF8(u8g2, x, y, content);
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

/* ---- panels ---- */

static void draw_weather_panel(u8g2_t *u8g2, const espink_slots_t *s)
{
    char token[16];
    const char *cond;
    draw_panel_frame(u8g2, PL_X, PT_Y, PL_W, PT_H);

    espink_split_icon(GET(s, ESPINK_ATTR_NOW_LABEL), token, sizeof(token), &cond);
    uint8_t g = icon_glyph(token);
    if (g) {
        u8g2_SetFont(u8g2, u8g2_font_open_iconic_weather_6x_t);
        u8g2_DrawGlyph(u8g2, W_ICON_X, W_ICON_BASE, g);
    }
    draw_temp_text(u8g2, NOW_VAL_X, NOW_VAL_BASE, u8g2_font_fub30_tf,
                   GET(s, ESPINK_ATTR_NOW_VALUE), false);
    u8g2_SetFont(u8g2, u8g2_font_helvR14_tf);
    u8g2_DrawUTF8(u8g2, W_COND_X, W_COND_BASE, cond);
    draw_hilo(u8g2, NOW_HILO_X, NOW_HILO_BASE, u8g2_font_helvR14_tf,
              GET(s, ESPINK_ATTR_NOW_HILO), false);

    u8g2_DrawHLine(u8g2, PL_X + 12, W_DIV_Y, PL_W - 24);

    const uint16_t fc[] = {ESPINK_ATTR_FC1, ESPINK_ATTR_FC2, ESPINK_ATTR_FC3};
    for (int i = 0; i < 3; i++) {
        const char *raw = GET(s, fc[i]);
        if (raw[0] == '\0') {
            continue;
        }
        char day[16] = "", ic[16] = "", hilo[16] = "";
        const char *c1 = strchr(raw, ':');
        if (c1) {
            size_t n = (size_t)(c1 - raw); if (n > 15) n = 15;
            memcpy(day, raw, n); day[n] = '\0';
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
        int cx = k_fc_cx[i];
        u8g2_SetFont(u8g2, u8g2_font_helvB14_tf);
        u8g2_DrawUTF8(u8g2, cx - u8g2_GetUTF8Width(u8g2, day) / 2, FC_BASE, day);
        uint8_t fg = icon_glyph(ic);
        if (fg) {
            u8g2_SetFont(u8g2, u8g2_font_open_iconic_weather_4x_t);
            u8g2_DrawGlyph(u8g2, cx - 16, FC_BASE + 40, fg);
        }
        u8g2_SetFont(u8g2, u8g2_font_helvR14_tf);
        draw_hilo(u8g2, cx - u8g2_GetUTF8Width(u8g2, hilo) / 2, FC_BASE + 72,
                  u8g2_font_helvR14_tf, hilo, false);
    }
}

static void draw_headache_panel(u8g2_t *u8g2, const espink_slots_t *s)
{
    draw_panel_frame(u8g2, PR_X, PT_Y, PR_W, PT_H);
    int hv = headache_value(s);
    u8g2_SetFont(u8g2, u8g2_font_helvB18_tf);
    u8g2_DrawUTF8(u8g2, HDC_TEXT_X, 108, "HEADACHE RISK");
    if (hv < 0) {
        return;
    }
    int lvl = espink_headache_level(hv);
    if (lvl < 5) {
        u8g2_DrawXBM(u8g2, HDC_ICON_X, HDC_ICON_Y, HEADACHE_ICON_W,
                     HEADACHE_ICON_H, headache_icons[lvl - 1]);
    }
    /* Level word + the risk percentage, e.g. "Low (14%)". */
    char buf[24];
    snprintf(buf, sizeof(buf), "%s (%d%%)", k_headache_words[lvl - 1], hv);
    u8g2_SetFont(u8g2, u8g2_font_helvB18_tf);
    u8g2_DrawUTF8(u8g2, HDC_TEXT_X, 154, buf);
}

static void draw_living_agenda_panel(u8g2_t *u8g2, const espink_slots_t *s)
{
    draw_panel_frame(u8g2, PL_X, PB_Y, PL_W, PB_H);
    draw_house(u8g2, 40, 286);
    u8g2_SetFont(u8g2, u8g2_font_helvB14_tf);
    u8g2_DrawUTF8(u8g2, 18, LR_TITLE_BASE, "LIVING ROOM");
    draw_temp_text(u8g2, 18, LR_L1_BASE, u8g2_font_helvR12_tf, GET(s, ESPINK_ATTR_SNS1), false);
    u8g2_SetFont(u8g2, u8g2_font_helvR12_tf);
    u8g2_DrawUTF8(u8g2, 18, LR_L2_BASE, GET(s, ESPINK_ATTR_SNS2));

    u8g2_DrawVLine(u8g2, 150, PB_Y + 12, PB_H - 24);

    u8g2_SetFont(u8g2, u8g2_font_helvB14_tf);
    u8g2_DrawUTF8(u8g2, AG_X, AG_TITLE_BASE, "DAILY AGENDA");
    const uint16_t cal[] = {ESPINK_ATTR_CAL1, ESPINK_ATTR_CAL2,
                            ESPINK_ATTR_CAL3, ESPINK_ATTR_CAL4};
    u8g2_SetFont(u8g2, u8g2_font_helvR14_tf);
    for (int i = 0; i < 4; i++) {
        const char *it = GET(s, cal[i]);
        if (it[0] == '\0') {
            continue;
        }
        int y = AG_BASE + i * AG_ROW_H;
        draw_checkbox(u8g2, AG_X, y);
        u8g2_DrawUTF8(u8g2, AG_X + 20, y, it);
    }
}

static void draw_shopping_panel(u8g2_t *u8g2, const espink_slots_t *s)
{
    draw_panel_frame(u8g2, PR_X, PB_Y, PR_W, PB_H);
    draw_cart(u8g2, 426, 278);
    u8g2_SetFont(u8g2, u8g2_font_helvB18_tf);
    u8g2_DrawUTF8(u8g2, 474, SH_TITLE_BASE, "SHOPPING LIST");
    const uint16_t shop[] = {ESPINK_ATTR_SHOP1, ESPINK_ATTR_SHOP2, ESPINK_ATTR_SHOP3,
                             ESPINK_ATTR_SHOP4, ESPINK_ATTR_SHOP5, ESPINK_ATTR_SHOP6};
    u8g2_SetFont(u8g2, u8g2_font_helvR14_tf);
    for (int i = 0; i < 6; i++) {
        const char *it = GET(s, shop[i]);
        if (it[0] == '\0') {
            continue;
        }
        int y = SH_BASE + i * SH_ROW_H;
        draw_checkbox(u8g2, PR_X + 12, y);
        u8g2_DrawUTF8(u8g2, SH_X, y, it);
    }
}

static void draw_footer(u8g2_t *u8g2)
{
    /* Battery indicator (bottom-right); becomes real in Phase 2. */
    u8g2_DrawFrame(u8g2, 768, 461, 22, 12);      /* battery body */
    u8g2_DrawBox(u8g2, 790, 464, 3, 6);          /* battery nub */
    u8g2_DrawBox(u8g2, 770, 463, 16, 8);         /* charge level */
}

static void draw_black_pass(u8g2_t *u8g2, const espink_slots_t *s)
{
    /* Header (hdr_left may be red-marked for holidays/Sundays). */
    draw_marked(u8g2, M, HDR_BASE, u8g2_font_helvR14_tf, GET(s, ESPINK_ATTR_HDR_LEFT), false);
    const char *hr = GET(s, ESPINK_ATTR_HDR_RIGHT);
    u8g2_SetFont(u8g2, u8g2_font_helvR14_tf);
    u8g2_DrawUTF8(u8g2, ESPINK_FB_WIDTH - M - u8g2_GetUTF8Width(u8g2, hr), HDR_BASE, hr);
    u8g2_DrawHLine(u8g2, M, RULE_Y, ESPINK_FB_WIDTH - 2 * M);

    draw_weather_panel(u8g2, s);
    draw_headache_panel(u8g2, s);
    draw_living_agenda_panel(u8g2, s);
    draw_shopping_panel(u8g2, s);
    draw_footer(u8g2);
}

/* ---- red pass: only the elements that should be red ---- */

static void draw_red_pass(u8g2_t *u8g2, const espink_slots_t *s)
{
    /* Holiday/Sunday header. */
    draw_marked(u8g2, M, HDR_BASE, u8g2_font_helvR14_tf, GET(s, ESPINK_ATTR_HDR_LEFT), true);

    /* Hot current temperatures: hero value + the living-room tile. */
    draw_temp_text(u8g2, NOW_VAL_X, NOW_VAL_BASE, u8g2_font_fub30_tf,
                   GET(s, ESPINK_ATTR_NOW_VALUE), true);
    draw_temp_text(u8g2, 18, LR_L1_BASE, u8g2_font_helvR12_tf, GET(s, ESPINK_ATTR_SNS1), true);

    /* Hot highs: today + forecast columns. */
    draw_hilo(u8g2, NOW_HILO_X, NOW_HILO_BASE, u8g2_font_helvR14_tf,
              GET(s, ESPINK_ATTR_NOW_HILO), true);
    for (int i = 0; i < 3; i++) {
        char hilo[16];
        forecast_hilo(s, i, hilo, sizeof(hilo));
        u8g2_SetFont(u8g2, u8g2_font_helvR14_tf);
        draw_hilo(u8g2, k_fc_cx[i] - u8g2_GetUTF8Width(u8g2, hilo) / 2, FC_BASE + 72,
                  u8g2_font_helvR14_tf, hilo, true);
    }

    /* Severe headache icon in red. */
    int hv = headache_value(s);
    if (hv >= 0 && espink_headache_level(hv) == 5) {
        u8g2_DrawXBM(u8g2, HDC_ICON_X, HDC_ICON_Y, HEADACHE_ICON_W,
                     HEADACHE_ICON_H, headache_icons[4]);
    }

    /* Red alert ribbon (bottom of the living/agenda panel) when active. */
    if (strcmp(GET(s, ESPINK_ATTR_ALERT_ACTIVE), "1") == 0) {
        u8g2_DrawBox(u8g2, PL_X + 12, PB_Y + PB_H - 24, 8, 18);
        u8g2_SetFont(u8g2, u8g2_font_helvB14_tf);
        u8g2_DrawUTF8(u8g2, PL_X + 26, PB_Y + PB_H - 10, GET(s, ESPINK_ATTR_ALERT_TEXT));
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
    espink_u8g2_to_plane(d->u8g2buf, d->red);

    ESP_LOGI(TAG, "rendering frame (full refresh)");
    return epd_uc8179_display(d->epd, d->black, d->red);
}
