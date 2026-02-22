# Throttle Selection Flow

## Overview

A user assigns a physical knob to an unallocated throttle, browses the roster, and acquires a locomotive.

---

## Sequence

```mermaid
sequenceDiagram
    participant User
    participant TM as ThrottleMeter
    participant MS as MainScreen
    participant TC as ThrottleController
    participant K as Knob
    participant T as Throttle
    participant RC as RosterCarousel
    participant WT as WiThrottleClient
    participant JMRI as JMRI Server

    Note over T: UNALLOCATED
    Note over K: IDLE

    User->>TM: Touch "L" knob indicator
    TM->>MS: onKnobIndicatorTouched (LVGL event)
    MS->>TC: onKnobIndicatorTouched(throttleId=2, knobId=0)

    activate TC
    Note over TC: Lock m_stateMutex
    TC->>K: assignToThrottle(2)
    Note over K: → SELECTING
    TC->>T: assignKnob(0)
    Note over T: → SELECTING
    TC->>MS: uiUpdateCallback()
    Note over TC: Unlock m_stateMutex
    deactivate TC

    MS->>RC: update(throttleController)
    Note over RC: Shows first roster entry

    loop Roster browsing
        User->>TC: onKnobRotation(knob=0, delta)
        TC->>K: handleRotation(delta, rosterSize)
        Note over K: rosterIndex wraps around
        TC->>MS: uiUpdateCallback()
        MS->>RC: update() → shows new loco
    end

    User->>TC: onKnobPress(knob=0)
    activate TC
    Note over TC: Lock m_stateMutex

    TC->>TC: Get loco at rosterIndex
    TC->>T: assignLocomotive(locoCopy)
    Note over T: → ALLOCATED_WITH_KNOB
    TC->>K: startControlling()
    Note over K: → CONTROLLING

    TC->>WT: acquireLocomotive("2", "L41", true)
    WT->>JMRI: Acquire loco command

    TC->>MS: uiUpdateCallback()
    Note over TC: Unlock m_stateMutex
    deactivate TC

    JMRI-->>WT: Acquire confirmed
    JMRI-->>WT: Function labels received
    WT->>TC: onFunctionLabelsReceived(2, labels)
    TC->>T: clearFunctions() + addFunction() × n
    TC->>MS: uiUpdateCallback()

    Note over T: Ready for speed control
```

---

## Assigning a Knob to an Already-Allocated Throttle

When a throttle is in `ALLOCATED_NO_KNOB` (has a loco but no active knob), touching a knob indicator skips the roster phase:

```mermaid
sequenceDiagram
    participant User
    participant TC as ThrottleController
    participant K as Knob
    participant T as Throttle

    Note over T: ALLOCATED_NO_KNOB (has loco)
    Note over K: IDLE

    User->>TC: onKnobIndicatorTouched(throttleId, knobId)
    TC->>K: assignToThrottle(throttleId)
    TC->>K: startControlling()
    Note over K: → CONTROLLING (skips SELECTING)
    TC->>T: assignKnob(knobId)
    Note over T: → ALLOCATED_WITH_KNOB
    Note over T: Immediately ready for speed control
```

---

## Moving a Knob Between Throttles

```mermaid
sequenceDiagram
    participant User
    participant TC as ThrottleController
    participant K as Knob
    participant T1 as Throttle[old]
    participant T2 as Throttle[new]

    Note over T1: ALLOCATED_WITH_KNOB
    Note over T2: ALLOCATED_NO_KNOB
    Note over K: CONTROLLING (on T1)

    User->>TC: onKnobIndicatorTouched(newThrottleId, knobId)
    TC->>T1: unassignKnob()
    Note over T1: → ALLOCATED_NO_KNOB
    TC->>T2: assignKnob(knobId)
    Note over T2: → ALLOCATED_WITH_KNOB
    TC->>K: reassignToThrottle(newThrottleId, CONTROLLING)
    Note over K: Still CONTROLLING, new target
```
