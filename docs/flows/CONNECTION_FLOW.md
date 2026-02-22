# Connection Flow

## Overview

Three connections are established: WiFi, WiThrottle (TCP), and JMRI JSON (WebSocket). They have a dependency chain — each requires the previous to be up.

```mermaid
flowchart LR
    A["WiFi STA"] -->|"IP obtained"| B["WiThrottle\n(TCP :12090)"]
    B -->|"PW message\ndiscovers web port"| C["JMRI JSON\n(WS :12080)"]
```

---

## Full Connection Sequence

```mermaid
sequenceDiagram
    participant WC as WiFiController
    participant WM as WiFiManager
    participant JCC as JmriConnectionController
    participant WT as WiThrottleClient
    participant JMRI_WT as JMRI (WiThrottle)
    participant JC as JmriJsonClient
    participant JMRI_JSON as JMRI (JSON)

    Note over WC,WM: Phase 1: WiFi

    WC->>WM: autoConnect()
    WM->>WM: Load NVS (ssid, password)
    WM->>WM: esp_wifi_connect()
    WM-->>WC: StateCallback(CONNECTED, "192.168.1.50")

    Note over JCC,JMRI_JSON: Phase 2: JMRI (background task)

    JCC->>JCC: jmri_autoconn task: poll WiFi (30s max)
    JCC->>JCC: loadSettingsAndAutoConnect()
    JCC->>JCC: Read NVS (server_ip, wt_port, power_mgr)

    JCC->>WT: connect(serverIp, 12090)
    WT->>JMRI_WT: TCP connect
    WT->>JMRI_WT: HU<deviceId>, N<deviceName>
    JMRI_WT-->>WT: VN2.0 (version)
    JMRI_WT-->>WT: RL<count>]\[... (roster)
    WT-->>JCC: RosterCallback(locos)
    JMRI_WT-->>WT: PPA<state> (power)
    WT-->>JCC: PowerStateCallback(state)
    JMRI_WT-->>WT: PW12080 (web port)
    WT-->>JCC: WebPortCallback(12080)

    JCC->>JC: connect(serverIp, 12080)
    JC->>JMRI_JSON: WebSocket /json/
    JMRI_JSON-->>JC: {"type":"hello",...}
    JC->>JMRI_JSON: Subscribe to power updates
    JC-->>JCC: ConnectionStateCallback(CONNECTED)

    Note over JCC: Phase 3: Auto-reconnect

    JCC->>JCC: enableAutoReconnect(true)
    Note over JCC: jmri_reconnect task:\nmonitor every 5s,\nexponential backoff (5s→60s)
```

---

## Auto-Reconnect Behaviour

```mermaid
flowchart TD
    A["jmri_reconnect task\n(runs every 5s)"] --> B{"WiFi connected?"}
    B -->|No| C["Reset backoff\nWait 5s"]
    C --> A
    B -->|Yes| D{"WiThrottle connected?"}
    D -->|Yes| E{"JSON connected?"}
    D -->|No| F["Attempt WiThrottle connect"]
    F --> G{"Success?"}
    G -->|Yes| A
    G -->|No| H["Exponential backoff\n(5s → 10s → 20s → 40s → 60s cap)"]
    H --> A
    E -->|Yes| I["All connected ✓\nReset backoff"]
    I --> A
    E -->|No| J["Attempt JSON connect"]
    J --> A
```

## WiFi Config Screen

If WiFi credentials are not stored (first boot) or the user navigates to settings, the `WiFiConfigScreen` provides:
- Network scanning
- SSID/password entry with on-screen keyboard
- Connect/disconnect/forget actions
- Credentials saved to NVS on successful connection
