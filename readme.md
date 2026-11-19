# ESP32-C3 Water Tank Level Monitor

A Wi-Fi water tank monitor built on an **ESP32-C3** with an **A02YYUW ultrasonic sensor** and a tiny **0.42" OLED**.
It measures the distance to the water and turns it into **percent and litres**. The level is shown on the OLED, on the onboard LED and on a web page with a history graph.
Other devices can read it through a simple **API**.

## Contents

- [Features](#features)
- [Hardware](#hardware)
- [Wiring](#wiring)
- [Getting started](#getting-started)
- [Using it](#using-it)
- [API for other devices](#api-for-other-devices)
- [Updating the firmware](#updating-the-firmware)
- [Project structure](#project-structure)
- [Troubleshooting](#troubleshooting)
- [Other details](#other-details)

## Features

- **Level in percent and litres.** Supports box-shaped, upright round and lying round tanks, and up to 4 tanks connected in parallel.
- **Steady readings.** The sensor value is a median of the last 7 readings, so ripples and stray echoes are ignored.
- **OLED display:** IP address, current reading and a level bar. A button turns the screen on or off.
- **Status LED:** red / yellow / green by level, or switched on and off by hand from the web page.
- **Web page** at `http://watertank.local`: live level, 24-hour and 7-day history graph, and a **Settings** section (tank setup, time zone, Wi-Fi) that can be protected with a PIN. Light and dark mode: follows the phone/PC by default, or press the ◐ button at the top to choose.
- **Up to 5 Wi-Fi networks** in priority order. If one fails, the board moves to the next one, and it switches back to the main network when that returns.
- **Setup mode:** if no network works, the board opens its own `ESP32-Config` Wi-Fi network for setup.
- **Time zone** setting (GMT offset + summer time). The clock is set from the internet, or from your phone when there's no internet.
- **API:** `/api/level` (JSON) and `/api/level.txt` (plain text) for other devices, such as a second ESP driving a big display.
- **Wireless firmware updates (OTA):** after the first USB upload, you can update the board over Wi-Fi.
- **Settings survive restarts and updates:** Wi-Fi networks, tank setup, time zone, settings PIN and 7-day history are kept in flash.

## Hardware

| Part | Notes |
|------|-------|
| ESP32-C3 board (Super Mini / DevKitM-1) | Onboard LED on GPIO 8, BOOT button on GPIO 9 |
| A02YYUW ultrasonic sensor (UART) | Waterproof, 5 V. Mount it at the top of the tank, pointing straight down |
| 0.42" OLED, 72×40, SSD1306, I²C | |
| Push button | For turning the screen on/off |
| 5 V power supply | Powers the board and the sensor |
| Cable | This build uses a 2-pair, 30 m cable to the sensor |

## Wiring

### A02YYUW sensor → ESP32-C3

| Sensor wire | 2-pair cable | DC barrel jack | ESP32-C3 pin |
|-------------|--------------|----------------|--------------|
| Red (5V)    | orange       | Red            | 5V           |
| Black (GND) | brown        | Black          | GND          |
| White (TX)  | blue         | Black          | GPIO 4 (RX)  |
| Yellow (RX) | green        | Red            | GPIO 7 (TX)  |

### OLED (SSD1306, 0.42" 72×40) → ESP32-C3

| OLED pin | ESP32-C3 pin |
|----------|--------------|
| VCC      | 3.3V         |
| GND      | GND          |
| SDA      | GPIO 5       |
| SCL      | GPIO 6       |

### Buttons and LED

| What | ESP32-C3 pin | Wiring | Function |
|------|--------------|--------|----------|
| Screen button | GPIO 3 | Button to GND | Short press: OLED on/off |
| **BOOT** button (on the board) | GPIO 9 | Built-in | Hold 3 s while running: forget all saved Wi-Fi networks and the settings PIN |
| Status LED (on the board) | GPIO 8 | Built-in | Level colour, see [LED](#oled-led-and-buttons) |
| Power switch | — | Inline in the 5 V supply | On/off |

### All connections

| ESP32-C3 | Goes to |
|----------|---------|
| 5V       | A02YYUW red |
| 3.3V     | OLED VCC |
| GND      | A02YYUW black, OLED GND, screen button |
| GPIO 3   | Screen button |
| GPIO 4   | A02YYUW white (sensor TX) |
| GPIO 5   | OLED SDA |
| GPIO 6   | OLED SCL |
| GPIO 7   | A02YYUW yellow (sensor RX) |
| GPIO 8   | Onboard LED (built-in) |
| GPIO 9   | BOOT button (built-in) |

> Don't use **GPIO 18/19**: they are the USB data pins. Using them stops USB uploads from working.
> **GPIO 0** is free.

## Getting started

### 1. Install the tools

1. Install [VS Code](https://code.visualstudio.com/).
2. Install the **PlatformIO IDE** extension.
3. Open the `main/` folder in VS Code (it contains `platformio.ini`).

PlatformIO downloads the ESP32 platform and the U8g2 display library by itself.
`platformio.ini` also turns on **USB CDC On Boot**, which you had to switch on by hand in the Arduino IDE.

### 2. Set your Wi-Fi and OTA password

Copy `main/secrets.example.h` to `main/secrets.h` and enter your home network:

```cpp
#define DEFAULT_WIFI_SSID     "your-network-name"
#define DEFAULT_WIFI_PASSWORD "your-password"
```

`secrets.h` is in `.gitignore`, so your password never goes to GitHub.
Without this file the board starts in setup mode, and you add the network from your phone (see below).

Also copy `main/secrets.example.ini` to `main/secrets.ini` and set your own **OTA password** (used for updates over Wi-Fi):

```ini
[ota]
password = something-only-you-know
```

`secrets.ini` is in `.gitignore` too. Without it, the public default password `watertank-ota` is used, and anyone on your Wi-Fi who reads this readme could upload firmware to your board.

### 3. Upload over USB

Connect the board with a USB cable. Then in the PlatformIO sidebar choose **esp32-c3-devkitm-1 → Upload**, or run:

```
pio run -e esp32-c3-devkitm-1 -t upload
pio device monitor -b 115200
```

The serial monitor shows which networks the board finds, its IP address and the tank settings.

### 4. Connect to Wi-Fi

- If the board joins your network, the OLED shows its IP address. Open `http://watertank.local` or `http://<IP>`.
- If it can't join any network, the OLED shows `AP:192.168.4.1`. Then:
  1. Connect your phone to the Wi-Fi network **`ESP32-Config`** (no password).
  2. Open `http://192.168.4.1`.
  3. Under **Settings → Wi-Fi Networks**, press **Scan Networks**, pick your network, enter the password and press **Save network**.
  4. The board connects, and the setup network disappears. Reconnect your phone to your home Wi-Fi and open `http://watertank.local`.

### 5. Set up the tank

On the web page, open **⚙ Settings** and go to **Tank Setup** (all sizes are *inside* measurements in cm):

```
  sensor  ─┬─                    Sensor gap  = sensor face → water line when FULL
           │ gap
  full ────┼──────────           Tank height = water line when full → tank bottom
           │ height
  bottom ──┴──────────
```

- Add a tank and choose its shape:
  - **Box:** length × width.
  - **Round, standing upright:** diameter.
  - **Round, lying on its side:** length. Its diameter is the tank height.
- For tanks connected at the bottom, press **+ Add parallel tank** (up to 4). They fill to the same level, so they must all be the same height. Their shapes and sizes may differ.
- With 2 or more tanks, choose **The sensor is in: Tank N**. This matters when a valve is closed (see *Valves* below).
- Press **Save Tank Setup**. The line below the button shows how the current reading works out (depth and litres), so you can check the numbers.

Without tank sizes, the level is shown in percent of the height only.

#### Valves (2 or more tanks)

If each tank has a valve, tell the board when you close one. Under the level on the main page, each tank has a **Close valve / Open valve** button. It asks for the settings PIN if Settings is locked.

- **Another tank's valve closed:** that tank is left out of the litres, capacity and percent. The sensor can't see its water any more.
- **The sensor tank's valve closed:** only the sensor tank is measured. The other tanks are left out.
- A closed tank shows roughly how much water it held when you closed it.
- **Open valve:** the tank is counted again, using the parallel connection and its size.
- The valve states are saved in flash. If you change the number of tanks in Tank Setup, all valves are set back to open.

### 6. Set the time zone

Under **Settings → Time**, choose your GMT offset (or press **Use this phone's time zone**). Tick **Summer time** if it applies, then press **Save time zone**.
The history graph and the device time use this zone.

### 7. Protect the settings with a PIN (optional)

At the bottom of **Settings**, enter a PIN of 4 to 8 digits under **Settings PIN** and press **Save PIN**.
From then on, the Settings section asks for the PIN before it opens, and the board refuses any setting change without it.
The live level, history and LED/screen buttons stay open for everyone.

- The page asks for the PIN again after a refresh, after saving a new PIN, and when you fold Settings closed. **Lock settings now** also locks them right away.
- After 5 wrong PINs, the board refuses all tries for 60 seconds.
- **Forgot the PIN?** Hold the **BOOT** button for 3 seconds. This removes the PIN and also forgets all saved Wi-Fi networks.
- The page uses plain HTTP, so the PIN keeps out people on your Wi-Fi who don't know it. Someone recording the network traffic could still read it.

## Using it

### Web page

| Section | What it does |
|---------|--------------|
| Water Tank Level | Live level bar, distance, percent and litres (updates every second); valve buttons when there are 2+ tanks |
| History | Graph of the last 24 hours (one point per 5 min) or 7 days (one point per hour) |
| LED / Screen Control | Turn the LED and OLED on or off |
| **⚙ Settings** (fold-out, PIN if set) | |
| → Tank Setup | Tank height, sensor gap and tank shapes/sizes |
| → Time | Device time, time zone, summer time |
| → Wi-Fi Networks | Saved networks: reorder, connect, delete, add |
| → Settings PIN | Set, change or remove the PIN |

The history starts once the clock is set. The 7-day history is kept in flash and survives restarts; the 24-hour history is lost on restart.

### Wi-Fi networks

- Up to 5 networks can be saved. **The order is the priority:** #1 is the main network.
- At start-up the board joins the highest network that is in range and accepts the password.
- If the connection drops for more than 20 seconds, it tries the other networks in order.
- When it is on a lower network, it checks every **10 minutes** whether a higher one is back in range, and switches to it.
- If nothing works, it opens the `ESP32-Config` setup network and retries every minute.
- On the page:
  - **▲ / ▼** change the order.
  - **Connect** switches to a network now.
  - **✕** deletes it.
  - Saving a name that is already in the list changes its password.
- Holding the **BOOT** button for 3 seconds forgets all networks (and the settings PIN). The board then restarts with the default from `secrets.h`.

The timings are constants near the top of `main/user-wifi.cpp` (`PRIORITY_CHECK_INTERVAL` and the others).

### OLED, LED and buttons

**OLED (3 rows):**
1. IP address. Shows `AP:192.168.4.1` in setup mode, or `WiFi connecting` while it's switching networks.
2. The current reading: litres when tank sizes are set, otherwise distance. Shows `No reading` if the sensor stops answering.
3. Level bar.

**LED (auto mode):**
- Red: below 20 %, or no reading.
- Yellow: 20–70 %.
- Green: above 70 %.

The LED buttons on the web page switch the LED to manual on/off.

**Buttons:**
- **Screen button (GPIO 3):** OLED on/off.
- **BOOT:** hold 3 s to forget all Wi-Fi networks and the settings PIN.

## API for other devices

Other devices on the same network can read the level, for example a second ESP with a big screen.
Use the board's IP address. It's faster and more reliable than `watertank.local` from another ESP.
Give the board a fixed IP (a DHCP reservation in your router) so the address never changes.

**`GET /api/level`** returns JSON:

```json
{"valid":true,"percent":45.2,"litres":720,"capacity":1592,"depth":77.1,"distance":122.9,"age":1,"time":1795033980}
```

| Field | Meaning |
|-------|---------|
| `valid` | `false` when the sensor isn't answering; the numbers are then `-1` |
| `percent` | Water level 0–100 % (by volume when tank sizes are set) |
| `litres`, `capacity` | Water now / when full, in litres. `-1` if no tank sizes are set |
| `depth`, `distance` | Water depth and sensor distance, in cm |
| `age` | Seconds since the last good sensor reading |
| `time` | Board's clock (unix seconds, UTC), `0` if not set |
| `tanks`, `measured` | Tanks set up / tanks counted now. A closed valve leaves a tank out of `litres`, `capacity` and `percent`. |

**`GET /api/level.txt`** returns plain text `percent;litres`, for example `45.2;720`. It's easy to read without a JSON library.

Example for the display ESP (Arduino):

```cpp
#include <WiFi.h>
#include <HTTPClient.h>

void readTank() {
  HTTPClient http;
  http.begin("http://192.168.1.50/api/level.txt");    // your water tank board's IP
  http.setTimeout(3000);
  if (http.GET() == 200) {
    float percent; int litres;
    if (sscanf(http.getString().c_str(), "%f;%d", &percent, &litres) == 2 && percent >= 0) {
      Serial.printf("Tank: %.0f %%, %d L\n", percent, litres);   // draw it on your screen here
    }
  }
  http.end();
}
// Call readTank() every few seconds from loop(). The level updates about 3 times a second.
```

Both endpoints send `Access-Control-Allow-Origin: *`, so a web page on another device can read them too.

<details>
<summary>Other endpoints</summary>

Used by the web page. They may change between versions; use `/api/level` for your own projects.
When a PIN is set, every `POST` that changes a setting needs a `pin` field, otherwise it gets `403`.

| Endpoint | Description |
|----------|-------------|
| `GET /data` | Live reading for the web page |
| `GET /status` | LED, screen, Wi-Fi mode, IP, SSID, signal, uptime |
| `GET /history` | 24 h and 7 day history |
| `GET /lock`, `POST /lock/check`, `POST /lock/set` | Settings PIN: is one set, check it, set/remove it |
| `GET/POST /tank` | Tank setup (`GET` also shows the sensor tank and each valve) |
| `POST /tank/valve` | Open (`open=1`) or close (`open=0`) the valve of tank `i` (0 = tank 1) |
| `GET/POST /time`, `POST /time/set` | Time zone, set the clock by hand |
| `GET /wifi`, `POST /wifi/save`, `/wifi/delete`, `/wifi/move`, `/wifi/use` | Saved Wi-Fi networks |
| `GET /scan` | Nearby Wi-Fi networks |
| `GET /led?state=on\|off`, `GET /screen?state=on\|off` | LED and screen control |

</details>

## Updating the firmware

After the first USB upload you can update over Wi-Fi. The board must be on and on the same network.

```
pio run -e ota -t upload
```

Or use **ota → Upload** in the PlatformIO sidebar.

- The OTA password is set in `main/secrets.ini` (see *Getting started*, step 2). If you change it, do one USB upload first, because the board still expects the old password over Wi-Fi.
- **Windows:** the firewall must allow incoming TCP port 18180. This is a one-time setup, in PowerShell **run as administrator**:
  ```
  New-NetFirewallRule -DisplayName "ESP32 OTA" -Direction Inbound -Protocol TCP -LocalPort 18180 -Action Allow -Profile Private
  ```

## Project structure

```
ESP32-C3-mini-water-level-sensor/
├── readme.md
└── main/                        The firmware (open this folder in VS Code)
    ├── platformio.ini           Build settings, USB and OTA upload
    ├── secrets.example.h        Copy to secrets.h: default Wi-Fi network
    ├── secrets.example.ini      Copy to secrets.ini: OTA password
    ├── main.cpp                 setup()/loop(): read sensor → smooth → screen, LED, web, history
    ├── A02YYUW.h/.cpp           Ultrasonic sensor driver (UART)
    ├── user-tank.h/.cpp         Tank shapes and sizes → depth, litres, percent
    ├── user-history.h/.cpp      24 h / 7 day history
    ├── user-clock.h/.cpp        Clock (NTP) and time zone
    ├── user-networks.h/.cpp     Saved Wi-Fi networks list
    ├── user-lock.h/.cpp         Optional PIN for the settings
    ├── user-wifi.h/.cpp         Wi-Fi connection manager, web page, API, mDNS, OTA
    ├── user-screen.h/.cpp       OLED display and screen button
    ├── user-led.h/.cpp          Status LED
    └── .claude/skills/          Instructions for Claude Code (build, device check, code conventions)
```

Settings stored in flash (they survive restarts and firmware updates):

| Namespace | Contents | Cleared by |
|-----------|----------|------------|
| `wifi`  | Saved Wi-Fi networks | BOOT button (hold 3 s) |
| `tank`  | Tank height, sensor gap, tank shapes and sizes, sensor tank, valves | — |
| `clock` | Time zone and summer time | — |
| `lock`  | Settings PIN | BOOT button (hold 3 s) |
| `hist`  | 7-day history | — |

## Troubleshooting

| Problem | What to check |
|---------|---------------|
| OLED shows `No reading` | Sensor wiring: TX and RX swapped? 5 V present? The sensor needs a clear path to the water; foam or a pipe that's too narrow gives bad echoes. |
| Level always 0 % or 100 % | **Tank Setup:** check the tank height and sensor gap. The check line under the button shows the depth the board calculates. |
| Board doesn't join Wi-Fi | The ESP32-C3 only supports **2.4 GHz** networks. The serial monitor lists the networks it can see. If yours is visible, the password is probably wrong. |
| `watertank.local` doesn't open | Use the IP address from the OLED. Some phones and Windows PCs are slow to find `.local` names. |
| Weak or dropping Wi-Fi | The transmit power is reduced on purpose (the Super Mini's antenna distorts at full power), so keep the board reasonably close to the router. Check `rssi` in `/status`; below about -80 dBm is weak. |
| OTA upload: "No response from device" | Windows Firewall, see [Updating the firmware](#updating-the-firmware). |
| No serial output | USB CDC On Boot must be on. It's already set in `platformio.ini`; in the Arduino IDE it's under **Tools → USB CDC On Boot → Enabled**. |
| USB upload doesn't start | Hold **BOOT**, press **RST**, release BOOT. Then upload again. |
| History graph is empty | It needs the clock: internet, or **Set from this phone** under Time. The first point appears after 5 minutes. |

## Other details

- **Cable:** 2-pair, 30 meters.
- **DC inputs:**
  1. 5 V power input.
  2. RX/TX for UART communication.
  3. 5 V output to the sensor.
