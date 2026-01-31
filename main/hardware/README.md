# Hardware Layer

This directory contains hardware abstraction layer (HAL) code for physical devices.

## Planned Components

### Rotary Encoders
- **File**: `RotaryEncoderHal.h/cpp`
- **Purpose**: HAL for I2C rotary encoders used for throttle control
- **Hardware**: 2x Adafruit I2C rotary encoders
- **Addresses (fixed)**: `0x76` (encoder 1), `0x77` (encoder 2)

### Knob Assignment
- **File**: `KnobManager.h/cpp`
- **Purpose**: Manage assignment of physical knobs to throttles
- **Features**: Track which encoder controls which throttle

## Guidelines

- Keep hardware dependencies isolated to this layer
- Use C++ for new hardware drivers
- Provide clean interfaces to controller layer
- Handle debouncing and hardware quirks here
