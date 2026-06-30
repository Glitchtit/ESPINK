# ESPINK — Zigbee e-Paper Dashboard

An ESP32-C6 firmware + Home Assistant integration that drives a **Waveshare 7.5"
e-Paper (B)** panel (800×480, red/black/white, UC8179) as a Home Assistant status
dashboard over **Zigbee2MQTT**. The firmware owns a fixed layout; HA sends only
short per-slot strings over Zigbee, so payloads stay tiny and play to Zigbee's
strengths. End goal: a battery-powered sleepy end device that wakes every
~15–30 min, refreshes, and sleeps.

Design rationale and research are in `../.claude/plans/pure-coalescing-pretzel.md`.

## Status

| Phase | Scope | State |
|-------|-------|-------|
| 0 | Panel + UC8179 driver + U8g2 renderer, static placeholder dashboard | **Builds**; needs hardware to validate on the panel |
| 1 | Zigbee end device + custom cluster 0xFC00, Z2M converter, HA push | Converter + HA examples written; **firmware Zigbee layer TODO** |
| 2 | Battery deep-sleep SED, on-wake push, battery % | TODO |

Verified on host (no hardware): pure slot logic and the U8g2→plane transpose
(`make -C test/host` → 29 checks pass). The firmware compiles for esp32c6.

## Hardware wiring (4-wire SPI; HAT Interface Config switch = 0)

| HAT pin | Function | C6 GPIO (default in `main/main.c`) |
|---------|----------|-----|
| VCC | 3.3 V | 3V3 |
| GND | GND | GND |
| DIN | SPI MOSI | 7 |
| CLK | SPI SCK | 6 |
| CS  | chip select | 10 |
| DC  | data/command | 4 |
| RST | reset | 5 |
| BUSY| busy (input) | 3 |
| PWR | panel power gate | 2 |

Set the two HAT slide switches for the 7.5" panel:

- **Display Config = B** (0.47R) — the 7.5" / 7.5"(B) panels use the 0.47R
  booster setting; position A (3R) is for the smaller panels.
- **Interface Config = 0** (4-line SPI) — this firmware uses 4-wire SPI (DC pin).

Adjust the pin map in `main/main.c` (`k_pins`) for your board — avoid strapping pins.

## Build & flash

```sh
. ~/esp/esp-idf/export.sh
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

The vendored U8g2 lives at `components/u8g2/upstream/` (gitignored). Re-create with:

```sh
git clone --depth 1 https://github.com/olikraus/u8g2 components/u8g2/upstream
```

## Host tests

```sh
make -C test/host        # slot logic + framebuffer transpose
```

## Zigbee2MQTT setup

1. Copy `z2m/espink_dashboard.js` to `<z2m-data>/external_converters/`.
2. Restart Z2M; confirm "loaded external converter" in the log.
3. Pair the device (enable join). It appears as `ESPINK-7in5b` with one writable
   `text` entity per slot plus `switch.*_alert_active`.

The custom cluster is `0xFC00` (a plain custom cluster, no manufacturer code) —
the attribute IDs must match `main/slots.h` and the Phase 1 Zigbee handler.

## Home Assistant

See `ha/automations.yaml` for a reusable `script.espink_push_dashboard` plus a
periodic-refresh automation (Phase 1) and an on-device-online push (Phase 2).
Adapt the entity IDs and source sensors to your setup.

## Layout

```
main/            app entry, slot store, framebuffer transpose, renderer
components/
  epd_uc8179/    UC8179/GD7965 SPI driver (Waveshare 7.5"B V2)
  u8g2/          U8g2 RAM-only 800x480 backend + vendored upstream
z2m/             Zigbee2MQTT external converter
ha/              Home Assistant script + automation examples
test/host/       host-compiled unit tests (pure logic)
```
