# Throttle UI & Control Implementation Plan

## Current Status: Phases 1, 2, 3, 4, 5, 6 Complete! 🎉
**Last Updated:** January 21, 2026

✅ **Completed:**
- Phase 1: Core State Management (Knob & Throttle state machines)
- Phase 2: Throttle Meter UI Updates (indicators, buttons, visual feedback)
- Phase 4: Virtual Knobs for Testing (VirtualEncoderPanel - compact single row!)
- Phase 6: Throttle Change Notifications (proper source of truth pattern)
- ThrottleController coordination layer fully implemented
- Speed control now works correctly via notifications!

🔄 **Next Up:**
- Phase 7: Hardware Integration (rotary encoders)

---

## Overview
Implementation plan for the complete throttle control system with 4 throttles, 2 physical rotary encoders (knobs), and touchscreen interface.

---

## State Machines

### Knob States
```
IDLE → (touch throttle indicator) → SELECTING
SELECTING → (press knob) → CONTROLLING
CONTROLLING → (touch different throttle) → CONTROLLING (different throttle)
CONTROLLING → (release on screen) → IDLE
```

- **IDLE**: Not assigned to any throttle. Rotate/press = no action
- **SELECTING**: Assigned to throttle, rotating scrolls roster
- **CONTROLLING**: Assigned to throttle with loco, rotating controls speed

### Throttle States
```
UNALLOCATED → (assign knob) → SELECTING
SELECTING → (press knob to acquire) → ALLOCATED_WITH_KNOB
ALLOCATED_WITH_KNOB → (move knob away) → ALLOCATED_NO_KNOB
ALLOCATED_WITH_KNOB → (release loco) → UNALLOCATED
ALLOCATED_NO_KNOB → (assign knob) → ALLOCATED_WITH_KNOB
ALLOCATED_NO_KNOB → (release loco) → UNALLOCATED
```

- **UNALLOCATED**: No loco, no knob assigned
- **SELECTING**: Knob assigned, scrolling roster to pick loco
- **ALLOCATED_WITH_KNOB**: Has loco, knob actively controlling
- **ALLOCATED_NO_KNOB**: Has loco, but knob moved elsewhere or released

---

## User Interactions

### 1. Assign Knob to Select Loco
**Starting from UNALLOCATED throttle:**
- User touches L or R knob indicator on throttle meter
- Throttle → SELECTING
- Knob → SELECTING
- Right side overlay shows roster carousel
- User rotates knob: carousel slides left/right through roster
- User presses knob: acquire currently displayed loco
- Send WiThrottle acquire command
- Throttle → ALLOCATED_WITH_KNOB
- Knob → CONTROLLING
- Close roster overlay

### 2. Assign Knob to Existing Loco
**Starting from ALLOCATED_NO_KNOB throttle:**
- User touches L or R indicator (only if that knob is currently IDLE)
- Knob → CONTROLLING
- Throttle → ALLOCATED_WITH_KNOB
- No roster selection needed

### 3. Move Knob Between Throttles
**Pre-condition: Knob is CONTROLLING one throttle, user touches indicator on different ALLOCATED_NO_KNOB throttle:**
- Previous throttle: ALLOCATED_WITH_KNOB → ALLOCATED_NO_KNOB
- New throttle: ALLOCATED_NO_KNOB → ALLOCATED_WITH_KNOB
- Knob stays CONTROLLING, just changes target throttle
- **Cannot steal from ALLOCATED_WITH_KNOB throttle (other knob active)**

### 4. Control Speed While CONTROLLING
- Rotate knob: change loco speed (0-126)
- Press knob: emergency stop (set speed to 0, stay ALLOCATED_WITH_KNOB)
- Visual: throttle meter updates in real-time

### 5. Release Loco
- Press "Release" button on throttle meter (touchscreen)
- Send WiThrottle release command
- Release loco in JMRI
- Throttle → UNALLOCATED
- If knob was assigned to this throttle → IDLE

### 6. Access Function Panel
- Press "Functions" button on throttle meter
- Open function panel overlay on right side
- Show grid of function buttons (based on loco function data)
- Button press: send F1<n> (state ON)
- Button release: send F0<n> (state OFF)
- Close button returns to normal view
- Function state updates come asynchronously via throttle change notifications

---

## Visual Layout

### Left Side: 4 Throttle Meters (2x2 Grid)
Each throttle meter displays:
- Loco name/address (when allocated)
- Current speed
- Current direction (F/R indicator)
- **L and R knob indicators** (touch zones)
  - Active knob: highlighted/colored
  - Available knob: normal appearance, touchable
  - Unavailable knob (other knob active): grayed out, disabled
- "Functions" button (when allocated)
- "Release" button (when allocated)

### Right Side: Overlays + Controls
**Three layers:**

