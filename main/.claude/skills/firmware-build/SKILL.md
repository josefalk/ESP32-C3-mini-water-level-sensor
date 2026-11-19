---
name: firmware-build
description: Build, flash (USB or Wi-Fi OTA) and serial-monitor the ESP32-C3 water tank firmware with PlatformIO. Use when asked to compile, check that the code builds, upload/flash the board, do an OTA update, or read the serial log.
---

# Build and flash the water tank firmware

PlatformIO project; `platformio.ini` sets `src_dir = .`, so every `.cpp`/`.h` in the project root is compiled.

## The `pio` command

`pio` is **not on PATH**. Call it by full path:

- Bash: `~/.platformio/penv/Scripts/pio.exe`
- PowerShell: `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"`

Run every command from the project root (the folder containing `platformio.ini`).

## Environments

| Env | Use | Command |
|-----|-----|---------|
| `esp32-c3-devkitm-1` | Build, or upload over the USB cable (default env) | `pio run -e esp32-c3-devkitm-1` / `... -t upload` |
| `ota` | Upload over Wi-Fi to `watertank.local` | `pio run -e ota -t upload` |

## Steps

1. **Build first, always:** `pio run -e esp32-c3-devkitm-1`. Fix every error before trying to upload. Report the RAM/Flash usage lines from the output.
2. **Upload:** only when the user asked to flash. Ask which method if they didn't say:
   - USB: `pio run -e esp32-c3-devkitm-1 -t upload`. If the port isn't found, run `pio device list` and pass `--upload-port COMx`.
   - OTA: `pio run -e ota -t upload`. The board must be powered and on the same network. If it fails with "No response from device" or a timeout, the likely cause is Windows Firewall blocking inbound TCP 18180. The one-time admin fix is in the comments in `platformio.ini`; tell the user about it, don't run it yourself.
3. **Serial monitor:** `pio device monitor -b 115200` never exits on its own. Run it in the background or with a timeout, then read the output. Useful boot lines: `Tank: height=...`, `History: loaded N hourly points`, `Web page: http://watertank.local`, `HTTP server started`.

## Gotchas

- The OTA password comes from `[ota] password` in `secrets.ini` (git-ignored, loaded through `extra_configs`; `platformio.ini` only holds the public default `watertank-ota`). It's used both as a build flag (`OTA_PASSWORD`) and as an upload flag, so if it changes, the board needs one USB flash before OTA works with the new password. Never print it or commit `secrets.ini`.
- `ARDUINO_USB_CDC_ON_BOOT=1` is required, or the serial output won't appear over the C3's native USB.
- GPIO18/19 are the USB D-/D+ pins. Code that uses them as GPIO breaks USB enumeration and makes USB upload impossible (recover by holding BOOT while plugging in).
- `.pio/` is build output and is git-ignored. For a clean build use `pio run -t clean`.
