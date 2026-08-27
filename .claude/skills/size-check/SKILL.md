---
name: size-check
description: Measure the firmware image against the flash partition budget and report the headroom. Use before adding a managed component or a large feature, when a build fails on image size, or when asked how much flash is left.
---

# Flash budget

The constraint that is easy to get wrong here: the board has **8 MB of flash**, but the
app does not get 8 MB. `sdkconfig` selects `partitions_singleapp_large.csv`, whose factory
partition is **1500 K**. There is no OTA partition — `singleapp_large` is factory-only.

So the numbers that matter are:

| | |
|---|---|
| Factory partition | 1500 K (1,536,000 bytes) |
| Last measured image | ~1.25 MB |
| Headroom | roughly 250 KB |

**Re-measure rather than quoting that table.** It was accurate when written and the whole
point of this skill is that it moves.

## Measuring

```powershell
.\tools\ensure-idf.ps1; idf.py size
```

`idf.py size` reports used/free against the partition directly. For attribution when
something has grown:

```powershell
.\tools\ensure-idf.ps1; idf.py size-components
```

Both need a completed build. If the tree has not been built, build first — do not infer a
size from the `.bin` timestamp in `build/`, which may be stale by months.

## When to run this unprompted

- Before adding an entry to `main/idf_component.yml`. A managed component is a flash
  decision, not a dependency detail.
- After adding a JSON parser, a TLS stack, or a second protocol client.
- When a build fails with a region overflow or "image too large".

## If headroom is gone

In rough order of yield, and each is a real trade rather than a free win:

1. **Switch the app to `-Os`.** As of 2026-08 the build is
   `CONFIG_COMPILER_OPTIMIZATION_DEBUG=y` (`-Og`) — only the *bootloader* is size-optimised.
   This is the largest single lever available and it is currently unpulled. It costs
   debuggability, so it is the user's call, but measure it before reaching for anything
   below.
2. **`CONFIG_LV_USE_PERF_MONITOR`** is enabled in `sdkconfig.defaults` — a development
   overlay shipping in the production image.
3. **LVGL fonts.** Four Montserrat sizes (12/16/20/24) are compiled in. Fonts are usually
   the largest single item in an LVGL image; dropping an unused size is cheap, but check
   the UI actually stopped referencing it.
4. **Log level.** `CONFIG_LOG_MAXIMUM_LEVEL` at `INFO` or below strips format strings,
   which are a surprising share of an ESP-IDF image.
5. **A partition table change** — a custom CSV giving the app more of the 8 MB. This is the
   real answer if the app is genuinely growing, but it **changes the flash layout**, so it
   needs an erase and a deliberate decision, not a quiet edit.

Report the measurement and the options. Do not start trimming LVGL fonts or lowering log
levels on your own initiative — those change runtime behaviour and belong to the user.
