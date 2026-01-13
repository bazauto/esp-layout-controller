# Code Architecture

This document describes the architecture and organization of the ESP Layout Controller firmware.

## Language Strategy

### C Code (Minimal)
C code is **only** used where necessary:
- **`main.c`**: Minimal entry point
- **Hardware drivers**: ESP-IDF platform-specific code (`lvgl_port.c`, `waveshare_rgb_lcd_port.c`)
- **Legacy widgets**: `throttle_meter.c` (will migrate to C++ eventually)

All C headers that are called from C++ **must** have `extern "C"` guards.

### C++ Code (Primary)
All application logic, business rules, and new features are written in C++:
- Modern C++ (C++17 or later)
- Object-oriented design
- RAII for resource management
- Smart pointers where appropriate

## Layer Architecture

```
┌─────────────────────────────────────────────┐
│           Application Entry (C)              │
│              main.c                          │
└─────────────────┬───────────────────────────┘
                  │
┌─────────────────▼───────────────────────────┐
│              UI Layer (C++)                  │
│  ┌─────────────────────────────────────┐   │
│  │ MainScreen           WiFiConfigScreen│   │
│  │ (main controller UI) (settings UI)   │   │
│  └─────────────────────────────────────┘   │
└─────────────────┬───────────────────────────┘
                  │
┌─────────────────▼───────────────────────────┐
│          Controller Layer (C++)              │
│  ┌─────────────────────────────────────┐   │
│  │ AppController    ThrottleController  │   │
│  │ (coordinator)    (throttle logic)    │   │
│  └─────────────────────────────────────┘   │
└───────┬─────────────────────┬───────────────┘
        │                     │
┌───────▼────────┐    ┌───────▼──────────────┐
│  Model (C++)   │    │ Communication (C++)  │
│  ┌──────────┐ │    │ ┌─────────────────┐ │
│  │Locomotive│ │    │ │ WiFiManager     │ │
│  │Throttle  │ │    │ │ WiThrottleClient│ │
│  │Roster    │ │    │ │ MQTTClient      │ │
│  └──────────┘ │    │ └─────────────────┘ │
└────────────────┘    └──────────────────────┘
        │
┌───────▼────────────────────────────────────┐
│          Hardware Layer (C++)               │
│  ┌─────────────────────────────────────┐  │
│  │ RotaryEncoder    KnobManager         │  │
│  │ (EC11 driver)    (knob assignment)   │  │
│  └─────────────────────────────────────┘  │
└────────────────────────────────────────────┘
        │
┌───────▼────────────────────────────────────┐
│      Platform Layer (C - ESP-IDF)          │
│  ┌─────────────────────────────────────┐  │
│  │ lvgl_port    waveshare_rgb_lcd_port  │  │
│  │ (LVGL HAL)   (LCD driver)            │  │
│  └─────────────────────────────────────┘  │
└────────────────────────────────────────────┘
```

## Directory Structure

```
main/
├── main.c                      # Application entry (C)
├── lvgl_port.c/h              # LVGL platform integration (C)
├── waveshare_rgb_lcd_port.c/h # LCD hardware driver (C)
├── throttle_meter.c/h         # Legacy meter widget (C)
│
├── model/                     # Data models (C++)
│   ├── Locomotive.cpp/h       # Locomotive data
│   ├── Throttle.cpp/h         # Throttle state machine
│   └── Roster.cpp/h           # Roster management
│
├── communication/             # Network communication (C++)
│   ├── WiFiManager.cpp/h      # WiFi connection management
│   ├── WiThrottleClient.cpp/h # JMRI WiThrottle protocol
│   └── MQTTClient.cpp/h       # MQTT for cab signals
│
├── ui/                        # User interface (C++)
│   ├── MainScreen.cpp/h       # Main application screen
│   ├── WiFiConfigScreen.cpp/h # WiFi configuration UI
│   ├── main_screen_wrapper.cpp/h
│   └── wifi_config_wrapper.cpp/h
│
├── controller/                # Business logic (C++)
│   ├── AppController.cpp/h    # Main coordinator
│   └── ThrottleController.cpp/h
│
├── hardware/                  # Hardware abstraction (C++)
│   ├── RotaryEncoder.cpp/h    # Encoder driver
│   └── KnobManager.cpp/h      # Knob assignment
│
└── utils/                     # Utilities (C++)
    ├── StringUtils.h
    └── MathUtils.h
```

## Design Principles

### Single Responsibility
Each class has one clear responsibility.

### Dependency Injection
Dependencies are passed via constructor, making code testable.

### Separation of Concerns
- **Model**: Pure data, no I/O
- **View (UI)**: Display only, minimal logic
- **Controller**: Coordinates between layers
- **Hardware**: Abstracts physical devices

### RAII (Resource Acquisition Is Initialization)
Resources managed by constructors/destructors, not manual cleanup.

### Const Correctness
Methods that don't modify state are marked `const`.

### Smart Pointers
Use `std::unique_ptr` and `std::shared_ptr` instead of raw `new`/`delete`.

## Communication Patterns

### Between Layers
- Use callbacks for asynchronous events
- Use function pointers or `std::function` for C/C++ boundaries
- Avoid tight coupling between layers

### Event-Driven
- Hardware events (encoder rotation) trigger callbacks
- Network events update models
- Model changes trigger UI updates

### State Machines
- Throttle states: Unallocated → Selecting → Allocated → Released
- Connection states: Disconnected → Connecting → Connected → Failed

## Migration Path

### Current Status (Phase 2)
- ✅ Model layer complete
- ✅ WiFi communication complete
- ✅ Basic UI framework established
- ✅ C code minimized
- ⏳ Hardware layer pending
- ⏳ WiThrottle client pending
- ⏳ Full throttle controller pending

### Next Steps
1. Implement RotaryEncoder driver (hardware layer)
2. Create ThrottleController (controller layer)
3. Implement WiThrottleClient (communication layer)
4. Enhance MainScreen with full functionality
5. Add loco selection and function button UI

### Long-term
- Migrate `throttle_meter.c` to C++ class
- Add more sophisticated UI elements
- Implement MQTT for cab signals
- Add configuration persistence
- Optimize for performance
