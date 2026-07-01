# ESPINK Carrier PCB Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task (inline — the EasyEDA browser session is shared state that cannot be handed between subagents). Steps use checkbox (`- [ ]`) syntax for tracking.
> **REQUIRED SUB-SKILL for all EasyEDA work:** `easyeda-pro` (already covers Run Script recipes, DRC habits, pour/outline gotchas). `Ctrl+S` after every step; SCH/PCB DRC between steps.

**Goal:** A hand-solderable 2-layer carrier PCB in EasyEDA Pro: ESP32-C6-MINI-1 + 1S2P dual-18650 pack + USB-C charging, hosting the Waveshare 7.5" e-Paper HAT on a Raspberry-Pi-pinout 40-pin header, with 3 remote capacitive-touch pads (TTP223).

**Architecture:** Spec at `docs/superpowers/specs/2026-07-02-espink-pcb-design.md`. Power: VBUS→TP4056→protected 1S2P→load-share→XC6220 3V3. Schematic captured by connect-by-name Run Scripts; PCB placed by function; outline/pours GUI-drawn; DRC 0 then 3D review.

**Tech Stack:** EasyEDA Pro V3.2.148 (pro.easyeda.com) via Claude-in-Chrome; JLCPCB 2-layer fab; parts loose-ordered from LCSC (hand assembly).

## Global Constraints

- Firmware pin map UNCHANGED: PWR=GPIO2, BUSY=3, DC=4, RST=5, CLK=6, MOSI=7, CS=10 (spec §4).
- Hand-solder only: passives ≥0603 (prefer 0805), ICs SOT-23/SOT-25/SOP-8, through-hole connectors, USB-C with through-hole shell tabs. No QFN/BGA (spec §7).
- Loose-LCSC sourcing rule: every part LCSC retail Stock > 0 AND first price tier ≤ 100, picked stock-sorted in the Device picker at selection time. No `C9900xxxxxx` codes.
- HAT is 3.3 V only — 40-pin header 5 V pins (2, 4) left NC (spec §5).
- Board ~90 × 50 mm, 2-layer, GND pour both sides; power nets ≥0.5 mm net class; JLCPCB standard process floors (5 mil track/space, 0.3 mm via).
- `Ctrl+S` after every editor step; DRC between steps; save before risky ops.

---

## Authoritative netlist (used by every capture task)

Nets: `VBUS, USB_DP, USB_DM, CC1, CC2, VBAT, BATN, VSYS, V3V3, GND, EN, BOOT, EPD_PWR, EPD_BUSY, EPD_DC, EPD_RST, EPD_CLK, EPD_MOSI, EPD_CS, VBAT_ADC, ADC_EN_G, TOUCH1..3, TPAD1..3, CHRG, STDBY, GATE1(=VBUS), DWOD, DWOC`

