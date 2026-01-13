# ESP Layout Controller - Project Overview

## Current Status (Phase 1 → Phase 2 Transition)
✅ **Completed:**
- Basic LVGL UI framework operational
- 4 throttle meters displaying in 2x2 grid
- WiThrottle protocol research complete (see WITHROTTLE_PROTOCOL.md)
- Project planning and documentation
- I2C address conflict resolved (LTC4316 translator solution)

🔄 **In Progress:**
- C++ migration and directory structure setup
- Awaiting rotary encoder hardware (LTC4316 translators on order)

⏭️ **Next Steps:**
1. Create C++ directory structure
2. Migrate existing code to C++
3. Implement WiFi connection management
4. Begin WiThrottle client implementation
5. Test with encoders when hardware arrives

## Project Goal
Create a 7" touchscreen control interface for a model railway layout, capable of controlling up to 4 locomotives simultaneously using the WiThrottle protocol.

## Hardware Components
- **Display**: 7" touchscreen (currently configured, uses I2C 0x36-0x3D)
- **Microcontroller**: ESP32-S3
- **Input Devices**: 2x Adafruit I2C QT Rotary Encoders with push buttons
  - **I2C Addresses**: 0x76 and 0x77 (via LTC4316 address translator)
  - **Address Translation**: Using Adafruit LTC4316 I2C Address Translator
    - Translates encoder base addresses (0x36, 0x37) by XOR 0x40
    - Final addresses: 0x76 (encoder 1), 0x77 (encoder 2)
  - **STATUS**: ✅ Solution identified, hardware on order
  - **Note**: Cannot test encoder functionality until hardware arrives
- **Communication**: WiFi (WiThrottle protocol), MQTT (cab signals)

## Core Features

### 1. Locomotive Control (4 Throttles)
- **Initial State**: All throttles unallocated and idle
- **Loco Selection**: Use rotary encoder to scroll through roster, button press to select
- **Speed Control**: Rotary encoder adjusts speed once loco is assigned
- **Emergency Stop**: Button press issues stop command
- **Release**: Touch interface allows releasing loco and resetting throttle

### 2. Physical Control Interface
- **2 Rotary Encoders**: Can control any of the 4 throttles (2 active at once)
- **Knob Assignment**: Touch throttle on screen to highlight and connect to selected knob
- **Visual Feedback**: Highlighted indicator shows which knob controls which throttle
- **Dynamic Switching**: Touch knob selector on any throttle to reassign control

### 3. Touchscreen Interface

#### Left Side (50% width)
- 4 throttle meters in 2x2 grid
- Each throttle shows:
  - Current speed/state
  - Loco identification
  - Knob assignment buttons/indicators
  - Touch to select for detail view

#### Right Side (50% width)
- **Throttle Details Panel** (when throttle selected):
  - Function buttons (from WiThrottle roster data)
  - Release button
  - Additional loco information
- **Layout Control Buttons**:
  - Track power control
  - Other layout functions (TBD)

### 4. WiThrottle Protocol Integration
- **Roster Management**: Retrieve and display available locos
- **Bidirectional Control**: 
  - Send throttle commands to JMRI
  - Receive updates from other throttles controlling same locos
- **Multi-Throttle Support**: Handle concurrent control from multiple sources

### 5. MQTT Integration (Future Phase)
- Subscribe to signal state topics
- Display cab signals for each controlled loco
- Provide visual warnings/danger indicators
- Operator alert system for track ahead conditions

### 6. Layout Control Features
- Track power on/off via JMRI JSON/HTTP API
- Power state display
- Additional control buttons (to be defined)
- Turnout control (future, via JSON API)
- Route activation (future, via JSON API)

## Technical Architecture

### Code Organization
- **Language**: C++ (migration from current C code)
- **Structure**: Class-based OOP design
- **Encapsulation**: Separate classes for different subsystems

### Key Software Components Needed

#### 1. Hardware Abstraction Layer
- **RotaryEncoder Class**: I2C rotary encoder interface
  - Position tracking
  - Button state management
  - Event callbacks (rotation, button press)
  - Debouncing

#### 2. Communication Layer
- **WiThrottleClient Class**: WiThrottle protocol implementation
  - Connection management
  - Roster retrieval and parsing
  - Throttle command sending
  - Status update receiving
  - Protocol message parsing
  - Heartbeat/keepalive

