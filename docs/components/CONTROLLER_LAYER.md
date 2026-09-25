# Controller Layer

## AppController

**File:** `main/controller/AppController.cpp/h`

### Purpose

Singleton (Meyer's pattern) that owns all shared services and manages screen lifecycle. Created once at startup; persists for the application lifetime.

### Owned Objects

| Object | Type | Purpose |
|--------|------|---------|
| `m_settingsWriter` | `unique_ptr<SettingsWriter>` | Every NVS write the UI asks for, off the LVGL task. Created first |
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
| Orchestrator | `OrchestratorClient` + `OrchestratorBackend`, `orch_connect` supervisor task | JMRI auto-connect never begins |

The backend is chosen once and never swapped on a live `ThrottleController` — doing so would
strand locos mid-command — so a transport change takes effect on restart.

---

## SettingsWriter

**File:** `main/controller/SettingsWriter.cpp/h`

Carries out the UI's NVS writes on its own task, `settings_writer`, in the order they were
asked for (F-39). An LVGL event handler must not block (F-05), and an NVS write can block
when NVS has to erase a page first.

It is one task and a queue, not a task per write. Two saves of one setting made in quick
succession then land in the order they were made. For the same reason, work that must follow
a save goes onto the same queue. The orchestrator screen's Connect, for example, posts the
supervisor's wake-up after its save, so the login reads what was typed.

| Caller | What it posts |
|--------|---------------|
| `SettingsScreen` | Speed steps per click; the transport choice (as a read-modify-write, on the writer) |
| `OrchestratorConfigScreen` | Host, port and credential (read-modify-write); Connect's wake-up |
| `WiFiConfigScreen` | Forget |
| `JmriConnectionController` | The JMRI settings, when the orchestrator is the transport |

`post()` runs the work on the caller's task, with a warning, if the queue is full or the
writer never started. A stalled frame is better than a lost save.

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
| `m_mainScreen` | `unique_ptr<MainScreen>` | The main screen. Built on first show and kept for the life of the app: rebuilding it was a use-after-free (F-21) |
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

The function and its `userData` are held together in a `CallbackSlot` (`main/utils/`), set on
the LVGL task and invoked from network and encoder tasks. The slot copies under its own lock
and calls the copy with the lock released, so a caller never sees half of a pair being
replaced (F-30). Every client callback slot works the same way.

### Thread Safety

All public methods acquire `m_stateMutex` before accessing throttle/knob state. The LVGL port lock is **not** acquired inside `ThrottleController` — that's the UI's responsibility.

The order is `lvgl_port_lock`, then `m_stateMutex`: the UI reads the controller while holding the LVGL lock, so the controller always releases its mutex before it sends a command or calls the UI (F-29).

Knob input is refused in the controller while the backend reports the link down, because the physical encoders call `onKnobRotation` / `onKnobPress` directly (F-25). A speed or stop command that fails is rolled back in the model, unless a transport report has landed since.

A transport report is applied only if it names the loco the throttle now holds. A late reply
about the previous loco would otherwise set the new one's baseline. Speeds above 126 and
function numbers above 28 are dropped too (F-37).

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
JmriConnectionController(JmriJsonClient* json, WiThrottleClient* wt, WiFiController* wifi,
                         SettingsWriter* writer)
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
| `jmri_conn` | 6 KB | Every JMRI connect and disconnect: waits for WiFi indefinitely, connects both clients, reconnects with backoff (5 s → 60 s cap), carries out the config screen's requests |

### Key Methods

| Method | Description |
|--------|-------------|
| `start()` | Read NVS and start `jmri_conn`. Idempotent. WiThrottle transport only |
| `requestConnect(ip, wtPort, powerMgr)` | Save these settings and (re)connect, on the worker. Returns at once. Under the orchestrator it only saves, through the `SettingsWriter` |
| `requestDisconnect()` | Disconnect both and stop reconnecting until the next connect. Returns at once |
| `isBusy()` | True while a request is being carried out, for the screen's buttons |
