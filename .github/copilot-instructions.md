# GitHub Copilot Instructions for ESP Layout Controller

## Project Context
This is an ESP32-S3 firmware project for a model railway control interface using LVGL for UI, WiThrottle protocol for loco control, and MQTT for cab signals. See PROJECT_OVERVIEW.md for complete project description.

## Code Style & Standards

### Language & Version
- **Primary Language**: C++ (C++17 or later)
- **Legacy Code**: Some C code exists, migrate to C++ as we work on files
- **Build System**: ESP-IDF CMake-based build

### Naming Conventions
```cpp
// Classes: PascalCase
class ThrottleController { };

// Methods/Functions: camelCase
void assignKnob(int knobId);

// Member variables: m_ prefix + camelCase
int m_currentSpeed;
Locomotive* m_assignedLoco;

// Constants: UPPER_SNAKE_CASE
const int MAX_THROTTLES = 4;

// Enums: PascalCase for type, UPPER_SNAKE_CASE for values
enum class ThrottleState {
    UNALLOCATED,
    SELECTING_LOCO,
    ALLOCATED
};
```

### File Organization
- **Header files**: `.h` or `.hpp` extensions
- **Implementation**: `.cpp` extensions
- **One class per file**: `ThrottleController.h` / `ThrottleController.cpp`
- **Directory structure**:
  ```
  main/
    ├── hardware/       # Hardware abstraction (encoders, I2C)
    ├── communication/  # WiThrottle, MQTT clients
    ├── model/          # Data models (Loco, Throttle, Roster)
    ├── controller/     # Business logic controllers
    ├── ui/             # LVGL UI components
    ├── utils/          # Helper functions, utilities
    └── main.cpp        # Application entry
  ```

### Header File Structure
```cpp
#pragma once

// System includes
#include <cstdint>
#include <memory>

// ESP-IDF includes
#include "esp_log.h"

// Third-party includes
#include "lvgl.h"

// Project includes
#include "Locomotive.h"

class ClassName {
public:
    // Public types and constants
    enum class State { };
    
    // Constructors/Destructors
    ClassName();
    ~ClassName();
    
    // Delete copy/move if not needed
    ClassName(const ClassName&) = delete;
    ClassName& operator=(const ClassName&) = delete;
    
    // Public methods
    void publicMethod();
    
private:
    // Private methods
    void privateMethod();
    
    // Member variables (m_ prefix)
    int m_memberVar;
    std::unique_ptr<Dependency> m_dependency;
};
```

### Class Design Principles
- **RAII**: Use constructors/destructors for resource management
- **Single Responsibility**: Each class has one clear purpose
- **Dependency Injection**: Pass dependencies via constructor
- **Smart Pointers**: Use `std::unique_ptr` and `std::shared_ptr` appropriately
- **Const Correctness**: Mark methods `const` when they don't modify state
- **Interface Segregation**: Small, focused interfaces

### ESP-IDF Specific
- **Logging**: Use ESP_LOG macros
  ```cpp
  static const char* TAG = "ThrottleController";
  ESP_LOGI(TAG, "Throttle %d assigned to loco %d", throttleId, locoAddr);
  ESP_LOGW(TAG, "Connection lost, attempting reconnect");
  ESP_LOGE(TAG, "Failed to parse roster: %s", error);
  ```
- **Tasks**: Use FreeRTOS tasks appropriately
  ```cpp
  xTaskCreate(taskFunction, "TaskName", 4096, nullptr, 5, &taskHandle);
  ```
- **Memory**: Be mindful of stack sizes, use heap judiciously
- **GPIO/I2C**: Use ESP-IDF driver APIs, not direct register access

### LVGL UI Guidelines
- **Object Lifecycle**: Track LVGL objects carefully, they're managed by LVGL
- **Callbacks**: Use `lv_obj_add_event_cb()` for event handling
- **User Data**: Store C++ object pointers via `lv_obj_set_user_data()`
- **Thread Safety**: LVGL calls must be from UI task or use locks
- **Styling**: Prefer consistent style approaches, consider reusable style functions

### Error Handling
- **Return Types**: Use `esp_err_t` for ESP-IDF style functions
- **Exceptions**: Generally avoid exceptions in embedded, use return codes
- **Validation**: Check parameters, especially at API boundaries
- **Recovery**: Design for graceful degradation on errors
- **Logging**: Always log errors with context

### Comments & Documentation
- **Header Comments**: Explain class purpose and responsibilities
  ```cpp
  /**
   * @brief Manages a single throttle instance including loco assignment,
   *        speed control, and knob assignment.
   */
  class Throttle {
  ```
- **Method Comments**: Document complex logic, not obvious code
- **Inline Comments**: Explain "why", not "what"
- **TODO Comments**: Format as `// TODO: Description` for tracking

## Architectural Patterns

### Separation of Concerns
- **Model**: Pure data and state (no UI, no I/O)
- **Controller**: Business logic, coordinates between layers
- **View**: UI presentation only, minimal logic
- **HAL**: Hardware abstraction, no business logic

### Event-Driven Design
- Use callbacks for asynchronous events (encoder rotation, network messages)
- Event structures for passing data
- Consider event queue for complex scenarios

### State Machines
- Use enums for states
- Clear transition functions
- Document state diagrams in comments for complex flows

## Testing & Development

### Test Screens
- Create separate test functions like `test_throttle_screen()`
- Keep test code separate from production code
- Use for visual and functional testing during development

### Debug Features
- Add temporary borders/colors for layout debugging (as we just did)
- Include debug logging that can be disabled for production
- Create test modes for hardware testing

### Incremental Development
- Build one feature at a time
- Test each component independently before integration
- Keep main branch working, use branches for major features

## Project-Specific Guidelines

### Throttle System
- **4 Throttles**: Always arrays of size 4, use constants not magic numbers
- **2 Knobs**: Manage assignment carefully, only one throttle per knob
- **State Machine**: Unallocated → Selecting → Allocated → Released
- **Touch Interaction**: Remember throttles are selected by touch for detail view

### WiThrottle Protocol
- **Async Communication**: Don't block UI on network operations
- **State Sync**: Remote updates must reflect in UI immediately
- **Heartbeat**: Maintain connection with regular heartbeats
- **Error Recovery**: Handle disconnects gracefully

### UI Responsiveness
- **No Blocking**: Network and I2C operations must not block UI
- **Immediate Feedback**: Touch actions should have instant visual response
- **Background Updates**: State changes update UI from tasks

### Memory Management
- **Limited RAM**: ESP32-S3 has constraints, be efficient
- **LVGL Buffers**: Already configured, don't change casually
- **String Handling**: Be careful with dynamic strings, consider fixed buffers
- **Roster Size**: May need to limit number of locos

## Communication Patterns

### Between Tasks
- Use queues for passing data between tasks
- Use event groups for synchronization
- Protect shared data with mutexes

### Callbacks
```cpp
// Encoder callback example
typedef void (*EncoderCallback)(int position, int delta);

class RotaryEncoder {
    void setCallback(EncoderCallback cb) { m_callback = cb; }
private:
    EncoderCallback m_callback;
};
```

### Observer Pattern
Consider observer pattern for state changes that multiple components need to know about:
```cpp
class ThrottleObserver {
public:
    virtual void onSpeedChanged(int throttleId, int speed) = 0;
    virtual void onLocoAssigned(int throttleId, const Locomotive& loco) = 0;
};
```

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
