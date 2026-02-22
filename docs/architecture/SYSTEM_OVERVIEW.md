# System Overview

## Summary

ESP Layout Controller is an ESP32-S3 firmware providing a 7" touchscreen interface for model railway control. It manages 4 simultaneous locomotive throttles via the WiThrottle protocol, with 2 I2C rotary encoders for physical speed/selection control.

**Hardware:** ESP32-S3 (dual-core, 8 MB flash), 800×480 RGB LCD (ST7701), GT911 capacitive touch, 2× Adafruit I2C Seesaw rotary encoders (0x76, 0x77 via LTC4316 address translator).

**Protocols:** WiThrottle (TCP, throttle control + roster), JMRI JSON (WebSocket, track power), MQTT (future, cab signals).

---

## Layered Architecture

```mermaid
block-beta
    columns 1
    block:entry["Entry Point"]
        main_c["main.c (C)"]
    end
    space
    block:app["Application Layer — owns all state"]
        AppController
        ThrottleController
        WiFiController
        JmriConnectionController
    end
    space
    block:lower["Service Layers"]
        columns 3
        block:comm["Communication"]
            WiThrottleClient
            JmriJsonClient
            WiFiManager
        end
        block:model["Model"]
            Throttle
            Knob
            Locomotive
            Roster
        end
        block:hw["Hardware"]
            RotaryEncoderHal
        end
    end
    space
    block:ui["UI Layer — presentation only, never owns state"]
        MainScreen
        WiFiConfigScreen
        JmriConfigScreen
    end

    entry --> app
    app --> lower
    app --> ui
```

### Key Principle

**State lives at the application layer, not in the UI.** The UI holds raw pointers to controllers and can be destroyed/recreated freely (e.g. navigating to settings and back) without losing throttle assignments, speeds, or connection state.

```
Application Layer (survives screen changes)
  └─ ThrottleController (singleton)
       ├─ Throttle[4] → Locomotive (unique_ptr)
       └─ Knob[2]

UI Layer (disposable)
  └─ MainScreen → ThrottleController*  (raw pointer, not owned)
```

---

## Dependency Graph

```mermaid
graph TD
    main_c["main.c"] --> AppController

    AppController --> WiFiController
    AppController --> WiThrottleClient
    AppController --> JmriJsonClient
    AppController --> JmriConnectionController
    AppController --> ThrottleController
    AppController --> RotaryEncoderHal
    AppController --> MainScreen

    WiFiController --> WiFiManager

    JmriConnectionController --> JmriJsonClient
    JmriConnectionController --> WiThrottleClient
    JmriConnectionController --> WiFiController

    ThrottleController --> WiThrottleClient
    ThrottleController --> Throttle
    ThrottleController --> Knob
    Throttle --> Locomotive

    RotaryEncoderHal -.->|callbacks| ThrottleController

    MainScreen -.->|raw ptr| WiThrottleClient
    MainScreen -.->|raw ptr| JmriJsonClient
    MainScreen -.->|raw ptr| ThrottleController
    MainScreen --> ThrottleMeter
    MainScreen --> PowerStatusBar
    MainScreen --> RosterCarousel
    MainScreen --> FunctionPanel
    MainScreen --> VirtualEncoderPanel

    PowerStatusBar -.->|raw ptr| JmriJsonClient
    RosterCarousel -.->|raw ptr| ThrottleController

    style AppController fill:#4a9,stroke:#333,color:#fff
    style ThrottleController fill:#4a9,stroke:#333,color:#fff
    style MainScreen fill:#69c,stroke:#333,color:#fff
    style WiThrottleClient fill:#c74,stroke:#333,color:#fff
    style JmriJsonClient fill:#c74,stroke:#333,color:#fff
```

**Legend:** Green = controllers (own state), Blue = UI (presentation), Red = communication (network I/O).

Solid arrows = ownership (`unique_ptr` or direct member). Dashed arrows = borrowed reference (raw pointer, not owned).

---

## Data Flow: Speed Change (End-to-End)

```mermaid
flowchart LR
    A["Encoder rotation"] --> B["RotaryEncoderHal\n(polling task)"]
    B -->|"callback(knobId, delta)"| C["ThrottleController\n(lock mutex)"]
    C -->|"optimistic update"| D["Throttle model\n(setSpeed/setDirection)"]
    C -->|"setSpeed()"| E["WiThrottleClient\n(TCP send)"]
    E -->|"M0AS3<;>V50"| F["JMRI Server"]
    F -->|"M0AS3<;>V50"| G["WiThrottleClient\n(receive task)"]
    G -->|"onThrottleStateChanged"| C
    C -->|"uiUpdateCallback"| H["MainScreen\n(LVGL lock → update)"]
```

---

## File Organisation

```
main/
├── main.c                          # C entry point (hardware init only)
├── lvgl_port.c/h                   # LVGL display driver, mutex
├── waveshare_rgb_lcd_port.c/h      # LCD hardware driver
├── Kconfig.projbuild               # Build configuration (display, tests)
├── hardware/
│   └── RotaryEncoderHal.cpp/h      # I2C Seesaw encoder HAL
├── communication/
│   ├── WiFiManager.cpp/h           # WiFi STA + NVS credentials
│   ├── WiThrottleClient.cpp/h      # WiThrottle TCP protocol
│   └── JmriJsonClient.cpp/h        # JMRI JSON WebSocket
├── model/
│   ├── Locomotive.cpp/h            # Loco data (address, speed, functions)
│   ├── Throttle.cpp/h              # Throttle state machine + loco ownership
│   ├── Knob.cpp/h                  # Encoder knob state machine
│   └── Roster.cpp/h                # Available locos collection
├── controller/
│   ├── AppController.cpp/h         # Singleton, owns all services
│   ├── ThrottleController.cpp/h    # 4 throttles + 2 knobs coordinator
│   ├── WiFiController.cpp/h        # WiFi lifecycle
│   └── JmriConnectionController.cpp/h  # JMRI auto-connect + reconnect
├── ui/
│   ├── MainScreen.cpp/h            # Main 2×2 throttle grid + panels
│   ├── WiFiConfigScreen.cpp/h      # WiFi settings
│   ├── JmriConfigScreen.cpp/h      # JMRI connection + system status
│   ├── components/
│   │   ├── ThrottleMeter.cpp/h     # Circular gauge widget
│   │   ├── VirtualEncoderPanel.cpp/h  # On-screen encoder buttons (test)
│   │   ├── RosterCarousel.cpp/h    # Loco selection display
│   │   ├── FunctionPanel.cpp/h     # F0–F28 toggle buttons
│   │   └── PowerStatusBar.cpp/h    # Track power + connection status
│   └── wrappers/                   # extern "C" wrappers for cross-language calls
└── tests/                          # Unity on-device tests
```
