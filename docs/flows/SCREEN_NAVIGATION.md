# Screen Navigation

## Overview

Three screens exist. Navigation is managed through `AppController` and C-linkage wrapper functions that bridge `main.c` (C) and the C++ screen classes.

---

## Navigation Map

```mermaid
flowchart TD
    MS["MainScreen\n(main throttle UI)"]
    WCS["WiFiConfigScreen\n(network settings)"]
    JCS["JmriConfigScreen\n(JMRI settings + system status)"]

    MS -->|"Settings button\nshow_wifi_config_screen()"| WCS
    MS -->|"JMRI button\nshow_jmri_config_screen()"| JCS
    WCS -->|"Back button\nclose_wifi_config_screen()\n→ show_main_screen()"| MS
    JCS -->|"Back button\nshow_main_screen()"| MS
```

## Screen Lifecycle

```mermaid
sequenceDiagram
    participant User
    participant AC as AppController
    participant MS as MainScreen
    participant WCS as WiFiConfigScreen
    participant TC as ThrottleController

    Note over TC: State persists across screen changes

    AC->>MS: new + create(WT*, JC*, TC*)
    Note over MS: Active — showing throttles

    User->>MS: Press "Settings"
    MS->>AC: show_wifi_config_screen()
    AC->>MS: delete MainScreen
    AC->>WCS: new WiFiConfigScreen(WiFiManager&)
    AC->>WCS: create()
    Note over WCS: Active — showing WiFi config

    User->>WCS: Press "Back"
    WCS->>AC: close_wifi_config_screen() → show_main_screen()
    AC->>WCS: delete WiFiConfigScreen
    AC->>MS: new MainScreen()
    AC->>MS: create(WT*, JC*, TC*)
    Note over MS: Rebuilt from scratch — state intact from TC
```

## Key Points

- **MainScreen is recreated** each time `showMainScreen()` is called. This is safe because all state lives in `ThrottleController`.
- **Config screens are heap-allocated** and deleted when navigating away.
- **Wrapper functions** (`show_main_screen()`, `show_wifi_config_screen()`, etc.) provide `extern "C"` linkage so `main.c` and inter-screen navigation work without C++ name mangling.
- **LVGL lock** must be held when creating/destroying screens (handled by the callers).

## Wrapper Function Reference

| Wrapper | Defined in | Calls |
|---------|-----------|-------|
| `init_app_controller()` | `main_screen_wrapper.cpp` | `AppController::instance().initialise()` |
| `show_main_screen()` | `main_screen_wrapper.cpp` | `AppController::instance().showMainScreen()` |
| `show_wifi_config_screen()` | `wifi_config_wrapper.cpp` | `AppController::instance().showWiFiConfigScreen()` |
| `close_wifi_config_screen()` | `wifi_config_wrapper.cpp` | Calls `show_main_screen()` |
| `show_jmri_config_screen()` | `jmri_config_wrapper.cpp` | `AppController::instance().showJmriConfigScreen()` |
