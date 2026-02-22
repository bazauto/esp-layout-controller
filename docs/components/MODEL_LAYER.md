# Model Layer

## Locomotive

**File:** `main/model/Locomotive.cpp/h`

### Purpose

Pure data model for a DCC locomotive — address, speed, direction, and up to 29 function states.

### Key Types

| Type | Values |
|------|--------|
| `AddressType` | `SHORT`, `LONG` |
| `Direction` | `REVERSE` (0), `FORWARD` (1) |
| `SpeedStepMode` | `STEPS_14` (8), `STEPS_27` (4), `STEPS_28` (2), `STEPS_128` (1) |

### Fields

| Field | Type | Description |
|-------|------|-------------|
| `m_name` | `string` | Display name (e.g. "RGS 41") |
| `m_address` | `int` | DCC address |
| `m_addressType` | `AddressType` | Short or long |
| `m_speed` | `int` | 0–126 |
| `m_direction` | `Direction` | Forward/reverse |
| `m_speedStepMode` | `SpeedStepMode` | Default: 128 steps |
| `m_functionStates[29]` | `bool[]` | F0–F28 on/off |
| `m_functionLabels[29]` | `string[]` | Labels from JMRI roster |

### Key Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `getAddressString()` | `string` | `"S3"` or `"L41"` — for WiThrottle commands |
| `getFunctionState(n)` | `bool` | State of function n |
| `getFunctionLabel(n)` | `string` | Label of function n |

Copy and move operations are enabled (needed for roster management).

---

## Throttle

**File:** `main/model/Throttle.cpp/h`

### Purpose

State machine for a single throttle slot. Manages the relationship between a knob, a locomotive, and the WiThrottle allocation.

### States

See [STATE_MACHINES.md](../architecture/STATE_MACHINES.md) for the full state diagram.

| State | Has Knob | Has Loco |
|-------|----------|----------|
| `UNALLOCATED` | No | No |
| `SELECTING` | Yes | No |
| `ALLOCATED_WITH_KNOB` | Yes | Yes |
| `ALLOCATED_NO_KNOB` | No | Yes |

### Key Fields

| Field | Type | Description |
|-------|------|-------------|
| `m_throttleId` | `int` | 0–3 |
| `m_state` | `State` | Current state |
| `m_assignedKnob` | `int` | `KNOB_NONE` (-1), `KNOB_1` (0), or `KNOB_2` (1) |
| `m_locomotive` | `unique_ptr<Locomotive>` | Owned locomotive (null when unallocated) |
| `m_currentSpeed` | `int` | 0–126 |
| `m_direction` | `bool` | true = forward |
| `m_functions` | `vector<Function>` | {number, label, state} |

### State Transition Methods

| Method | Transition |
|--------|-----------|
| `assignKnob(knobId)` | UNALLOCATED→SELECTING or ALLOCATED_NO_KNOB→ALLOCATED_WITH_KNOB |
| `unassignKnob()` | SELECTING→UNALLOCATED or ALLOCATED_WITH_KNOB→ALLOCATED_NO_KNOB |
| `assignLocomotive(unique_ptr)` | SELECTING→ALLOCATED_WITH_KNOB |
| `releaseLocomotive()` | Any allocated→UNALLOCATED |

Copy deleted, move allowed.

### Function Struct

```cpp
struct Function {
    int number;      // 0–28
    std::string label; // e.g. "Headlight"
    bool state;       // on/off
};
```

---

## Knob

**File:** `main/model/Knob.cpp/h`

### Purpose

State machine for one physical rotary encoder, tracking which throttle it's assigned to and what mode it's in.

### States

See [STATE_MACHINES.md](../architecture/STATE_MACHINES.md) for the full state diagram.

| State | Rotation does | Press does |
|-------|--------------|------------|
| `IDLE` | Nothing | Nothing |
| `SELECTING` | Scrolls roster index | Acquires loco |
| `CONTROLLING` | Changes speed | Emergency stop |

### Key Fields

| Field | Type | Description |
|-------|------|-------------|
| `m_id` | `int` | 0 (left/top) or 1 (right/bottom) |
| `m_state` | `State` | Current state |
| `m_assignedThrottleId` | `int` | -1 if unassigned |
| `m_rosterIndex` | `int` | Current position in roster (SELECTING mode) |

### Key Methods

| Method | Description |
|--------|-------------|
| `assignToThrottle(throttleId)` | IDLE→SELECTING |
| `startControlling()` | SELECTING→CONTROLLING |
| `release()` | Any→IDLE, resets throttle ID and roster index |
| `reassignToThrottle(id, state, resetIndex)` | Direct state jump (for knob moves) |
| `handleRotation(delta, rosterSize)` | Wrapping index navigation in SELECTING |

---

## Roster

**File:** `main/model/Roster.cpp/h`

### Purpose

Collection of available locomotives, populated from the WiThrottle roster list. Maximum 50 entries.

### Key Methods

| Method | Description |
|--------|-------------|
| `addLocomotive(name, address, type)` | Add entry to roster |
| `getLocomotive(index)` | Get const reference by index |
| `createLocomotiveCopy(index)` | Create `unique_ptr<Locomotive>` for throttle assignment |
| `findByName(name)` | Search by display name |
| `findByAddress(addr, type)` | Search by DCC address |
| `getNextIndex(idx)` / `getPreviousIndex(idx)` | Wrapping navigation |
| `clear()` | Remove all entries |

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `MAX_LOCOS` | 50 | Maximum roster entries (matches command station limit) |
