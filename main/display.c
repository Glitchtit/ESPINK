/* Dashboard renderer (see display.h).
 *
 * Two U8g2 passes share one drawing buffer: the black pass draws all monochrome
 * content and is transposed into the black plane; the red pass draws only the
 * alert highlight and is transposed into the red plane. The UC8179 driver then
 * does a single full refresh. */
#include "display.h"

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "u8g2.h"
#include "u8g2_espink.h"
#include "render_fb.h"

static const char *TAG = "display";

/* Layout constants (pixels). */
#define M           12   /* outer margin */
#define HDR_BASE    30
#define RULE_Y      44
#define NOW_VAL_X   M
#define NOW_VAL_BASE 150
#define NOW_ICON_X  220
#define NOW_ICON_BASE 150
#define NOW_LBL_BASE 210
#define FC_X        430
#define FC_COL_W    120
#define FC_BASE     110
#define FC_ROW_H    60
#define CAL_BASE    300
#define CAL_ROW_H   30
#define SNS_TOP     380
#define SNS_ROW_H   26
#define ALERT_BASE  462

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

    /* "Now" big value + condition. */
    u8g2_SetFont(u8g2, u8g2_font_helvB24_tf);
    u8g2_DrawUTF8(u8g2, NOW_VAL_X, NOW_VAL_BASE, GET(s, ESPINK_ATTR_NOW_VALUE));

    espink_split_icon(GET(s, ESPINK_ATTR_NOW_LABEL), token, sizeof(token), &text);
    uint8_t g = icon_glyph(token);
    if (g) {
        u8g2_SetFont(u8g2, u8g2_font_open_iconic_weather_6x_t);
        u8g2_DrawGlyph(u8g2, NOW_ICON_X, NOW_ICON_BASE, g);
    }
    u8g2_SetFont(u8g2, u8g2_font_helvR14_tf);
    u8g2_DrawUTF8(u8g2, NOW_VAL_X, NOW_LBL_BASE, text);

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
        u8g2_SetFont(u8g2, u8g2_font_helvR14_tf);
        u8g2_DrawUTF8(u8g2, x, FC_BASE + FC_ROW_H + 8, hilo);
    }

    /* Calendar / agenda lines. */
    u8g2_SetFont(u8g2, u8g2_font_helvR14_tf);
    const uint16_t cal[] = {ESPINK_ATTR_CAL1, ESPINK_ATTR_CAL2,
                            ESPINK_ATTR_CAL3, ESPINK_ATTR_CAL4};
    for (int i = 0; i < 4; i++) {
        u8g2_DrawUTF8(u8g2, M, CAL_BASE + i * CAL_ROW_H, GET(s, cal[i]));
    }

    /* Sensor tiles (two columns). */
    u8g2_SetFont(u8g2, u8g2_font_6x13_tf);
    const uint16_t sns[] = {ESPINK_ATTR_SNS1, ESPINK_ATTR_SNS2,
                            ESPINK_ATTR_SNS3, ESPINK_ATTR_SNS4};
    for (int i = 0; i < 4; i++) {
        int x = M + (i % 2) * (ESPINK_FB_WIDTH / 2);
        int y = SNS_TOP + (i / 2) * SNS_ROW_H;
        u8g2_DrawUTF8(u8g2, x, y, GET(s, sns[i]));
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
