# C++ Migration Progress

## Completed

### Directory Structure ✅
Created organized directory structure for C++ code:
```
main/
  ├── hardware/          # Hardware abstraction layer (rotary encoders, I2C)
  ├── communication/     # Network protocols (WiThrottle, MQTT)
  ├── model/            # ✅ Data models (completed)
  ├── controller/       # Business logic controllers
  ├── ui/               # LVGL UI components
  └── utils/            # Helper functions and utilities
```

### Model Layer ✅
**Pure C++ data model classes with no dependencies on UI or I/O:**

#### `Locomotive.h/.cpp`
- Represents a single locomotive with DCC address, name, and state
- Stores speed (0-126 for 128 speed steps)
- Stores direction (FORWARD/REVERSE)
- Tracks 29 functions (F0-F28) with states and labels
- Speed step mode support (14/27/28/128 steps)
- WiThrottle address string formatting ("S3", "L2102")

#### `Throttle.h/.cpp`
- Represents one of 4 throttle instances
- State machine: UNALLOCATED → SELECTING_LOCO → ALLOCATED
- Tracks knob assignment (KNOB_1, KNOB_2, or none)
- Owns assigned locomotive (unique_ptr)
- Provides clean state transitions

#### `Roster.h/.cpp`
- Manages up to 50 locomotives (command station limit)
- Stores locomotive definitions from WiThrottle roster
- Provides navigation (next/previous with wraparound)
- Search by name or address
- Creates locomotive copies for throttle assignment

### Build System ✅
- CMakeLists.txt updated to include C++ files
- C++17 standard enabled
- Include directories configured for all layers

## Next Steps

### 1. Hardware Layer
**After LTC4316 hardware arrives:**
- `RotaryEncoder.h/.cpp` - Adafruit I2C QT encoder driver
  - I2C addresses: 0x76, 0x77
  - Position tracking
  - Button state management
  - Event callbacks
  - Debouncing

### 2. Communication Layer
**Can start now (doesn't require encoder hardware):**
- `WiFiManager.h/.cpp` - WiFi connection management
  - Configuration UI support
  - Connection status tracking
  - Reconnection logic
  
- `WiThrottleClient.h/.cpp` - WiThrottle protocol implementation
  - Socket management
  - Protocol message parser
  - Roster retrieval (`RL` command parsing with `]\[` and `}|{` delimiters)
  - Throttle commands (M commands)
  - Bidirectional sync (M notifications)
  - **CRITICAL**: Heartbeat system (10-second interval)
  
- `MQTTClient.h/.cpp` - MQTT for cab signals (future phase)

### 3. Controller Layer
**Orchestration and business logic:**
- `ThrottleController.h/.cpp` - Manages 4 throttles
  - Knob assignment logic
  - Roster navigation during selection
  - Speed control when allocated
  - Emergency stop handling
  - State machine coordination
  
- `LayoutController.h/.cpp` - Layout-wide operations
  - Track power control
  - System state management

### 4. UI Layer
**Migrate existing C code to C++ and enhance:**
- Move `throttle_meter.c/.h` to `ui/ThrottleMeter.cpp/.h`
- Move `test_throttle_screen.c/.h` to `ui/` (or remove after migration)
- Create `ThrottleDetailPanel.h/.cpp` - Right-side detail view
- Create `LayoutControlPanel.h/.cpp` - Control buttons
- Create `KnobIndicator.h/.cpp` - Visual knob assignment

### 5. Main Application
**Migrate main.c to main.cpp:**
- Initialize all subsystems
- Create application controller
- Set up FreeRTOS tasks
- Handle lifecycle

## Testing Plan

### Unit Testing (Without Hardware)
1. Test Locomotive class with various addresses and functions
2. Test Throttle state machine transitions
3. Test Roster navigation and search

### Integration Testing (With Hardware)
1. Test rotary encoder I2C communication (0x76, 0x77)
2. Test encoder callbacks and debouncing
3. Test WiFi connection and WiThrottle protocol
4. Test full throttle control flow

### System Testing
1. Test with JMRI test layout
2. Test multi-throttle operation
3. Test bidirectional sync with other throttles
4. Test heartbeat and connection recovery

## Development Priority

**Phase 1: Communication (Start Now)**
1. WiFiManager - Get network connectivity
2. WiThrottleClient - Get roster and basic throttle control
3. Test with JMRI using hardcoded test data

**Phase 2: Hardware Integration (When LTC4316 arrives)**
1. RotaryEncoder driver
2. Hardware test utilities
3. Integration with throttle controller

**Phase 3: Controller Logic**
1. ThrottleController with full state machine
2. Wire up encoders to throttles
3. Complete UI integration

**Phase 4: Polish**
1. WiFi configuration UI
2. Function buttons
3. Layout controls
4. Visual enhancements

## Notes

- All C++ code follows project standards (see .copilot-instructions.md)
- Using C++17 features (smart pointers, string_view where appropriate)
- ESP-IDF compatible (no exceptions, careful with heap)
- LVGL integration via C++ wrappers where needed
- Legacy C files will be migrated incrementally

## Hardware Status
- ✅ I2C address conflict resolved (LTC4316 translator)
- ⏳ Waiting for LTC4316 hardware to arrive
- 📋 Encoder addresses will be 0x76 and 0x77
