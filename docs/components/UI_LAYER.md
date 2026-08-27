# UI Layer

## Overview

All UI code uses LVGL (Light and Versatile Graphics Library). The UI layer holds **raw pointers** to controllers and never owns application state. Screens can be destroyed and recreated freely.

---

## Colour: UiTheme

**File:** `main/ui/UiTheme.h`

The one place UI colours are defined. Before it, the app mixed two systems — raw hex like
`0x00AA00` on the config screens and `lv_palette_main(LV_PALETTE_GREEN)` on the widgets — and
the two never quite matched.

Buttons are **desaturated**, so a screenful reads as one surface rather than a set of
warnings. Status *text* stays brighter, because a one-line label has far less area to carry
its meaning with.

Names say what a colour means, not what it looks like: a red button is `BUTTON_DESTRUCTIVE`,
so changing the shade is one edit here and none anywhere else.

| Group | Members |
|-------|---------|
| Buttons | `BUTTON_PRIMARY`, `BUTTON_POSITIVE`, `BUTTON_DESTRUCTIVE`, `BUTTON_CAUTION`, `BUTTON_NEUTRAL` |
| State indicators | `STATE_ACTIVE`, `STATE_ALTERNATE`, `STATE_INACTIVE`, `STATE_FAULT` |
| Text | `TEXT_OK`, `TEXT_WARNING`, `TEXT_ERROR`, `TEXT_MUTED`, `TEXT_LABEL` |
| Surfaces | `SURFACE_SCREEN`, `SURFACE_PANEL`, `SURFACE_OVERLAY` |

`ThrottleMeter`'s gauge chrome — needle, ticks, the indicator dot — deliberately keeps LVGL's
own greys. They are structural rather than semantic, and already muted.

**Screen shape is also a convention:** scrolling content above, a fixed button bar pinned to
the bottom. Every screen follows it.

---

## Screens

### MainScreen

**File:** `main/ui/MainScreen.cpp/h`

**Purpose:** Primary application screen — 2×2 throttle grid with right-side panels.

**Layout:**

```
┌─────────────────────────┬──────────────────┐
│  ThrottleMeter[0]       │  PowerStatusBar  │
│  ThrottleMeter[1]       │  RosterCarousel  │
├─────────────────────────┤  FunctionPanel   │
│  ThrottleMeter[2]       │                  │
│  ThrottleMeter[3]       │  (Virtual        │
│                         │   EncoderPanel)  │
└─────────────────────────┴──────────────────┘
```

**Dependencies (raw pointers, not owned):**
- `WiThrottleClient*`
- `JmriJsonClient*`
- `ThrottleController*`

**Key Methods:**

| Method | Description |
|--------|-------------|
| `create(WT*, JC*, TC*)` | Build LVGL widget tree, register callbacks |
| `updateThrottle(id)` | Refresh one throttle meter from snapshot |
| `updateAllThrottles()` | Refresh all meters + roster carousel |

**Event Handlers (static):**

| Handler | Trigger | Action |
|---------|---------|--------|
| `onKnobIndicatorTouched` | Knob L/R button tap | `TC::onKnobIndicatorTouched()` |
| `onFunctionsButtonClicked` | "Functions" button | Show `FunctionPanel` |
| `onReleaseButtonClicked` | "Release" button | `TC::onThrottleRelease()` |
| `onVirtualEncoderRotation` | Virtual encoder ±buttons | `TC::onKnobRotation()` |
| `onVirtualEncoderPress` | Virtual encoder press | `TC::onKnobPress()` |
| `onFunctionButtonClicked` | Function toggle | `TC::setFunction()` — through the port, not a client |
| `onSettingsButtonClicked` | Settings gear icon | Navigate to WiFiConfigScreen |
| `onJmriButtonClicked` | Settings icon | Navigate to SettingsScreen |

**UI Update Callback:** Registered with `ThrottleController::setUIUpdateCallback()`. Acquires `lvgl_port_lock(200)` before calling `updateAllThrottles()`.

---

### WiFiConfigScreen

**File:** `main/ui/WiFiConfigScreen.cpp/h`

**Purpose:** WiFi network configuration with on-screen keyboard.

**Constructor:** `WiFiConfigScreen(WiFiManager& wifiManager)`

**Features:**
- AP scanning and display
- SSID/password input with LVGL keyboard
- Connect / disconnect / forget network
- Status display (IP address, connection state)
- Credentials saved to NVS via `WiFiManager`

**Navigation:** Back button → `close_wifi_config_screen()` → `show_main_screen()`

---

### JmriConfigScreen

**File:** `main/ui/JmriConfigScreen.cpp/h`

**Purpose:** JMRI server connection settings. **Only JMRI** — the transport choice, speed
steps and system status moved to `SettingsScreen` when the second transport landed, because a
screen titled "JMRI Server Configuration" had no business owning any of them.

**Config fields:**
- Server IP address
- WiThrottle port
- Power manager name

**JMRI connections panel:** WiThrottle and JMRI JSON status. Device-wide status (software,
hardware, WiFi, encoders) is on the settings screen, not duplicated here.

**Connect flow:** Connects WiThrottle first; when the server sends back the `PW` (web port) message, auto-connects the JSON client using the discovered port.

**Navigation:** Back button → `show_settings_screen()`

---

### SettingsScreen

**File:** `main/ui/SettingsScreen.cpp/h`

**Purpose:** Device settings, and the front door to each transport's own config. This is what
the main screen's settings button opens.

It exists because the JMRI config screen had grown into two unrelated things: it was titled
"JMRI Server Configuration" while owning the **global** choice of transport, and its System
Status section knew nothing about the orchestrator.

