# GitHub Copilot Instructions for ESP Layout Controller

## Project Context
ESP32-S3 model railway control interface: LVGL UI, WiThrottle protocol, 4 throttles, 2 rotary encoders.
See `docs/PROJECT_OVERVIEW.md` for complete description.

## Code Style

### Language & Naming
- **C++ (C++17)**: All application code
- **Classes**: PascalCase (`ThrottleController`)
- **Methods**: camelCase (`assignKnob`)
- **Members**: `m_` prefix (`m_currentSpeed`)
- **Constants**: UPPER_SNAKE_CASE (`MAX_THROTTLES`)
- **Enums**: PascalCase type, UPPER values (`ThrottleState::ALLOCATED`)

### File Organization
```
main/
  ├── hardware/       # HAL (encoders, I2C)
  ├── communication/  # WiThrottle, MQTT
  ├── model/          # Data (Loco, Throttle, Knob)
  ├── controller/     # Business logic
  └── ui/             # LVGL screens
```

### Class Structure
- One class per file
- RAII for resources
- Delete copy/move if not needed
- Dependency injection via constructor
- Const correctness

## Architecture

### ⚠️ CRITICAL: Layered State Management
**State lives at APPLICATION LAYER, NOT in UI!**

```
Application Layer (main_screen_wrapper.cpp)
  ├─> WiThrottleClient (singleton)
  ├─> JmriJsonClient (singleton)  
  └─> ThrottleController (singleton) ← OWNS ALL STATE
       └─> Models (Throttle, Knob, Locomotive)

UI Layer (MainScreen)
  └─> Raw pointers only, NEVER owns state
```

**Pattern:**
```cpp
// Application layer - owns state
static ThrottleController* g_throttleController = nullptr;

// UI layer - just references
class MainScreen {
    ThrottleController* m_throttleController;  // Not owned!
};

// UI can be deleted/recreated without state loss
delete g_mainScreen;
g_mainScreen = new MainScreen();
g_mainScreen->create(..., g_throttleController);
```

### ⚠️ CRITICAL: LVGL Thread Safety
**LVGL is NOT thread-safe!** Lock before UI access from network tasks:

```cpp
// From WiThrottle/network task:
void callback() {
    if (lvgl_port_lock(100)) {  // 100ms timeout
        lv_label_set_text(label, "Update");
        lvgl_port_unlock();
    }
}
```

**When locks required:**
- ✅ Network tasks (WiThrottle, MQTT, WebSocket)
- ✅ FreeRTOS timers
- ✅ Hardware interrupts
- ❌ LVGL event callbacks (already on UI task)

**Timeouts:**
- 100ms: Frequent updates (can skip if busy)
- -1: Critical updates (must succeed)

## ESP-IDF Specifics
- **Logging**: ESP_LOG macros with `static const char* TAG`
- **Tasks**: `xTaskCreate()` with appropriate stack sizes
- **Memory**: Mind stack limits, use heap judiciously
- **Drivers**: Use ESP-IDF APIs, not direct register access

## Project Specifics
- **4 Throttles, 2 Knobs**: Use constants, not magic numbers
- **State Machines**: Document with enums and comments
- **WiThrottle**: Async, don't block UI
- **UI Responsiveness**: Immediate visual feedback, no blocking

## Development
- Ask if requirements unclear
- Test incrementally
- Document architecture changes
- Be honest about uncertainties
- Point out code quality issues

Current Phase: Core functionality complete, testing with virtual encoders.


## Working with Me (AI Assistant)

### When Making Changes
- **Ask First**: If requirements are unclear, ask before implementing
- **Show Context**: I'll read relevant files before making changes
- **Incremental Steps**: We'll build features step by step
- **Explain Decisions**: I'll explain architectural choices

### What to Provide
- **Specific Models/Parts**: Tell me exact hardware parts (encoder chips, etc.)
- **Protocol Details**: Share WiThrottle message formats if you have them
- **Preferences**: Tell me your preferences on design decisions
- **Constraints**: Let me know any specific limitations

### Testing Approach
- After significant changes, we'll test before moving on
- I'll help create test code for new features
- We'll use debug outputs and visual feedback for testing

## Current Phase
We are at the beginning of Phase 2: Planning complete, starting C++ migration and class structure definition.

## Next Immediate Steps
1. Answer questions in PROJECT_OVERVIEW.md
2. Create base class structure
3. Migrate existing code to C++
4. Implement rotary encoder driver
5. Begin WiThrottle client

## Remember
- **Ask questions** when details are unclear
- **Keep it simple** - don't over-engineer
- **Test incrementally** - small working steps
- **Document as we go** - update docs when architecture changes
- **Maintain consistency** - follow these guidelines throughout
- **Focus on reliability** - embedded systems need to be robust
- **Prioritize user experience** - UI must be responsive and intuitive
- **Optimize for constraints** - be mindful of ESP32-S3 limitations
- **Honesty about uncertainties** - if unsure, let's discuss before proceeding, be brutally honest about what you don't know
- **Honest reviews** - if code doesn't meet standards, point it out clearly, suggest improvements and when I ask for changes, be honest about feasibility and implications