| Ref | Part (target) | Pin → Net |
|---|---|---|
| U1 | ESP32-C6-MINI-1-N8 | 3V3→V3V3, GND/EP→GND, EN→EN, IO0→ADC_EN_G, IO1→VBAT_ADC, IO2→EPD_PWR, IO3→EPD_BUSY, IO4→EPD_DC, IO5→EPD_RST, IO6→EPD_CLK, IO7→EPD_MOSI, IO9→BOOT, IO10→EPD_CS, IO12→USB_DM, IO13→USB_DP, IO14→TOUCH1, IO18→TOUCH2, IO19→TOUCH3 (confirm exposure in Task 1; all other IOs NC-flag) |
| J1 | USB-C 16P `C165948` | VBUS→VBUS, GND+EH→GND, CC1→CC1, CC2→CC2, DP1/DP2→USB_DP, DN1/DN2→USB_DM |
| R1,R2 | 5.1k 0805 | CC1→GND, CC2→GND |
| D1 | USBLC6-2SC6 | 1→USB_DM, 2/5→GND... (probe pinout; I/O1,I/O2 to J1 side, VBUS pin→VBUS) — route D+/D− THROUGH D1 between J1 and U1 |
| U2 | TP4056 ESOP-8 | VCC→VBUS, GND→GND, BAT→VBAT, PROG→R3→GND, CE→VBUS, TEMP→GND, CHRG→CHRG, STDBY→STDBY |
| R3 | 1.2k 0805 (PROG ≈1 A) | U2.PROG→GND |
| LED1+R4 (1k) | red | VBUS→R4→LED1→CHRG |
| LED2+R5 (1k) | green | VBUS→R5→LED2→STDBY |
| C1 | 10µF 0805 | VBUS→GND |
| C2 | 10µF 0805 | VBAT→GND |
| BT1,BT2 | 18650 holder THT | +→VBAT, −→BATN |
| U3 | DW01A SOT-23-6 | 1(OD)→DWOD, 2(CS)→R7(100Ω? no: CS sense→BATN via 1k... probe: 1=OD,2=CS,3=OC,4=TD/NC,5=VCC,6=GND-is-BATN wiring per datasheet) VCC→R6(100Ω)→VBAT + C3(100nF)→BATN, GND→BATN, OD/OC→U4 gates, CS→GND(pack−) |
| U4 | FS8205A dual NFET | per datasheet: FET1 S→BATN, FET1 G→DWOD, common D-D, FET2 G→DWOC, FET2 S→GND |
| Q1 | AO3401A `C15127` | G→VBUS, D→VBAT, S→VSYS |
| R8 | 100k 0805 | Q1.G→GND |
| D2 | B5819W Schottky `C8598` | A→VBUS, K→VSYS |
| U5 | XC6220B331MR SOT-25 | VIN→VSYS + C4(10µF), VOUT→V3V3 + C5(10µF), CE→VSYS, GND→GND |
| C6,C7 | 10µF + 100nF 0805 | V3V3→GND at U1 |
| R9 + C8 | 10k 0805 + 1µF | EN→R9→V3V3; EN→C8→GND |
| SW1 (RESET) | TS-1187A `C318884` | EN→GND |
| SW2 (BOOT) | TS-1187A | BOOT→GND (+optional C 100nF) |
| Q2 | AO3401A | S→VBAT, D→R10(470k)→VBAT_ADC node, G→R11(100k)→VBAT and G→Q3.D |
| R12 | 470k 0805 | VBAT_ADC→GND |
| C9 | 100nF 0805 | VBAT_ADC→GND |
| Q3 | AO3400A `C20917` | D→Q2.G, S→GND, G→ADC_EN_G + R13(100k)→GND |
| U6,U7,U8 | TTP223 SOT-23-6 | 1(Q)→TOUCH1/2/3, 2(VSS)→GND, 3(I)→R14/15/16(1k)→TPAD1/2/3, 4(AHLB)→GND, 5(VDD)→V3V3 + 100nF (C10–C12), 6(TOG)→GND |
| C13–C15 | 0603 DNP (Cs 0–50pF) | TPADx→GND |
| TP1–TP3 | solder pads (Pad tool / 1-pin THT pad) | TPAD1/2/3 |
| J2 | 2×20 male 2.54 header | pin1,17→V3V3; pins 6,9,14,20,25,30,34,39→GND; 19→EPD_MOSI; 23→EPD_CLK; 24→EPD_CS; 22→EPD_DC; 11→EPD_RST; 18→EPD_BUSY; 12→EPD_PWR; ALL others NC-flag |
| H1–H4 | M3 mounting pads | no net |

⚠️ Rows marked "probe" or "per datasheet": pin numbers MUST come from the Task-1/Task-2 probe (`getAllPinsByPrimitiveId` pinName dump), never from this table alone. The **net→function** mapping above is authoritative; pin numbers are not.

---

### Task 1: Verify external facts (no EasyEDA changes)

**Files:** Append findings to `docs/superpowers/plans/2026-07-02-espink-carrier-pcb-notes.md` (create).
**Produces:** confirmed HAT 40-pin map, confirmed U1 usable GPIOs, Pi-HAT header/hole geometry.

