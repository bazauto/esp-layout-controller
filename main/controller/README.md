# Controller Layer

This directory contains business logic and coordination between layers.

## Planned Components

### Application Controller
- **File**: `AppController.h/cpp`
- **Purpose**: Main application coordinator
- **Responsibilities**:
  - Initialize all subsystems
  - Coordinate between UI, hardware, and communication layers
  - Manage application state
  - Handle global events

### Throttle Controller
- **File**: `ThrottleController.h/cpp`
- **Purpose**: Coordinate throttle operations
- **Responsibilities**:
  - Process encoder input and update throttle speed
  - Send speed commands to WiThrottle server
  - Update UI when state changes
  - Handle throttle acquisition/release

## Guidelines

- Controllers coordinate between layers but don't contain hardware or UI code
- Use dependency injection for testability
- Keep controllers focused on coordination, not implementation details
- Controllers should be the "glue" between model, view, and hardware
