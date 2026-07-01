/* UC8179 / GD7965 driver (Waveshare 7.5"B V2) on ESP-IDF spi_master.
 * Command sequences ported verbatim from Waveshare's EPD_7in5b_V2 reference. */
#include "epd_uc8179.h"

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "epd_uc8179";

/* BUSY is active-low: the panel holds it LOW while busy and drives it HIGH when
 * idle. Cap the wait so a missing/miswired panel can't hang the task forever. */
#define EPD_BUSY_TIMEOUT_MS 40000

/* How many times to re-init + redraw when a refresh doesn't signal completion.
 * The usual cause is a transient supply sag on the high-current 3-colour refresh
 * that has passed by the retry, so one extra attempt catches almost all cases;
 * keep it small since each failed attempt burns up to EPD_BUSY_TIMEOUT_MS. */
#define EPD_REFRESH_RETRIES 1

struct epd_uc8179_dev {
    epd_uc8179_pins_t pins;
    spi_device_handle_t spi;
};

static esp_err_t epd_write(epd_uc8179_dev_t *dev, int dc_level,
                           const uint8_t *data, size_t len)
{
    gpio_set_level(dev->pins.dc, dc_level);
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    return spi_device_polling_transmit(dev->spi, &t);
}

static esp_err_t epd_cmd(epd_uc8179_dev_t *dev, uint8_t cmd)
{
    return epd_write(dev, 0, &cmd, 1);
}

static esp_err_t epd_data(epd_uc8179_dev_t *dev, const uint8_t *data, size_t len)
{
    return epd_write(dev, 1, data, len);
}

static esp_err_t epd_data1(epd_uc8179_dev_t *dev, uint8_t byte)
{
    return epd_write(dev, 1, &byte, 1);
}

/* Send a plane as a single constant byte, in CHUNKED transfers (used by clear). */
static esp_err_t epd_send_const(epd_uc8179_dev_t *dev, uint8_t value, size_t total)
{
    static uint8_t chunk[512];
    memset(chunk, value, sizeof(chunk));
    gpio_set_level(dev->pins.dc, 1);
    while (total > 0) {
        size_t n = total < sizeof(chunk) ? total : sizeof(chunk);
        spi_transaction_t t = { .length = n * 8, .tx_buffer = chunk };
        esp_err_t err = spi_device_polling_transmit(dev->spi, &t);
        if (err != ESP_OK) {
            return err;
        }
        total -= n;
    }
    return ESP_OK;
}

/* Send a plane buffer, optionally bitwise-inverted, in chunked transfers so we
 * never need a second 48 KB scratch buffer. */
static esp_err_t epd_send_plane(epd_uc8179_dev_t *dev, const uint8_t *buf,
                                size_t len, bool invert)
{
    static uint8_t chunk[512];
    gpio_set_level(dev->pins.dc, 1);
    size_t off = 0;
    while (off < len) {
        size_t n = (len - off) < sizeof(chunk) ? (len - off) : sizeof(chunk);
        if (invert) {
            for (size_t i = 0; i < n; i++) {
                chunk[i] = (uint8_t)~buf[off + i];
            }
        } else {
            memcpy(chunk, buf + off, n);
        }
        spi_transaction_t t = { .length = n * 8, .tx_buffer = chunk };
        esp_err_t err = spi_device_polling_transmit(dev->spi, &t);
        if (err != ESP_OK) {
            return err;
        }
        off += n;
    }
    return ESP_OK;
}

static esp_err_t epd_wait_idle(epd_uc8179_dev_t *dev)
{
    /* Per reference: issue 0x71 then poll BUSY until HIGH (idle). */
    int waited = 0;
    do {
        epd_cmd(dev, 0x71);
        if (gpio_get_level(dev->pins.busy) == 1) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        waited += 20;
        if (waited >= EPD_BUSY_TIMEOUT_MS) {
            ESP_LOGE(TAG, "BUSY timeout (panel not responding)");
            return ESP_ERR_TIMEOUT;
        }
    } while (true);
    vTaskDelay(pdMS_TO_TICKS(20));
    return ESP_OK;
}

