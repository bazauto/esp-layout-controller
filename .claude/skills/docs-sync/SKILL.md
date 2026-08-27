---
name: docs-sync
description: Bring this repo's documentation into line with the code on the current branch, before the PR opens. Use after landing a change and before opening a PR, or when asked whether the docs still match the code or whether a change falsifies CLAUDE.md.
---

# Doc sync

Documentation moves **with** the code, in the same PR — never as a follow-up. Before
opening a PR, check whether the change falsifies any of these, in this order.

## The checklist

1. **`CLAUDE.md`** — the index, the traps, "Where this is going", and the open limits. A
   trap that is no longer true is worse than no trap at all, because it argues against a
   correct fix. When a change supersedes an entry, **rewrite it** rather than appending the
   new story underneath the old one.
2. **`.github/copilot-instructions.md`** — covers the same conventions for Copilot. If the
   change touches a convention, a layer boundary, or a threading rule, both files move
   together or they drift.
3. **`docs/architecture/THREADING_MODEL.md`** — the task table is a real reference, not
   decoration. A new `xTaskCreate`, a changed stack size or priority, or a new lock means
   this file changes in the same commit.
4. **`docs/architecture/NVS_STORAGE.md`** — the namespace and key map. Any new persisted
   setting belongs here, with its default and who reads and writes it.
5. **`docs/components/`** — per-layer reference. A new class or a changed responsibility
   lands here.
6. **`docs/flows/`** — sequence diagrams. A changed connection, startup, or control flow
   invalidates one of these; they are mermaid, so they are editable, not regenerated.
7. **`README.md`** — the feature list, the build and test commands, the licence table for
   third-party components.
8. **`docs/PROJECT_OVERVIEW.md`** — the status section and the phase checklist.

## Known drift to fix when you are next in the area

These were true when written and are not any more. Fixing them opportunistically is
welcome; fixing them in an unrelated PR is not — note them and move on.

- `docs/PROJECT_OVERVIEW.md` describes MQTT as the planned integration ("MQTT & Cab
  Signals", "MQTTClient Class (future)"), and lists open questions about MQTT topic
  patterns. The decision recorded in `CLAUDE.md` is that this device speaks the
  orchestrator's **WebSocket** control plane and does not become an MQTT client.
- `README.md` similarly ends on "potential MQTT cab signal integration".
- Both describe the device as WiThrottle-only, which stops being true when the
  `ThrottleBackend` port lands.

## What not to do

- Do not write a decision record for a change that has none. This repo documents *what
  exists* plus a short "where this is going" in `CLAUDE.md`; it has no `D1`/`D2` decision
  numbering, and inventing one is not an improvement.
- Do not paste code into the docs. They describe shape and intent; the code is the detail,
  and a pasted snippet goes stale silently.
- Do not update a doc to describe something you have not verified in the code on this
  branch.
