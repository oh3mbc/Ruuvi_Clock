# Retro Ruuvi Air Display Clock - Project Documentation

## 1. Project Overview
This project transforms an **ESP32 microcontroller (NodeMCU-32S)** into an automated environmental tracking dashboard clock using a **4-in-1 MAX7219 LED Dot Matrix display panel**. The clock synchronizes accurate time over the internet via Wi-Fi and gathers high-precision local environmental metrics from a **Ruuvi Air** sensor beacon using Bluetooth Low Energy (BLE).

The system is optimized for an ultra-clean, retro aesthetic, stripping away complex scrolling animations in favor of static, **5-character labeled layouts** utilizing the crisp, native 5x7 hardware font.

---

## 2. Hardware Architecture & Pinout Map

### Hardware Components
* **Microcontroller:** NodeMCU-32S (ESP32-WROOM-32 Core)
* **Environmental Sensor:** Ruuvi Air broadcasting Data Format 6 (Temperature, Humidity, Pressure, PM2.5, and CO2).
* **Matrix Display Panel:** 4-in-1 MAX7219 Dot Matrix Module (32x8 Pixel Grid arranged in 4 zones).

### Wiring Configuration Matrix

| MAX7219 Matrix Pin | NodeMCU-32S / ESP32 Pin | Function / Hardware Role |
| :--- | :--- | :--- |
| **VCC** | **5V / VIN** | 5V Main Power Input (Required for crisp brightness) |
| **GND** | **GND** | Common Ground |
| **DIN** | **GPIO 23** | SPI Master Out (MOSI) — Fixed Hardware VSPI |
| **CS** | **GPIO 5** | Cable Chip Select (CS_P) — Configurable |
| **CLK** | **GPIO 18** | SPI Clock Pulse (SCK) — Fixed Hardware VSPI |

### 🔄 Physical Mounting & Software 180-Degree Rotation
To accommodate custom enclosure restrictions where the LED matrix panel is physically mounted upside down, the display orientation is corrected entirely through software drivers inside the `setup()` block. 

By applying dual zone inversion flags (`PA_FLIP_UD` and `PA_FLIP_LR`), the character pixel columns and rows are mirrored vertically and horizontally. This achieves a perfect **180-degree electronic rotation** without requiring case modifications or physical wire re-routing.

---

## 3. Firmware Architecture & Software Mechanics

The code is designed as a standalone state machine using non-blocking asynchronous timing hooks to maximize tracking loop reliability and avoid processor hangs.

### A. Initialization & Asynchronous Captive Portal
* On power-up, the device checks for a stored Wi-Fi profile and configuration mapping data inside non-volatile flash memory preferences.
* If no profile is detected or the manual **GPIO 4** configuration pin is grounded, the system boots into an Access Point portal layout.
* Before spinning up the portal, the ESP32 performs an absolute **15-second BLE boot scan**, echo-printing nearby MAC addresses and device models directly to the serial terminal to pre-populate available Ruuvi tags.
* The portal menu interface is customized via JavaScript injection into distinct sub-modules (**Configure WiFi**, **Configure Display**, and **Close**). 
* Clicking **Save** updates flash parameters seamlessly and redirects to the root page context without crashing data sessions. Clicking **Close** commands a secure on-demand software reboot (`ESP.restart()`).

### B. Network Time & Data Stream Optimization
* Once a profile is verified, the system wakes the Wi-Fi card to fetch local time coordinates from an online time provider (`fi.pool.ntp.org`) over NTP, calculates regional shifts via timezone configurations (`EET-2EEST`), and instantly puts the Wi-Fi modem completely to sleep (`WIFI_OFF`) to prevent network background threads from interfering with the BLE radio. Time synchronization triggers autonomously once every 2 hours.
* The BLE engine wakes up every 30 seconds to poll for the registered Ruuvi MAC address. When a matching target packet is captured, it decodes Data Format 6 payload arrays cleanly into specific memory allocation variables.

### C. Visual Frame Rendering & Representation Map
Screens cycle smoothly every 4 seconds. The metrics are truncated tightly down to a maximum of 5 flat character arrays to fit perfectly within the boundaries of the 4-panel module:

* 🕒 **Time (`17:46`):** Standard 24-hour clock. The colon symbol toggles parity dynamically using millisecond modulos, causing it to pulse once per second.
* 📅 **Date (`08.11`):** Two-digit numeric representation of Day and Month.
* 🌡️ **Temperature (`22.4C`):** High-precision CO2-sensor matched decimal output with a trailing **C** identifier.
* 💧 **Humidity (`45.2H`):** Local humidity metric with a trailing **H** identifier.
* 🎈 **Pressure (`1013P`):** Local barometric whole integer metric with a trailing **P** identifier.
* 💨 **PM2.5 (`12.5M`):** Particulate Matter levels labeled with an **M** (Matter) identifier.
* 🏭 **CO2 (` 850G`):** Carbon Dioxide parts per million levels labeled with a **G** (Gas) identifier.

### D. Hardened Runtime Factory Reset
* A safety interrupt listener polls the **BOOT Button (GPIO 0)** constantly.
* Holding down the button for 3 continuous seconds initiates a visual countdown panel (`CLR 3` -> `CLR 2` -> `CLR 1`).
* If confirmed, the system wipes all registered Wi-Fi memory, clears flash preference spaces completely, prints `WIPE` to the LED display matrix, and boots back into fresh AP configuration states securely.

---

## 4. Compilation & Deployment Details
* **Build Tool:** PlatformIO IDE
* **Framework:** Arduino framework for ESP32
* **Core Libraries Used:** `WiFiManager` (Development Branch), `Preferences`, `BLEDevice`, `MD_Parola`, `MD_MAX72xx`.
