---
name: device-check
description: Query the running water tank board over HTTP (watertank.local) to read the live level, Wi-Fi status, tank setup and history, or to confirm a freshly flashed change works on real hardware. Use when asked "is it working", "what's the level", or to verify on the device.
---

# Check the live device

After joining Wi-Fi, the board serves HTTP at `http://watertank.local` (mDNS hostname `watertank`, set in `user-wifi.cpp`). In AP/setup mode it's at `http://192.168.4.1` on the `ESP32-Config` network instead.

If `watertank.local` doesn't resolve on Windows, get the IP from the serial log (see the `firmware-build` skill) or the OLED's top row, and use that.

Use `curl -s --max-time 5`. The web server is single-threaded, so send requests one after another, not in parallel.

## Read-only endpoints (safe to call any time)

| Path | Returns |
|------|---------|
| `/data` | `{"valid","distance" cm,"depth" cm,"percent","litres","capacity","tanks"}`. `litres`/`capacity` are `-1` when no tank sizes are set. `valid:false` means the sensor stopped answering. |
| `/status` | `{"led","ledMode","screen","mode":"AP"/"STA","ip","ssid","rssi","uptime" s}` |
| `/tank` (GET) | `{"height","gap","capacity","tanks":[{"shape":1-3,"length","width","diameter"}]}`. Shapes: 1 = box, 2 = upright cylinder, 3 = horizontal cylinder. |
| `/history` | `{"now","day":[[unix,pct],...],"week":[...]}`. Empty until NTP has synced (needs internet). |
| `/scan` | Nearby Wi-Fi networks. Slow (a few seconds). |

## Endpoints that change the device: ask first

- `POST /tank` (`height`, `gap`, `count`, `shape0..`, `length0..`, `width0..`, `diameter0..`) **overwrites the saved tank setup in flash.**
- `POST /connect` (`ssid`, `password`) **saves new Wi-Fi credentials and reboots.** A wrong value drops the board into AP mode.
- `/led?state=on|off|auto`, `/screen?state=on|off` are harmless but visible to the user.

Before any write, read the current value (for example `GET /tank`) so it can be restored.

## Interpreting results

- `percent` is by volume when tanks are configured, otherwise by height.
- `distance` is the median of the last 7 readings, so a change in water level takes a few seconds to show.
- `rssi` below about -80 explains dropouts. The TX power is deliberately reduced (C3 Super Mini antenna issue), so range is short.
- `uptime` that resets unexpectedly points to a crash/reboot loop; check the serial log.
