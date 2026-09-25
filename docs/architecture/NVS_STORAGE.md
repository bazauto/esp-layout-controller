# NVS Storage Reference

All persistent configuration is stored in ESP-IDF's NVS (Non-Volatile Storage).

## Namespace Map

| Namespace | Key | Type | Default | Written by | Read by |
|-----------|-----|------|---------|------------|---------|
| `wifi` | `ssid` | string | — | `WiFiManager` | `WiFiManager` |
| `wifi` | `password` | string | — | `WiFiManager` | `WiFiManager` |
| `jmri` | `server_ip` | string | — | `JmriConnectionController` | `JmriConnectionController`, `JmriConfigScreen` |
| `jmri` | `wt_port` | string | `"12090"` | `JmriConnectionController` | `JmriConnectionController`, `JmriConfigScreen` |
| `jmri` | `json_port` | string | `"12080"` | `JmriConnectionController` | `JmriConnectionController` |
| `jmri` | `power_mgr` | string | `"DCC++"` | `JmriConnectionController` | `JmriConnectionController`, `JmriConfigScreen` |
| `jmri` | `speed_steps` | i32 | `4` | `SettingsScreen`, through `SettingsWriter` | `ThrottleController` |
| `orch` | `transport` | u8 | `0` (WiThrottle) | `SettingsScreen`, through `SettingsWriter` | `AppController` |
| `orch` | `host` | string | — | `OrchestratorConfigScreen`, through `SettingsWriter` | `AppController` |
| `orch` | `port` | u16 | `3000` | `OrchestratorConfigScreen`, through `SettingsWriter` | `AppController` |
| `orch` | `user` | string | — | `OrchestratorConfigScreen`, through `SettingsWriter` | `AppController` |
| `orch` | `pass` | string | — | `OrchestratorConfigScreen`, through `SettingsWriter` | `AppController` |

## Notes

### Who writes, and on which task

Nothing in the UI writes NVS on the LVGL task (F-05, F-39). Screens hand each write to
`SettingsWriter`, whose one task carries them out in the order they were asked for. The `orch`
keys are written as a read-modify-write on that task, so a transport choice and an
orchestrator Save made moments apart cannot overwrite each other with stale copies. WiFi
credentials are written by `WiFiManager` on the event loop, once they have produced an IP
address (F-40); Forget goes through the writer.

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

- **WiFi credentials** are saved on successful connection — only once they have produced an IP address, so a mistyped password never replaces a good one (F-40) — and loaded on boot for auto-connect.
- **JMRI settings** are saved when the user presses "Connect" on the JMRI config screen, by `JmriConnectionController` on its own task, not on the LVGL task. The `json_port` is discovered from the WiThrottle `PW` message and saved by the same task when it changes, rather than configured manually.
- **Speed steps per click** (1–20) controls how many speed steps each encoder detent applies. Higher values = coarser control. Configurable from the JMRI settings screen.