1. **Roster Selection Overlay** (when SELECTING)
   - Carousel view showing one loco at a time
   - Loco name and address (no address type displayed)
   - Slide animation left/right as knob rotates
   - Centered display of current selection

2. **Function Panel Overlay** (when Functions button pressed)
   - Grid of function buttons (scrollable if needed)
   - Each button shows:
     - Function number (F0, F1, F2...)
     - Function label/name (from WiThrottle data)
     - Current state (on/off indicator)
   - Press & hold for momentary
   - Press & release updates state
   - Close button to exit

3. **Virtual Knobs** (testing only, hidden when real hardware available)
   - Two knob simulators (Knob 1 and Knob 2)
   - Each with: CCW button, Press button, CW button
   - Simple, minimal design
   - Located below overlays or at bottom

4. **Track Power Controls** (always visible at top)
   - Current implementation stays

---

## WiThrottle Protocol Details

### Acquire Loco
**Send:**
```
M0+S3<;>S3
```
(Using throttle IDs 0-3 instead of letters)

**Receive (multi-line response):**
```
M0+L41<;>
M0LL41<;>]\[Headlight]\[Bell]\[Whistle]\[Short Whistle]\[Steam Release]\[FX5 Light]\[FX6 Light]\[Dimmer]\[Mute]\[Water Stop]\[Injectors]\[Brake Squeal]\[Coupler]\[]\[]\[]\[]\[]\[]\[]\[]\[]\[]\[]\[]\[]\[]\[]\[
M0AL41<;>F00
M0AL41<;>F01
M0AL41<;>F02
...
M0AL41<;>F027
M0AL41<;>F028
M0AL41<;>V0
M0AL41<;>R1
M0AL41<;>s1
```