Holds the transport dropdown, buttons through to both transport config screens, speed steps
per click (a property of the encoder, not of either transport), and System Status.

**The status rows follow the selected transport**, so the screen never reports on a link the
device is not bringing up: `Control plane` under the orchestrator, `WiThrottle` + `JMRI JSON`
under JMRI. Software, hardware, WiFi and the encoders are shown either way.

Both config screens stay reachable whichever transport is selected, so one can be set up
before being switched to -- the dropdown refuses an unconfigured orchestrator.

**Navigation:** Back -> `show_main_screen()`; the two buttons -> the config screens, which
return here.

---

### OrchestratorConfigScreen

**File:** `main/ui/OrchestratorConfigScreen.cpp/h`

**Purpose:** Layout orchestrator connection settings — host, port, and the `operator`
username and password. Its own screen rather than a section of the JMRI one: the two
transports are peers, and this one needs four fields including a credential.

The password field is masked on screen. It is still plaintext in NVS — the accepted F-18
risk.

**Connect flow:** Save & Connect writes NVS, then does the login and roster fetch **on its
own task** (`orch_ui_conn`). The login is a blocking HTTP round trip; running it on the LVGL
task would freeze every throttle at once (F-05).

**Navigation:** Back button → `show_settings_screen()`

---

## UI Components

### PowerStatusBar

**File:** `main/ui/components/PowerStatusBar.cpp/h`

**Purpose:** Track power button and link status, driven by the **active transport** through
`ThrottleController` — never by a concrete client.

It previously read `JmriJsonClient` directly, so under the orchestrator transport the button
did nothing and the label read "Disconnected" while the layout was in fact connected.

- A transport answering `supportsTrackPower() == false` gets the button **hidden**, not left
  dead for the operator to press and wonder about.
- `TrackPower::UNKNOWN` renders as its own state ("Power ?"), not as off.
- The press returns immediately: the orchestrator's power command is a blocking HTTP round
  trip, so the write happens on a short-lived task (F-05). The button repaints when the
  layout says power changed, not when we asked.

---

### ThrottleMeter

**File:** `main/ui/components/ThrottleMeter.cpp/h`

**Purpose:** Circular gauge widget displaying one throttle's state.

**Visual elements:**
- Needle indicator (speed)
- Colour-coded arc zones
- L/R knob indicator buttons (disabled while the **active transport** is disconnected)
- Direction indicator (F/R)
- Locomotive name and address labels
- "Functions" and "Release" buttons

**Constructor:** `ThrottleMeter(lv_obj_t* parent, float scale)`

**Configurable callbacks via setters:**
- `setKnobTouchCallback(lv_event_cb_t)`
- `setFunctionsCallback(lv_event_cb_t)`
- `setReleaseCallback(lv_event_cb_t)`

**Constants:** `BASE_SIZE = 200`, `EXTRA_HEIGHT = 60`

---

### RosterCarousel

**File:** `main/ui/components/RosterCarousel.cpp/h`

**Purpose:** Displays the currently selected roster entry during knob SELECTING mode. Shows loco name and address with navigation arrows.

**Key Method:** `update(ThrottleController*)` — reads `RosterSelectionSnapshot` to display current selection. Hidden when no knob is in SELECTING state.

---

### FunctionPanel

**File:** `main/ui/components/FunctionPanel.cpp/h`

**Purpose:** Scrollable overlay panel showing F0–F28 toggle buttons for the selected throttle.

**Key Methods:**

| Method | Description |
|--------|-------------|
| `create(parent, closeCallback, userData)` | Build panel structure |
| `show(throttleId, locoName, functions)` | Populate and display |
| `hide()` | Hide panel |
| `isScrolling()` | Guard against accidental presses while scrolling |

**Callbacks:** `setFunctionCallback(lv_event_cb_t, userData)` — fires on `LV_EVENT_PRESSED` and `LV_EVENT_RELEASED`.

---

### PowerStatusBar

**File:** `main/ui/components/PowerStatusBar.cpp/h`

**Purpose:** Track power toggle button + JMRI JSON connection status label.

**Key Method:** `create(parent, JmriJsonClient*)` — creates button and registers click handler that calls `JmriJsonClient::setPower()`.

---

### VirtualEncoderPanel

**File:** `main/ui/components/VirtualEncoderPanel.cpp/h`

**Purpose:** On-screen buttons simulating 2 rotary encoders for testing without physical hardware.

**Compile guard:** `#if ENABLE_VIRTUAL_ENCODER`

**Key Method:** `create(parent, rotationCallback, pressCallback, userData)` — creates L/R rotation buttons and press button for each virtual knob.

---

## Wrappers

**Directory:** `main/ui/wrappers/`

C-linkage (`extern "C"`) functions that bridge `main.c` and inter-screen navigation without C++ name mangling.

| File | Functions | Purpose |
|------|-----------|---------|
| `main_screen_wrapper.cpp/h` | `init_app_controller()`, `show_main_screen()` | App init + main screen |
| `wifi_config_wrapper.cpp/h` | `show_wifi_config_screen()`, `close_wifi_config_screen()`, `is_wifi_connected()` | WiFi settings |
| `settings_wrapper.cpp/h` | `show_settings_screen()` | Device settings (the main screen's settings button) |
| `jmri_config_wrapper.cpp/h` | `show_jmri_config_screen()`, `jmri_auto_connect()` | JMRI settings |
| `orchestrator_config_wrapper.cpp/h` | `show_orchestrator_config_screen()` | Orchestrator settings |
