# CLAUDE.md

## Project Overview

Monolith v2 is a DIY data logging platform for Student Formula / Baja racing: a single ESP32-S3 firmware system (C) that acquires sensor data and writes it straight to an SD card as a `.log` file. No companion web app, server, WiFi, or MQTT — recorded logs are analyzed offline on the external upstream site (https://v2.monolith.luftaquila.io/).

## Build & Development Commands

### Firmware (`device/firmware/`)
Requires **ESP-IDF v6.0.1** (`idf.py --version` to verify — mismatched versions will break the build).
- Linux/macOS: install at `~/esp/esp-idf`
- Windows: install at `C:\esp\v6.0.1\esp-idf`, build via PowerShell (`export.ps1` + `idf.py`) — `make` and Git Bash are not supported on Windows

```bash
make build           # compile firmware
make run             # build, flash (921600 baud), and monitor
make config          # open ESP-IDF menuconfig
make clean           # remove build directory
```

## Key Patterns

- **SD log compatibility is a hard requirement.** SD `.log` files must remain loadable in the external upstream site (https://v2.monolith.luftaquila.io/) — the only way recorded data gets viewed/analyzed. The binary wire/storage format must NOT change structurally: keep the 24-byte `log_t` layout/field offsets, `LOG_MAGIC` (0xAE), `PROTOCOL_VERSION` (1), the checksum algorithm (`log_prepare` in `main.h`), and BOOT-record-first recording order. Field *renames* are fine (bytes unchanged, e.g. `voltage`/`temperature` → `ain7`/`ain8`); layout/version/magic/checksum changes are not — confirm before making any.
- Wall-clock time source: GPS (`$GNRMC`/`$GPRMC`). The RTC has no battery backup, so the firmware sets the system clock once from the first valid GPS fix (`task_gps` in `gps.c`) and retroactively corrects the SD log's BOOT record `boot_time` in place.

## Hardware Notes

- **Board/module:** ESP32-S3-DevKitC-1 **v1.1** + ESP32-S3-WROOM-1 **N16R8** (16MB flash + 8MB octal PSRAM).
- **Board revision matters — always verify pin info against v1.1; never cite v1.0 pin data.** The onboard addressable RGB LED is on **GPIO38** on v1.1 (GPIO48 on v1.0); citing v1.0 data once put SD MISO on GPIO38 and it collided with the onboard LED. SD MISO is now **GPIO48** (free on v1.1) and must not be moved back to GPIO38.
- **Pins not usable as external I/O:** internal flash **GPIO26–32**, octal PSRAM **GPIO33–37** (reserved by the N16R8 module even when `CONFIG_SPIRAM` is disabled), USB-JTAG **GPIO19/20**, strapping **GPIO0/3/45/46**. GPIO22–25 do not exist on the chip.
- **SD breakout module power:** VCC = **5V** (onboard AMS1117 + level shifter); ESP32-S3 signal lines stay 3.3V — never drive a GPIO with 5V.

## CI/CD

- **firmware.yml** — Builds firmware on pushes to `device/firmware/**`
- **release.yml** — Triggered on `v*` tags; packages firmware binaries into GitHub releases

## Commit Messages

Use lowercase prefixed format: `fix:`, `add:`, `update:`, `bump:`, `impl:` (e.g., `fix: CAN decoder idx mismatch with chart series order`).

---

## Line endings

This repo has a `.gitattributes`: the repository stores LF, Windows checkouts
get CRLF. `*.sh` and `gradlew` are forced to LF (CRLF breaks execution on
Linux); `*.bat`, `*.cmd`, `*.ps1` are forced to CRLF. Binaries are excluded
from conversion.

Do not disable this, and do not commit line-ending-only changes. If a diff
shows every line of a file as changed, that is an EOL issue — check
`.gitattributes` and `core.autocrlf` instead of committing it.

## Two-machine sync (desktop PC / laptop)

Sync is via GitHub `origin` (this repo is a fork; `upstream` points at
luftaquila/monolith). Pull before starting work, push when finishing.