- **JMRIHttpClient Class**: JMRI JSON/HTTP API for layout control
  - Track power control (GET/POST)
  - Turnout control (future)
  - Sensor queries (future)
  - Route activation (future)
  - HTTP client with JSON parsing

- **MQTTClient Class**: MQTT communication (Phase 7)
  - Connection management
  - Topic subscription
  - Message handling
  - Signal state parsing

#### 3. Model Layer
- **Locomotive Class**: Loco data and state
  - DCC address
  - Name/road number
  - Function definitions (from roster)
  - Current speed
  - Direction
  - Function states

- **Roster Class**: Available locos management
  - Loco list storage
  - Search/filter capabilities
  - Selection navigation

- **Throttle Class**: Individual throttle state
  - Assigned locomotive (nullable)
  - Current speed setting
  - Assigned knob (0, 1, or none)
  - Allocation state
  - Direction

- **SignalState Class**: Cab signal information
  - Signal aspect (clear/approach/danger)
  - Associated throttle/loco

#### 4. Controller Layer
- **ThrottleController Class**: Orchestrates throttle operations
  - Manages 4 throttle instances
  - Handles knob assignments
  - Routes encoder input to active throttles
  - Synchronizes with WiThrottle updates
  - State machine for selection vs control modes

- **LayoutController Class**: Layout-wide operations
  - Track power control
  - System state management

#### 5. UI Layer
- **ThrottleView Class**: Visual throttle meter (existing component)
  - LVGL-based display
  - Touch event handling
  - State visualization

- **ThrottleDetailPanel Class**: Right-side detail view
  - Function button grid
  - Release button
  - Loco information display

- **LayoutControlPanel Class**: Layout control buttons
  - Track power toggle
  - Additional controls

- **KnobIndicator Class**: Visual knob assignment feedback
  - Highlight active assignments
  - Touch target for selection

#### 6. State Management
- **AppState Class**: Global application state
  - Active throttle selections
  - Connection status (WiFi, JMRI, MQTT)
  - System mode

## Implementation Phases

### Phase 1: Foundation & Migration ✓ (Partially Complete)
- [x] Basic LVGL UI structure
- [x] Throttle meter display
- [ ] Migrate to C++
- [ ] Create base class structure
- [ ] Setup project organization

### Phase 2: Hardware Integration
- [ ] Research Adafruit I2C QT Rotary Encoder library/datasheet
- [ ] I2C rotary encoder driver implementation (addresses 0x76, 0x77)
- [ ] Encoder input handling with callbacks
- [ ] Button debouncing and event system
- [ ] Hardware testing utilities
- [ ] ⏳ **BLOCKED**: Waiting for LTC4316 address translator hardware

### Phase 3: WiThrottle Protocol
- [x] Research WiThrottle protocol specification (see WITHROTTLE_PROTOCOL.md)
- [ ] Network connection management (WiFi)
- [ ] WiThrottle protocol parser
- [ ] Roster retrieval and parsing (50 locos max, `]\[` and `}|{` delimiters)
- [ ] Throttle command implementation (128 speed steps, 0-126 range)
- [ ] Function support (F0-F28, with labels from roster)
- [ ] Bidirectional state synchronization (handle M notifications)
- [ ] Connection recovery/resilience
- [ ] **CRITICAL**: Heartbeat implementation (prevent E-stop)

### Phase 4: Core Throttle Functionality
- [ ] Throttle state machine (unallocated → selecting → allocated)
- [ ] Loco roster navigation
- [ ] Knob assignment logic
- [ ] Speed control implementation
- [ ] Emergency stop
- [ ] Multi-throttle coordination

### Phase 5: UI Enhancement
- [ ] Touch event handling
- [ ] Detail panel implementation
- [ ] Function button generation from roster data
- [ ] Visual feedback and animations
- [ ] Knob assignment indicators
- [ ] Status displays

### Phase 6: Layout Controls
- [ ] Track power control
- [ ] WiFi configuration UI
- [ ] Additional control buttons
- [ ] System status indicators

### Phase 7: MQTT & Cab Signals (Future)
- [ ] MQTT client implementation
- [ ] Signal state subscription
- [ ] Cab signal display
- [ ] Warning/danger visual indicators
- [ ] Alert system

## Key Considerations & Questions

### Technical Challenges
1. **State Synchronization**: How to handle conflicts when multiple throttles control the same loco?
   - JMRI should be source of truth
   - Local state must quickly reflect remote changes
   
