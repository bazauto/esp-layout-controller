# Hardware Layer

Hardware abstraction for physical devices. See [docs/components/HARDWARE_LAYER.md](../../docs/components/HARDWARE_LAYER.md) for full reference.

## Components

| File | Purpose |
|------|---------|
| `RotaryEncoderHal` | I2C HAL for 2× Adafruit Seesaw rotary encoders (0x76, 0x77) |

Knob-to-throttle assignment logic lives in `ThrottleController`, not in the hardware layer.