Response contains:
- **M0+L41<;>**: Acknowledge acquire
- **M0LL41<;>]\[labels...]**: Function labels separated by ]\[ delimiters
  - Empty labels (]\[]\[) indicate functions without names
- **M0AL41<;>F0<state>**: Function 0 initial state (0=off, 1=on)
- **M0AL41<;>F1<state>**: Function 1 initial state
- ... (one line per function)
- **M0AL41<;>V0**: Current speed (0)
- **M0AL41<;>R1**: Current direction (1=forward, 0=reverse)
- **M0AL41<;>s1**: Speed steps (1=128 steps, 0=28 steps)

### Speed Control
**Send:**
```
M0AS3<;>V50
```
Set loco S3 on throttle 0 to speed 50

### Direction Control
**Send:**
```
M0AS3<;>R1   (forward)
M0AS3<;>R0   (reverse)
```

### Function Control
**Press button (activate):**
```
M0AS3<;>F10
```

**Release button (deactivate):**
```
M0AS3<;>F00
```

### Throttle Change Notifications
**Receive asynchronously:**
```
M0AS3<;>V50        (speed changed to 50)
M0AS3<;>R1         (direction changed to forward)
M0AS3<;>F15        (function 1 turned on)
M0AS3<;>F00        (function 0 turned off)
```

These come from:
- Other WiThrottle clients controlling same loco
- JMRI operations
- Server-side automation
- **Initial throttle state after acquire** (speed, direction, function states)
- **Confirmation of our own commands** (what actually happened vs what we requested)

**Action:** Parse and update stored state, refresh UI

**Critical Design Note:** Throttle change notifications are the **source of truth**. Even when we send a command (e.g., set speed to 50), we should wait for the corresponding notification to update our UI state. This confirms the action was actually executed by JMRI and handles cases where:
- Command was rejected or modified by JMRI
- Another client changed the state simultaneously
- Speed curves or other JMRI logic modified the value
- Network delays or dropped packets

**Implementation Pattern:**
1. User rotates knob → Send speed command to WiThrottle
2. Optimistically update UI (optional feedback)
3. Receive notification → Update stored state and UI definitively
4. If notification differs from expectation, UI shows actual state

---

## Data Model Updates

### Knob Model (New)
```cpp
enum class KnobState {
    IDLE,
    SELECTING,
    CONTROLLING
};

class Knob {
    int m_id;                    // 0 or 1 (left/right)
    KnobState m_state;
    int m_assignedThrottleId;    // -1 if not assigned
    int m_rosterIndex;           // Current position when SELECTING
};
```

### Throttle Model (Update)
```cpp
enum class ThrottleState {
    UNALLOCATED,
    SELECTING,
    ALLOCATED_WITH_KNOB,
    ALLOCATED_NO_KNOB
};

class Throttle {
    int m_id;                     // 0-3
    ThrottleState m_state;
    int m_assignedKnobId;         // -1, 0, or 1
    
    // Loco data (when allocated)
    Locomotive* m_loco;
    int m_currentSpeed;           // 0-126
    bool m_direction;             // true=forward
    
    // Function data
    std::vector<Function> m_functions;
};

struct Function {
    int number;                   // 0-28
    std::string label;            // "Lights", "Horn", etc.
    bool state;                   // Current on/off state
    // Note: momentary vs toggle determined by user behavior
};
```

### WiThrottleClient Updates
- Parse acquire response for function list and initial states
- Parse throttle change notifications
- Store function metadata
- Provide callbacks for state changes

---

## Implementation Phases

### Phase 1: Core State Management ✅ **COMPLETE**
1. ✅ Create Knob class with state machine
2. ✅ Update Throttle model with new states
3. ✅ Add knob assignment tracking to Throttle
4. ✅ Implement state transition logic
5. ✅ Add function data storage to Throttle

### Phase 2: Throttle Meter UI Updates ✅ **COMPLETE**
1. ✅ Add L/R knob indicator touch zones
2. ✅ Visual feedback for active/inactive/available states (Green/Blue/Gray)
3. ✅ Add Functions button (when allocated)
4. ✅ Add Release button (when allocated)
5. ✅ Update display to show knob assignment
6. ✅ Wire up event handlers to ThrottleController

### Phase 3: Roster Selection Overlay ✅ **COMPLETE**
1. Create carousel container on right side
2. Implement slide animation for roster scrolling
3. Hook up to knob rotation events
4. Display current selection (name, address, type)
5. Acquire on press

### Phase 4: Virtual Knobs (Testing) ✅ **COMPLETE**
1. ✅ Create simple knob simulator UI (VirtualEncoderPanel)
2. ✅ Two knobs with CCW/Press/CW buttons
3. ✅ Wire to same event handlers as real hardware will use
4. [ ] Add compile-time flag to hide when hardware ready

### Phase 5: Function Panel ✅ **COMPLETE**
1. Parse function data from WiThrottle acquire response
2. Create function button grid overlay
3. Implement press/release for momentary control
4. Send F1<n> on press, F0<n> on release
5. Show current state (on/off indicators)
6. Close button to exit

### Phase 6: Throttle Change Notifications ✅ **COMPLETE**
1. ✅ Parse async throttle state messages from WiThrottle
2. ✅ Update speed/direction/function states from notifications
3. ✅ Refresh UI when changes received
4. ✅ Handle JMRI quirks with optimistic updates + polling
   - Optimistic local state update on speed/direction commands
   - 10-second polling of speed (qV) and direction (qR) for sync
   - Notification handler kept for functions and opportunistic speed updates

### Phase 7: Hardware Integration
1. Implement rotary encoder I2C driver
2. Map encoder events to knob state machine
3. Remove/hide virtual knobs
4. Test with real hardware

---

## Technical Considerations

### Touch Zone Hit Testing
- Throttle meters need clear L/R indicator zones
- Hit testing for small targets on 800x480 screen
- Visual feedback on touch (highlight)

### State Synchronization
- Local state (what we think)
- WiThrottle state (what JMRI thinks)
- Handle async updates gracefully
- Don't fight with other controllers

### Roster Carousel Animation
- Smooth sliding transitions
- Wrapping (end → start)
- Responsive to rapid rotation
- Current selection always centered

### Function Button Layout
- Dynamic grid based on available functions
- Scrollable if many functions (F0-F28 = 29 buttons)
- Clear labeling
- On/off state indication

### Memory Management
- Throttle/Knob objects persistent
- Function data allocated per throttle
- Roster data shared reference (don't duplicate)
- Clean up on release

---

## Testing Strategy

### Unit Testing
- Knob state machine transitions
- Throttle state machine transitions
- Edge cases (steal attempts, simultaneous touches)

### Integration Testing
- Knob → Throttle interactions
- WiThrottle command generation
- Response parsing
- UI updates on state changes

### Manual Testing with Virtual Knobs
- All state transitions
- Roster scrolling and selection
- Speed control
- Function panel
- Release and reassignment
- Multi-throttle scenarios

### Hardware Testing (when available)
- Real encoder rotation
- Button press detection
- Speed control feel
- Emergency stop response

---

## Open Questions / Future Enhancements

1. **Momentum/Acceleration:** Speed changes are immediate (momentum handled by JMRI)
2. **Consist Support:** Future feature
3. **Route/Turnout Control:** Future consideration
4. **Persistent State:** Not implemented
5. **Function Macros:** Not implemented
6. **Feedback:** No haptic feedback (hardware limitation)

---

## Success Criteria

- ✅ 2 knobs can independently control any of 4 throttles
- ✅ Smooth roster selection with carousel
- ✅ Intuitive knob assignment via touch
- ✅ Speed control responsive to rotation
- ✅ Emergency stop on press
- ✅ Function panel works for all functions
- ✅ State stays synchronized with JMRI
- ✅ Clean transitions between states
- ✅ No conflicts or locked states
- ✅ Ready for hardware swap (virtual → real encoders)

---

**Document Version:** 1.0  
**Last Updated:** January 21, 2026  
**Status:** Design Complete - Ready for Implementation
