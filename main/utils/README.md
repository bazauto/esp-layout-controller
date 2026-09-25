# Utilities Layer

Generic helpers used across the layers. No dependency on any layer above.

| File | What it is |
|------|------------|
| `CallbackSlot.h` | A callback set on one task and invoked from another: set under its own lock, called as a copy with the lock released (F-30). Every client and controller callback slot uses it. |
| `StackReport.h/.cpp` | Logs each task's stack high-water mark when `CONFIG_THROTTLE_STACK_REPORT` is set; compiled out otherwise (F-33). |

Utility functions used by one class only (string parsing, speed scaling) stay in that class.
