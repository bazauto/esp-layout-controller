# Startup Flow

## Sequence

```mermaid
sequenceDiagram
    participant main as main.c
    participant HW as LCD/Touch/I2C
    participant AC as AppController
    participant WC as WiFiController
    participant WM as WiFiManager
    participant WT as WiThrottleClient
    participant JC as JmriJsonClient
    participant JCC as JmriConnectionController
    participant TC as ThrottleController
    participant RE as RotaryEncoderHal
    participant MS as MainScreen

    main->>HW: waveshare_esp32_s3_rgb_lcd_init()
    Note over HW: LCD, touch, I2C bus, LVGL initialised

    main->>AC: init_app_controller()
    activate AC

    AC->>WC: initialize() + autoConnect()
    WC->>WM: initialize() + connect()
    Note over WM: Load NVS creds → WiFi STA connect

    AC->>WT: initialize()
    AC->>JC: initialize()

    AC->>JCC: new(JmriJsonClient, WiThrottleClient, WiFiController)
    AC->>JCC: startAutoConnectTask()
    Note over JCC: FreeRTOS task: wait WiFi → load NVS → connect both clients

    AC->>TC: new(WiThrottleClient)
    AC->>TC: initialize()
    Note over TC: Start 10s polling timer

    AC->>RE: initialise()
    Note over RE: I2C scan for encoders at 0x76, 0x77
    AC->>RE: setRotationCallback → TC::onKnobRotation
    AC->>RE: setPressCallback → TC::onKnobPress
    AC->>RE: startPollingTask()
    Note over RE: FreeRTOS task: poll every 100ms

    deactivate AC

    main->>main: lvgl_port_lock(-1)
    main->>AC: show_main_screen()
    AC->>MS: new MainScreen()
    AC->>MS: create(WT*, JC*, TC*)
    Note over MS: Build LVGL widgets, register UI update callback
    main->>main: lvgl_port_unlock()
```

## Key Points

1. **Hardware first** — LCD, touch, and I2C bus are initialised before any application code runs.
2. **WiFi auto-connect** — attempts immediately using stored NVS credentials. Non-blocking.
3. **JMRI auto-connect** — runs in a background task that waits up to 30 s for WiFi before attempting.
4. **Encoder polling** — starts regardless of whether physical encoders are detected. Missing encoders are logged but don't block startup.
5. **UI last** — the main screen is created after all services are initialised, ensuring it can safely reference all controllers.
6. **Test mode** — when `CONFIG_THROTTLE_TESTS` is set in Kconfig, `app_main()` calls `run_throttle_tests()` instead of the above sequence.
