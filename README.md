# HB Dependency Doctor v1.0

**3DS Homebrew Health Analyser** — by Ikoko

A diagnostic tool for modded Nintendo 3DS consoles that scans your SD card and
CFW setup for common problems, misconfigurations, and leftover files — then
presents a colour-coded, scrollable, filterable report with one-button auto-fixes
where possible.

---

## Features

- **9 independent check modules** covering the full CFW ecosystem
- **Interactive hardware test** — test every button, D-pad, circle pad, and touch screen
- GPU-accelerated citro2d GUI (no console flickering)
- One-button auto-fix with confirmation for safe operations
- Severity-based filtering (ALL / WARN+ / ERR+ / FATAL)
- Full report export to SD card (`/HBDD_report.txt`)
- Touch screen buttons + D-pad navigation

---

## Check modules

| Module        | What is checked |
|---------------|-----------------|
| **LUMA**      | boot.firm size, config.ini, Luma version, plugin loader vs. plugin dir mismatch, game patching vs. LayeredFS, DSiWare autoboot crash, payloads/, backups/, seeddb.bin |
| **PLUGIN**    | 3GX files in /luma/plugins/: empty, corrupt, missing plugin.3gx, non-TitleID folders |
| **TICKET**    | Orphaned tickets (no matching installed title), zero-ID tickets via AM service |
| **CHEAT**     | /luma/cheats/ filenames, empty files, malformed [Section]/code-line format |
| **HOMEBREW**  | Deprecated apps (FreeShop, TWLoader...), essential tools (FBI, Checkpoint, GodMode9, Universal-Updater), leftover /cias/, empty /Themes/ |
| **SYSTEM**    | Firmware version, console model, A9LH legacy detection, SafeB9SInstaller leftovers, boot.3dsx, NAND backup, sensitive files at SD root (otp.bin, movable.sed...), SD free space, GodMode9 scripts |
| **LAYEREDFS** | /luma/titles/ — empty mod folders, missing romfs/ content, non-TitleID dirs |
| **MODULES**   | /luma/sysmodules/ — empty/corrupt .cxi files, wrong-extension files |
| **HARDWARE**  | Battery level/charging, circle pad drift, SD read speed, WiFi status, system clock, CTRNAND free space, touch screen ghost touches |

---

## Hardware Test

Press **L** from the report view to open the interactive hardware test. This screen lets you verify every input on your console:

- **Buttons**: A, B, X, Y, L, R, START, SELECT, D-Pad — all light up when pressed
- **ZL / ZR**: Shown only on New 3DS models (detected automatically)
- **Circle Pad**: Live position display with visualizer circle
- **Touch Screen**: Touch position shown on bottom screen with coordinates

To exit the hardware test, hold **L+R** simultaneously. START and B are not used as exit keys so they can be tested freely.

---

## Auto-fix capabilities

| Action          | Triggered by |
|-----------------|--------------|
| Create directory| Missing /luma/payloads/, /luma/backups/, /luma/plugins/, /3ds/ |
| Delete file     | SafeB9SInstaller.bin, empty .3gx, empty cheat file, corrupt module |
| Delete directory| Empty plugin TID folders, empty romfs/, FreeShop, /cias/, empty /Themes/ |
| Move file       | Sensitive files (otp.bin etc.) → /gm9/out/ |
| Edit config.ini | Plugin loader toggle, game patching toggle, disable broken autoboot |

All destructive operations show a **confirmation screen** (A = confirm, B = cancel).

---

## Controls

### Report view

| Button    | Action |
|-----------|--------|
| Up / Down | Navigate issue list |
| Left / Right | Previous / next page |
| **A**     | Apply auto-fix (with confirmation) |
| **B**     | Cancel / dismiss |
| **X**     | Cycle filter: ALL → WARN+ → ERR+ → FATAL |
| **Y**     | Export full report to `/HBDD_report.txt` |
| SELECT    | Toggle fix-hint display |
| **L**     | Open hardware test |
| START     | Exit |

Touch buttons are also available on the bottom screen.

### Hardware test view

| Button    | Action |
|-----------|--------|
| Any button| Test button (lights up when pressed) |
| Circle Pad| Live position + visualizer |
| Touch     | Touch coordinates on bottom screen |
| **L+R**   | Exit hardware test |

---

## Severity levels

| Label | Colour  | Meaning |
|-------|---------|---------|
| OK    | Green   | Everything is fine |
| INFO  | Cyan    | Informational note, no action required |
| WARN  | Yellow  | Potential problem, may affect functionality |
| ERROR | Red     | Confirmed problem affecting features |
| FATAL | Magenta | Critical — console may not boot/function correctly |

---

## Build requirements

- [devkitPro](https://devkitpro.org/) with the `3ds-dev` group installed
- `libctru` and `citro2d` (included with devkitPro 3ds-dev)
- `bannertool` and `makerom` (for CIA builds only)

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM

cd HBDependencyDoctor
make          # builds .3dsx + .smdh into out/
make cia      # also builds .cia into out/
```

---

## Installing on 3DS

**3DSX (Homebrew Launcher):**
```
SD:/3ds/HBDependencyDoctor.3dsx
```
Launch from the Homebrew Launcher.

**CIA (Home Menu icon):**
```bash
make cia
# then install out/HBDependencyDoctor.cia with FBI
```

---

## Report export

Press **Y** at any time to write `/HBDD_report.txt` to the SD card root.
The report groups all issues by severity and includes fix hints and auto-fix results.

---

## Known limitations

- Ticket analysis requires the AM service (always available with Luma3DS).
- Auto-fix cannot download files — use Universal-Updater for that.
- NAND-level corruption (e.g. broken ticket.db) requires GodMode9.
- The tool runs from ARM11 userland and cannot inspect NAND partitions directly.

---

## License

This project is licensed under the **GNU General Public License v3.0** — see the [LICENSE](LICENSE) file for details.