- [ ] **Step 1:** WebFetch `https://www.waveshare.com/wiki/7.5inch_e-Paper_HAT_(B)_Manual` (and the HAT schematic link on that page). Confirm RPi physical pins: DIN=19, CLK=23, CS=24, DC=22, RST=11, BUSY=18, PWR=12, VCC=3.3V. Record exactly which power pins the HAT consumes and whether PWR exists on this HAT revision. **If any pin differs from the netlist table, fix the J2 row before Task 3.**
- [ ] **Step 2:** WebFetch the ESP32-C6-MINI-1 datasheet (espressif.com) — confirm: IO14, IO18, IO19 are exposed module pins; IO12/13 are USB; strapping pins list; antenna keep-out drawing. If IO14/18/19 unavailable, substitute other free non-strapping exposed IOs and update the netlist table + spec.
- [ ] **Step 3:** WebFetch the Raspberry Pi HAT mechanical spec (or Pi 40-pin drawing): header position relative to the two adjacent mounting holes (3.5 mm from edges, 58 mm hole pitch along the header, 49 mm transverse). Record the header-centerline↔hole offsets so Task 6 can place J2 + 2 standoff holes to match.
- [ ] **Step 4:** Write all three results into the notes file; commit: `git add docs/superpowers/plans/ && git commit -m "docs: verified HAT/C6/Pi-HAT facts for carrier PCB"`.

### Task 2: EasyEDA project + full BOM sourcing & probe

**Consumes:** notes from Task 1. **Produces:** in the notes file, a `REF|LCSC|pinNumber:net,...` line per part — the exact input for Task 3–5 scripts.

- [ ] **Step 1:** `tabs_context_mcp`; use the user's open EasyEDA Pro tab. `File → New → Project`, name `ESPINK-carrier`. `Ctrl+S`.
- [ ] **Step 2:** For EVERY part in the netlist table, in the bottom Library picker: structured filter (Category + Package + value, lowercase `10uf`/`5.1k`), **Stock-sort**, check **LCSC Stock > 0 and first tier ≤ 100**, record the chosen code. Search MPNs for ICs (`TP4056`, `DW01A`, `FS8205A`, `XC6220B331`, `TTP223`, `USBLC6-2SC6`, `ESP32-C6-MINI-1`, `B5819W`, `AO3401A`, `AO3400A`), connectors (`18650 holder` THT, `2.54 2x20` male header — real catalogue part, not C9900), LEDs 0805, tactile `TS-1187A`.
- [ ] **Step 3:** Probe-resolve the whole BOM in ONE Run Script: for each code `eda.lib_Device.getByLcscIds(code)` → place a probe instance → `getAllPinsByPrimitiveId` → report `code: N pins, pinNumber=pinName list` via `sys_MessageBox`. Single-line script, no brackets in strings. Screenshot the output.
- [ ] **Step 4:** Any code resolving to 0 length or 0 pins: try an alternative LCSC entry of the same part; if none works, mark that part **GUI-place + hand-net-label** in the notes. Any pinout differing from the netlist table's assumption (D1, U2, U3, U4, U1 especially): write the corrected `pin:net` map in the notes now.
- [ ] **Step 5:** Delete all probe instances (script `sch_PrimitiveComponent.delete`), `Ctrl+S`. Commit the notes file: `git commit -m "docs: sourced+probed carrier PCB BOM"`.

### Task 3: Schematic — power section

**Consumes:** `REF|LCSC|pin:net` lines. **Produces:** power nets capturable by later tasks (VBUS, VBAT, BATN, VSYS, V3V3, GND all exist).

- [ ] **Step 1:** One Run Script: place J1, R1, R2, D1, U2, R3, LED1/R4, LED2/R5, C1, C2, BT1, BT2, U3+R6+C3, U4, Q1, R8, D2, U5, C4, C5 on a coarse grid (≥200-unit pitch, nothing overlapping), stub-wire every pin to its net (`sch_PrimitiveWire.create([px,py,px+5,py], net)`), add `createNetFlag('Ground','GND',…)` and power flags for VBUS/VBAT/VSYS/V3V3.
- [ ] **Step 2:** `Design → Check DRC`. Fix every "multiple net names" (= overlap short) by moving parts apart and re-stubbing. Expected: 0 errors; floating-pin warnings only for parts not yet placed.
- [ ] **Step 3:** `Ctrl+S`.

