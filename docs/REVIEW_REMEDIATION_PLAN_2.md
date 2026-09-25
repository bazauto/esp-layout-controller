# Review Remediation Plan — Second Pass

**Created:** 2026-09-24
**Status:** In progress — Batches 1–4 code done (every HIGH and the operator-facing MEDIUMs), awaiting their bench cycles
**Source:** Static review of the whole firmware (`main/`, build config, CI), with the
orchestrator-facing behaviour checked against `bazauto/layout-orchestration`. Nothing was
flashed during the review: every "bench" criterion below is unverified until someone runs it
on the board.

IDs continue from the first pass (`docs/REVIEW_REMEDIATION_PLAN.md`, F-01…F-18), so an `F-nn`
reference in a code comment is unambiguous across both documents.

Each finding also has a GitHub issue — `F-nn` is `bazauto/esp-layout-controller#(nn+3)`, #22 to
#47 — as a permanent record outside the tree. This document stays the place its status is kept.

Severity is weighed the way CLAUDE.md asks: by whether a train moves when nobody commanded it,
whether the operator can lose the ability to stop one, or whether the device can crash or
freeze while one is moving.

---

## Progress Tracker

| ID | Finding | Severity | Effort | Batch | Status | Issue |
|----|---------|----------|--------|-------|--------|-------|
| F-19 | [Unbounded encoder delta](#f-19-unbounded-encoder-delta) | HIGH | Small | 1 | Code done | #22 |
| F-20 | [Orchestrator acquire starts from speed 0](#f-20-orchestrator-acquire-starts-from-speed-0) | HIGH | Medium | 1 | Code done | #23 |
| F-21 | [Use-after-free on return to the main screen](#f-21-use-after-free-on-return-to-the-main-screen) | HIGH | Medium | 1 | Code done | #24 |
| F-22 | [WiThrottle reconnect leaks a socket and loses the session](#f-22-withrottle-reconnect-leaks-a-socket-and-loses-the-session) | HIGH | Medium | 2 | Code done | #25 |
| F-23 | [WiThrottle heartbeat never armed](#f-23-withrottle-heartbeat-never-armed) | HIGH | Small | 2 | Code done | #26 |
| F-24 | [No automatic recovery after an ordinary outage](#f-24-no-automatic-recovery-after-an-ordinary-outage) | HIGH | Medium | 3 | Code done | #27 |
| F-25 | [Physical knobs not gated; optimistic update outlives a failed send](#f-25-physical-knobs-not-gated-optimistic-update-outlives-a-failed-send) | MEDIUM | Small | 4 | Code done | #28 |
| F-26 | [`OrchestratorClient::m_client` destroyed under a sender](#f-26-orchestratorclientm_client-destroyed-under-a-sender) | MEDIUM | Medium | 4 | Code done | #29 |
| F-27 | [No emergency stop; power button fails the wrong way](#f-27-no-emergency-stop-power-button-fails-the-wrong-way) | MEDIUM | Medium | 4 | Code done | #30 |
| F-28 | [Every function is momentary under the orchestrator](#f-28-every-function-is-momentary-under-the-orchestrator) | MEDIUM | Small | 4 | Code done | #31 |
| F-29 | [Documented lock order is the reverse of the code's](#f-29-documented-lock-order-is-the-reverse-of-the-codes) | MEDIUM | Small | 4 | Code done (docs only; no bench check) | #32 |
| F-30 | [Callback slots unsynchronised and clobbered](#f-30-callback-slots-unsynchronised-and-clobbered) | MEDIUM | Medium | 1 (part), 5 | Slots: code done; sync: open | #33 |
| F-31 | [JMRI heartbeat task deleted from outside](#f-31-jmri-heartbeat-task-deleted-from-outside) | MEDIUM | Small | 2 | Code done | #34 |
| F-32 | [Main screen LVGL tree leaks on every return](#f-32-main-screen-lvgl-tree-leaks-on-every-return) | MEDIUM | Small | 1 | Code done | #35 |
| F-33 | [Task stack headroom unmeasured](#f-33-task-stack-headroom-unmeasured) | MEDIUM | Small | 5 | Open | #36 |
| F-34 | [JMRI config screen connect/disconnect faults](#f-34-jmri-config-screen-connectdisconnect-faults) | MEDIUM | Medium | 3 | Code done | #37 |
| F-35 | [Orchestrator roster refused above ~35 locos](#f-35-orchestrator-roster-refused-above-35-locos) | MEDIUM | Small | 5 | Open | #38 |
| F-36 | [Operator credential and session cookie in cleartext](#f-36-operator-credential-and-session-cookie-in-cleartext) | MEDIUM | Small (decision) | 5 | Open | #39 |
| F-37 | [WiThrottle updates unvalidated](#f-37-withrottle-updates-unvalidated) | LOW | Small | 5 | Open | #40 |
| F-38 | [Hot-path logging and repaint cost](#f-38-hot-path-logging-and-repaint-cost) | LOW | Small | 5 | Open | #41 |
| F-39 | [NVS writes on the LVGL task](#f-39-nvs-writes-on-the-lvgl-task) | LOW | Small | 5 | JMRI settings: done (batch 3); rest: open | #42 |
| F-40 | [WiFi credential save and reboot-on-error](#f-40-wifi-credential-save-and-reboot-on-error) | LOW | Small | 3 | Code done | #43 |
| F-41 | [Seesaw read timing](#f-41-seesaw-read-timing) | LOW | Small | 5 | Open | #44 |
| F-42 | [Protocol hygiene odds and ends](#f-42-protocol-hygiene-odds-and-ends) | LOW | Small | 5 | Open | #45 |
| F-43 | [CI hardening](#f-43-ci-hardening) | LOW | Small | 5 | Open | #46 |
| F-44 | [Threading-model task table drift](#f-44-threading-model-task-table-drift) | LOW | Small | 5 | Open | #47 |

**Status key:** *Open* — not started. *Code done* — implemented and compiled, bench criteria
still unticked. *Done* — bench criteria confirmed on the board.

---

## Implementation Order

Batches are sized to one flash-and-test cycle each. Nothing here can be verified by CI beyond
"it compiles", so a batch is only closed when its bench checks have been run.

1. **Batch 1 — local, high-consequence fixes.** F-19, F-20, F-21, and F-32 and the
   connection-slot half of F-30 as consequences of F-21's fix. Each is confined to a file or
   two and needs no protocol change.
2. **Batch 2 — WiThrottle session lifecycle.** F-22, F-23, F-31. All three are about what
   survives a connection ending, and are tested with the same bench procedure (restart JMRI,
   pull the ESP's power).
3. **Batch 3 — recovery.** F-24, F-34, F-40. Tested by power-cycling the router and the
   orchestrator host independently of the device.
4. **Batch 4 — operator-facing control.** F-25 to F-29.
5. **Batch 5 — the rest.** Measurement (F-33), limits, hygiene and docs.

---

## Detailed Findings

---

### F-19: Unbounded encoder delta

**Severity:** HIGH | **Effort:** Small | **Batch:** 1

#### Description

`RotaryEncoderHal::readEncoderDelta` assembles a 32-bit signed delta from four I2C bytes and
`pollOnce` passes it to the rotation callback unchecked. `ThrottleController::onKnobRotation`
computes `delta * stepsPerClick` — signed overflow, which is undefined behaviour, for a large
delta — and clamps the result to ±126. So one corrupted upper byte on a bus that runs alongside
DCC wiring commands **full speed, in either direction**. A garbage delta in `SELECTING` state
also reaches `Knob::handleRotation`, whose `while` wrap loops can run for millions of iterations
while holding `m_stateMutex`.

The double-read workaround makes this likelier than it looks. If the Seesaw is answering the
*previous* request (the likely reason the second read is the one that "works"), one failed
transaction shifts the pipeline and a GPIO-bulk word — bit 24 set, about 16.7 million — is read
as a delta.

#### Plan

1. In `RotaryEncoderHal::pollOnce`, treat a delta outside ±`MAX_PLAUSIBLE_DELTA` as a failed
   read: log it at WARN with the raw value and drop it. One full revolution per poll is far
   beyond a human hand; that is the bound.
2. In `ThrottleController::onKnobRotation`, clamp `delta` before multiplying, so no caller can
   reach the overflow.
3. Replace the `while` wrap loops in `Knob::handleRotation` with modular arithmetic.
4. Unit tests for the controller and the knob with an absurd delta.

**Files:** `RotaryEncoderHal.cpp`, `ThrottleController.cpp`, `Knob.cpp`, tests

#### Acceptance Criteria

- [x] A delta beyond the plausibility bound never reaches the rotation callback
      (`RotaryEncoderHal::isPlausibleDelta`, ±24).
- [x] `onKnobRotation(knob, INT_MIN)` is well-defined: the delta is clamped before the
      multiply, and the result saturates at the end of the range.
- [x] `Knob::handleRotation` with a huge delta returns at once with a valid index.
- [ ] Unit tests `test_encoder_implausible_delta_is_refused`,
      `test_controller_extreme_delta_is_well_defined` and
      `test_knob_rotation_huge_delta_wraps_directly` pass on the board (they compile).
- [ ] Bench: log raw deltas while spinning fast, and while unplugging an encoder mid-spin; no
      out-of-bound value reaches the controller.

---

### F-20: Orchestrator acquire starts from speed 0

**Severity:** HIGH | **Effort:** Medium | **Batch:** 1

#### Description

On acquire, `Throttle::assignLocomotive` zeroes the model and
`OrchestratorBackend::acquireLocomotive` zeroes its shadow. The loco's actual state arrived in
the opening `STATE_SNAPSHOT` (and in every `LOCO_STATE` since), but `onLocoState` only routes
addresses that are already assigned, so it was discarded. Taking over a loco another operator
or an automation run has at speed 60, **one click clockwise commands speed 4; one click
anticlockwise commands reverse 4.**

WiThrottle does not have this problem: JMRI restates speed and direction after `M…+`.

#### Plan

1. `OrchestratorClient` keeps a compact cache of the latest state per address, filled from
   `STATE_SNAPSHOT` (which replaces it wholesale) and `LOCO_STATE`, cleared when the link goes
   down. Bounded in size.
2. `OrchestratorBackend::acquireLocomotive` seeds its shadow from the cache and replays the
   cached state through the throttle-state callback, exactly as a `LOCO_STATE` would arrive.
   This is display-side use of layout state: nothing is sent outward, so the "a snapshot is
   never replayed as commands" rule stands.
3. Unit test: snapshot, then acquire, then assert the update carried the snapshot's speed and
   direction.

**Files:** `OrchestratorClient.h/.cpp`, `OrchestratorBackend.cpp`, tests

#### Acceptance Criteria

- [x] Acquiring a loco the orchestrator already reports as moving shows its speed and
      direction immediately (replayed through the throttle-state callback).
- [x] The next knob click moves from that speed, not from 0 — the controller's model and
      the backend's shadow are both seeded.
- [x] Nothing is sent to the orchestrator on acquire.
- [ ] Unit tests `test_orch_backend_acquire_seeds_from_layout_state`,
      `test_orch_backend_acquire_of_unreported_loco_shows_nothing` and
      `test_orch_snapshot_replaces_cached_state` pass on the board (they compile).
- [ ] Bench: drive a loco from the web UI, take it over on the device, click once each way.

---

### F-21: Use-after-free on return to the main screen

**Severity:** HIGH | **Effort:** Medium | **Batch:** 1

#### Description

`ThrottleController::updateUI` reads the UI callback and its `userData` without a lock, and
`MainScreen::onUIUpdateNeeded` then blocks in `lvgl_port_lock(200)`. If the LVGL task is running
a Back handler at that moment, `AppController::showMainScreen()` destroys the old `MainScreen`
and builds a new one while holding the LVGL lock. Deregistering in the destructor is too late:
the encoder or network task already holds the old pointer, and when the lock frees it repaints
freed memory — heap corruption or a reboot with trains running. `PowerStatusBar`'s track-power
callback has the same shape.

#### Plan

Stop destroying the main screen. `AppController` builds one `MainScreen` for the life of the
application and `showMainScreen()` re-shows it (`lv_scr_load` plus a repaint). A screen that is
never freed cannot be used after it is freed, and the fix also closes F-32.

That makes `MainScreen::create` a one-off, which exposes a second problem: it overwrote the
WiThrottle client's single connection-state slot, which the active backend owns and the knob
gating depends on, and `JmriConfigScreen` then nulled that slot on Back. Once the main screen is
no longer rebuilt on return, nothing would put it back. So:

1. `MainScreen` no longer registers on `WiThrottleClient` at all: the backend's own callback
   already reaches `updateUI`, which repaints the main screen.
2. `JmriConfigScreen` polls its status on an LVGL timer, as `SettingsScreen` and
   `OrchestratorConfigScreen` already do, instead of taking either client's slot.

**Files:** `AppController.h/.cpp`, `MainScreen.h/.cpp`, `JmriConfigScreen.h/.cpp`

#### Acceptance Criteria

- [x] `MainScreen` is constructed once per boot; `MainScreen::create` now takes only the
      controller.
- [x] No UI class calls `setConnectionStateCallback` on a client.
- [ ] Knob gating still follows the WiThrottle link after visiting the JMRI screen and
      returning.
- [ ] Bench: navigate Settings → Back twenty times while spinning a knob, on a build with
      `CONFIG_HEAP_POISONING_COMPREHENSIVE`; no crash, no heap-corruption report.

---

### F-22: WiThrottle reconnect leaks a socket and loses the session

**Severity:** HIGH | **Effort:** Medium | **Batch:** 2

#### Description

When JMRI drops the link, `receiveTask` exits but nothing calls `disconnect()`: the socket is
never closed, the task-exit semaphore is left given, and `m_throttleStates` still records every
loco as acquired. The reconnect task then calls `connect()` directly, which overwrites
`m_socket`. Each JMRI restart or long WiFi drop leaks one of lwIP's ten sockets, and the next
`disconnect()` "joins" a task that has not exited.

Worse, the new JMRI session never has the locos acquired. The UI shows every throttle live, the
client sends `M…A` actions for throttles JMRI does not know about, and JMRI's MultiThrottle
should drop them — so the knob and the **stop** press do nothing while the loco runs on at
whatever the command station last refreshed.

#### Plan

1. `WiThrottleClient::connect()` tears down any previous socket and receive task first, and
   takes the exit semaphore to a known state before starting a new task.
2. On a fresh session, re-acquire every loco that was acquired on the old one (the client
   already records address and type per throttle). JMRI then restates speed and direction,
   which re-seeds the display.
3. Keep `m_throttleStates` consistent with what the UI shows as allocated, not with the TCP
   session: it survives any disconnect, explicit or not, and is replayed on every new
   session. Only a release removes an entry — even while disconnected, so a loco the operator
   let go is not re-acquired.

**Files:** `WiThrottleClient.h/.cpp`, possibly `JmriConnectionController.cpp`

#### Acceptance Criteria

- [x] `connect()` reaps any previous socket and receive task before opening another; teardown
      clears `m_socket` under the send mutex before closing, and the receive task reads its own
      copy of the descriptor.
- [x] The acquisition record survives a disconnect and is re-acquired on every new session;
      only `releaseLocomotive()` removes an entry, even when disconnected.
- [ ] Socket count is stable across ten JMRI restarts (lwIP stats or `socket()` fd numbers in
      the log).
- [ ] After a JMRI restart the device re-acquires its locos without operator action.
- [ ] Bench: restart JMRI with a loco running; after reconnect, the knob and stop press
      control it.

---

### F-23: WiThrottle heartbeat never armed

**Severity:** HIGH | **Effort:** Small | **Batch:** 2

#### Description

`WiThrottleClient::connect()` never sends `*+`, and nothing sends a periodic `*`. The repo's own
protocol notes call the heartbeat "CRITICAL". Without it, JMRI never e-stops this device's locos
when the device crashes, reboots, or goes half-open on WiFi — the dead-man switch is simply
off. The device also sends `HESP32-S3`, where the protocol expects `HU<unique-id>`.

#### Plan

1. Send `HU<MAC>` (a stable per-device id) and `N<name>` on connect.
2. Parse the server's `*<seconds>` announcement; if non-zero, send `*+` and then `*` at half
   that interval. Sent from the receive task, which wakes at least once a second anyway, so
   the heartbeat needs no task of its own and ends with the session.

**Files:** `WiThrottleClient.h/.cpp`

#### Acceptance Criteria

- [x] `*+` is sent once per session when the server announces a non-zero interval.
- [x] `*` is sent at half the announced interval for the life of the session, from the
      receive task (which now wakes every second).
- [x] `HU<station MAC>` replaces the malformed `HESP32-S3`.
- [ ] Unit tests `test_withrottle_heartbeat_announcement_arms_monitoring` and
      `test_withrottle_malformed_heartbeat_is_ignored` pass on the board (they compile).
- [ ] Bench: pull the ESP's power with a loco running; JMRI e-stops it within the interval.

---

### F-24: No automatic recovery after an ordinary outage

**Severity:** HIGH | **Effort:** Medium | **Batch:** 3

#### Description

- WiFi gives up after five immediate retries and stays `FAILED` for good. A router reboot takes
  longer than those retries, and nothing retries afterwards.
- The orchestrator login is attempted once at boot. With the layout on one switch, the host
  comes up after the ESP and the device sits at `FAILED`.
- JMRI auto-connect waits 30 s for WiFi and then exits without ever starting the reconnect task.

In each case the operator has no control until they fix it by hand in settings.

#### Plan

1. `WiFiManager`: after the fast retries, keep retrying with a capped backoff until the
   operator disconnects or forgets the network.
2. Orchestrator: a supervising task that retries login and socket with backoff until
   connected, and re-logs in (rather than relying on the WebSocket's cookie-reusing
   auto-reconnect) when the socket stays down.
3. JMRI: wait for WiFi indefinitely and keep reconnecting once settings exist — done by the
   single `jmri_conn` task F-34 introduces.

**Files:** `WiFiManager.h/.cpp`, `AppController.h/.cpp`, `JmriConnectionController.cpp`

#### Acceptance Criteria

- [x] `WiFiManager` keeps retrying after the immediate retries (one-shot `esp_timer`, 5 s
      doubling to 60 s) until Disconnect or Forget, both of which the WiFi screen now offers
      while it is in `FAILED`.
- [x] `orch_connect` supervises for the life of the app: waits for WiFi indefinitely, retries
      a failed login with backoff, logs in afresh after 30 s without a socket, retries a failed
      roster read. The config screen's Connect wakes it instead of connecting itself.
- [x] `jmri_conn` waits for WiFi indefinitely (see F-34).
- [ ] Bench: power-cycle the router with the device running; it reconnects without a touch.
- [ ] Bench: boot the device before the orchestrator host; it connects when the host is up.
- [ ] Bench: same for JMRI.

---

### F-25: Physical knobs not gated; optimistic update outlives a failed send

**Severity:** MEDIUM | **Effort:** Small | **Batch:** 4

#### Description

CLAUDE.md says K1/K2 are disabled while the transport is disconnected, but only the touch path
checks: the encoder path (`AppController` → `ThrottleController::onKnobRotation`) does not. The
model is also updated before the send, and `sendSpeedCommand` ignores the result, so a refused
command leaves the display ahead of the loco. The orchestrator refuses manual commands while
`offline`; the display creeps to 40 with the loco at 0, and the first click afterwards sends 44.

#### Plan

1. `onKnobRotation` and the `CONTROLLING` branch of `onKnobPress` refuse when the backend is not
   connected.
2. Roll the model back to the pre-command value when the send fails, and repaint.

**Files:** `ThrottleController.cpp`, tests

#### Acceptance Criteria

- [x] A knob turn or press while disconnected changes nothing (`knobInputAllowed`, in the
      controller, so the physical encoders are covered).
- [x] A failed send rolls the model back — unless a transport report has landed since.
- [x] The orchestrator's case is a *refused* command, not a failed send: it answers `ERROR`.
      `OrchestratorBackend` now re-seeds every assigned throttle from the layout's last
      reported state on any `ERROR` (the F-20 cache makes that possible).
- [ ] Unit tests `test_controller_knob_ignored_while_disconnected`,
      `test_controller_failed_send_rolls_back` and
      `test_orch_backend_refusal_reseeds_from_layout_state` pass on the board (they compile).
- [ ] Bench: with the orchestrator `offline`, turn a knob; the display returns to the layout's
      speed. Pull the WiFi and turn a physical knob; nothing moves on screen.

---

### F-26: `OrchestratorClient::m_client` destroyed under a sender

**Severity:** MEDIUM | **Effort:** Medium | **Batch:** 4

#### Description

`connect()` calls `disconnect()`, which stops and destroys the WebSocket handle, while
`sendJson` on the encoder task dereferences `m_client` without a lock. Pressing Connect on the
orchestrator screen while driving — or the screen's connect racing the boot `orch_connect` task
— is a use-after-free inside `esp_websocket_client_send_text`. The "already connecting" check is
also check-then-act, so two connect tasks can both pass it.

#### Plan

Guard the handle with its own mutex held across send and across stop/destroy (sends are
bounded at 1 s), and make the connecting check atomic.

**Progress:** Batch 3 made the `orch_connect` supervisor the only caller of `connect()` — the
config screen now wakes it rather than connecting on a task of its own — so two connects can
no longer race. Batch 4 added the handle mutex, so a re-login can no longer free the handle
under a sender.

**Files:** `OrchestratorClient.h/.cpp`

#### Acceptance Criteria

- [x] No path reads `m_client` without the handle mutex: sends take it (bounded, and only
      once the link is known up), `openSocket` publishes under it, and `disconnect()` holds it
      across stop and destroy.
- [x] The connecting check need not be atomic any more: the supervisor is the only caller of
      `connect()` (batch 3).
- [ ] Bench: press Connect repeatedly while spinning a knob; no crash.

---

### F-27: No emergency stop; power button fails the wrong way

**Severity:** MEDIUM | **Effort:** Medium | **Batch:** 4

#### Description

`OrchestratorClient::sendEmergencyStop` exists but nothing calls it, and nothing sends
WiThrottle's `X`. A loco with no knob assigned cannot be stopped without assigning one. The
track-power toggle turns power **on** when the state is unknown — the wrong default for a panic
press — and under WiThrottle it depends on the separate JSON link, so it silently does nothing
when that link is down.

#### Plan

1. Add `emergencyStop()` to the `ThrottleBackend` port (orchestrator: `EMERGENCY_STOP`;
   WiThrottle: `X` on every acquired throttle) and a dedicated on-screen control.
2. An unknown power state turns power off, not on.
3. Under WiThrottle, an OFF goes over `PPA0` when the JSON link is down.

**Files:** `ThrottleBackend.h`, both backends, `ThrottleController`, `MainScreen`,
`PowerStatusBar.cpp`

#### Acceptance Criteria

- [x] `ThrottleBackend::emergencyStop()`; `ThrottleController::emergencyStop()` shows the
      throttles stopped only once it was sent.
- [x] An **E-STOP** button in the main screen's bottom row, in `UiTheme::BUTTON_EMERGENCY` —
      the one saturated button — firing on press rather than on click.
- [x] A press on an unknown power state turns power off.
- [x] Under WiThrottle, OFF falls back to `PPA0` when the JSON link is down; ON still waits for
      it. The button shows WiThrottle's `PPA` state when JSON has none.
- [ ] Unit test `test_controller_emergency_stop` passes on the board (it compiles).
- [ ] Bench: E-STOP under each transport; check the button's placement against the carousel
      and the icon buttons (laid out without a screen to look at).
- [ ] Bench: stop JMRI's web server, press the power button; power goes off over WiThrottle.

---

### F-28: Every function is momentary under the orchestrator

**Severity:** MEDIUM | **Effort:** Small | **Batch:** 4

#### Description

The UI sends `true` on press and `false` on release. That is right for WiThrottle, where JMRI
applies each function's latching setting. The orchestrator stores `FUNCTION_COMMAND.state` as
the new absolute state (`LayoutService.handleFunctionCommand`), so headlights and sound only stay
on while a finger is held down.

#### Plan

A port capability — press/release events versus absolute state — and a controller method that
toggles from the known state on press for state-style backends and ignores the release.

**Files:** `ThrottleBackend.h`, both backends, `ThrottleController`, `MainScreen.cpp`, tests

#### Acceptance Criteria

- [x] `ThrottleBackend::functionCommandIsButtonEvent()`: true for WiThrottle, false for the
      orchestrator.
- [x] The UI reports the button to `ThrottleController::onFunctionButton()`, which toggles on
      press for a state-style transport and ignores the release.
- [ ] Unit tests `test_controller_function_press_release_for_event_backend`,
      `test_controller_function_toggles_for_state_backend` and
      `test_orch_backend_function_commands_are_states` pass on the board (they compile).
- [ ] Bench: under the orchestrator, tap F0; the headlight stays on until tapped again.

---

### F-29: Documented lock order is the reverse of the code's

**Severity:** MEDIUM | **Effort:** Small | **Batch:** 4

#### Description

CLAUDE.md (hard constraint 3) and `THREADING_MODEL.md` say `m_stateMutex` before
`lvgl_port_lock`, and the threading diagram shows `uiUpdateCallback` running inside the state
lock. The code does the reverse throughout and does it consistently: `onUIUpdateNeeded` takes
the LVGL lock and then `getThrottleSnapshot` takes the state mutex, and every LVGL event handler
calls into the controller the same way; the controller never calls out to the UI while holding
its own mutex. A contributor following the documented rule would introduce the deadlock it
claims to prevent.

#### Plan

Correct CLAUDE.md, `THREADING_MODEL.md` (text and diagram), and any other copy of the rule to
"LVGL lock before `m_stateMutex`; the controller never calls the UI while holding its mutex".

#### Acceptance Criteria

- [x] CLAUDE.md hard constraint 3, `THREADING_MODEL.md` (text and sequence diagram),
      `.claude/agents/firmware-review.md` and `CONTROLLER_LAYER.md` all state the order the code
      uses. No copy of the old rule remains.

---

### F-30: Callback slots unsynchronised and clobbered

**Severity:** MEDIUM | **Effort:** Medium | **Batch:** 1 (clobbering), 5 (synchronisation)

#### Description

On `WiThrottleClient`, `JmriJsonClient`, `WiFiManager` and `ThrottleController`, a
`std::function` (or function pointer and `userData` pair) is assigned on the LVGL task and
invoked in place from network tasks, so a torn pair or a closure destroyed mid-call is possible.
Separately, `MainScreen` and `JmriConfigScreen` overwrote the WiThrottle client's single
connection slot that the backend owns — the F-08 problem the orchestrator screens were written
to avoid. `OrchestratorClient` copies callbacks under its mutex and invokes the copy; that is
the pattern to follow.

#### Plan

1. Batch 1: stop the UI taking backend-owned slots (see F-21).
2. Batch 5: copy-under-lock for every remaining slot.

---

### F-31: JMRI heartbeat task deleted from outside

**Severity:** MEDIUM | **Effort:** Small | **Batch:** 2

#### Description

`JmriJsonClient::stopHeartbeat` calls `vTaskDelete` on a task that may be inside
`esp_websocket_client_send_text`, holding the client's lock — the F-02 shape again. It is
called from both the WebSocket task and `disconnect()` with an unguarded handle, so the same
handle can be deleted twice. The task's stack is 2 KB with `ESP_LOG` in its path.

#### Plan

Cooperative shutdown with an exit signal, as F-02 did for the WiThrottle receive task, and a
3 KB stack pending F-33's measurement.

#### Acceptance Criteria

- [x] The task is woken to exit by a notification, signals, and suspends itself;
      `stopHeartbeat()` deletes it only after that signal, under a mutex that also guards
      `startHeartbeat()`.
- [x] Stack raised to 3 KB.
- [ ] Bench: repeated JMRI JSON connect/disconnect (twenty cycles) with no hang and no heap
      growth.

---

### F-32: Main screen LVGL tree leaks on every return

**Severity:** MEDIUM | **Effort:** Small | **Batch:** 1

#### Description

`lv_scr_load` does not free the previous screen — the comment in `MainScreen::~MainScreen`
says it does — and nothing else freed the main screen's tree. Every return to it leaked four
meters, the power bar, the carousel and a 29-button function panel. Small LVGL allocations come
from internal RAM first, the same heap task stacks and WiFi need. F-03's "20 round trips"
check was never ticked.

#### Plan

Closed by F-21's fix: the main screen is built once and re-shown.

#### Acceptance Criteria

- [ ] Bench: `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` is stable across twenty
      Settings → Back round trips.

---

### F-33: Task stack headroom unmeasured

**Severity:** MEDIUM | **Effort:** Small | **Batch:** 5

#### Description

`rotary_enc` (3 KB) and `withrottle_rx` (4 KB) run the whole `updateAllThrottles` LVGL repaint
on their own stacks; the receive task also ran `JmriJsonClient::connect` from the `PW` callback
(see F-34). A too-small stack fails as corruption, not as an error.

#### Plan

Log `uxTaskGetStackHighWaterMark` for every application task on the bench under load, record
the results in `THREADING_MODEL.md`, and resize with a margin.

---

### F-34: JMRI config screen connect/disconnect faults

**Severity:** MEDIUM | **Effort:** Medium | **Batch:** 3

#### Description

- Disconnect runs on the LVGL task and blocks it for up to about two seconds.
- Disconnect does not stick: the reconnect task undoes it within five seconds.
- `setWebPortCallback` is registered after `connect()` has started the receive task, so it
  races the `PW` line.
- That callback reconnects the JSON client on the WiThrottle receive task, concurrently with
  the reconnect task — a possible double `esp_websocket_client_destroy`.
- A manual connect never updates `JmriConnectionController`'s saved address or enables
  auto-reconnect, so later reconnects go to whatever was loaded at boot.

#### Plan

Route both the screen's connect and disconnect through `JmriConnectionController`, which owns
the saved settings, the auto-reconnect flag and the one task that connects.

#### Acceptance Criteria

- [x] One task, `jmri_conn`, carries out every JMRI connect and disconnect, including the
      screen's; `jmri_autoconn`, `jmri_reconnect` and the screen's `jmri_connect` are gone.
- [x] Disconnect and Connect return at once on the LVGL task; saving happens on the worker.
- [x] Disconnect turns reconnecting off until the next Connect.
- [x] The `PW` callback is registered once, before any connect, and only records the port;
      the worker saves it as `json_port` and moves the JSON client.
- [x] A manual connect updates the settings the worker reconnects to.
- [x] Under the orchestrator, Connect saves only (one-shot `jmri_save` task).
- [ ] Bench: Connect, Disconnect (stays down past ten seconds), Connect again; change server
      address and confirm reconnects go to the new one.

---

### F-35: Orchestrator roster refused above ~35 locos

**Severity:** MEDIUM | **Effort:** Small | **Batch:** 5

#### Description

`HTTP_RESPONSE_LIMIT` is 8 KB and each `LocoRecord` is about 230 bytes (two UUIDs among its
fields). A truncated response is refused whole, so a larger roster leaves nothing selectable.

#### Plan

Raise the limit for the roster read (allocating from PSRAM), or parse incrementally keeping
only address and name.

---

### F-36: Operator credential and session cookie in cleartext

**Severity:** MEDIUM | **Effort:** Small (decision) | **Batch:** 5

#### Description

The login POST, the session cookie and the control plane travel over plain HTTP and WS.
`SESSION_TTL_MS` is 30 days with sliding refresh, so a cookie captured on the layout WiFi
drives trains for a month. Storing it in plaintext NVS is the accepted F-18 risk; nothing
records a decision about cleartext on the wire.

#### Plan

Record the decision in CLAUDE.md's transport section. If TLS is wanted, size it with
`idf.py size` first against the flash budget.

---

### F-37: WiThrottle updates unvalidated

**Severity:** LOW | **Effort:** Small | **Batch:** 5

- `M…A` updates are applied without checking the address against the throttle's current loco,
  so a late reply for the previous loco can set the wrong baseline.
- Function numbers from the server are unbounded, so `Throttle::setFunctionState` and the
  function panel grow without limit.

---

### F-38: Hot-path logging and repaint cost

**Severity:** LOW | **Effort:** Small | **Batch:** 5

- Every received WiThrottle line, knob tick and throttle update logs at INFO; console writes
  block the encoder and receive tasks.
- `PowerStatusBar::refresh()` runs four times per repaint, and labels are rewritten (and so
  invalidated) even when unchanged.

---

### F-39: NVS writes on the LVGL task

**Severity:** LOW | **Effort:** Small | **Batch:** 5

`JmriConfigScreen::saveSettings`, the orchestrator screen's save and `saveSpeedSteps` still
write NVS from event handlers, against F-05's rule.

**Progress:** the JMRI settings are now saved by `JmriConnectionController` on its own task
(batch 3, F-34). The orchestrator screen's save, `saveSpeedSteps`, and the WiFi screen's
Forget remain.

---

### F-40: WiFi credential save and reboot-on-error

**Severity:** LOW | **Effort:** Small | **Batch:** 3

`WiFiManager::connect` saves credentials before it knows they work, overwriting a good password
with a typo; and `ESP_ERROR_CHECK(esp_wifi_set_config)` turns a recoverable error into a reboot.

#### Acceptance Criteria

- [x] Credentials entered on the screen are saved on `IP_EVENT_STA_GOT_IP`, read back from the
      driver; the stored-credential path saves nothing.
- [x] A failed `esp_wifi_set_config` reports `FAILED` and returns the error.
- [ ] Bench: enter a wrong password over a working network, reboot; the device rejoins the
      working network.

---

### F-41: Seesaw read timing

**Severity:** LOW | **Effort:** Small | **Batch:** 5

The double read compensates for a missing write → delay → read sequence (the Seesaw needs time
to prepare a response). Rotation during the 5 ms between reads is lost. Split each transaction
into a write, a short delay and a read, and drop the double read — bench-verify before merging.

---

### F-42: Protocol hygiene odds and ends

**Severity:** LOW | **Effort:** Small | **Batch:** 5

- `JmriJsonClient::requestPowerList` (unused) treats a byte count as an error and blocks with
  `portMAX_DELAY`.
- Several `OrchestratorClient` lock-timeout fallbacks write shared strings unlocked.
- WebSocket continuation frames are accepted but the buffer is cleared per fragment (fails safe).
- `getLayoutId` always uses the first layout and is not cleared when the host changes.
- `handleRosterMessage`'s warning can read past the end of a malformed message.

---

### F-43: CI hardening

**Severity:** LOW | **Effort:** Small | **Batch:** 5

No `permissions:` block, actions pinned by tag rather than SHA, and
`espressif/esp_websocket_client: '*'` in the manifest (the lock file pins it).

---

### F-44: Threading-model task table drift

**Severity:** LOW | **Effort:** Small | **Batch:** 5

`THREADING_MODEL.md`'s table is missing `wifi_scan` (4 KB), and will need the measured figures
from F-33. (It was also missing `jmri_connect`; batch 3 removed that task, folding it into
`jmri_conn`, which the table now lists.)
