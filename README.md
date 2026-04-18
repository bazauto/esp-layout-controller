# ESP Layout Controller

A 7" touchscreen interface for model railway control, built on the ESP32-S3. Connects to [JMRI](https://www.jmri.org/) via the [WiThrottle protocol](https://www.jmri.org/help/en/package/jmri/jmrit/withrottle/Protocol.shtml) over WiFi to drive up to 4 locomotives simultaneously, with 2 physical rotary encoders for speed control and roster browsing.

## Features

- **4 simultaneous throttles** — control 4 locomotives independently from a single device
- **2 rotary encoders** — physical speed knobs with push-button assignment via Adafruit I2C Seesaw encoders
- **Touch UI** — 800×480 LVGL interface with throttle meters, roster carousel, and function buttons
- **WiThrottle protocol** — standard wireless throttle protocol, compatible with JMRI and other WiThrottle servers
- **JMRI JSON API** — WebSocket connection for track power control and roster retrieval
- **NVS persistence** — WiFi credentials and JMRI server settings saved across reboots
- **Virtual encoders** — on-screen encoder substitutes for development without physical hardware

## Hardware

| Component | Detail |
|-----------|--------|
| MCU | ESP32-S3 (dual-core, 8 MB flash, 8 MB PSRAM) |
| Display | 7" RGB LCD, 800×480, ST7701 controller |
| Touch | GT911 capacitive (I2C) |
| Encoders | 2× Adafruit I2C Seesaw rotary encoders (0x76, 0x77 via LTC4316) |

## Architecture

The project follows a layered architecture where **state lives at the application layer, not in the UI**. The UI can be destroyed and recreated without losing throttle state or network connections.

```
main/
├── model/          # Data: Locomotive, Throttle, Knob, Roster
├── hardware/       # HAL: rotary encoder driver
├── communication/  # WiFi, WiThrottle (TCP), JMRI JSON (WebSocket)
├── controller/     # AppController, ThrottleController, WiFiController
├── ui/             # LVGL screens and components
└── tests/          # On-device unit tests (Unity)
```

See [docs/](docs/) for detailed architecture, threading model, state machines, protocol references, and sequence diagrams.

## Prerequisites

- **ESP-IDF v5.5.2** — installed locally (not bundled in this repo)
- **Python 3** — for the build system and test runner
- **Windows** — the helper scripts assume PowerShell (contributions for other platforms welcome)

## Getting Started

### Set up ESP-IDF

In a fresh PowerShell terminal, run the helper script to initialise the ESP-IDF environment:

```powershell
.\tools\ensure-idf.ps1
```

This runs the ESP-IDF export script if needed and makes `idf.py` available in your session.

### Build and Flash

```powershell
.\tools\ensure-idf.ps1; idf.py -p COM4 build flash monitor
```

To use a different serial port, set the `ESP_PORT` environment variable (defaults to `COM4`):

```powershell
$env:ESP_PORT = "COM5"
.\tools\ensure-idf.ps1; idf.py -p $env:ESP_PORT build flash monitor
```

The first build will download managed components from the ESP-IDF component registry. To exit the serial monitor, press `Ctrl-]`.

### Run Tests

Unit tests run on-device using a separate build configuration:

```powershell
.\tools\ensure-idf.ps1; idf.py -B build-tests -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.test.defaults" -D SDKCONFIG="build-tests/sdkconfig" flash_test
```

This uses a dedicated build directory so test config doesn't interfere with the normal build. Delete `build-tests/` to force a clean config. Ensure the serial monitor is closed so the flash step can access the COM port.

## Project Status

All core phases are complete. The device is fully functional with touchscreen UI, WiThrottle/JMRI connectivity, and physical rotary encoder control. Future work includes refinement, optimisation, and potential MQTT cab signal integration.

## Licence

This project is licensed under the [MIT License](LICENSE).

The original hardware driver and LVGL port files (`waveshare_rgb_lcd_port.c`, `lvgl_port.c`) retain their upstream SPDX headers (CC0-1.0 and Apache-2.0 respectively).

Third-party components carry their own licences:

| Component | Licence |
|-----------|---------|
| [LVGL](https://lvgl.io/) v8.4.0 | MIT |
| ESP LCD Touch / GT911 | Apache-2.0 |
| ESP WebSocket Client | Apache-2.0 |
| ESP-IDF | Apache-2.0 |
