---
name: contract-check
description: Validate firmware protocol code against the layout orchestrator's binding contracts — the WebSocket ClientMessage/ServerMessage vocabulary and the MQTT contract. Use before or after writing any code that sends, receives, or parses a message to or from the orchestrator, and when reviewing a diff that touches main/communication/.
---

# Checking against the orchestrator's contracts

This firmware is a client of contracts owned by another repo. Neither is negotiable from
this side: the orchestrator's frontend and the sensor boards are built against the same
documents, and inventing a field here produces a device that looks like it works and
silently disagrees.

The repo is at `E:\Development\layout-orchestration` (`bazauto/layout-orchestration`).

| What you are writing | Source of truth | Read |
|---|---|---|
| WebSocket control plane | `packages/backend/src/domain/types.ts` | `ClientMessage`, `ServerMessage`, and the payload types they name |
| Anything MQTT | `docs/mqtt-contract.md` | The topic table and the payload schemas |
| Auth / session | `docs/auth.md`, `packages/backend/src/transport/http/auth/` | The three roles and where enforcement happens |

## The WebSocket path — the normal case

This is an operator device, so it speaks the control plane, not the telemetry bus.

1. **Read `ClientMessage` and `ServerMessage` in `domain/types.ts` before writing a
   message struct.** Do not work from an example payload seen in a log — read the type.
2. **Check the direction.** `ClientMessage` is what this device may *send*.
   `ServerMessage` is what it *receives* (`STATE_SNAPSHOT`, `HEARTBEAT`, and every
   `LayoutEvent`). Sending a `ServerMessage` shape is a silent no-op at the far end.
3. **Check the role gate.** `transport/websocket/index.ts` refuses `THROTTLE_COMMAND`,
   `POINT_COMMAND`, `FUNCTION_COMMAND` and `SET_MODE` from a `monitor` session, and the
   role is captured **once at the upgrade** — it cannot be raised mid-connection. A device
   configured with the wrong credential fails at the first command, not at connect.
4. **A snapshot arrives on connect and is the whole state.** Do not build a REST poll for
   something already in `STATE_SNAPSHOT`; do not assume a field persists across a
   reconnect without re-reading it from the new snapshot.
5. **Parse defensively and refuse.** A malformed message must be dropped and logged, never
   half-parsed. The substring extraction in `JmriJsonClient` is not a model to copy here —
   it cannot distinguish absent from malformed, and on this path that distinction is a
   speed command.

## The MQTT path — read this before assuming it applies

**This device is not an MQTT client and should not become one.** The contract's
`loco/{address}/command` row (Backend → ESP) describes an architecture where the ESP drives
DCC. It does not: the orchestrator drives DCC over serial to PicoDCC, and nothing in the
backend has ever published or subscribed to that topic.

If a task seems to need MQTT here, that is the signal to stop and ask, not to implement it.
MQTT in this stack carries sensor and point telemetry from the RP2040 boards.

Should MQTT ever genuinely be in scope, the two rules that matter most:

- **Control topics are never retained.** A retained throttle command moves a train the
  instant a controller reconnects. This is the single most important line in the contract.
- **A retained message is never evidence of liveness.** Telemetry may be retained only
  where its publisher is contractually obliged to re-assert it.

## Reporting

State which document you checked against and quote the relevant line. If the firmware needs
something the contract does not define, **say so and stop** — the contract is amended in
`layout-orchestration` first, in its own PR, before the firmware is written. Do not
improvise a field, a topic, or a message type to keep moving.
