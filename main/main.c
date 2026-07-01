/* ESPINK — Zigbee e-Paper dashboard, application entry point.
 *
 * Phase 1: join Zigbee2MQTT, receive slot values via custom cluster 0xFC00, and
 * repaint the dashboard when writes settle. The Zigbee module coalesces writes
 * and notifies the render task; the render task does the (slow) panel refresh so
 * the Zigbee stack task is never blocked. */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "nvs_flash.h"
#include "slots.h"
#include "display.h"
#include "epd_uc8179.h"
#include "zigbee.h"

static const char *TAG = "espink";

/* Pin map — generic ESP32-C6 devkit -> e-Paper Driver HAT (4-wire SPI).
 * Adjust to your board; avoid strapping pins. See plan wiring table. */
static const epd_uc8179_pins_t k_pins = {
    .host = SPI2_HOST,           /* FSPI */
    .clock_hz = 10 * 1000 * 1000,
    .mosi = 7, .sclk = 6, .cs = 10, .dc = 4, .rst = 5, .busy = 3, .pwr = 2,
};

#define BOOT_MAGIC 0xE5B00711u
/* Retained across a soft reset (reboot) but random after a power cycle. */
static RTC_NOINIT_ATTR uint32_t s_boot_magic;

static espink_display_t *s_disp;
static TaskHandle_t s_render_task;
static const espink_slots_t *volatile s_pending_slots;

/* Runs in the Zigbee coalesce-timer context: keep it short, just hand off. */
static void on_repaint_due(const espink_slots_t *slots, void *ctx)
{
    (void)ctx;
    s_pending_slots = slots;
    xTaskNotifyGive(s_render_task);
}

static void render_task(void *arg)
{
    (void)arg;
    static espink_slots_t snapshot;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        /* Snapshot quickly, then run the ~26 s refresh from the copy. A write
         * landing mid-copy is cosmetic (one stale slot until the next wake). */
        if (s_pending_slots) {
            memcpy(&snapshot, (const void *)s_pending_slots, sizeof(snapshot));
        }
        ESP_LOGI(TAG, "repaint");
        esp_err_t err = espink_display_render(s_disp, &snapshot);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "render failed: %s", esp_err_to_name(err));
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESPINK booting (Phase 1: Zigbee dashboard)");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    epd_uc8179_dev_t *epd = NULL;
    ESP_ERROR_CHECK(epd_uc8179_init(&k_pins, &epd));
    ESP_ERROR_CHECK(espink_display_create(epd, &s_disp));

    /* Clear only on a true power-on, not on a reboot. E-paper retains its image
     * without power, so skipping the clear on soft-resets means a crash/reboot
     * leaves the last dashboard on screen instead of going blank. s_boot_magic
     * survives a soft reset (RTC_NOINIT) but is random after power-on. */
    if (s_boot_magic != BOOT_MAGIC) {
        s_boot_magic = BOOT_MAGIC;
        ESP_LOGI(TAG, "power-on: clearing panel");
        epd_uc8179_clear(epd);
    } else {
        ESP_LOGI(TAG, "reboot: keeping last image");
    }

    xTaskCreate(render_task, "espink_render", 8192, NULL, 4, &s_render_task);
    espink_zigbee_start(on_repaint_due, NULL);

    ESP_LOGI(TAG, "ready; waiting for Zigbee slot writes");
}
