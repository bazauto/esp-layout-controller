# Controller Layer

Business logic and coordination between layers. See [docs/components/CONTROLLER_LAYER.md](../../docs/components/CONTROLLER_LAYER.md) for full reference.

## Components

| File | Purpose |
|------|---------|
| `AppController` | Singleton owning all services, manages screen lifecycle |
| `ThrottleController` | Coordinates 4 throttles + 2 knobs, routes input, sends network commands |
| `WiFiController` | WiFi lifecycle and auto-connect |
| `JmriConnectionController` | JMRI auto-connect, NVS settings, reconnect with backoff |