static void epd_reset(epd_uc8179_dev_t *dev)
{
    gpio_set_level(dev->pins.rst, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(dev->pins.rst, 0);
    vTaskDelay(pdMS_TO_TICKS(4));
    gpio_set_level(dev->pins.rst, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
}

void epd_uc8179_power_on(epd_uc8179_dev_t *dev)
{
    if (dev->pins.pwr != GPIO_NUM_NC) {
        gpio_set_level(dev->pins.pwr, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void epd_uc8179_power_off(epd_uc8179_dev_t *dev)
{
    if (dev->pins.pwr != GPIO_NUM_NC) {
        gpio_set_level(dev->pins.pwr, 0);
    }
}

esp_err_t epd_uc8179_panel_init(epd_uc8179_dev_t *dev)
{
    epd_reset(dev);

    epd_cmd(dev, 0x01);                                   /* POWER SETTING */
    const uint8_t pwr[] = {0x07, 0x07, 0x3f, 0x3f};
    epd_data(dev, pwr, sizeof(pwr));

    epd_cmd(dev, 0x04);                                   /* POWER ON */
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_err_t err = epd_wait_idle(dev);
    if (err != ESP_OK) {
        return err;
    }

    epd_cmd(dev, 0x00);                                   /* PANEL SETTING */
    epd_data1(dev, 0x0F);

    epd_cmd(dev, 0x61);                                   /* TCON RESOLUTION 800x480 */
    const uint8_t res[] = {0x03, 0x20, 0x01, 0xE0};
    epd_data(dev, res, sizeof(res));

    epd_cmd(dev, 0x15);
    epd_data1(dev, 0x00);

    epd_cmd(dev, 0x50);                                   /* VCOM AND DATA INTERVAL */
    epd_data1(dev, 0x11);
    epd_data1(dev, 0x07);

    epd_cmd(dev, 0x60);                                   /* TCON SETTING */
    epd_data1(dev, 0x22);

    epd_cmd(dev, 0x65);                                   /* Resolution setting */
    const uint8_t res2[] = {0x00, 0x00, 0x00, 0x00};
    epd_data(dev, res2, sizeof(res2));

    ESP_LOGI(TAG, "panel init complete");
    return ESP_OK;
}

esp_err_t epd_uc8179_init(const epd_uc8179_pins_t *pins, epd_uc8179_dev_t **out)
{
    epd_uc8179_dev_t *dev = calloc(1, sizeof(*dev));
    if (dev == NULL) {
        return ESP_ERR_NO_MEM;
    }
    dev->pins = *pins;

    /* Control GPIOs. */
    uint64_t out_mask = (1ULL << pins->dc) | (1ULL << pins->rst) | (1ULL << pins->cs);
    if (pins->pwr != GPIO_NUM_NC) {
        out_mask |= (1ULL << pins->pwr);
    }
    gpio_config_t out_cfg = {
        .pin_bit_mask = out_mask,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out_cfg);
    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << pins->busy),
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&in_cfg);
    gpio_set_level(pins->cs, 1);

    /* SPI bus + device. CS is driven by the SPI peripheral. */
    spi_bus_config_t bus = {
        .mosi_io_num = pins->mosi,
        .miso_io_num = -1,
        .sclk_io_num = pins->sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(pins->host, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        free(dev);
        return err;
    }
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = pins->clock_hz,
        .mode = 0,
        .spics_io_num = pins->cs,
        .queue_size = 1,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };
    err = spi_bus_add_device(pins->host, &devcfg, &dev->spi);
    if (err != ESP_OK) {
        free(dev);
        return err;
    }

    epd_uc8179_power_on(dev);
    err = epd_uc8179_panel_init(dev);
    if (err != ESP_OK) {
        return err;
    }
    *out = dev;
    return ESP_OK;
}

/* One attempt: push both planes and fire a single refresh. Returns the BUSY
 * wait result — ESP_ERR_TIMEOUT means the panel never signalled completion and
 * is left mid-waveform (dim/ghosted/inverted). The public wrapper handles that. */
static esp_err_t epd_do_refresh(epd_uc8179_dev_t *dev,
                                const uint8_t *black, const uint8_t *red)
{
    esp_err_t err;

    epd_cmd(dev, 0x10);                                   /* black plane, direct */
    if (black) {
        err = epd_send_plane(dev, black, EPD_UC8179_PLANE_BYTES, false);
    } else {
        err = epd_send_const(dev, 0xFF, EPD_UC8179_PLANE_BYTES);
    }
    if (err != ESP_OK) {
        return err;
    }

    epd_cmd(dev, 0x13);                                   /* red plane, inverted */
    if (red) {
        err = epd_send_plane(dev, red, EPD_UC8179_PLANE_BYTES, true);
    } else {
        err = epd_send_const(dev, 0x00, EPD_UC8179_PLANE_BYTES);
    }
    if (err != ESP_OK) {
        return err;
    }

    epd_cmd(dev, 0x12);                                   /* refresh */
    vTaskDelay(pdMS_TO_TICKS(100));
    return epd_wait_idle(dev);
}

esp_err_t epd_uc8179_display(epd_uc8179_dev_t *dev,
                             const uint8_t *black, const uint8_t *red)
{
    esp_err_t err = ESP_OK;

    for (int attempt = 0; attempt <= EPD_REFRESH_RETRIES; attempt++) {
        if (attempt > 0) {
            /* The previous refresh never signalled completion, so the panel is
             * frozen mid-waveform (the dim/inverted state). A full HW reset +
             * re-init aborts the stuck waveform and puts the controller back in
             * a known state; then we redraw the whole frame from scratch. */
            ESP_LOGW(TAG, "refresh did not complete (%s); recovering panel (retry %d/%d)",
                     esp_err_to_name(err), attempt, EPD_REFRESH_RETRIES);
            esp_err_t rerr = epd_uc8179_panel_init(dev);
            if (rerr != ESP_OK) {
                ESP_LOGE(TAG, "panel re-init failed during recovery: %s",
                         esp_err_to_name(rerr));
            }
        }
        err = epd_do_refresh(dev, black, red);
        if (err == ESP_OK) {
            if (attempt > 0) {
                ESP_LOGI(TAG, "refresh recovered on retry %d", attempt);
            }
            return ESP_OK;
        }
    }

    /* Out of retries. Never leave the panel stuck mid-waveform: blank it to a
     * clean white so the failure degrades to an empty dashboard rather than a
     * corrupt one. The next slot push repaints normally. Best effort — if the
     * panel is genuinely unresponsive this clear also times out, and we still
     * report the original refresh error to the caller. */
    ESP_LOGE(TAG, "refresh failed after %d retr%s (%s); clearing to avoid a corrupt image",
             EPD_REFRESH_RETRIES, EPD_REFRESH_RETRIES == 1 ? "y" : "ies",
             esp_err_to_name(err));
    epd_uc8179_panel_init(dev);                           /* fresh state pre-clear */
    esp_err_t cerr = epd_uc8179_clear(dev);
    if (cerr != ESP_OK) {
        ESP_LOGE(TAG, "recovery clear also failed: %s", esp_err_to_name(cerr));
    }
    return err;                                           /* original failure */
}

esp_err_t epd_uc8179_clear(epd_uc8179_dev_t *dev)
{
    esp_err_t err;
    epd_cmd(dev, 0x10);
    err = epd_send_const(dev, 0xFF, EPD_UC8179_PLANE_BYTES);   /* white */
    if (err != ESP_OK) {
        return err;
    }
    epd_cmd(dev, 0x13);
    err = epd_send_const(dev, 0x00, EPD_UC8179_PLANE_BYTES);   /* no red */
    if (err != ESP_OK) {
        return err;
    }
    epd_cmd(dev, 0x12);
    vTaskDelay(pdMS_TO_TICKS(100));
    return epd_wait_idle(dev);
}

esp_err_t epd_uc8179_sleep(epd_uc8179_dev_t *dev)
{
    epd_cmd(dev, 0x02);                                   /* POWER OFF */
    esp_err_t err = epd_wait_idle(dev);
    if (err != ESP_OK) {
        return err;
    }
    epd_cmd(dev, 0x07);                                   /* DEEP SLEEP */
    epd_data1(dev, 0xA5);
    return ESP_OK;
}
