# ESPINK hardware bring-up guide

Step-by-step validation on real hardware. Everything compiles and the pure logic
is unit-tested, but these things can only be confirmed on the panel + a live
Zigbee2MQTT: **pin map, plane polarity, font sizes, weather-icon glyph codes, and
the custom-cluster write path.** Work through the stages in order — each builds on
the previous one.

## 0. Flash & serial

```sh
. ~/esp/esp-idf/export.sh
idf.py set-target esp32c6      # first time only
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

The C6 dev board usually enumerates as `/dev/ttyACM0` (USB-Serial-JTAG). If flash
fails, hold BOOT, tap RST, release BOOT, retry.

## 1. Panel bring-up (rendering)

Phase 1 `main.c` clears the panel to white on boot, then waits for Zigbee data —
so a successful boot shows a **clean white screen** and the log line
`epd_uc8179: panel init complete`. If instead you want to see the full static
dashboard immediately (recommended for first bring-up), temporarily render the
placeholder: in `app_main`, after `espink_display_create`, add a local
`espink_slots_t`, call the old `load_placeholder()` + `espink_display_render()`
once (see git history of `main.c` Phase 0), flash, then revert.

Check, in order:

| Symptom | Cause | Fix |
|---------|-------|-----|
| Nothing happens, log hangs at "POWER ON" | BUSY wiring/polarity, or panel not powered | Verify BUSY GPIO + PWR GPIO; confirm `EPD_BUSY_TIMEOUT_MS` log doesn't fire |
| Screen all black or garbage | wrong `Display Config` HAT switch, or SPI pins | Set HAT switches (Interface=0, Display per 7.5"B wiki); recheck MOSI/SCK/CS/DC |
| Image is photo-negative (black↔white) | plane polarity | flip the send convention in `epd_uc8179_display` (drop/add the `~` on the black plane; the red plane already inverts) |
| Red where black expected (or none) | red-plane polarity | toggle the `invert` arg on the red `epd_send_plane` call |
| Text too small / clipped | font choice | swap fonts in `display.c` (e.g. a `logisoso*` font for `now_value`) |
| Wrong/blank weather icons | glyph codes | tune `icon_glyph()` in `display.c` against the open-iconic-weather chart |

The big number font (`helvB24`) is modest; for a bold hero temperature, generate
or pick a larger U8g2 font (e.g. `u8g2_font_logisoso58_tn` for digits) — confirm
the symbol exists in `components/u8g2/upstream/csrc/u8g2.h` first.

## 2. Zigbee join (Zigbee2MQTT)

1. Install the converter: copy `z2m/espink_dashboard.js` to
   `<z2m-data>/external_converters/`, restart Z2M, watch the log for
   `loaded external converter espink_dashboard.js`.
2. Permit join (Z2M frontend → "Permit join", or all-devices).
3. Power-cycle the device. In the serial log you should see
   `zigbee: stack started` → `network steering` → `joined network successfully`.
4. The device appears in Z2M as `ESPINK-7in5b`. In Home Assistant it exposes one
   `text.*` entity per slot plus `switch.*_alert_active`.

| Symptom | Cause | Fix |
|---------|-------|-----|
| Never joins | not in join mode / out of range / wrong channel | enable join; move closer; the firmware scans all channels |
| Joins but no entities | converter not loaded / model mismatch | check Z2M log; confirm `zigbeeModel: ['ESPINK-7in5b']` matches the Basic cluster model |
| Entities present but writes do nothing | manufacturer-code mismatch | both sides omit it by default — if you added one, add it to BOTH (see converter header) |

## 3. Data path (HA → panel)

1. In HA: Developer Tools → Actions → `text.set_value` on
   `text.espink_7in5b_now_value` = `"21°"`. Within ~2 s (coalesce debounce) the
   panel should repaint. Serial log: `zigbee: slot 0x0010 updated -> '21°'` then
   `display: rendering frame (full refresh)`.
2. Toggle `switch.espink_7in5b_alert_active` on and set `alert_text` → confirm the
   red ribbon appears (this forces a ~26 s full refresh).
3. Load `ha/automations.yaml` (adapt entity IDs/sources), run
   `script.espink_push_dashboard`, confirm the whole dashboard fills in.

Expect noisy `did not respond` warnings in the Z2M log when the device is busy
with a 26 s refresh — harmless; the converter sets `disableDefaultResponse`.

## 4. Record what you changed

If you adjusted pins, polarity, fonts, or glyph codes, commit those edits — they
are the hardware-specific truth that the unit tests and build can't capture.

## Next: Phase 2 (battery)

Once stages 1–3 pass on USB power, Phase 2 converts the device to a sleepy end
device: deep sleep with a ~20 min RTC wake, on-wake render, then PWR-pin cut +
`esp_deep_sleep_start()`; HA pushes current values on the device-online trigger
(already stubbed in `ha/automations.yaml`); add the Power Configuration cluster
for battery %. See the plan's Phase 2 section.
