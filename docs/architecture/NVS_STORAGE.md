# NVS Storage Reference

All persistent configuration is stored in ESP-IDF's NVS (Non-Volatile Storage).

## Namespace Map

| Namespace | Key | Type | Default | Written by | Read by |
|-----------|-----|------|---------|------------|---------|
| `wifi` | `ssid` | string | — | `WiFiManager` | `WiFiManager` |
| `wifi` | `password` | string | — | `WiFiManager` | `WiFiManager` |
| `jmri` | `server_ip` | string | — | `JmriConfigScreen` | `JmriConnectionController` |
| `jmri` | `wt_port` | string | `"12090"` | `JmriConfigScreen` | `JmriConnectionController` |
| `jmri` | `json_port` | string | `"12080"` | `JmriConnectionController` | `JmriConnectionController` |
| `jmri` | `power_mgr` | string | `"DCC++"` | `JmriConfigScreen` | `JmriConnectionController` |
| `jmri` | `speed_steps` | i32 | `4` | `JmriConfigScreen` | `ThrottleController` |
| `orch` | `transport` | u8 | `0` (WiThrottle) | `JmriConfigScreen` | `AppController` |
| `orch` | `host` | string | — | `OrchestratorConfigScreen` | `AppController` |
| `orch` | `port` | u16 | `3000` | `OrchestratorConfigScreen` | `AppController` |
| `orch` | `user` | string | — | `OrchestratorConfigScreen` | `AppController` |
| `orch` | `pass` | string | — | `OrchestratorConfigScreen` | `AppController` |

## Notes

### The `orch` namespace

`transport` selects which transport drives locomotives: `0` = WiThrottle, `1` = the layout
orchestrator. It is read once, in `AppController::initialise()`, before anything connects —
it decides which network stack comes up at all, not merely which one drives locos. Changing
it therefore takes effect on restart, which the config screen says plainly.

Two refusals are deliberate, both in `TransportSettings::load()`:

- An **unrecognised** stored value falls back to WiThrottle rather than being cast blindly
  into the enum.
- The orchestrator being selected **without a host and credential** falls back to WiThrottle
  too. Booting into a transport that cannot possibly connect would leave every knob dead
  with nothing on screen explaining why.

`pass` is the orchestrator `operator` account password, stored in **plaintext**. This is the
same accepted and documented risk as the WiFi password (F-18), not an oversight. It is
masked on screen and never logged.

- **WiFi credentials** are saved on successful connection and loaded on boot for auto-connect.
- **JMRI settings** are saved when the user presses "Connect" on the JMRI config screen. The `json_port` is typically discovered automatically from the WiThrottle `PW` message rather than configured manually.
- **Speed steps per click** (1–20) controls how many speed steps each encoder detent applies. Higher values = coarser control. Configurable from the JMRI settings screen.
