# Controller Layer

## AppController

**File:** `main/controller/AppController.cpp/h`

### Purpose

Singleton (Meyer's pattern) that owns all shared services and manages screen lifecycle. Created once at startup; persists for the application lifetime.

### Owned Objects

| Object | Type | Purpose |
|--------|------|---------|
| `m_wifiController` | `unique_ptr<WiFiController>` | WiFi lifecycle |
| `m_wiThrottleClient` | `unique_ptr<WiThrottleClient>` | WiThrottle protocol |
| `m_throttleBackend` | `unique_ptr<ThrottleBackend>` | The transport in use — held as the port, not the concrete adapter |
| `m_orchestratorClient` | `unique_ptr<OrchestratorClient>` | Null unless the orchestrator is the selected transport |

### Transport selection

`initialise()` reads `TransportSettings` **before anything connects**, because the choice
decides which network stack comes up at all:

| Selected | Started | Not started |
|----------|---------|-------------|
| WiThrottle (default) | `WiThrottleBackend`, JMRI auto-connect | No orchestrator client is even constructed |
| Orchestrator | `OrchestratorClient` + `OrchestratorBackend`, `orch_connect` task | JMRI auto-connect never begins |

The backend is chosen once and never swapped on a live `ThrottleController` — doing so would
strand locos mid-command — so a transport change takes effect on restart.

---

## TransportSettings

**File:** `main/controller/TransportSettings.cpp/h`

Plain settings struct over NVS namespace `orch`: the transport choice plus the orchestrator's
host, port and `operator` credential. See
[NVS_STORAGE.md](../architecture/NVS_STORAGE.md) for the key map and the two deliberate
fallbacks to WiThrottle (an unrecognised stored value, and the orchestrator selected but not
configured).
| `m_jmriJsonClient` | `unique_ptr<JmriJsonClient>` | JSON WebSocket |
| `m_jmriConnectionController` | `unique_ptr<JmriConnectionController>` | Auto-connect + reconnect |
| `m_throttleController` | `unique_ptr<ThrottleController>` | Throttle/knob state |
| `m_encoderHal` | `unique_ptr<RotaryEncoderHal>` | Encoder hardware |
| `m_mainScreen` | `unique_ptr<MainScreen>` | Current main screen instance |
| `m_settingsScreen` | `unique_ptr<SettingsScreen>` | Device settings. Rebuilt on each open, because which status rows it shows depends on the selected transport |

### Key Methods

| Method | Description |
|--------|-------------|
| `instance()` | Static — returns singleton reference |
| `initialise()` | Creates and wires all objects (see [STARTUP_FLOW.md](../flows/STARTUP_FLOW.md)) |
| `showMainScreen()` | Deletes old MainScreen, creates new one with controller references |
| `showWiFiConfigScreen()` | Creates WiFiConfigScreen |
| `showJmriConfigScreen()` | Creates JmriConfigScreen |
| `autoConnectJmri()` | Triggers JMRI auto-connect |
| Getters | `getWiThrottleClient()`, `getJmriJsonClient()`, `getThrottleController()`, etc. |

### Encoder Wiring

During `initialise()`, the encoder callbacks are connected:
- Rotation → `ThrottleController::onKnobRotation(knobId, delta)`
- Press (down edge only) → `ThrottleController::onKnobPress(knobId)`

---

## ThrottleController

**File:** `main/controller/ThrottleController.cpp/h`

### Purpose

Central coordinator for the 4 throttles and 2 knobs. Implements all state machine logic, routes hardware input, sends network commands, and triggers UI updates.

Drives locos through the `ThrottleBackend` port (see the communication layer) and holds no
reference to any concrete client, so neither transport's wire format reaches this layer.
Throttle ids crossing the port are plain `int` indices.

### Owned Objects

| Object | Count | Type |
|--------|-------|------|
| Throttles | 4 | `vector<unique_ptr<Throttle>>` |
| Knobs | 2 | `vector<unique_ptr<Knob>>` |

The backend is **not** owned — it is injected and must outlive the controller. `AppController`
guarantees that by declaration order.

### Constants

| Constant | Value |
|----------|-------|
| `NUM_THROTTLES` | 4 |
| `NUM_KNOBS` | 2 |

### Snapshot Types

Used for thread-safe UI reads without holding the state mutex:

| Type | Fields |
|------|--------|
| `ThrottleSnapshot` | throttleId, state, assignedKnob, speed, direction, locoName, locoAddress |
| `RosterSelectionSnapshot` | active, knobId, throttleId, rosterIndex, locoName, locoAddress |

### API — Input Handling

| Method | Called by | Description |
|--------|----------|-------------|
| `onKnobIndicatorTouched(throttleId, knobId)` | MainScreen | Touch event on knob indicator |
| `onKnobRotation(knobId, delta)` | RotaryEncoderHal / VirtualEncoderPanel | Encoder rotation |
| `onKnobPress(knobId)` | RotaryEncoderHal / VirtualEncoderPanel | Encoder button press |
| `onThrottleRelease(throttleId)` | MainScreen | Release button press |
| `onThrottleFunctions(throttleId)` | MainScreen | Functions button press |

### API — State Queries

| Method | Returns |
|--------|---------|
| `getThrottle(id)` | `Throttle*` (raw pointer) |
| `getKnob(id)` | `Knob*` (raw pointer) |
| `getRosterSize()` | `int` |
| `getLocoAtRosterIndex(idx, outName, outAddr)` | `bool` |
| `getThrottleSnapshot(id, out)` | `bool` (thread-safe) |
| `getRosterSelectionSnapshot(out)` | `bool` (thread-safe) |
| `getFunctionsSnapshot(id, out)` | `bool` (thread-safe) |

### UI Update Callback

```cpp
void setUIUpdateCallback(void (*callback)(void*), void* userData);
```

Fired after any state change. `MainScreen` registers this and calls `updateAllThrottles()` inside an LVGL lock.

### Thread Safety

All public methods acquire `m_stateMutex` before accessing throttle/knob state. The LVGL port lock is **not** acquired inside `ThrottleController` — that's the UI's responsibility.

### Polling Task

`throttle_poll` (a FreeRTOS task, 4 KB, priority 3 — not an `esp_timer`) wakes every 10 s and
calls `pollThrottleStates()`, which calls `refreshThrottleState()` on the backend for each
allocated throttle. Under WiThrottle that becomes a `qV`/`qR` pair.

The task is created only when the backend reports `requiresPolling()`. A transport that pushes
state changes gets no task and never allocates its stack.

---

## WiFiController

**File:** `main/controller/WiFiController.cpp/h`

### Purpose

Thin wrapper owning the `WiFiManager` lifecycle and providing auto-connect-on-boot behaviour.

### API

| Method | Description |
|--------|-------------|
| `initialize()` | Creates and initialises `WiFiManager` |
| `autoConnect()` | Load NVS creds, attempt connection |
| `isConnected()` | Check WiFi state |
| `getManager()` | Return `WiFiManager*` for config screen |

---

## JmriConnectionController

**File:** `main/controller/JmriConnectionController.cpp/h`

### Purpose

Manages JMRI connection persistence (NVS settings) and automatic reconnection with exponential backoff.

### Constructor

```cpp
JmriConnectionController(JmriJsonClient* json, WiThrottleClient* wt, WiFiController* wifi)
```

### NVS Settings (namespace: `jmri`)

| Key | Default | Description |
|-----|---------|-------------|
| `server_ip` | — | JMRI server IP address |
| `wt_port` | `"12090"` | WiThrottle TCP port |
| `json_port` | `"12080"` | JSON WebSocket port |
| `power_mgr` | `"DCC++"` | Track power manager name |

### Background Tasks

| Task | Stack | Purpose |
|------|-------|---------|
| `jmri_autoconn` | 4 KB | Wait for WiFi (up to 30 s) → load NVS → connect both clients |
| `jmri_reconnect` | 3 KB | Monitor every 5 s, exponential backoff (5 s → 60 s cap) |

### Key Methods

| Method | Description |
|--------|-------------|
| `loadSettingsAndAutoConnect()` | Read NVS, connect WiThrottle + JSON |
| `startAutoConnectTask()` | Spawn background auto-connect task |
| `enableAutoReconnect(bool)` | Start/stop reconnect monitoring task |