### Task 4: Schematic — MCU, buttons, battery sense

- [ ] **Step 1:** One Run Script: place U1 (if it probe-resolved; else GUI-place and hand-label per notes), C6, C7, R9, C8, SW1, SW2, Q2, Q3, R10–R13, C9 — stub-wire per netlist. U1 needs a large clear bounding box (~150-unit ring minimum).
- [ ] **Step 2:** SCH DRC → 0 errors (warnings = still-unplaced parts + intended NC). `Ctrl+S`.

### Task 5: Schematic — 40-pin header, touch, annotate, final DRC

- [ ] **Step 1:** Run Script: place J2 and stub-wire ONLY the 17 used pins per the (Task-1-verified) J2 row; place U6–U8, R14–R16, C10–C15, TP1–TP3 (TPx as 1-pin pad part or small THT pad footprint part), stub-wire per netlist.
- [ ] **Step 2:** `Design → Annotate Designator` → Confirm.
- [ ] **Step 3:** SCH DRC. Read the floating-pin list: **wire any real gap**, then NC-flag (`P` then `C`, zoomed, green-X snap) every intended NC: J2 unused pins (incl. 2 & 4), U1 spare IOs, D1 unused, U3/U4 NC pins. Expected end state: **0 errors, 0 warnings**.
- [ ] **Step 4:** `Ctrl+S`. Update the notes file with final designators; commit.

### Task 6: Convert to PCB + placement + outline + mounting holes

- [ ] **Step 1:** `Design → Update/Convert Schematic to PCB` (`Alt+I`) → Apply Changes. `Ctrl+S`.
- [ ] **Step 2:** Placement by FUNCTION via `pcb_PrimitiveComponent.modify` (read pads/nets first with `getAllPinsByPrimitiveId`):
  - J2 (40-pin) along the top long edge; the two Pi-HAT standoff holes at the Task-1 offsets from it.
  - BT1/BT2 side by side filling the board middle/bottom (they dominate the area).
  - J1 USB-C on the short edge nearest U2, **mouth off-board** (pad-bulk rule; verify head-on in 3D later); charger block (U2,R3–R5,LEDs,C1,C2,Q1,R8,D2) clustered at J1.
  - Protection block (U3,U4,R6,C3) at the BT1/BT2 terminals.
  - U5+C4+C5 between charger block and U1.
  - U1 at the board edge with the module antenna edge overhanging/flush, **no copper/parts in the antenna zone**; C6/C7 hard against its 3V3 pin; SW1/SW2 near U1 at reachable open space; Q2/Q3/dividers near U1 ADC side.
  - U6–U8 + TP1–TP3 along the edge nearest the enclosure buttons, electrode pads at the very edge, away from EPD SPI lines.
  - ≥250 mil part pitch; ~6 mm keep-clear halo is not needed (no QFN — the MINI-1 is a castellated module) but keep its pin rows clear.
- [ ] **Step 3:** PCB DRC now (placement check, pre-route). Fix overlaps.
- [ ] **Step 4:** Board outline: GUI `Place → Board Outline → Rectangle`, then numeric resize in the Property panel to ~3543 × 1969 mil (90 × 50 mm) enclosing the pad bbox + ≥150 mil margin; Corner Radius `2mm`. **Never script-edit the outline.**
- [ ] **Step 5:** Mounting holes: `Alt+P` pad, Diameter `6mm`, Hole `3.2mm`, no net, one per corner inset ~200 mil; plus the two Pi-standoff holes at the Task-1 geometry (these may be net-GND). Fix each pasted pad's Center Y + Net in its own Property panel.
- [ ] **Step 6:** DRC again → outline/pad clearances clean. `Ctrl+S`.

### Task 7: Rules, keep-out, routing, pours

