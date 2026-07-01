# ESPINK carrier PCB — design spec

Date: 2026-07-02
Status: draft for user review
Tool: EasyEDA Pro (schematic + PCB), fab target JLCPCB 2-layer

## Goal

A custom carrier PCB that turns the ESPINK prototype (devkit + Waveshare 7.5"
e-Paper (B) HAT + bench power) into a self-contained battery device:

- **MCU:** ESP32-C6-MINI-1 module (8 MB), running the existing firmware with
  the validated pin map **unchanged**.
- **Power:** 2× 18650 in **parallel (1S2P)**, on-board protection, USB-C
  recharge.
- **Display:** the existing, validated Waveshare e-Paper Driver HAT plugs onto
  a standard **Raspberry Pi 40-pin GPIO header** on this board. No panel-drive
  circuitry is re-implemented.
- **Input:** 3 capacitive touch buttons as **wire-soldered remote electrodes**
  (pads on the PCB, electrodes mounted in the enclosure).
- **Assembly:** hand-soldered, SMD allowed — packages ≥0603 passives and
  SOT/SOP ICs only; no QFN/BGA/fine-pitch.

Out of scope: enclosure design, firmware changes (noted where new firmware
work is implied), integrated UC8179 drive circuitry (rejected — would
re-validate a working power stage).

## Architecture

```
USB-C (5V) ──► TP4056 charger ──► 1S2P 18650 pack (DW01A+FS8205A protection)
     │              │                    │
     │        load-share P-FET ──────────┤
     │                                   ▼
     ├─ D+/D− ─► ESP32-C6 USB-JTAG   XC6220B331 LDO ──► 3V3 rail
     │                                   │
     └─ CC1/CC2 5.1k pulldowns           ├─► ESP32-C6-MINI-1
                                         ├─► 40-pin RPi header (HAT)
                                         └─► 3× TTP223 touch ICs
```

## 1. Battery

- 2× through-hole 18650 holders, wired in **parallel** (~3.6 V nominal,
  ≥6000 mAh with typical cells). **Cells must be an identical, matched pair
  installed at the same state of charge** (parallel pack; document on silk).
- Protection: **DW01A + FS8205A** (over-charge 4.25 V, over-discharge 2.4 V,
  overcurrent). Allows unprotected cells.
- No fuel gauge IC. Battery voltage divider (2× 470 k + 100 nF) into a C6 ADC
  pin, **high-side switched by a P-MOSFET on a GPIO** so it draws nothing in
  sleep. Feeds the roadmap Zigbee Power Configuration battery %.

## 2. Charging & USB

- **TP4056** (SOP-8 w/ thermal pad variant TP4056-42-ESOP8), PROG = 1.2 kΩ →
  ~1 A charge current. CHRG / STDBY LEDs (only lit while VBUS present).
- **Load sharing:** standard P-MOSFET + Schottky circuit so the system runs
  from VBUS while charging and the TP4056 terminates correctly.
- **USB-C receptacle**, 16-pin power+USB2.0 type with through-hole shell tabs
  (hand-solderable). CC1/CC2 → 5.1 kΩ pulldowns (UFP, 5 V from any charger).
- D+/D− → C6 **GPIO12/13 (native USB-Serial-JTAG)** through a USBLC6-2SC6 ESD
  protector. Same `idf.py flash monitor` workflow, one connector for
  charge + flash. (Reminder from firmware: light sleep kills USB-JTAG — the
  12 s boot grace window is the reflash opportunity.)
- VBUS is never routed to the HAT 5 V pins (HAT is 3.3 V-only; 5 V pins left NC).

## 3. 3V3 rail

- **XC6220B331MR** LDO: 1 A, ~0.1 V dropout, ~8 µA Iq. Input from the
  load-share node, output is the single 3V3 rail.
- Rationale vs a buck (TPS62840): buck saves ~15 % only during the ~26 s
  refresh bursts; with ≥6 Ah and a µA sleep floor, LDO simplicity + low Iq +
  hand-solderable SOT-25 wins.
