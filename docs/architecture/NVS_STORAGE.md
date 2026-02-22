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

## Notes

- **WiFi credentials** are saved on successful connection and loaded on boot for auto-connect.
- **JMRI settings** are saved when the user presses "Connect" on the JMRI config screen. The `json_port` is typically discovered automatically from the WiThrottle `PW` message rather than configured manually.
- **Speed steps per click** (1–20) controls how many speed steps each encoder detent applies. Higher values = coarser control. Configurable from the JMRI settings screen.
