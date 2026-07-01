# ESPINK Phase 2a — Light-sleep Sleepy End Device

**Date:** 2026-07-02
**Status:** approved direction (user pointed at ESPIR slave's working sleep as the reference)

## Goal

Make the ESP32-C6 sleep between Zigbee polls so the dashboard can run from battery,
by porting the proven light-sleep SED pattern from `~/GIT/ESPIR`
(`components/espir_device/espir_device.c`, slave role).

## Decision: light-sleep SED, not deep sleep

The original plan's Phase 2 called for deep sleep + RTC wake + rejoin. The ESPIR
slave instead stays **joined forever** and auto-light-sleeps between parent polls:

- `esp_zb_sleep_enable(true)` + `esp_zb_set_rx_on_when_idle(false)` → true sleepy
  end device (radio off when idle, polls parent every `keep_alive` = 3 s).
- `esp_pm_configure(.light_sleep_enable = true)` + `CONFIG_PM_ENABLE` +
  `CONFIG_FREERTOS_USE_TICKLESS_IDLE` → CPU powers down between polls.
  All three are required; any one alone leaves the C6 at full clock.
- On `ESP_ZB_COMMON_SIGNAL_CAN_SLEEP` → `esp_zb_sleep_now()`, gated on
  (a) joined and (b) a 12 s boot grace window.

Why this beats deep sleep for ESPINK today:

- **HA push model unchanged.** Poll interval (3 s) < Zigbee parent buffer
  (7.68 s), so the existing time-pattern push automation keeps working; no
  on-device-online automation, no rejoin, no reboot per wake.
- **Proven on the same silicon** (both projects are ESP32-C6, same
  esp-zigbee-sdk); ESPIR's comments record the failure modes already paid for.
- Cost: ~100s of µA average instead of µA-class deep sleep. ESPIR's field
  experience: months on a LiPo. The ~26 s three-colour refresh dominates
  per-cycle energy anyway.

Deep sleep stays available as a later phase if measured battery life disappoints.

## Changes

### `sdkconfig.defaults`

```
CONFIG_PM_ENABLE=y
CONFIG_FREERTOS_USE_TICKLESS_IDLE=y
CONFIG_PM_SLP_IRAM_OPT=y
```

### `main/zigbee.c`

1. `power_save_init()` (under `#if CONFIG_PM_ENABLE`): `esp_pm_configure` with
   `max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ`, `min_freq_mhz = CONFIG_XTAL_FREQ`,
   `light_sleep_enable = true` (under tickless idle).
2. In `zb_task`, before `esp_zb_init`: call `power_save_init()` and
   `esp_zb_sleep_enable(true)`; after `esp_zb_init`:
   `esp_zb_set_rx_on_when_idle(false)`.
3. Signal handler: track `s_joined` (set on successful REBOOT-rejoin and
   STEERING); handle `ESP_ZB_COMMON_SIGNAL_CAN_SLEEP` →
   `esp_zb_sleep_now()` only when joined **and** ≥ 12 s after boot
   (light sleep drops USB-Serial-JTAG, and a crash-looping device that sleeps
   instantly can never be reflashed — the grace window keeps a flash window
   open every boot; see the ESPINK Errno-71 flashing gotcha).

## Interactions checked

- **EPD refresh (~26 s):** BUSY wait is a `vTaskDelay(20 ms)` poll loop —
  tickless-idle safe; the SPI master driver holds an `ESP_PM_APB_FREQ_MAX`
  lock during transactions, so DFS/light sleep can't corrupt transfers.
- **Coalesce timer:** `esp_timer` wakes the chip from light sleep; the 10 s
  quiet window still works because per-write gaps stay ≤ the 3 s poll latency.
- **Write bursts:** each of the ~24 push writes now waits for a poll; ZBOSS
  fast-polls during active transactions, and `CONFIG_IEEE802154_RX_BUFFER_SIZE=50`
  already absorbs bursts.
- **Console:** primary console is UART0; light sleep only degrades the
  secondary USB-JTAG console (same trade ESPIR accepted).

## Out of scope (future)

- Basic-cluster `power_source` → battery (0x03) and Power Configuration
  cluster battery %, once the hardware actually moves off USB.
- Panel PWR-GPIO power-cut between refreshes (orthogonal; works with either
  sleep model).
- Deep-sleep phase, if light-sleep battery life measures short.

## Testing

- Build clean; flash; confirm join, then log silence after grace window
  (sleep active) while a full dashboard push still renders correctly.
- Z2M should show the device with its usual availability; pushes land within
  seconds (poll latency), single repaint per push preserved.
