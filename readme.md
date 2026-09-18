# ESP32-C3 Water Level Monitoring System

Ultrasonic tank-level monitor with 
**A02YYUW sensor**, **0.42" 72x40 SSD1306 OLED**, 
and an onboard status **LED**, 
featuring WiFi setup and live web dashboard.

##  Features
- **A02YYUW ultrasonic distance sensor** (UART)
- **SSD1306 OLED display** (I²C, U8g2) for live readings
- **Onboard LED** status indicator (auto red/yellow/green by level)
- **WiFi setup portal** (AP/STA modes)
- Configurable **full** and **empty** tank distances
- Modular C++/ESP32 code

##  Project Structure

esp32-distance-sensor/
├── README.md
├── main/
│ ├── main.ino # Main application
│ ├── A02YYUW.h / A02YYUW.cpp # Ultrasonic sensor driver
│ ├── user-led.h / user-led.cpp # Onboard LED control
│ ├── user-screen.h / user-screen.cpp # OLED display module
│ └── user-wifi.h / user-wifi.cpp # WiFi & web server
└── libraries/
└── ...


##  Wiring

### A02YYUW → ESP32-C3

| Sensor Wire | ESP32-C3 Pin |
|-------------|--------------|
| Red (5V)    | 5V           |
| Black (GND) | GND          |
| White (TX)  | GPIO 4 (RX)  |
| Yellow (RX) | GPIO 7 (TX)  |

| Sensor Wire | 2 pair cable | DC Barrel Jack | ESP32-C3 Pin |
|-------------|--------------|----------------|--------------|
| Red (5V)    | orange       | Red            | 5V           |
| Black (GND) | brown        | Black          | GND          |
| White (TX)  | blue         | Black          | GPIO 4 (RX)  |
| Yellow (RX) | green        | Red            | GPIO 7 (TX)  |

### OLED (SSD1306, 0.42" 72x40) → ESP32-C3

| OLED Pin | ESP32-C3 Pin |
|----------|--------------|
| VCC      | 3.3V         |
| GND      | GND          |
| SDA      | GPIO 5       |
| SCL      | GPIO 6       |

### Onboard LED → ESP32-C3

The status LED is the board's onboard single-color LED, driven directly on **GPIO 8** — no external wiring needed.

### Buttons → ESP32-C3

| Button              | ESP32-C3 Pin | Other side | Behavior                          |
|----------------------|--------------|------------|------------------------------------|
| Screen toggle button | GPIO 3       | GND        | Short press: toggle OLED on/off    |
| WiFi reset button     | GPIO 0       | GND        | Hold 3 seconds: clears WiFi creds  |

---


## ESP32-C3 Connections

| Source         | Destination         |
|----------------|---------------------|
| 5V             | A02YYUW RED         |
| GND            | A02YYUW BLACK       |
| GND            | SSD1306 GND         |
| GND            | Screen toggle button |
| GND            | WiFi reset button    |
| 3.3V           | SSD1306 VCC         |
| GPIO 0         | WiFi reset button    |
| GPIO 3         | Screen toggle button |
| GPIO 4         | A02YYUW White (RX)  |
| GPIO 5         | SSD1306 I2C_SDA     |
| GPIO 6         | SSD1306 I2C_SCL     |
| GPIO 7         | A02YYUW Yellow (TX) |
| GPIO 8         | Onboard LED (built-in) |


## Screen shot of Webserver:

![Webserver-view](Images/Webserver-view.png)


 ## Photos:

 ![photo1](Images/photo1.jpg)
 ![photo2](Images/photo2.jpg)


## don't forget to turn on UART Serial.
![photo3](Images/Turn-on-serial-connectoin.png)


## ⚙️ Other Details

- **Cable**: 2-pair, 30 meters
- **DC Inputs**:
  1. 5V power input
  2. RX/TX for UART communication
  3. 5V output to sensor
- **Buttons**:
  - **Power switch**: Turns the device on/off (inline, not GPIO-controlled)
  - **Screen toggle button** (GPIO 3): Turns OLED display on/off
  - **WiFi reset button** (GPIO 0): Hold 3 seconds to clear WiFi credentials
