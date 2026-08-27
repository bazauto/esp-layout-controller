# ESP Layout Controller

A 7" touchscreen interface for model railway control, built on the ESP32-S3. Connects to [JMRI](https://www.jmri.org/) via the [WiThrottle protocol](https://www.jmri.org/help/en/package/jmri/jmrit/withrottle/Protocol.shtml) over WiFi to drive up to 4 locomotives simultaneously, with 2 physical rotary encoders for speed control and roster browsing.

Part of a four-repo control stack for the Westgate Hollow model railway:
[layout-orchestration](https://github.com/bazauto/layout-orchestration) (backend and operator
UI), [PicoDCC](https://github.com/bazauto/PicoDCC) (DCC command station),
[layout-feedback](https://github.com/bazauto/layout-feedback) (MicroPython sensor nodes), and
this — the handheld throttle.

## Features

- **4 simultaneous throttles** — control 4 locomotives independently from a single device
- **2 rotary encoders** — physical speed knobs with push-button assignment via Adafruit I2C Seesaw encoders
- **Touch UI** — 800×480 LVGL interface with throttle meters, roster carousel, and function buttons
- **WiThrottle protocol** — standard wireless throttle protocol, compatible with JMRI and other WiThrottle servers
- **Orchestrator WebSocket** — cookie-authenticated control plane for the Westgate Hollow layout orchestrator
- **JMRI JSON API** — WebSocket connection for track power control and roster retrieval
- **NVS persistence** — WiFi credentials and JMRI server settings saved across reboots
- **Virtual encoders** — on-screen encoder substitutes for development without physical hardware

## Hardware

| Component | Detail |
|-----------|--------|
| Board | [Waveshare ESP32-S3 7" Touch Display](https://www.waveshare.com/esp32-s3-touch-lcd-7.htm) |
| MCU | ESP32-S3 (dual-core, 8 MB flash, 8 MB PSRAM) |
| Display | 7" RGB LCD, 800×480, ST7701 controller |
| Touch | GT911 capacitive (I2C) |
| Encoders | 2× [Adafruit I2C QT Rotary Encoder](https://www.adafruit.com/product/4991) with Seesaw (0x76, 0x77 via LTC4316) |

## Architecture

The project follows a layered architecture where **state lives at the application layer, not in the UI**. The UI can be destroyed and recreated without losing throttle state or network connections.

```
main/
├── model/          # Data: Locomotive, Throttle, Knob, Roster
├── hardware/       # HAL: rotary encoder driver
├── communication/  # ThrottleBackend port + transports, WiFi, WiThrottle (TCP),
│                   # JMRI JSON and orchestrator control plane (WebSocket)
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

The first build downloads LVGL and the Espressif components from the component registry into `managed_components/`, at the versions `dependencies.lock` pins — so it needs network access. Later builds work offline. To exit the serial monitor, press `Ctrl-]`.

### Run Tests

Unit tests run on-device using a separate build configuration:

```powershell
.\tools\ensure-idf.ps1; idf.py -B build-tests -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.test.defaults" -D SDKCONFIG="build-tests/sdkconfig" flash_test
```

This uses a dedicated build directory so test config doesn't interfere with the normal build. Delete `build-tests/` to force a clean config. Ensure the serial monitor is closed so the flash step can access the COM port.

## Project Status

All core phases are complete. The device is fully functional with touchscreen UI, WiThrottle/JMRI connectivity, and physical rotary encoder control.

Two throttle transports, selectable at runtime from the settings screen and persisted in NVS: **WiThrottle** to JMRI, or the [layout orchestrator](https://github.com/bazauto/layout-orchestration)'s **WebSocket control plane**. Only the selected one's network stack is started. Both are first-class; neither is a legacy path.

## Licence

The first-party application code is licensed under the [MIT License](LICENSE) — `main/`
(except four Espressif-derived files), `tools/` and the documentation.

No third-party source is committed here. LVGL and the Espressif components are fetched by the
IDF component manager into the gitignored `managed_components/`, pinned by
`dependencies.lock`, and carry their own licences. Full detail — what the MIT grant covers,
each component's licence and notice location, the Apache-2.0 modification notices on the
Espressif-derived files, and what a published binary would need — is in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