- Bulk: ≥22 µF at LDO in/out plus 100 µF near the 40-pin header (panel refresh
  transients on 3V3).

## 4. MCU section

- **ESP32-C6-MINI-1-N8**. PCB antenna keep-out per Espressif hardware design
  guide: module antenna edge overhangs or faces board edge, no copper under
  antenna on any layer.
- EN: 10 k pull-up + 1 µF RC; **RESET** and **BOOT (GPIO9)** tactile buttons.
- Panel signals use the firmware's existing map (unchanged): PWR 2, BUSY 3,
  DC 4, RST 5, CLK 6, MOSI 7, CS 10.
- New allocations (firmware work implied, exact pins verified against the
  MINI-1 datasheet during implementation planning): battery ADC sense (ADC
  pin, e.g. GPIO1), divider-enable GPIO (e.g. GPIO0), 3 touch inputs (e.g.
  GPIO14/18/19 — must be wake-capable for light-sleep wake). Strapping pins
  GPIO8/9/15 are not used for touch.

## 5. HAT interface — 40-pin RPi header

2×20 male 2.54 mm header, Raspberry Pi GPIO pinout positions, so the
Waveshare HAT stacks directly. Only the pins the HAT uses are driven:

| Signal | C6 GPIO | RPi physical pin (BCM) |
|--------|---------|------------------------|
| 3V3    | rail    | 1, 17                  |
| GND    | rail    | 6, 9, 14, 20, 25       |
| DIN    | 7       | 19 (MOSI/GPIO10)       |
| CLK    | 6       | 23 (SCLK/GPIO11)       |
| CS     | 10      | 24 (CE0/GPIO8)         |
| DC     | 4       | 22 (GPIO25)            |
| RST    | 5       | 11 (GPIO17)            |
| BUSY   | 3       | 18 (GPIO24)            |
| PWR    | 2       | 12 (GPIO18)            |

All other pins NC (5 V pins explicitly unconnected). **Verify this mapping
against the Waveshare 7.5" e-Paper HAT wiki/schematic during implementation
planning before routing.** Header + two mounting holes placed to match the Pi
HAT mechanical spec (49 × 58 mm hole grid relative to the header) so the HAT
stacks on standoffs.

## 6. Capacitive touch (×3)

- 3× **TTP223** (SOT-23-6), one per button, strapped for low-power mode
  (~1.5 µA each) and momentary active-high output.
- Each channel: solder pad (wire to remote electrode in the enclosure), Cs
  sensitivity-tuning capacitor footprint (0603, DNP by default, fit 0–50 pF
  to de-sensitize for wire length), 1 k series resistor on the electrode line.
- Outputs → C6 GPIOs (wake-capable, see §4). Keep electrode wires short
  (<10 cm) and away from the SPI lines.

## 7. Board & assembly

- 2-layer, ~90 × 50 mm (dominated by two 18650 holders side by side); ground
  pour both sides. USB-C on a short edge; touch pads on the edge nearest the
  planned enclosure buttons; M3 mounting holes in corners **plus** the
  HAT-standoff pair from §5.
- Silkscreen: cell polarity + "matched cells only", pin-1 marks, signal names
  on the touch/battery pads.
- Hand-solder constraints: passives 0603 minimum (0805 preferred), ICs
  SOT-23/SOT-25/SOP-8 only, USB-C with through-hole shield tabs, all
  connectors through-hole.

## 8. Bring-up plan

1. Populate power path only → verify 3V3, charge cycle with one cell, and
   load-share switchover.
2. Fit MCU section → flash over USB-C, confirm USB-JTAG + Zigbee join.
3. Stack HAT → full dashboard render.
4. Touch ICs last → tune Cs for final wire/electrode setup.

## Firmware follow-ups implied (separate work, not this board)

- Battery ADC read + divider-enable GPIO + Power Configuration cluster %.
- Touch GPIO handlers (light-sleep wake; button actions defined when that
  firmware work starts — the board only commits pads and GPIOs).
- `power_source = battery` in Basic cluster.
