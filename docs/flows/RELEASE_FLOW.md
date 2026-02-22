# Release Flow

## Overview

Releasing a locomotive disconnects it from the throttle, frees any assigned knob, and sends a release command to JMRI.

---

## Sequence

```mermaid
sequenceDiagram
    participant User
    participant TM as ThrottleMeter
    participant MS as MainScreen
    participant TC as ThrottleController
    participant T as Throttle
    participant K as Knob
    participant WT as WiThrottleClient
    participant JMRI as JMRI Server

    Note over T: ALLOCATED_WITH_KNOB or ALLOCATED_NO_KNOB
    
    User->>TM: Press "Release" button
    TM->>MS: onReleaseButtonClicked (LVGL event)
    MS->>TC: onThrottleRelease(throttleId)

    activate TC
    Note over TC: Lock m_stateMutex

    opt Knob assigned
        TC->>K: release()
        Note over K: → IDLE
    end

    TC->>T: releaseLocomotive()
    Note over T: → UNALLOCATED
    Note over T: Locomotive unique_ptr destroyed

    TC->>WT: releaseLocomotive("2")
    WT->>JMRI: Release command

    TC->>MS: uiUpdateCallback()
    Note over TC: Unlock m_stateMutex
    deactivate TC

    Note over MS: ThrottleMeter shows empty state
    Note over MS: RosterCarousel hidden (no SELECTING knob)
```

## Post-Release State

| Object | State after release |
|--------|-------------------|
| Throttle | `UNALLOCATED` — no loco, no knob |
| Knob (if was assigned) | `IDLE` — ready to assign to any throttle |
| Locomotive | Destroyed (unique_ptr released) |
| WiThrottle | Loco released in JMRI — other throttles can acquire it |
| UI | Throttle meter shows blank/idle, knob indicator deselected |
