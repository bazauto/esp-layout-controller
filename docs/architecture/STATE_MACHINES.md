# State Machines

## Overview

Two state machines drive the core interaction model: **Throttle** (allocation lifecycle) and **Knob** (encoder mode). They are tightly coupled — knob transitions often trigger throttle transitions and vice versa.

---

## Throttle State Machine

Each of the 4 throttles independently tracks its allocation state.

```mermaid
stateDiagram-v2
    [*] --> UNALLOCATED

    UNALLOCATED --> SELECTING : assignKnob(knobId)
    SELECTING --> UNALLOCATED : unassignKnob()\n(knob moved away)
    SELECTING --> ALLOCATED_WITH_KNOB : assignLocomotive(loco)\n(knob press confirms)

    ALLOCATED_WITH_KNOB --> ALLOCATED_NO_KNOB : unassignKnob()\n(knob moved to another throttle)
    ALLOCATED_WITH_KNOB --> UNALLOCATED : releaseLocomotive()\n(release button pressed)

    ALLOCATED_NO_KNOB --> ALLOCATED_WITH_KNOB : assignKnob(knobId)\n(knob re-assigned here)
    ALLOCATED_NO_KNOB --> UNALLOCATED : releaseLocomotive()\n(release button pressed)
```

### States

| State | Knob | Loco | User can... |
|-------|------|------|-------------|
| `UNALLOCATED` | None | None | Touch knob indicator to start selection |
| `SELECTING` | Assigned | None | Rotate to browse roster, press to acquire |
| `ALLOCATED_WITH_KNOB` | Assigned | Assigned | Rotate to control speed, press for e-stop |
| `ALLOCATED_NO_KNOB` | None | Assigned | Touch knob indicator to re-assign, press release button |

### Transition Details

| From | To | Trigger | Side Effects |
|------|----|---------|--------------|
| `UNALLOCATED` | `SELECTING` | `assignKnob(knobId)` | Knob → SELECTING, roster carousel shown |
| `SELECTING` | `UNALLOCATED` | `unassignKnob()` | Knob released, roster carousel hidden |
| `SELECTING` | `ALLOCATED_WITH_KNOB` | `assignLocomotive(loco)` | WiThrottle acquire sent, knob → CONTROLLING |
| `ALLOCATED_WITH_KNOB` | `ALLOCATED_NO_KNOB` | `unassignKnob()` | Knob freed for another throttle |
| `ALLOCATED_WITH_KNOB` | `UNALLOCATED` | `releaseLocomotive()` | WiThrottle release sent, knob → IDLE |
| `ALLOCATED_NO_KNOB` | `ALLOCATED_WITH_KNOB` | `assignKnob(knobId)` | Knob → CONTROLLING (no roster phase) |
| `ALLOCATED_NO_KNOB` | `UNALLOCATED` | `releaseLocomotive()` | WiThrottle release sent |

---

## Knob State Machine

Each of the 2 knobs independently tracks its current mode.

```mermaid
stateDiagram-v2
    [*] --> IDLE

    IDLE --> SELECTING : assignToThrottle(throttleId)\n(user touches indicator on UNALLOCATED throttle)

    SELECTING --> CONTROLLING : startControlling()\n(knob press acquires loco)
    SELECTING --> IDLE : release()\n(throttle released / knob moved)

    CONTROLLING --> CONTROLLING : reassignToThrottle(newId)\n(moved to ALLOCATED_NO_KNOB throttle)
    CONTROLLING --> IDLE : release()\n(throttle released)
```

### States

| State | Rotation does... | Press does... |
|-------|------------------|---------------|
| `IDLE` | Nothing | Nothing |
| `SELECTING` | Scrolls roster (wrapping) | Acquires displayed loco |
| `CONTROLLING` | Changes speed (±speedStepsPerClick) | Emergency stop |

### Interaction Between the Two Machines

```mermaid
sequenceDiagram
    participant User
    participant TC as ThrottleController
    participant K as Knob[0]
    participant T as Throttle[2]
    participant WT as WiThrottleClient

    Note over T: State: UNALLOCATED
    Note over K: State: IDLE

    User->>TC: onKnobIndicatorTouched(throttle=2, knob=0)
    TC->>K: assignToThrottle(2)
    Note over K: State: SELECTING
    TC->>T: assignKnob(0)
    Note over T: State: SELECTING

    User->>TC: onKnobRotation(knob=0, delta=+3)
    TC->>K: handleRotation(+3, rosterSize)
    Note over K: rosterIndex updated

    User->>TC: onKnobPress(knob=0)
    TC->>T: assignLocomotive(locoCopy)
    Note over T: State: ALLOCATED_WITH_KNOB
    TC->>K: startControlling()
    Note over K: State: CONTROLLING
    TC->>WT: acquireLocomotive("2", "L41", true)

    User->>TC: onKnobRotation(knob=0, delta=+5)
    TC->>T: setSpeed(20), setDirection(FORWARD)
    TC->>WT: setSpeed("2", 20)
```

---

## Speed Control Model

Speed is modelled as a **signed value** internally (-126 to +126), where:
- Positive = forward, negative = reverse
- Crossing zero automatically flips direction
- Clockwise rotation always increases, counter-clockwise decreases

```mermaid
flowchart LR
    A["Encoder delta\n(e.g. +3)"] --> B["× speedStepsPerClick\n(NVS, default 4)"]
    B --> C["Add to signed speed"]
    C --> D{"Crosses zero?"}
    D -->|Yes| E["Flip direction\nClamp to ±126"]
    D -->|No| F["Clamp to ±126"]
    E --> G["Throttle.setSpeed(abs)\nThrottle.setDirection()"]
    F --> G
    G --> H["WiThrottle:\nsetSpeed + setDirection"]
```
