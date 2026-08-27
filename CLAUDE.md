# ESP Layout Controller — Working Agreement

A 7" touchscreen throttle for the Westgate Hollow layout. ESP32-S3 + LVGL, 4 simultaneous
throttles, 2 physical rotary encoders. Speaks WiThrottle to JMRI today; gaining a second,
peer transport that speaks to the layout orchestrator directly.

**This device commands physical locomotives.** The severity of a bug here is measured in
whether a train moves when nobody turned a knob, not in leaked data.

## Reading this repo without burning context

This file loads into **every session and every subagent**, so every line is a tax paid on
every task. It is an **index and a rulebook, nothing else**. Three layers, read in order,
and stop as soon as you know enough:

1. **`CLAUDE.md`** (this file) — the rules, and one line per area saying what exists.
2. **`docs/PROJECT_OVERVIEW.md`** and **`docs/architecture/`** — what an area consists of.
3. **`docs/flows/`**, **`docs/components/`**, **`docs/protocols/`** — the detail: sequence
   diagrams per flow, per-layer reference, protocol wire formats.

`docs/REVIEW_REMEDIATION_PLAN.md` is the record of a completed hardening pass (F-01…F-18,
all Done). Read a finding when you are about to touch the code it covers — several of the
"why is it written like this" answers live there and nowhere else.

**`.github/copilot-instructions.md` covers the same conventions for Copilot.** Where the
two overlap they must be changed together; where they disagree, this file wins for Claude,
and the disagreement is a bug to fix rather than a preference to pick.

## Commands

```powershell
.\tools\ensure-idf.ps1                          # idempotent: exports ESP-IDF v5.5.2 if needed
idf.py -p $env:ESP_PORT build flash monitor     # ESP_PORT defaults to COM4
idf.py size                                     # image against the flash budget below
idf.py size-components                          # where the bytes went
```

On-device Unity tests build into a **separate** build directory with its own sdkconfig:

```powershell
idf.py -B build-tests -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.test.defaults" -D SDKCONFIG="build-tests/sdkconfig" flash_test
```

`sdkconfig.test.defaults` sets `CONFIG_THROTTLE_TESTS=y`, which swaps the UI for the Unity
runner (`main/tests/TestRunner.cpp`). `test_app.py` is a pyserial harness that resets the
board over RTS/DTR and asserts on the Unity summary line.

**There are no host-side tests.** CI (`.github/workflows/ci.yml`) builds the firmware on
every push to `main` and every PR, and that is all it does — it cannot run the Unity suite,
which needs the board. So a green check means "it compiles", never "it works": every
behavioural check is still a flash-and-test cycle on real hardware, which is why batching
firmware changes matters. `.github/` also holds the review agents and Copilot instructions.

## Hard constraints

**1. The flash budget is real and tight.** The factory partition is **1500 K**
(`partitions_singleapp_large.csv`, IDF v5.5.2 — not the 3 MB the 8 MB flash suggests). The
last local build was ~1.29 MB, leaving roughly **178 KB (12%)** — the orchestrator transport
cost about 70 KB of that, cJSON and `esp_http_client` included. There is **no OTA partition**:
`singleapp_large` is factory-only, so every flash is a cable at the layout, and adding OTA
later would halve the remaining headroom. Re-measure with `idf.py size` rather than
trusting that number; treat a new managed component as a decision, not a detail.

**2. LVGL is not thread-safe.** Any LVGL call from a network task, FreeRTOS timer, or the
encoder polling task must hold `lvgl_port_lock(timeout)`. LVGL *event callbacks* already
run on the LVGL task and must not re-lock. Timeouts: `100` ms where an update can be
skipped (speed, direction — the next one will land), `-1` only for critical one-time
updates. F-14 removed an infinite lock from a frequently-called path; do not reintroduce
that shape.

**3. Lock ordering is fixed: `m_stateMutex` before `lvgl_port_lock`, never the reverse.**
`ThrottleController` guards its own state with a mutex separate from the LVGL port lock.
Taking them in the other order deadlocks.

**4. State lives at the application layer, never in the UI.** `AppController` owns the
clients and `ThrottleController`; `ThrottleController` owns the `Throttle` / `Knob` /
`Locomotive` models. UI classes hold **raw, non-owning** pointers. A screen must be
destroyable and recreatable without losing throttle state or a network connection.

**5. Nothing blocks in an LVGL event handler.** Scanning, connecting, and NVS writes move
to a task (F-05). A blocked handler freezes rendering and input for every throttle at once.

## Architecture

```
main/
├── model/          Throttle, Knob, Locomotive — data, no I/O
├── hardware/       RotaryEncoderHal — I2C Seesaw encoder polling
├── communication/  ThrottleBackend (port), WiThrottleBackend, OrchestratorBackend,
│                   WiFiManager, WiThrottleClient (TCP), JmriJsonClient (WebSocket),
│                   OrchestratorClient (WebSocket control plane)
├── controller/     AppController, ThrottleController, WiFiController, JmriConnectionController
└── ui/             LVGL screens and components
```

Dependency direction is one way: `ui` → `controller` → `communication` / `model`. A model
knows about no layer above it.

`docs/architecture/THREADING_MODEL.md` carries the full task table — stack sizes,
priorities, and which call creates each task. `docs/architecture/NVS_STORAGE.md` is the
namespace and key map, and the authoritative list of what persists.

## Conventions

- **C++17.** Classes `PascalCase`, methods `camelCase`, members `m_camelCase`, constants
  `UPPER_SNAKE_CASE`, enum types `PascalCase` with `UPPER` values.
- One class per `.h` / `.cpp` pair, RAII, copy/move deleted unless needed, dependencies
  injected via constructor, const-correct.
