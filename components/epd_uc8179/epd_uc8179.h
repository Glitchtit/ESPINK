/* UC8179 / GD7965 driver for the Waveshare 7.5" e-Paper (B) V2 (800x480, B/W/R),
 * ported from Waveshare's reference EPD_7in5b_V2 onto ESP-IDF spi_master.
 *
 * The caller owns the two 1-bit plane buffers (black and red), each
 * EPD_UC8179_PLANE_BYTES long. Buffer convention matches Waveshare's GUI_Paint:
 * in BOTH planes a bit = 1 means "no ink / white" and a bit = 0 means "ink".
 * The white background is therefore all 0xFF; you clear a bit to draw.
 *   - black plane (cmd 0x10): sent to the controller as-is
 *   - red plane   (cmd 0x13): sent inverted (~), matching the reference Display()
 * So: black ink -> clear a bit in `black`; red ink -> clear a bit in `red`. */
#ifndef EPD_UC8179_H
#define EPD_UC8179_H

#include <stdint.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"

#define EPD_UC8179_WIDTH       800
#define EPD_UC8179_HEIGHT      480
#define EPD_UC8179_PLANE_BYTES ((EPD_UC8179_WIDTH * EPD_UC8179_HEIGHT) / 8) /* 48000 */

/* Pin + bus configuration. CS/DC/RST/BUSY/PWR are plain GPIOs; MOSI/SCLK belong
 * to the chosen SPI host (FSPI on the C6). */
typedef struct {
    spi_host_device_t host;     /* e.g. SPI2_HOST (FSPI) */
    int clock_hz;               /* SPI clock, e.g. 10*1000*1000 */
    gpio_num_t mosi;            /* DIN */
    gpio_num_t sclk;            /* CLK */
    gpio_num_t cs;              /* CS  */
    gpio_num_t dc;              /* DC  */
    gpio_num_t rst;             /* RST */
    gpio_num_t busy;            /* BUSY (input) */
    gpio_num_t pwr;             /* PWR: HAT power gate; GPIO_NUM_NC to disable */
} epd_uc8179_pins_t;

typedef struct epd_uc8179_dev epd_uc8179_dev_t;

/* Install the SPI device + GPIOs and power up + initialise the panel.
 * On return the panel is ready for epd_uc8179_display(). */
esp_err_t epd_uc8179_init(const epd_uc8179_pins_t *pins, epd_uc8179_dev_t **out);

/* Gate panel power via the PWR pin (no-op if pwr == GPIO_NUM_NC). After
 * power_on() you must re-run the init sequence (epd_uc8179_panel_init()). */
void epd_uc8179_power_on(epd_uc8179_dev_t *dev);
void epd_uc8179_power_off(epd_uc8179_dev_t *dev);

/* Hardware reset + controller init sequence (call after a power_on). */
esp_err_t epd_uc8179_panel_init(epd_uc8179_dev_t *dev);

/* Push both planes and trigger a full refresh; blocks until the panel is idle.
 * black/red are each EPD_UC8179_PLANE_BYTES. red may be NULL (treated as blank,
 * giving a black/white-only image). */
esp_err_t epd_uc8179_display(epd_uc8179_dev_t *dev,
                             const uint8_t *black, const uint8_t *red);

/* Clear the panel to white. */
esp_err_t epd_uc8179_clear(epd_uc8179_dev_t *dev);

/* Controller power-off + deep sleep (DSLP 0xA5). Wake requires a hardware reset
 * + panel_init. Does NOT touch the PWR pin — call epd_uc8179_power_off() after
 * this to also cut the HAT regulator for battery use. */
esp_err_t epd_uc8179_sleep(epd_uc8179_dev_t *dev);

#endif /* EPD_UC8179_H */