2. **Network Resilience**: What happens on WiFi/JMRI disconnection?
   - Maintain last known state
   - Visual indication of connection status
   - Automatic reconnection
   - Graceful degradation

3. **I2C Reliability**: Multiple devices on I2C bus
   - Address management
   - Error handling
   - Bus recovery

4. **Rotary Encoder Timing**: Fast rotation handling
   - Interrupt-driven or polling?
   - Acceleration curves for scrolling?

5. **Memory Management**: ESP32-S3 constraints
   - LVGL memory requirements
   - Roster size limits
   - Efficient string handling

6. **UI Responsiveness**: Touch feedback must be immediate
   - Non-blocking network operations
   - Separate tasks for network and UI
   - FreeRTOS task priorities

### Design Decisions Needed
1. **Roster Size**: ✅ **50 locos maximum** (matching command station capability)
2. **Function Buttons**: Layout and maximum number per loco?
3. **Speed Steps**: ✅ **128 speed steps** (JMRI/command station will downscale if needed)
4. **Encoder Behavior**: Linear or accelerated speed changes?
5. **Error Handling**: User feedback for connection errors, invalid operations?
6. **Configuration**: ✅ **WiFi configuration UI** (not hard-coded)
7. **Persistence**: Save last used locos to NVS?

### WiThrottle Protocol Details Needed
- ✅ Research protocol specification (to be done in Phase 3)
- Roster entry format and parsing
- Function label definitions
- Multi-throttle message format
- Heartbeat requirements
- Error/status codes

### MQTT Topics & Format
- What are the topic patterns for signals?
- Message payload format?
- QoS requirements?
- Retained messages?

### System Details
- **JMRI Version**: Latest (as of 2026)
- **Command Station**: Homebrew DCC-EX subset implementation
- **Test Environment**: ✅ Small test layout available for development
- **Initial Roster**: 2 locos defined for initial development

## Development Guidelines

### Code Standards
- **Language**: C++ (C++17 or later)
- **Naming Conventions**: 
  - Classes: PascalCase (e.g., `ThrottleController`)
  - Methods: camelCase (e.g., `assignKnob()`)
  - Members: m_camelCase (e.g., `m_currentSpeed`)
  - Constants: UPPER_SNAKE_CASE
- **File Organization**:
  - Header files: `.h` or `.hpp`
  - Implementation: `.cpp`
  - One class per file pair
  - Organized in `main/` subdirectories by layer

### Project Structure
```
main/
  ├── hardware/          # HAL classes
  ├── communication/     # Network protocols
  ├── model/            # Data models
  ├── controller/       # Business logic
  ├── ui/               # UI components
  ├── utils/            # Utilities and helpers
  └── main.cpp          # Application entry point
```

### Best Practices
- RAII for resource management
- Smart pointers where appropriate
- Const correctness
- Clear separation of concerns
- Event-driven architecture where possible
- Comprehensive error handling
- Logging for debugging (ESP_LOG macros)

### Testing Approach
- Unit tests for protocol parsing
- Hardware test modes
- Mock objects for network testing
- UI test screens (like current throttle test)

## Next Steps
1. Create `.copilot-instructions.md` for AI guidance
2. Begin C++ migration with core class definitions
3. Define interface contracts between layers
4. Identify external libraries needed (WiThrottle client library exists?)
5. Create hardware interface specifications

## Questions to Resolve
1. ~~**I2C Encoders**: What specific model/chip are you using?~~ ✅ **Adafruit I2C QT Rotary Encoder**
   - ~~**⚠️ Address conflict needs verification**~~ ✅ **Resolved with LTC4316 translator (0x76, 0x77)**
2. ~~**JMRI Setup**: What version? Any specific configuration?~~ ✅ **Latest version, 2 locos in roster**
3. ~~**Network**: Will WiFi credentials be hard-coded or configurable?~~ ✅ **Configuration UI**
4. ~~**DCC System**: What command station?~~ ✅ **Homebrew DCC-EX subset, max 50 locos**
5. ~~**Roster Size**: Typical number of locos?~~ ✅ **50 locos max**
6. ~~**Testing**: Do you have a test JMRI setup?~~ ✅ **Yes, test layout available**
7. ~~**Speed Steps**: 14, 28, or 128?~~ ✅ **128 speed steps**
8. **Function Buttons**: How many functions per loco? Layout preference?
9. **Encoder Behavior**: Linear speed control or acceleration curves?
10. **Persistence**: Should we save/restore last used locos on startup?
