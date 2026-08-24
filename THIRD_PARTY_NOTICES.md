# Third-party notices

The MIT Licence in [`LICENSE`](LICENSE) covers the **first-party application code only**:
`main/` (except the Espressif-derived files listed below), `tools/` and the documentation.

It does **not** cover the third-party components redistributed in this repository. Each is
governed by its own licence and carries its own notice, reproduced in the location named.

This file exists rather than a scope note inside `LICENSE` because GitHub detects a
repository's licence by matching the text of that file. Extra prose inside it stops the match,
and the repository shows as "Other" instead of MIT — which is worse than the ambiguity the
note was trying to remove. Keep `LICENSE` as the unmodified MIT text and scope it here.

## Vendored under `components/`

Committed to this repository, so their notices travel with it. **Nothing under `components/`
is first-party code.**

| Component | Version | Licence | Notice |
|---|---|---|---|
| [LVGL](https://lvgl.io/) | 8.4.0 | MIT | `components/lvgl__lvgl/LICENCE.txt` |
| ESP LCD Touch | 1.1.2 | Apache-2.0 | `components/espressif__esp_lcd_touch/license.txt` |
| ESP LCD Touch GT911 | 1.1.1 | Apache-2.0 | `components/espressif__esp_lcd_touch_gt911/license.txt` |

## Resolved at build time

Pulled by the IDF component manager, not redistributed here — `managed_components/` is
gitignored.

| Component | Version | Licence |
|---|---|---|
| ESP WebSocket Client | 1.6.1 | Apache-2.0 |
| ESP-IDF | 5.5.2 | Apache-2.0 |

## Espressif-derived files in `main/`

These keep their upstream SPDX headers rather than the project licence. Where they have been
changed, the header carries a modification notice, as Apache-2.0 §4(b) requires.

| File | Licence | Modified |
|---|---|---|
| `lvgl_port.c` | Apache-2.0 | Yes — installs the LVGL dark theme at init |
| `lvgl_port.h` | Apache-2.0 | No |
| `waveshare_rgb_lcd_port.c` | CC0-1.0 | Yes — `TAG` defined and renamed here |
| `waveshare_rgb_lcd_port.h` | CC0-1.0 | Yes — `TAG` moved out to the `.c` |

`main/main.c` is MIT and first-party, but is based on the Espressif RGB Avoid Tearing example
(CC0-1.0), which its header records.

## Binary distribution

No release artefacts are published today. A published firmware binary links all of the above,
and both MIT and Apache-2.0 require their notices accompany it — so ship this file with the
release rather than the `.bin` alone.
