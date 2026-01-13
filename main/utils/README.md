# Utilities Layer

This directory contains helper functions and utilities used across the application.

## Planned Components

### String Utilities
- **File**: `StringUtils.h/cpp`
- **Purpose**: String manipulation helpers
- **Examples**: Parsing, formatting, conversion

### Math Utilities
- **File**: `MathUtils.h/cpp`
- **Purpose**: Common math operations
- **Examples**: Speed scaling, acceleration curves, mapping functions

### Debouncing
- **File**: `Debounce.h/cpp`
- **Purpose**: Generic debouncing logic
- **Use**: For buttons, encoders, touch events

### Event System
- **File**: `EventBus.h/cpp`
- **Purpose**: Lightweight event dispatching
- **Use**: Decouple components via events

## Guidelines

- Keep utilities generic and reusable
- No hardware or UI dependencies
- Pure functions where possible
- Well-tested and documented
- Header-only templates are fine for simple utilities
