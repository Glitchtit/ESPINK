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

## Task 2: BOM sourcing & probe results (2026-07-02)

Project: existing empty **ESPINK** project (id 07e97782f97b43108c109eb615cfe4f2),
Board1/Schematic1/PCB1 — used instead of creating "ESPINK-carrier".

All parts picked stock-sorted in the Shift+F Device picker (LCSC retail stock > 0,
first tier ≤ 100). **All codes probe-resolve via `getByLcscIds` and expose pins**
(the first-choice FS8205A `C908285` did NOT resolve → swapped to `C2830320`).

| Ref | Part | LCSC | Stock @pick | MOQ |
|---|---|---|---|---|
| U1 | ESP32-C6-MINI-1-**N4** (4 MB; N8/H8 had 0 stock — firmware targets 2 MB so 4 MB is plenty; **spec deviation N8→N4**) | C5736265 | 2,309 | 1 |
| U2 | TP4056-42-ESOP8 (TopPower orig.) | C16581 | 66,595 | 5 |
| U3 | DW01A (YONGYUTAI) | C2927799 | 49,740 | 20 |
| U4 | FS8205A (TECH PUBLIC, SOT-23-6) | C2830320 | 12,240 | 10 |
| U5 | XC6220B331MR-G (Torex, 1 A) | C86534 | 5,586 | 1 |
| U6–U8 | TTP223-BA6-TD (TDSEMIC) | C42422127 | 47,920 | 5 |
| D1 | USBLC6-2SC6 (UMW) | C2687116 | 118,780 | 10 |
| D2 | B5819W (GOODWORK, SOD-123) | C2943878 | 1.88M | 50 |
| Q1,Q2 | AO3401A (UMW) | C347476 | 713k | 20 |
| Q3 | AO3400A (UMW) | C347475 | 985k | 20 |
| J1 | USB-C TYPE-C-31-M-12 | C165948 | 153k | 5 |
| J2 | 2×20 male 2.54 PZ254V-12-40P (XFCON) | C22465944 | 1,520 | 5 |
| J3 | 1×3 header PZ254V-11-03P (touch-wire pads) | C2937625 | 214k | 50 |
| BT1,BT2 | BH-18650-A1AJ006 (MYOUNG box holder) | C5290175 | 2,866 | 1 |
| SW1,SW2 | TS-1187A-B-A-B (XKB) | C318884 | 1.53M | 20 |
| LED1 | red 0603 XL-1608SURC-05 | C965799 | 6.8M | 100 |
| LED2 | green 0603 XL-1608UGC-04 | C965804 | 6.4M | 100 |
| R 5.1k | FRC0805J512TS | C2930296 | 1.36M | 100 |
| R 1.2k | FRC0805J122TS | C2933520 | 164k | 100 |
| R 1k | FRC0805F1001TS | C2907232 | 4.1M | 100 |
| R 10k | FRC0805J103TS | C2930231 | 2.1M | 100 |
| R 100k | FRC0805F1003TS | C2907217 | 944k | 100 |
| R 470k | FRC0805J474TS | C2930293 | 216k | 100 |
| R 100Ω | FRC0805F1000TS | C2907218 | 3.16M | 100 |
| C 10µF | FCC0805X106K100FT (FOJAN 0805) | C5137471 | 1.57M | 5 |
| C 100nF | CL10B104KB8NNNC (Samsung 0603) | C1591 | 914k | 100 |
| C 1µF | CL10A105KO8NNNC (Samsung 0603) | C1592 | 90k @2026-06-27 | 50 |
| Cs DNP | reuse C1591 footprint, DNP (fit 10–47 pF to desensitize) | — | — | — |

### Probed pin maps (authoritative for capture scripts)

- **U2 TP4056 C16581**: 1:TEMP 2:PROG 3:GND 4:VCC 5:BAT 6:STDBY# 7:CHRG# 8:CE 9:EP
- **U3 DW01A C2927799**: 1:OD 2:CSI 3:OC 4:NC 5:VDD 6:VSS
- **U4 FS8205A C2830320**: 1:S1 2:D1/D2 3:S2 4:G2 5:D1/D2 6:G1
- **U5 XC6220 C86534**: 1:VIN 2:VSS 3:CE 4:NC 5:VOUT
- **U6 TTP223 C42422127**: 1:OUT 2:GND 3:SO(input) 4:SLH(AHLB) 5:VDD 6:STG(TOG)
- **D1 USBLC6 C2687116**: 1:I/O1 2:GND 3:I/O2 4:I/O2 5:VBUS 6:I/O1 (line1 1↔6, line2 3↔4)
- **D2 B5819W**: 1:K 2:A · **LEDs**: 1:K 2:A · **Q1/Q2 AO3401A**: 1:G 2:S 3:D · **Q3 AO3400A**: 1:G 2:S 3:D
- **J1 USB-C C165948** (16): A1B12:GND B1A12:GND A4B9:VBUS B4A9:VBUS A5:CC1 B5:CC2 A6:DP1 B6:DP2 A7:DN1 B7:DN2 A8:SBU1 B8:SBU2 1–4:EH(shell)
- **J2 2×20**: pins 1–40 (zigzag = RPi physical numbering; verify pad 1/2 orientation at layout)
- **J3 1×3**: 1,2,3 · **BT holder C5290175**: pins 1,2 — **polarity NOT in symbol; verify +/− on footprint silk at layout before wiring convention is final** (assume 1=+, confirm in T6)
- **SW TS-1187A C318884**: pins A/B/C/D; symbol draws the switch contact between **3(C) and 4(D)** → wire signal→3, GND→4; **NC-flag 1(A), 2(B)** (works under any internal pole pairing)

### DW01A/FS8205A wiring (standard app circuit, from probed pins)

- Cell− = net **BATN**; system GND = pack−.
- DW01A: VDD←R(100Ω C2907218)←VBAT, C3 100nF VDD→BATN, VSS→BATN, CSI→R(1k)→GND, OD→U4.G1, OC→U4.G2
- FS8205A: S1→BATN, G1→DWOD, D1/D2 pins 2+5 tied (net DMID), S2→GND, G2→DWOC
- Extra parts added vs plan netlist: R 100Ω (DW01 VDD filter), R 1k CS (uses sourced 1k)