- [ ] **Step 1:** `Design → Design Rule → Rule Management → Physics → Track`: add rule `POWER`, Stroke Width Default `0.5` mm. Net Rule tab: bind GND, VBUS, VBAT, BATN, VSYS, V3V3 to `POWER`. Leave VBAT_ADC/TPADx/signals at default.
- [ ] **Step 2:** Antenna keep-out: `Place → Prohibited Region → Rectangle` over the U1 antenna area extending to the board edge; Layer=Multi-Layer; prohibit **Copper + Fill Region + Plane Zone only**. One `browser_batch` for the popup fields.
- [ ] **Step 3:** PCB DRC (pre-route habit). Then `Route → Auto Routing` (45°, both layers, Existing trace → Remove) → Run.
- [ ] **Step 4:** Hand-route USB_DP/USB_DM as a short differential pair (`Alt+D`) from J1 through D1 to U1 if the autorouter's result is long or split; length-match within ~0.5 mm. (Full-speed USB-JTAG is forgiving, but keep the pair together over continuous GND.)
- [ ] **Step 5:** GND pours top + bottom: oversized rectangle, popup Net=GND + **Layer set explicitly per pour**, one `browser_batch`; Esc after Confirm; `Shift+B`.
- [ ] **Step 6:** Suture vias: Filter→Copper Region only→`Ctrl+A` (both pours)→Place/Remove Suture Vias, Add, 24/12 mil, 200 mil grid. If they collide with routed vias (Hole-to-Hole DRC), Remove them — routed GND vias suffice on this low-speed board.
- [ ] **Step 7:** DRC → fix every connection error (isolated GND pads → `Alt+V` via over clear GND copper on the other layer). Expected: **All (0)**. `Ctrl+S`.

### Task 8: Silkscreen, 3D review, final DRC

- [ ] **Step 1:** Silkscreen pass: every refdes inside the outline, not on pads/other silk, just above its part (click text → Center X/Y/Rotation). Add silk text: `18650 MATCHED PAIR ONLY`, `+`/`−` at each holder, `T1 T2 T3` at touch pads, `BOOT`/`RST` at buttons.
- [ ] **Step 2:** `View → 3D Preview` checklist: USB-C mouth flush/overhanging its edge (verify HEAD-ON + measure pad-bbox vs outline edge), J2 clear of tall parts (HAT stacks above — nothing >~8 mm under the HAT footprint area), buttons reachable, touch pads at edge, pours reach all edges both layers, mounting holes in copper-free rings.
- [ ] **Step 3:** Final PCB DRC **All (0)** + SCH DRC 0/0 + schematic-sync button reports match. `Ctrl+S`.
- [ ] **Step 4:** Update notes file with any deviations; commit: `git commit -m "docs: carrier PCB captured and DRC-clean"`.

### Task 9: Manufacturing exports + human handoff

- [ ] **Step 1:** `Export → PCB Fabrication File (Gerber)`; `Export → Bill of Materials (BOM)`; check the Device Standardization panel — every line LCSC Stock > 0, first tier ≤ 100 (left number of `LCSC Stock / SMT Stock`). Swap any failure via Device Manager (batch by value) + re-sync (`Alt+I`) + re-DRC.
- [ ] **Step 2:** Save exports to `~/GIT/ESPINK/hw/` (create), commit: `git commit -m "hw: carrier PCB gerber + BOM"`.
- [ ] **Step 3:** Report the division-of-labour handoff list to the user: final component nudging, pour-edge cosmetics, any remaining GND via fine placement, and ordering (user does JLCPCB/LCSC orders themselves).

## Self-review notes

- Spec coverage: §1 battery→T3+T6, §2 charging/USB→T3+T7.4, §3 LDO→T3, §4 MCU→T4, §5 header→T1+T5+T6, §6 touch→T5+T6, §7 board/assembly→T6+T2 sourcing rule, §8 bring-up is post-fab (user), firmware follow-ups out of scope.
- Pin numbers for multi-pin ICs intentionally defer to the Task-2 probe (skill rule: never trust remembered pinouts); the net→function map is the plan's contract.