- Logging is `ESP_LOG*` with a file-local `static const char* TAG`.
- New source files must be added to `APP_SRCS` in `main/CMakeLists.txt` — there is no glob.
- **British English** in comments, docs, and commit messages.
- `sdkconfig` is generated and git-ignored. `sdkconfig.defaults` and
  `sdkconfig.test.defaults` are the tracked sources of truth — change those, never the
  generated file.

## Cross-repo

One of four repos forming the Westgate Hollow control stack (see the global working
agreement). What binds this one:

| Source of truth | Governs |
|---|---|
| `bazauto/layout-orchestration` → `packages/backend/src/domain/types.ts` | `ClientMessage` / `ServerMessage` — the WebSocket control-plane vocabulary. Authoritative for the whole system, this firmware included. |
| `bazauto/layout-orchestration` → `docs/mqtt-contract.md` | MQTT topics and payloads. **Binds the sensor and point boards, not this one.** |

Cross-repo issues are referenced `owner/repo#N`. The tracking issue for this migration is
`bazauto/layout-orchestration#9`.

**MQTT is not this device's transport.** The contract's `loco/{address}/command` row
describes an architecture where the ESP drives DCC; it does not, and nothing in the backend
has ever published or subscribed to that topic. The orchestrator drives DCC over serial to
PicoDCC. MQTT is the hardware telemetry bus (sensors, point feedback); WebSocket is the
operator control plane, and this is an operator device.

## The two transports

Decided 2026-08-24, **built 2026-08-27**. Recorded so it is not re-litigated:

- **The orchestrator is reached over its WebSocket `/ws`**, not MQTT. Auth is the existing
  session cookie: POST `/api/auth/login`, capture `Set-Cookie`, set `Cookie:` on the
  upgrade. `esp_websocket_client` is already a dependency and already linked for
  `JmriJsonClient`, so no new component is needed.
- **The device holds a dedicated `operator` credential in NVS**, alongside the WiFi
  credentials. Same accepted plaintext-NVS risk as F-18.
- **WiThrottle stays a peer, not a legacy path.** Both transports remain first-class and
  selectable at **runtime** from the config screen, persisted in NVS. Deliberately not a
  Kconfig choice: with no OTA, a compile-time switch means a cable at the layout to A/B a
  bug, and it would multiply an already-forked build matrix.
- **The seam is a `ThrottleBackend` port.** `ThrottleController` depends on the interface;
  `WiThrottleBackend` and `OrchestratorBackend` sit behind it. Capabilities
  (`requiresAcquisition` / `providesRoster` / `providesFunctionLabels` / `requiresPolling`)
  are how each backend answers in its own terms; do **not** make one protocol impersonate
  the other. See `docs/components/COMMUNICATION_LAYER.md`.
- **Only the selected transport's stack comes up.** `AppController::initialise()` reads
  `TransportSettings` before anything connects. Under the orchestrator, JMRI auto-connect is
  never started; under WiThrottle, no orchestrator task exists. The device must never sit
  retrying a server the operator did not choose.
- **`setSpeedAndDirection` is the call to use when both change.** `THROTTLE_COMMAND` carries
  the pair, and sending speed against the *old* direction first commands the loco faster the
  way it was already going before reversing it. The port's default implementation orders it
  direction-then-speed for transports with no combined command.

## Traps

Things that look like bugs or oversights and are not. One line each.

- **`getThrottle` / `getKnob` bypass `m_stateMutex` and are guarded by
  `CONFIG_THROTTLE_TESTS`** — deliberately unsafe test-only accessors (F-06 removed the
  unguarded ones). They must never gain a non-test caller.
- **`JmriJsonClient` parses JSON by substring search** (`extractJsonString` /
  `extractJsonInt`) rather than with a parser. Adequate for the narrow JMRI subset it
  reads; **not** adequate for orchestrator payloads. `OrchestratorClient` uses cJSON and
  refuses a malformed message outright rather than half-parsing one into a speed command —
  do not "simplify" it back to substring search.
- **A `STATE_SNAPSHOT` updates the display, never the outbound command path.** It describes
  what the layout believes, including locos already moving that this device did not start.
  Replaying it as commands on reconnect is exactly the ghost-movement bug the orchestrator's
  non-retained control topics exist to prevent.
- **The WiFi password is stored in plaintext NVS** — an accepted and documented risk
  (F-18), not an oversight.
- **`json_port` is normally discovered from the WiThrottle `PW` message**, not configured,
  which is why it has a default and no prominent UI field.
- **K1/K2 are disabled while WiThrottle is disconnected** (472a955) — a knob that still
  turned would move a model that no longer tracks anything real.
- **`sdkconfig.tests` is tracked despite appearing in `.gitignore`** — it predates the
  ignore rule. `sdkconfig.test.defaults` is the file that actually matters.

## Open limits

- **No host-side test harness.** CI compiles the firmware but cannot run a test; every test
  is a flash onto real hardware, so coverage is bounded by bench time. A host-buildable core
  (the models and the protocol parsing have no ESP dependencies) is unscoped, and would be
  the highest-leverage change available here. The `ThrottleBackend` port makes it markedly
  easier: `ThrottleController` now has no ESP-networking dependency to stub out, and the
  fake backend in `main/tests/ThrottleControllerTests.cpp` is the shape a host build needs.
- **Function labels come from JMRI's decoder database and have no equivalent yet.** The
  orchestrator's `locos` table stores no labels, so a WebSocket-connected device would show
  `F0`…`F28`. Being fixed orchestrator-side: labels become operator-authored per loco.
- **No OTA.** See the flash budget above.
