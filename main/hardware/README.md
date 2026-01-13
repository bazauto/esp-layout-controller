# Hardware Layer

This directory contains hardware abstraction layer (HAL) code for physical devices.

## Planned Components

### Rotary Encoders
- **File**: `RotaryEncoder.h/cpp`
- **Purpose**: Driver for EC11 rotary encoders used for throttle control
- **Hardware**: 2x EC11 encoders connected via I2C (PCA9554 expander)

### Knob Assignment
- **File**: `KnobManager.h/cpp`
- **Purpose**: Manage assignment of physical knobs to throttles
- **Features**: Track which encoder controls which throttle

## Guidelines

- Keep hardware dependencies isolated to this layer
- Use C++ for new hardware drivers
- Provide clean interfaces to controller layer
- Handle debouncing and hardware quirks here
