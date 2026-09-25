# Track Power Flow

## Overview

Track power is controlled via the JMRI JSON WebSocket API, not the WiThrottle protocol. This allows named power manager targeting (e.g. `"DCC++"`).

---

## Sequence

```mermaid
sequenceDiagram
    participant User
    participant PSB as PowerStatusBar
    participant JC as JmriJsonClient
    participant JMRI as JMRI Server

    User->>PSB: Press power button
    PSB->>PSB: OFF → ON; ON or UNKNOWN → OFF
    PSB->>JC: setPower(true)

    JC->>JMRI: Power command
    Note over JMRI: state 2 = ON, state 4 = OFF

    JMRI-->>JC: Power state acknowledged
    JC->>PSB: PowerStateCallback("DCC++", ON)
    PSB->>PSB: Update button colour (green = ON)

    Note over PSB: Also shows JSON connection status label
```

## Power States

| JMRI JSON Value | Meaning | UI Display |
|-----------------|---------|------------|
| `2` | ON | Green button |
| `4` | OFF | Red/grey button |
| `0` | UNKNOWN | Amber/warning |

## What a press does

Only a known OFF turns power on. ON **and UNKNOWN** turn it off: the press may be a panic, and
energising rails nobody has reported on is the wrong way to fail (F-27). A second press, once
the state is known, turns it on.

## Connection Dependency

Under WiThrottle, power normally goes over the JMRI JSON WebSocket, to the configured power
manager. When that link is down:

- **OFF** still goes out, over WiThrottle's own `PPA0`, which switches off all power rather
  than the named district. That is the safe direction to widen (F-27).
- **ON** waits for the JSON link, so it keeps meaning the configured district only.
- The button shows WiThrottle's last `PPA` state when the JSON link has none.

For a stop that does not depend on power at all, use **E-STOP** on the main screen.

## Configured Power Manager

The power manager name (e.g. `"DCC++"`) is configurable via the JMRI settings screen and stored in NVS (`jmri`/`power_mgr`). This allows targeting a specific power district if the layout has multiple.
