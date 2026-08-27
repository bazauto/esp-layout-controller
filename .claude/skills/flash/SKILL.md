---
name: flash
description: Build and flash the firmware to the ESP32-S3 over USB, and read the serial log. Use when asked to build, flash, deploy to the device, put it on the board, or watch the serial output.
---

# Building and flashing

Everything runs from the repo root in **PowerShell**. ESP-IDF v5.5.2 must be on PATH;
`tools\ensure-idf.ps1` is idempotent, so always lead with it rather than checking first.

```powershell
.\tools\ensure-idf.ps1; idf.py -p $env:ESP_PORT build flash
```

`ESP_PORT` defaults to `COM4` — it is defaulted in the root `CMakeLists.txt` as well as the
README, so an unset variable is fine. If the user names a different port, set
`$env:ESP_PORT` first rather than passing `-p` alone, so the custom `flash_test` target
agrees with you later.

## Never run `idf.py monitor` from a tool call

The monitor is interactive and never exits on its own: it will sit until the tool times
out, and `Ctrl-]` is not something you can send. Options, in order of preference:

1. **Build and flash only** (above), then tell the user to run
   `idf.py -p $env:ESP_PORT monitor` themselves — suggest they type
   `! idf.py -p $env:ESP_PORT monitor` so the output lands in the conversation.
2. If you genuinely need log output for a diagnosis, run it in the background and stop it
   deliberately, rather than blocking a foreground call.

The same applies to anything else holding the port. **A serial monitor open anywhere blocks
the flash step** — if flashing fails with a port access error, that is almost always why,
and the fix is to close the monitor, not to retry.

## Reading a failure

- **`idf.py: command not found`** — `ensure-idf.ps1` did not run, or ran in a different
  shell. Shell state does not persist between tool calls, so it must be in the *same*
  command as `idf.py`, joined with `;`.
- **Port access denied / `could not open port`** — monitor still open, or the board is
  mid-reset. Not a code problem.
- **Region `iram0_0_seg` overflowed / image too large** — see the `size-check` skill. The
  factory partition is 1500 K and the headroom is around 250 KB.
- **A component download on first build** is expected and slow; it is not a hang.

## After flashing

Report what actually happened — the build result and the flash result, quoted. Do not
claim the device is working from a successful flash alone: a flash proves bytes landed, not
that a train responds to a knob. If behaviour needs confirming, that is a bench test the
user runs.
