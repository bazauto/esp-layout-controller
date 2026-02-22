# Hardware Layer

## RotaryEncoderHal

**File:** `main/hardware/RotaryEncoderHal.cpp/h`

### Purpose

HAL for 2× Adafruit I2C QT Rotary Encoders using the Seesaw protocol over I2C. Provides rotation deltas and button press events via callbacks.

### Hardware

| Parameter | Value |
|-----------|-------|
| Encoder 1 address | `0x77` (base `0x37`, XOR `0x40` via LTC4316) |
| Encoder 2 address | `0x76` (base `0x36`, XOR `0x40` via LTC4316) |
| I2C port | `I2C_NUM_0` |
| Button pin | Seesaw GPIO 24 |
| Polling interval | 100 ms |

### Seesaw Protocol

| Register | Base | Offset | Purpose |
|----------|------|--------|---------|
| Encoder delta | `0x11` | `0x40` | Read as int32, auto-resets on read |
| GPIO bulk | `0x01` | `0x04` | Read 4 bytes, bit 24 = button (active low) |

### API

| Method | Description |
|--------|-------------|
| `initialise()` | I2C scan, configure button pin as input + pullup |
| `startPollingTask()` | Spawns `rotary_enc` FreeRTOS task |
| `getStatus(index)` | Returns `EncoderStatus { address, present }` |
| `setRotationCallback(fn)` | `fn(int knobId, int delta)` — called from polling task |
| `setPressCallback(fn)` | `fn(int knobId, bool pressed)` — edge-detected, called on press down only |

### Threading

The polling task runs at priority 4 with a 3 KB stack. Callbacks fire from this task's context — `ThrottleController` handles its own mutex locking internally.

### Missing Hardware

If an encoder is not detected during `initialise()`, it is logged and skipped. The polling task continues to check for present encoders only. The `VirtualEncoderPanel` UI component provides a software substitute for testing.
