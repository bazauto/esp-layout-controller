# UI Layer

## Overview

All UI code uses LVGL (Light and Versatile Graphics Library). The UI layer holds **raw pointers** to controllers and never owns application state. Screens can be destroyed and recreated freely.

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
| `onFunctionButtonClicked` | Function toggle | `WT::setFunction()` |
| `onSettingsButtonClicked` | Settings gear icon | Navigate to WiFiConfigScreen |
| `onJmriButtonClicked` | JMRI icon | Navigate to JmriConfigScreen |

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

**Purpose:** JMRI server connection settings and system status display.

**Constructor:** `JmriConfigScreen(JmriJsonClient&, WiThrottleClient&, WiFiController*, RotaryEncoderHal*)`

**Config fields:**
- Server IP address
- WiThrottle port
- Power manager name
- Speed steps per click (1–20)

**System status panel:**
- Software version, hardware revision
- WiFi / WiThrottle / JSON connection status indicators
- Encoder 1/2 presence indicators

**Connect flow:** Connects WiThrottle first; when the server sends back the `PW` (web port) message, auto-connects the JSON client using the discovered port.

**Navigation:** Back button → `show_main_screen()`

---

## UI Components

### ThrottleMeter

**File:** `main/ui/components/ThrottleMeter.cpp/h`

**Purpose:** Circular gauge widget displaying one throttle's state.

**Visual elements:**
- Needle indicator (speed)
- Colour-coded arc zones
- L/R knob indicator buttons
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
| `jmri_config_wrapper.cpp/h` | `show_jmri_config_screen()`, `jmri_auto_connect()` | JMRI settings |
