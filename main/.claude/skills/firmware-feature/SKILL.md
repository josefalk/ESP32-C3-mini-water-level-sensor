---
name: firmware-feature
description: Conventions for changing or adding features to the ESP32-C3 water tank firmware (modules, pins, flash storage, web page and endpoints, OLED). Use before editing any .cpp/.h in this project.
---

# Working on the water tank firmware

Arduino framework on an ESP32-C3 (about 400 KB RAM, no PSRAM), built with PlatformIO. The user reads this code themselves, so keep it simple and commented in plain language like the existing files.

## Module layout

Each feature is a `user-<name>.h/.cpp` pair with an `init<Name>()` called from `setup()` in `main.cpp`, plus a `handle...()` or update function called from `loop()` if needed.

| File | Owns |
|------|------|
| `main.cpp` | Sensor read → 7-sample median smoothing → fan-out to screen/LED/web/history. `loop()` runs every ~300 ms. |
| `A02YYUW.*` | UART ultrasonic driver (frames `FF H L SUM`, auto-falls back to 0x55 trigger). Returns cm or -1. |
| `user-tank.*` | Tank geometry → depth, litres, percent. Up to `MAX_TANKS` parallel tanks. |
| `user-history.*` | 24 h (5 min, RAM) and 7 day (1 h, flash) ring buffers, NTP timestamps. |
| `user-wifi.*` | STA/AP fallback, reset button, `WebServer` routes, the whole HTML page, mDNS, OTA, NTP. |
| `user-screen.*` | U8g2 72x40 OLED, 3 rows (IP / reading / bar), screen toggle button. |
| `user-led.*` | Onboard LED, manual/auto modes. |

Keep module internals in an anonymous `namespace {}` (as in `user-tank.cpp` and `user-history.cpp`), and expose only what the header declares.

## Pins already in use (don't reuse)

GPIO 0 Wi-Fi reset button (hold 3 s) · 3 screen button · 4 sensor RX · 5 OLED SDA · 6 OLED SCL · 7 sensor TX · 8 LED · **18/19 = native USB, never use.**

## Flash storage (`Preferences`)

Each module has its own namespace: `"wifi"` (credentials; cleared by the reset button), `"tank"`, `"hist"`. Put new settings in their own namespace so a Wi-Fi reset doesn't wipe them.

- Write rarely. Flash wears out, which is why history saves only once per hour. Never write from `loop()` on every reading.
- When changing what is stored, migrate old data on first boot the way `initTank()` migrates the old `"wifi"` calibration keys.
- Structs stored with `putBytes` must keep their layout. Check `len % sizeof(T)` when loading.

## Web page and endpoints (`user-wifi.cpp`)

- The page is built in `handleRoot()` as `html += "..."` string lines (inline CSS + JS, no external files or CDNs, because the device may have no internet in AP mode).
- Register routes in `initWiFi()` next to the others. JSON is built by hand with `String`; keep keys short and use `-1` for "not available", as `/data` does.
- Validate POST input in the owning module (like `setTankSetup()` returns an error string) and reply `400` with `"Error: ..."` on failure.
- Watch `String` growth in big responses: call `reserve()` (see `getHistoryJson()`).

## OLED

72x40 px. Row 1 uses font `u8g2_font_4x6_tr` at y=6, and the bar is at y=20, h=10. Every screen state draws the IP row first. Check that text fits in 72 px.

## Done checklist

1. It builds: use the `firmware-build` skill and report Flash/RAM usage.
2. Nothing blocks `loop()` for long. `delay()` stalls the web server and OTA, so use `millis()` timers instead.
3. If the board is available and the user wants it flashed, verify with the `device-check` skill.
4. Update `../readme.md` if wiring, features or setup steps changed.
