# Review Remediation Plan

**Created:** 2026-04-20
**Status:** Planning
**Source:** Multi-agent project review (Claude Opus 4.6, GPT-5.4, Gemini 3.1)

---

## Progress Tracker

| ID | Finding | Severity | Effort | Status |
|----|---------|----------|--------|--------|
| F-01 | [Unserialized TCP `send()` in WiThrottleClient](#f-01-unserialized-tcp-send-in-withrottleclient) | CRITICAL | Small | Not Started |
| F-02 | [Receive task lifecycle race in `disconnect()`](#f-02-receive-task-lifecycle-race-in-disconnect) | CRITICAL | Medium | Not Started |
| F-03 | [Config screen memory leaks](#f-03-config-screen-memory-leaks) | HIGH | Medium | Not Started |
| F-04 | [WiFi state callbacks bypass LVGL lock](#f-04-wifi-state-callbacks-bypass-lvgl-lock) | HIGH | Small | Not Started |
| F-05 | [Blocking I/O in LVGL event handlers](#f-05-blocking-io-in-lvgl-event-handlers) | HIGH | Large | Not Started |
| F-06 | [Raw pointer accessors bypass state mutex](#f-06-raw-pointer-accessors-bypass-state-mutex) | HIGH | Small | Not Started |
| F-07 | [`m_powerStates` unprotected across threads](#f-07-m_powerstates-unprotected-across-threads) | HIGH | Small | Not Started |
| F-08 | [Screen/callback ownership and lifecycle](#f-08-screencallback-ownership-and-lifecycle) | HIGH | Large | Not Started |
| F-09 | [Polling timer blocks `esp_timer` task](#f-09-polling-timer-blocks-esp_timer-task) | HIGH | Medium | Not Started |
| F-10 | [`pollThrottleStates()` bypasses `m_stateMutex`](#f-10-pollthrottlestates-bypasses-m_statemutex) | MEDIUM | Small | Not Started |
| F-11 | [`sendJsonCommand()` masks errors](#f-11-sendjsoncommand-masks-errors) | MEDIUM | Small | Not Started |
| F-12 | [Unbounded `messageBuffer` growth](#f-12-unbounded-messagebuffer-growth) | MEDIUM | Small | Not Started |
| F-13 | [NVS read on every encoder tick](#f-13-nvs-read-on-every-encoder-tick) | MEDIUM | Small | Not Started |
| F-14 | [Infinite LVGL lock in PowerStatusBar](#f-14-infinite-lvgl-lock-in-powerstatusbar) | MEDIUM | Small | Not Started |
| F-15 | [Dead test handlers in MainScreen](#f-15-dead-test-handlers-in-mainscreen) | LOW | Small | Not Started |
| F-16 | [Unused Roster model class](#f-16-unused-roster-model-class) | LOW | Small | Not Started |
| F-17 | [Duplicate speed/direction in Throttle and Locomotive](#f-17-duplicate-speeddirection-in-throttle-and-locomotive) | LOW | Small | Not Started |
| F-18 | [WiFi password in plaintext NVS](#f-18-wifi-password-in-plaintext-nvs) | LOW | Small | Not Started |

### Effort Key

| Rating | Meaning |
|--------|---------|
| Small | < 1 hour — localised change, single file, straightforward |
| Medium | 1–3 hours — touches 2–4 files, requires careful reasoning |
| Large | 3–8 hours — cross-cutting, new patterns, multiple components |

---

## Implementation Order

Work is grouped into phases to minimise risk and allow testing between batches.

### Phase 1 — Critical Thread Safety (Blocks everything)

These can cause hard faults, data corruption, or protocol stream corruption under normal use.

1. **F-01** — Add send mutex to WiThrottleClient
2. **F-02** — Cooperative receive task shutdown

### Phase 2 — High-Severity Thread Safety & Memory

Address remaining concurrency violations and memory leaks that cause intermittent failures.

3. **F-04** — LVGL lock in WiFi state callbacks
4. **F-06** — Remove/restrict raw pointer accessors
5. **F-07** — Mutex for `m_powerStates`
6. **F-10** — Lock `m_stateMutex` in polling timer
7. **F-14** — Finite LVGL lock timeout in PowerStatusBar
8. **F-03** — Config screen lifecycle management
9. **F-08** — Screen/callback ownership cleanup (depends on F-03)

### Phase 3 — Architecture & Performance

Structural improvements that reduce latency and improve robustness.

10. **F-09** — Replace `esp_timer` polling with dedicated task
11. **F-05** — Move blocking I/O off LVGL event handlers
12. **F-13** — Cache NVS speed-steps value
13. **F-11** — Return real error codes from `sendJsonCommand()`
14. **F-12** — Cap `messageBuffer` size

### Phase 4 — Cleanup

Low-risk code quality improvements.

15. **F-15** — Remove dead test handlers
16. **F-16** — Remove or integrate unused Roster class
17. **F-17** — Consolidate duplicate speed/direction fields
18. **F-18** — Document NVS encryption decision

---

## Detailed Finding Plans

---

### F-01: Unserialized TCP `send()` in WiThrottleClient

**Severity:** CRITICAL | **Effort:** Small | **Flagged by:** Claude, Gemini

#### Description

`WiThrottleClient::sendCommand()` writes to the TCP socket without any serialisation. It is called from the encoder polling task, the `esp_timer` task, and the LVGL UI task concurrently. Interleaved partial writes corrupt the WiThrottle protocol stream — potentially issuing wrong speed commands to physical locomotives.

#### Plan

1. Add a `SemaphoreHandle_t m_sendMutex` member to `WiThrottleClient`, created in the constructor.
2. Wrap the `send()` call in `sendCommand()` with `xSemaphoreTake`/`xSemaphoreGive` (200ms timeout).
3. Return `ESP_ERR_TIMEOUT` if the mutex cannot be acquired.
4. Delete the mutex in the destructor.

**Files:** `WiThrottleClient.h`, `WiThrottleClient.cpp`

#### Acceptance Criteria

- [ ] `sendCommand()` acquires `m_sendMutex` before calling `send()` and releases it after.
- [ ] Timeout returns `ESP_ERR_TIMEOUT` rather than blocking indefinitely.
- [ ] Existing unit tests pass (`ProtocolParsingTests`).
- [ ] Manual test: rapid knob rotation while polling timer is active produces no garbled protocol output (verify via serial log).

---

### F-02: Receive task lifecycle race in `disconnect()`

**Severity:** CRITICAL | **Effort:** Medium | **Flagged by:** Claude, GPT, Gemini

#### Description

`receiveTask` self-deletes with `vTaskDelete(nullptr)`. `disconnect()` then calls `eTaskGetState()` on the now-freed task handle — undefined behaviour in FreeRTOS. Additionally, the socket may be closed while the task is still blocked in `recv()`, and `vTaskDelete()` from outside doesn't allow the task to release resources.

#### Plan

1. Add an `EventGroupHandle_t m_taskExitEvent` member.
2. In `receiveTask`, set a bit on the event group just before `vTaskDelete(nullptr)`.
3. In `disconnect()`:
   a. Set `m_running = false`.
   b. Call `shutdown(m_socket, SHUT_RDWR)` to unblock `recv()`.
   c. Wait on the event group bit with a 2-second timeout.
   d. Close the socket and clean up state only after the task has signalled exit (or timeout expires).
4. Remove the `eTaskGetState()` check and the fallback `vTaskDelete()` call.

**Files:** `WiThrottleClient.h`, `WiThrottleClient.cpp`

#### Acceptance Criteria

- [ ] `disconnect()` never calls `eTaskGetState()` or `vTaskDelete(handle)` on the receive task.
- [ ] Receive task signals exit before self-deleting.
- [ ] `disconnect()` waits for the signal (with bounded timeout) before closing the socket.
- [ ] Existing unit tests pass.
- [ ] Manual test: repeated connect/disconnect cycles (10+) with no crash or heap corruption (check free heap before/after via serial log).

---

### F-03: Config screen memory leaks

**Severity:** HIGH | **Effort:** Medium | **Flagged by:** Claude, GPT, Gemini

#### Description

`AppController::showWiFiConfigScreen()` and `showJmriConfigScreen()` allocate screens with `new` but never store or `delete` them. Each navigation to a config screen leaks the C++ wrapper object (including `std::string`, `std::vector`, `std::function` members). On an embedded device with limited heap, repeated visits will eventually exhaust memory.

#### Plan

1. Add `std::unique_ptr<WiFiConfigScreen>` and `std::unique_ptr<JmriConfigScreen>` members to `AppController`.
2. On navigation, reset the existing `unique_ptr` (freeing the old instance) then create a new one — or reuse the existing instance.
3. Ensure the screen's LVGL objects are properly cleaned up when the `unique_ptr` is reset (the screen destructor should handle this).
4. This is a prerequisite for F-08 (callback lifecycle).

**Files:** `AppController.h`, `AppController.cpp`, potentially `WiFiConfigScreen.cpp`, `JmriConfigScreen.cpp` (destructors)

#### Acceptance Criteria

- [ ] Config screens are stored as `unique_ptr` members of `AppController`.
- [ ] Navigating to WiFi/JMRI settings and back 20 times does not grow heap usage (verify via `esp_get_free_heap_size()` logged before/after).
- [ ] Config screen destructors properly clean up LVGL objects and deregister callbacks.

---

### F-04: WiFi state callbacks bypass LVGL lock

**Severity:** HIGH | **Effort:** Small | **Flagged by:** GPT

#### Description

`WiFiConfigScreen` registers a WiFi state callback that directly calls `updateStatus()`, which manipulates LVGL widgets. This callback fires from the ESP event handler task, not the LVGL task, and no `lvgl_port_lock()` is taken — a direct violation of the project's documented threading model.

#### Plan

1. In the WiFi state callback within `WiFiConfigScreen`, wrap all LVGL widget updates with `lvgl_port_lock(200)` / `lvgl_port_unlock()`.
2. If the lock cannot be acquired within 200ms, skip the update (the next state change will retry).

**Files:** `WiFiConfigScreen.cpp`

#### Acceptance Criteria

- [ ] All LVGL widget access in WiFi state callbacks is guarded by `lvgl_port_lock()`.
- [ ] Lock timeout is finite (200ms).
- [ ] Manual test: connecting/disconnecting WiFi while on the WiFi config screen causes no UI corruption or crash.

---

### F-05: Blocking I/O in LVGL event handlers

**Severity:** HIGH | **Effort:** Large | **Flagged by:** GPT

#### Description

WiFi scan (`performScan()`, ~3s blocking) and WiThrottle connect (synchronous TCP + DNS) are called directly from LVGL button event handlers. Since LVGL event callbacks run on the UI task, this freezes the touchscreen and rendering for the duration of the blocking operation.

#### Plan

1. **WiFi scan:** Move `wifi_scan()` to a FreeRTOS task. The button handler starts the task and shows a spinner. On completion, the task takes the LVGL lock, updates the scan results list, and deletes itself.
2. **WiThrottle connect:** Move the `connect()` call to a FreeRTOS task. The button handler disables the connect button and shows a "Connecting..." status. The task posts the result back to the UI via the LVGL lock.
3. Pattern: Extract a reusable "async operation with UI feedback" pattern if both cases follow the same shape.

**Files:** `WiFiConfigScreen.cpp`, `JmriConfigScreen.cpp`, possibly a shared async helper

#### Acceptance Criteria

- [ ] WiFi scan button shows progress indicator; UI remains responsive during scan.
- [ ] WiThrottle connect button shows "Connecting..." status; UI remains responsive during connect.
- [ ] Touchscreen input is processed during both operations (test by tapping other UI elements).
- [ ] Error states (scan failure, connect timeout) are displayed correctly.

---

### F-06: Raw pointer accessors bypass state mutex

**Severity:** HIGH | **Effort:** Small | **Flagged by:** Claude, Gemini

#### Description

`ThrottleController::getThrottle()` and `getKnob()` return raw pointers to internal model objects without acquiring `m_stateMutex`. Callers can read or mutate throttle/knob state without synchronisation, undermining the snapshot pattern that is correctly used elsewhere. Similarly, `WiThrottleClient::getRoster()` returns a const reference that is not thread-safe.

#### Plan

1. Move `getThrottle()` and `getKnob()` to `private` in `ThrottleController.h`.
2. Audit all external callers — replace with snapshot-based access.
3. Move `getRoster()` in `WiThrottleClient` to `private` (safe alternatives `getRosterSnapshot()`/`getRosterEntry()` already exist).
4. If any test code needs direct access, gate behind `CONFIG_THROTTLE_TESTS`.

**Files:** `ThrottleController.h`, `WiThrottleClient.h`, any callers of `getThrottle()`/`getKnob()`/`getRoster()`

#### Acceptance Criteria

- [ ] `getThrottle()`, `getKnob()`, `getRoster()` are not part of the public API (or removed entirely).
- [ ] All external access uses snapshot methods.
- [ ] Project compiles cleanly with no new warnings.
- [ ] Existing unit tests pass (may need updating if they use these accessors).

---

### F-07: `m_powerStates` unprotected across threads

**Severity:** HIGH | **Effort:** Small | **Flagged by:** Gemini

#### Description

`JmriJsonClient::m_powerStates` (`std::map<std::string, PowerState>`) is written by `handlePowerMessage()` on the WebSocket event handler task and read by `getPower()` from the LVGL task. Concurrent `std::map` read/write is undefined behaviour — iterator invalidation or internal rebalancing during a concurrent read could crash.

#### Plan

1. Add a `SemaphoreHandle_t m_powerMutex` to `JmriJsonClient`.
2. Acquire the mutex in `handlePowerMessage()` when writing to `m_powerStates`.
3. Acquire the mutex in `getPower()` when reading from `m_powerStates`.
4. Use a short timeout (100ms) to avoid deadlock.

**Files:** `JmriJsonClient.h`, `JmriJsonClient.cpp`

#### Acceptance Criteria

- [ ] All access to `m_powerStates` is mutex-protected.
- [ ] Manual test: toggling power on/off from JMRI while the UI is active produces no crash.

---

### F-08: Screen/callback ownership and lifecycle

**Severity:** HIGH | **Effort:** Large | **Flagged by:** GPT

#### Description

Config screens register singleton-style callbacks on shared services (`WiFiManager`, `JmriJsonClient`, `WiThrottleClient`) using raw `this` captures. Because each service only exposes a single callback slot, screens overwrite one another's subscriptions. After navigation, there is no teardown path to clear callbacks, leaving dangling pointers to freed screen objects.

#### Plan

This depends on F-03 (screens owned by `AppController`).

1. Add `destroy()` or use destructors on config screens that explicitly deregister all callbacks (set callback slots to `nullptr`).
2. When `AppController` navigates away from a config screen, the `unique_ptr` reset triggers the destructor which clears callbacks.
3. Consider whether services should support a "clear callback" API (e.g. `setStateCallback(nullptr)`) for clean deregistration.
4. `PowerStatusBar` callbacks need the same treatment — deregister when the component is destroyed.

**Files:** `WiFiConfigScreen.cpp`, `JmriConfigScreen.cpp`, `PowerStatusBar.cpp`, `WiFiManager.h`, `JmriJsonClient.h`, `WiThrottleClient.h`

#### Acceptance Criteria

- [ ] Screen destructors deregister all callbacks from shared services.
- [ ] No callback fires targeting a destroyed screen object.
- [ ] Navigating Main → WiFi Config → Main → JMRI Config → Main (repeated) causes no crashes and no heap growth.

---

### F-09: Polling timer blocks `esp_timer` task

**Severity:** HIGH | **Effort:** Medium | **Flagged by:** Claude

#### Description

`ThrottleController::startPollingTimer()` creates a periodic `esp_timer` whose callback calls `pollThrottleStates()`, which performs blocking `send()` calls on a TCP socket. The `esp_timer` task is shared system-wide; blocking it delays all other timer callbacks (LVGL animations, heartbeats, watchdogs), causing UI stuttering or watchdog resets.

#### Plan

1. Replace the `esp_timer` with a dedicated FreeRTOS task.
2. The task loops with `vTaskDelay(pdMS_TO_TICKS(10000))` between polls.
3. The task acquires `m_stateMutex` to snapshot which throttles need polling, releases the mutex, then issues network queries.
4. Provide `startPolling()` / `stopPolling()` methods that create/signal the task.

**Files:** `ThrottleController.h`, `ThrottleController.cpp`

#### Acceptance Criteria

- [ ] No `esp_timer` is used for throttle polling.
- [ ] Polling runs on a dedicated FreeRTOS task with appropriate stack size.
- [ ] LVGL animations remain smooth during active polling (manual visual check).
- [ ] Polling stops cleanly when `stopPolling()` is called.

---

### F-10: `pollThrottleStates()` bypasses `m_stateMutex`

**Severity:** MEDIUM | **Effort:** Small | **Flagged by:** Claude, GPT, Gemini

#### Description

`pollThrottleStates()` iterates over throttles and reads each throttle's state without acquiring `m_stateMutex`, despite the controller's documented mutex discipline. A throttle could transition between the state check and the query calls.

#### Plan

1. At the start of `pollThrottleStates()`, acquire `m_stateMutex`.
2. Build a local list of throttle indices that are in `ALLOCATED` state.
3. Release `m_stateMutex`.
4. Issue network queries only for the captured indices.

Note: If F-09 is implemented first (dedicated task), this fix is naturally incorporated into that work.

**Files:** `ThrottleController.cpp`

#### Acceptance Criteria

- [ ] Throttle state reads in `pollThrottleStates()` are protected by `m_stateMutex`.
- [ ] The mutex is released before any network I/O.
- [ ] Existing unit tests pass.

---

### F-11: `sendJsonCommand()` masks errors

**Severity:** MEDIUM | **Effort:** Small | **Flagged by:** Claude, GPT, Gemini

#### Description

`JmriJsonClient::sendJsonCommand()` always returns `ESP_OK` regardless of the actual `esp_websocket_client_send_text()` return value. Callers cannot distinguish success from failure, weakening reconnect logic and masking real faults.

#### Plan

1. Check the return value of `esp_websocket_client_send_text()`.
2. If negative and `esp_websocket_client_is_connected()` returns false, return `ESP_FAIL`.
3. If negative but connected, log a warning and return `ESP_OK` (workaround for known library quirk — add a comment explaining this).
4. Update callers to handle the failure code if appropriate.

**Files:** `JmriJsonClient.cpp`

#### Acceptance Criteria

- [ ] `sendJsonCommand()` returns `ESP_FAIL` when the connection is down.
- [ ] The known library quirk is documented in a code comment.
- [ ] Callers that need to react to send failure do so (or at minimum, the error is logged at WARN level).

---

### F-12: Unbounded `messageBuffer` growth

**Severity:** MEDIUM | **Effort:** Small | **Flagged by:** Claude, Gemini

#### Description

The `messageBuffer` string in `WiThrottleClient::receiveTask()` accumulates data until a newline is found. A misbehaving or compromised server could send an arbitrarily long line, exhausting heap on the ESP32-S3.

#### Plan

1. After appending to `messageBuffer`, check if `messageBuffer.size() > 4096`.
2. If exceeded, log a warning and clear the buffer.
3. Optionally disconnect if the overflow is repeated (indicates a misbehaving server).

**Files:** `WiThrottleClient.cpp`

#### Acceptance Criteria

- [ ] `messageBuffer` is capped at 4 KB.
- [ ] Exceeding the cap logs a warning and clears the buffer.
- [ ] Normal WiThrottle protocol messages (well under 4 KB) are unaffected.

---

### F-13: NVS read on every encoder tick

**Severity:** MEDIUM | **Effort:** Small | **Flagged by:** Claude, Gemini

#### Description

`getSpeedStepsPerClick()` opens NVS, reads the `speed_steps` key, and closes the handle every time it is called — on every `onKnobRotation()` event. NVS reads involve flash access with mutex contention, adding unnecessary latency to the speed control hot path.

#### Plan

1. Add a `uint8_t m_cachedSpeedSteps` member to `ThrottleController`.
2. Load the value once in `initialize()`.
3. Replace `getSpeedStepsPerClick()` body with a return of the cached value.
4. Add a `reloadSpeedStepsFromNvs()` method to call when settings change.

**Files:** `ThrottleController.h`, `ThrottleController.cpp`

#### Acceptance Criteria

- [ ] NVS is read once at initialisation, not on every encoder event.
- [ ] Changing the setting via the settings screen takes effect without restart.
- [ ] Encoder responsiveness is at least as good as before (subjective manual test).

---

### F-14: Infinite LVGL lock in PowerStatusBar

**Severity:** MEDIUM | **Effort:** Small | **Flagged by:** Gemini

#### Description

`PowerStatusBar` callbacks use `lvgl_port_lock(-1)` (infinite timeout) from the WebSocket event handler task. If the LVGL task is performing a lengthy operation, the WebSocket event loop is blocked indefinitely, preventing heartbeat processing and potentially causing the JMRI server to time out the connection.

#### Plan

1. Change `lvgl_port_lock(-1)` to `lvgl_port_lock(200)` in `PowerStatusBar` callbacks.
2. If the lock cannot be acquired, skip the UI update (the next power state change or periodic refresh will retry).

**Files:** `PowerStatusBar.cpp`

#### Acceptance Criteria

- [ ] All `lvgl_port_lock` calls in `PowerStatusBar` use a finite timeout (≤ 200ms).
- [ ] Power state changes still update the UI under normal conditions.
- [ ] JMRI WebSocket connection does not time out during heavy UI activity.

---

### F-15: Dead test handlers in MainScreen

**Severity:** LOW | **Effort:** Small | **Flagged by:** Claude, GPT, Gemini

#### Description

Static methods `onAcquireButtonClicked`, `onSpeedButtonClicked`, `onForwardButtonClicked`, `onReverseButtonClicked`, `onF0ButtonClicked`, and `onOldReleaseButtonClicked` use hardcoded throttle ID `'T'` and bypass the `ThrottleController` entirely. They are remnants from early testing and not wired to any current UI element.

#### Plan

1. Remove all six static test handler methods from `MainScreen.cpp` and `MainScreen.h`.

**Files:** `MainScreen.h`, `MainScreen.cpp`

#### Acceptance Criteria

- [ ] Dead handler methods are removed.
- [ ] Project compiles cleanly.
- [ ] No references to removed methods remain.

---

### F-16: Unused Roster model class

**Severity:** LOW | **Effort:** Small | **Flagged by:** Claude, Gemini

#### Description

`model/Roster.h` and `model/Roster.cpp` implement a full roster management class (~200 lines), but the application uses `WiThrottleClient::m_roster` (a plain `std::vector<Locomotive>`) directly. The `Roster` class is dead code.

#### Plan

Decision needed: **integrate or remove**.
- **Option A (remove):** Delete `Roster.h`, `Roster.cpp`, and `ModelRosterTests.cpp`. Simpler, less code.
- **Option B (integrate):** Replace `WiThrottleClient`'s ad-hoc vector with `Roster`. More correct, but adds coupling.

Recommendation: **Option A** — the vector approach works fine and the Roster class adds unnecessary abstraction.

**Files:** `Roster.h`, `Roster.cpp`, `ModelRosterTests.cpp`, `TestRunner.cpp`, `CMakeLists.txt`

#### Acceptance Criteria

- [ ] No dead roster code remains (or the class is fully integrated).
- [ ] All tests pass.

---

### F-17: Duplicate speed/direction in Throttle and Locomotive

**Severity:** LOW | **Effort:** Small | **Flagged by:** Gemini

#### Description

Both `Throttle` (`m_currentSpeed`, `m_direction`) and `Locomotive` (`m_speed`, `m_direction`) track speed and direction. Only `Throttle`'s values are kept current during runtime. A developer could accidentally read stale values from the `Locomotive` model.

#### Plan

1. Remove `m_speed` and `m_direction` from `Locomotive` (treat it as immutable identity: name, address, type, functions).
2. Update any code that reads `Locomotive` speed/direction to read from `Throttle` instead.
3. Update `LocomotiveTests` accordingly.

**Files:** `Locomotive.h`, `Locomotive.cpp`, `LocomotiveTests.cpp`, any callers

#### Acceptance Criteria

- [ ] `Locomotive` has no speed/direction fields.
- [ ] All speed/direction reads go through `Throttle`.
- [ ] All tests pass.

---

### F-18: WiFi password in plaintext NVS

**Severity:** LOW | **Effort:** Small | **Flagged by:** Claude, Gemini

#### Description

WiFi credentials are stored via `nvs_set_str()` without NVS encryption. Physical access to the ESP32-S3 flash (JTAG, flash dump) exposes the WiFi password.

#### Plan

This is an accepted risk for a home model railway controller on a private network. Document the decision.

1. Add a comment in `WiFiManager.cpp` near the NVS read/write explaining that NVS encryption is not enabled and why this is acceptable for the current deployment context.
2. If the threat model changes, enable NVS encryption in `sdkconfig`.

**Files:** `WiFiManager.cpp`

#### Acceptance Criteria

- [ ] Risk is documented in a code comment.
- [ ] No functional change required.

---

## Testing Strategy

### Automated Testing (Existing Infrastructure)

The project has 28 Unity-based on-device unit tests covering model classes, state machines, and protocol parsing. These run via `pytest-embedded` over serial and are triggered by:

```powershell
.\tools\ensure-idf.ps1; idf.py -B build-tests -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.test.defaults" -D SDKCONFIG="build-tests/sdkconfig" flash_test
```

**For each finding, automated tests should be added or updated where possible.** Specifically:

| Finding | Automated Test Feasible? | Notes |
|---------|--------------------------|-------|
| F-01 | Partial | Can unit-test that `sendCommand` acquires mutex (mock), but true concurrency test needs hardware |
| F-02 | No | Requires real TCP socket + FreeRTOS task lifecycle |
| F-03 | Partial | Can check heap before/after in a test, but needs LVGL initialised |
| F-04–F-05 | No | Requires LVGL + WiFi hardware |
| F-06 | Yes | Compile-time check — if it compiles with accessors private, it passes |
| F-07 | No | Requires WebSocket + LVGL |
| F-08 | No | Requires full UI navigation |
| F-09 | Partial | Can test that polling task starts/stops correctly |
| F-10 | Yes | Extend existing `ThrottleControllerTests` with mock |
| F-11 | Partial | Can unit-test return value logic with mock WebSocket |
| F-12 | Yes | Feed >4 KB without newline, verify buffer is capped |
| F-13 | Yes | Verify NVS is only read once (mock NVS or count calls) |
| F-14 | No | Requires LVGL + WebSocket |
| F-15–F-18 | Yes | Compile/removal verification |

### Manual Functional Testing

For findings that cannot be fully automated, the following manual test procedures should be executed by a human operator with the device connected. Copilot can assist by monitoring serial output, checking heap usage, and verifying log messages.

---

#### MFT-01: WiThrottle Send Mutex Validation (F-01)

**Preconditions:** Device connected to WiThrottle server, at least one locomotive acquired on a throttle.

| Step | Operator Action | Expected Result | Copilot Verification |
|------|----------------|-----------------|---------------------|
| 1 | Enable verbose logging for WiThrottle TAG | Log level set | Check serial output confirms log level |
| 2 | Rapidly rotate encoder knob while polling is active | Speed commands sent | Monitor serial for interleaved/garbled `sendCommand` log lines — should see clean, complete commands only |
| 3 | While rotating, tap a function button simultaneously | Both commands sent | Verify no protocol errors logged by server or client |
| 4 | Repeat for 60 seconds | No errors | Check heap is stable, no crash |

**Pass criteria:** No garbled commands in serial log; no WiThrottle protocol errors.

---

#### MFT-02: Disconnect/Reconnect Stability (F-02)

**Preconditions:** Device connected to WiThrottle server.

| Step | Operator Action | Expected Result | Copilot Verification |
|------|----------------|-----------------|---------------------|
| 1 | Log free heap size | Baseline recorded | Capture `esp_get_free_heap_size()` value |
| 2 | Navigate to JMRI config, tap Disconnect | Clean disconnect | Serial log shows task exit signal, no crash |
| 3 | Tap Connect | Reconnects | Serial log shows successful connection |
| 4 | Repeat steps 2–3 ten times | All succeed | No crash, no watchdog reset |
| 5 | Log free heap size | Within 1 KB of baseline | Compare to step 1 |

**Pass criteria:** 10 cycles with no crash; heap stable within 1 KB.

---

#### MFT-03: Config Screen Navigation Leak Test (F-03, F-08)

**Preconditions:** Device running, main screen displayed.

| Step | Operator Action | Expected Result | Copilot Verification |
|------|----------------|-----------------|---------------------|
| 1 | Log free heap | Baseline | Capture value |
| 2 | Navigate to WiFi Config → Back to Main | Screen transitions | No crash |
| 3 | Navigate to JMRI Config → Back to Main | Screen transitions | No crash |
| 4 | Repeat steps 2–3 twenty times | All transitions clean | No crash, no visual artefacts |
| 5 | Log free heap | Within 2 KB of baseline | Compare to step 1 |

**Pass criteria:** 20 round-trips with no crash; heap stable within 2 KB.

---

#### MFT-04: WiFi State Change UI Safety (F-04)

**Preconditions:** Device on WiFi config screen, connected to a WiFi network.

| Step | Operator Action | Expected Result | Copilot Verification |
|------|----------------|-----------------|---------------------|
| 1 | Power-cycle the WiFi access point | WiFi disconnects | Serial log shows state change with LVGL lock acquired |
| 2 | Wait for AP to come back | WiFi reconnects | Status label updates without crash or corruption |
| 3 | While on WiFi screen, rapidly toggle AP power (3 times) | Multiple state changes | No crash, no visual corruption |

**Pass criteria:** All state changes reflected in UI; no crash or rendering glitch.

---

#### MFT-05: Non-Blocking UI During Network Operations (F-05)

**Preconditions:** Device on WiFi config screen.

| Step | Operator Action | Expected Result | Copilot Verification |
|------|----------------|-----------------|---------------------|
| 1 | Tap "Scan" button | Spinner/progress shown | UI does not freeze |
| 2 | During scan, tap other UI elements | Taps are registered | Touch events processed (visual feedback on tap) |
| 3 | Navigate to JMRI config, tap "Connect" with server offline | "Connecting..." shown | UI remains responsive during timeout |
| 4 | Tap "Cancel" or navigate back during connect | Operation cancelled | Clean return to previous screen |

**Pass criteria:** UI responsive throughout all network operations; no freeze >200ms.

---

#### MFT-06: LVGL Lock Timeout in PowerStatusBar (F-14)

**Preconditions:** Device connected to JMRI, power status bar visible.

| Step | Operator Action | Expected Result | Copilot Verification |
|------|----------------|-----------------|---------------------|
| 1 | Toggle track power on/off from JMRI | Power status updates in UI | Check serial log for lock acquisition |
| 2 | While navigating screens (heavy LVGL activity), toggle power rapidly from JMRI | Some updates may be skipped | No crash; JMRI WebSocket connection stays alive |
| 3 | After heavy activity, verify JMRI connection | Still connected | WebSocket heartbeat logs continue |

**Pass criteria:** No JMRI disconnection due to blocked event loop; UI updates eventually reflect correct state.

---

#### MFT-07: Polling Timer Smoothness (F-09)

**Preconditions:** Device connected, locomotive acquired, UI on main throttle screen.

| Step | Operator Action | Expected Result | Copilot Verification |
|------|----------------|-----------------|---------------------|
| 1 | Observe throttle meter animation while polling is active | Smooth animation | No visible stuttering or frame drops |
| 2 | Rapidly rotate encoder during poll cycle | Immediate speed response | Latency <200ms from rotation to UI update |
| 3 | Monitor for 5 minutes with periodic interaction | Stable operation | No watchdog resets in serial log |

**Pass criteria:** Smooth UI; no watchdog resets; encoder response within 200ms.

---

#### MFT-08: Encoder Responsiveness After NVS Caching (F-13)

**Preconditions:** Device running with locomotive acquired.

| Step | Operator Action | Expected Result | Copilot Verification |
|------|----------------|-----------------|---------------------|
| 1 | Rotate encoder slowly (1 click at a time) | Speed increments by configured steps | Verify correct step size in serial log |
| 2 | Rotate encoder rapidly | Speed changes track rotation | No dropped clicks or lag |
| 3 | Change speed-steps setting, return to throttle | New step size used | Verify cached value updated in serial log |

**Pass criteria:** Encoder response at least as good as before; setting changes take effect without restart.

---

## Additional Recommendations

The reviewers did not surface these, but they are worth noting as follow-up work after the remediation is complete:

### Test Coverage Gaps

1. **Integration tests for `AppController`** — the orchestrator is untested. Consider adding tests that exercise the full initialisation → connect → acquire → control → release → disconnect lifecycle with mocked network.
2. **Stress tests for concurrent throttle operations** — four throttles with two encoders under rapid rotation would exercise the mutex paths.
3. **Memory profiling test** — a long-running soak test (leave running for hours) to verify no slow leaks.

### Documentation Updates

After each finding is resolved, update:
- [docs/architecture/THREADING_MODEL.md](docs/architecture/) — if lock ordering or mutex inventory changes
- [docs/architecture/](docs/architecture/) — if the polling or callback patterns change
- This document — mark findings as completed with the date

---

## Change Log

| Date | Change |
|------|--------|
| 2026-04-20 | Initial plan created from multi-agent review |
