# Speed Control Flow

## Overview

When a knob is in `CONTROLLING` state, encoder rotation changes the locomotive's speed. The system uses **optimistic updates** — the local model is updated immediately for responsive UI, then the command is sent to JMRI. A periodic poll (every 10 s) reconciles any drift.

---

## Sequence

```mermaid
sequenceDiagram
    participant Enc as Encoder (HW)
    participant RE as RotaryEncoderHal
    participant TC as ThrottleController
    participant T as Throttle
    participant K as Knob
    participant WT as WiThrottleClient
    participant JMRI as JMRI Server
    participant MS as MainScreen

    Enc->>RE: I2C delta register read
    RE->>TC: rotationCallback(knobId=0, delta=+3)

    activate TC
    Note over TC: Lock m_stateMutex
    TC->>K: Check state == CONTROLLING
    TC->>TC: signedSpeed calculation
    TC->>T: setSpeed(abs(signedSpeed))
    TC->>T: setDirection(signedSpeed > 0)
    Note over T: Optimistic local update

    TC->>WT: setSpeed("0", 50)
    WT->>JMRI: Send speed command

    opt Direction changed
        TC->>WT: setDirection("0", true)
        WT->>JMRI: Send direction command
    end

    TC->>MS: uiUpdateCallback()
    Note over TC: Unlock m_stateMutex
    deactivate TC

    Note over MS: lvgl_port_lock(200)
    MS->>MS: updateAllThrottles()
    Note over MS: ThrottleMeter needle moves
    Note over MS: lvgl_port_unlock()

    Note over TC,JMRI: Confirmation (async)
    JMRI-->>WT: Acknowledgement received
    WT->>TC: onThrottleStateChanged(update)
    TC->>T: setSpeed(50)
    Note over T: Confirmed by server
```

---

## Speed Model Detail

```mermaid
flowchart TD
    A["Encoder delta (e.g. +3)"] --> B["× speedStepsPerClick\n(default 4, range 1–20)"]
    B --> C["signedSpeed += result\n(e.g. +12)"]
    C --> D{"signedSpeed\ncrosses zero?"}
    D -->|"Yes (was +5, now -7)"| E["Direction flips\nspeed = abs(-7) = 7"]
    D -->|"No"| F["Direction unchanged\nspeed = abs(signedSpeed)"]
    E --> G["Clamp 0–126"]
    F --> G
    G --> H["Throttle.setSpeed()\nThrottle.setDirection()"]
    H --> I["WiThrottle: V<speed>, R<dir>"]
```

## Emergency Stop

Pressing the encoder button while `CONTROLLING` triggers an immediate stop:

```mermaid
sequenceDiagram
    participant User
    participant TC as ThrottleController
    participant T as Throttle
    participant WT as WiThrottleClient

    User->>TC: onKnobPress(knobId)
    Note over TC: Knob state == CONTROLLING
    TC->>T: setSpeed(0)
    TC->>WT: setSpeed(throttleId, 0)
    Note over T: Speed = 0, stays ALLOCATED_WITH_KNOB
```

---

## Periodic Polling (Resilience)

An `esp_timer` fires every 10 seconds and queries JMRI for the true speed/direction of all allocated throttles. This catches cases where another controller changed the loco's state, or where a command was lost.

```mermaid
sequenceDiagram
    participant Timer as esp_timer
    participant TC as ThrottleController
    participant WT as WiThrottleClient
    participant JMRI as JMRI Server

    Timer->>TC: pollThrottleStates()
    loop For each ALLOCATED throttle
        TC->>WT: querySpeed(throttleId)
        TC->>WT: queryDirection(throttleId)
    end
    WT->>JMRI: Send qV and qR queries
    JMRI-->>WT: Response with speed and direction
    WT->>TC: onThrottleStateChanged(updates)
    TC->>TC: Update model if different
```
