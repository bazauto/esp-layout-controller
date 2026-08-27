---
name: device-tests
description: Run the on-device Unity test suite on the ESP32-S3 and report the results verbatim. Use when asked to run the tests, check whether the firmware still passes, or verify a change before a PR.
---

# On-device tests

**There is no host-side test runner.** Every test in this repo builds into firmware,
flashes onto the board, and runs there. If no board is connected, the tests cannot run —
say so plainly rather than reporting anything as passing.

## The command

One line, from the repo root, in PowerShell:

```powershell
.\tools\ensure-idf.ps1; idf.py -B build-tests -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.test.defaults" -D SDKCONFIG="build-tests/sdkconfig" flash_test
```

Every part of that is load-bearing:

- **`-B build-tests`** — a separate build directory. The test build sets
  `CONFIG_THROTTLE_TESTS=y`, which replaces the UI with the Unity runner. Sharing `build/`
  would leave the normal firmware configured as a test image.
- **`SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.test.defaults"`** — the second file
  layers the test flag over the normal config. Passing only the test file loses everything
  else.
- **`SDKCONFIG="build-tests/sdkconfig"`** — keeps the generated config out of the root, so
  the next ordinary build is not silently a test build.
- **`flash_test`** — a custom target in the root `CMakeLists.txt`. It flashes, then runs
  `pytest -s --port $ENV{ESP_PORT}`.

Delete `build-tests/` to force a clean config. Close any serial monitor first — it holds
the COM port and the flash step will fail.

## If the pytest step misbehaves

`test_app.py` parses `--port` out of `sys.argv` by hand rather than registering it as a
pytest option. If pytest rejects the argument, fall back to running the harness directly —
it has a `__main__` path that does the same thing:

```powershell
python test_app.py --port $env:ESP_PORT
```

That skips the flash, so flash first if the image is stale.

## Reporting

The harness resets the board over RTS/DTR, captures serial for up to 30 s, and looks for
Unity's `N Tests M Failures K Ignored` summary. Quote that line. Three outcomes, reported
differently:

- **Summary found, zero failures** — passing. Quote the line.
- **Summary found, failures present** — quote the line *and* the failing assertions.
- **No summary within the timeout** — this is not a pass and not a failure. It usually
  means the board did not boot into the test image, or the port is wrong. Say that it did
  not run.

Never write a test count into a PR body or a report without having run it in this session.
