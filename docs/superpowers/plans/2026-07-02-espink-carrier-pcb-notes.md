# ESPINK carrier PCB — execution notes

## Task 1: verified external facts (2026-07-02)

### Waveshare 7.5" e-Paper HAT (B) — Raspberry Pi pinout (official wiki, verified)

Source: waveshare.com/wiki/7.5inch_e-Paper_HAT_(B)_Manual, "Raspberry Pi Pinout" table
(e-Paper / BCM2835 / Board pin):

| e-Paper | BCM | RPi physical pin |
|---|---|---|
| VCC | 3.3V | 3.3V (pins 1/17) |
| GND | GND | GND |
| DIN | MOSI | **19** |
| CLK | SCLK | **23** |
| CS  | CE0  | **24** |
| DC  | 25   | **22** |
| RST | 17   | **11** |
| BUSY| 24   | **18** |
| PWR | 18   | **12** |

→ **Matches the plan's J2 row exactly. PWR pin exists on this revision.**
- HAT driver board is **65 × 30.2 mm** (small board, not full-Pi size).
- V2.1+ driver boards have level conversion (3.3 V or 5 V); we run VCC = 3.3 V,
  IO level = supply level ⇒ all-3.3 V is correct.

### ESP32-C6-MINI-1 (datasheet v1.5, verified)

- 53-pin module. Exposed IOs (module pin): IO0(12), IO1(13), IO2(5), IO3(6),
  IO4(9), IO5(10), IO6(15), IO7(16), IO8(22), IO9(23), IO12(17), IO13(18),
  IO14(19), IO15(20), IO18(24), IO19(25), IO20(26), IO21(27), IO22(28),
  IO23(29), RXD0=GPIO17(30), TXD0=GPIO16(31), EN(8), 3V3(3),
  GND(1,2,11,14,36–53 incl. center pads), NC(4,7,21,32–35).
- **IO14, IO18, IO19 confirmed exposed** → TOUCH1/2/3 as planned.
- **USB: GPIO12 = USB_D−, GPIO13 = USB_D+** (module pins 17/18) → J1 D−→IO12, D+→IO13.
- ADC: IO0 = ADC1_CH0, IO1 = ADC1_CH1 → VBAT_ADC on IO1, divider-enable on IO0 (digital out).
- Strapping pins: MTMS(IO4), MTDI(IO5), IO8, IO9 (weak pull-up default), IO15.
  - IO4/IO5 = EPD_DC/EPD_RST (unchanged firmware map): strapping only selects the
    SDIO clock edge (unused peripheral) and is already validated on hardware. OK.
  - IO8: leave NC (floating default OK; boot mode needs GPIO9=0 + GPIO8=1 only for
    download mode — USB-JTAG download still reachable via BOOT button per C6 ROM).
    NOTE for schematic: give IO8 a No-Connect flag; do NOT load it.
  - IO9 = BOOT button (pull-up internal, button to GND) as planned.
  - IO15: leave NC.
- Module has an antenna **Keepout Zone** at the antenna end (top of module) — place
  that edge flush/overhanging the board edge, prohibited region for copper.
- EN: must not float → 10 k pull-up + 1 µF RC + RESET button as planned.

### Raspberry Pi 40-pin header ↔ mounting-hole geometry (Pi 4 mechanical drawing, verified)

- Board 85 × 56 mm, corner radius 3.0 mm; mounting holes Ø2.7 mm (M2.5) at
  **3.5 mm from both edges**, grid **58 mm × 49 mm**.
- 40-pin header (J8): **centered between the two holes on the header edge —
  header center 29 mm from each hole; header centerline collinear with the two
  hole centers** (holes and header rows share the y = 3.5 mm line; rows at ±1.27 mm).
- Derived for our carrier: place two standoff holes on the header centerline,
  58 mm apart, header centered between them; pin span 48.26 mm ⇒ pin 1 is
  24.13 mm left of header center, pin-1 row on the board-interior side
  (pin 2 row toward the board edge), matching Pi orientation so the HAT's socket
  and overhang face off-board.
- Note: the Waveshare HAT (65 × 30.2 mm) is smaller than a full HAT; its own
  mounting holes don't follow the full-HAT spec. The two 58 mm standoff holes are
  for optional standoffs/enclosure bosses; the socket friction fit is the primary retention.

## Task 2: BOM sourcing & probe results

(to be filled)
