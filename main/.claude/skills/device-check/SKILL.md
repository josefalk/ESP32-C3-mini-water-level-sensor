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
| `/api/level` | **Public API for other devices, keep field names stable.** `{"valid","percent","litres","capacity","depth","distance","age" s,"time" unix}`. `-1` = not available. Has a CORS header. |
| `/api/level.txt` | Plain text `percent;litres`, e.g. `45.2;720`. |
| `/data` | `{"valid","distance" cm,"depth" cm,"percent","litres","capacity","tanks"}`. `litres`/`capacity` are `-1` when no tank sizes are set. `valid:false` means the sensor stopped answering. |
| `/status` | `{"led","ledMode","screen","mode":"AP"/"STA"/"CONNECTING","ip","ssid","rssi","uptime" s, "heapFree","heapMinFree","heapMaxBlock","heapTotal" bytes, "appSize","appSpace" bytes, "nvsUsed","nvsTotal" entries, "chipTemp" °C, "resetReason"}`. Baseline (Nov 2026): heapFree ~195 KB, heapMinFree ~160 KB after page loads, NVS 187/630. resetReason 4 = crash, 6/7 = watchdog, 9 = brownout (power supply). |
| `/tank` (GET) | `{"height","gap","capacity","sensor" (tank index),"tanks":[{"shape":1-3,"length","width","diameter","open","measured","held" L}]}`. Shapes: 1 = box, 2 = upright cylinder, 3 = horizontal cylinder. `open` = valve open; `measured` = counted in litres now; `held` = litres a closed tank held when closed (-1 unknown). |
| `/history` | `{"now","day":[[unix,pct],...],"week":[...]}`. Empty until NTP has synced (needs internet). |
| `/scan` | Nearby Wi-Fi networks. Slow (a few seconds). Returns `"busy":true` while the device is itself scanning or connecting. |
| `/time` (GET) | `{"now" unix UTC,"valid","offset" min,"summer","tz" total min,"local":"Wed 18 Nov 23:11"}` |
| `/lock` (GET) | `{"pin":true/false}`: whether the settings are PIN protected. |
| `/wifi` (GET) | `{"mode":"STA"/"AP"/"CONNECTING","current","checkMins","nets":[ssid,...]}`. Saved networks in priority order (#1 = main). Passwords are never returned. |

## Endpoints that change the device: ask first

**Settings PIN:** when `/lock` says `"pin":true`, every POST below needs a `pin` field (ask the user for it; never guess). Without it the reply is `403 Error: Wrong PIN`. After 5 wrong tries, all tries are refused for 60 s, which locks the user out too, so don't retry wrong PINs.
- `POST /lock/check` (`pin`) answers `OK` if the PIN is right.
- `POST /lock/set` (`pin` = the current PIN, `new` = 4-8 digits, empty = remove) sets or removes it.

- `POST /tank/valve` (`i`, `open` 1/0) opens/closes a tank's valve. This changes litres, capacity and percent (and the history).
- `POST /tank` (`height`, `gap`, `count`, `sensor`, `shape0..`, `length0..`, `width0..`, `diameter0..`) **overwrites the saved tank setup in flash.**
- `POST /time` (`offset` minutes from GMT, `summer` 1/0) saves the time zone; `POST /time/set` (`now` unix) sets the clock by hand.
- `POST /wifi/save` (`ssid`, `password`) adds a network to the end of the list, or changes its password.
- `POST /wifi/delete` (`i`) and `POST /wifi/move` (`i`, `dir` -1/1) edit the saved list.
- `POST /wifi/use` (`i`) **switches networks now, which drops the current connection.** Afterwards the board may be reachable only from the other network.
- `/led?state=on|off` (switches the LED to manual), `/screen?state=on|off` are harmless but visible to the user.

Before any write, read the current value (for example `GET /tank`) so it can be restored.

## Interpreting results

- `percent` is by volume when tanks are configured, otherwise by height.
- `distance` is the median of the last 7 readings, so a change in water level takes a few seconds to show.
- `rssi` below about -80 explains dropouts. The TX power is deliberately reduced (C3 Super Mini antenna issue), so range is short.
- `uptime` that resets unexpectedly points to a crash/reboot loop; check the serial log.
