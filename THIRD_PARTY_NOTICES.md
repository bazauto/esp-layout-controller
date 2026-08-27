# Third-party notices

The MIT Licence in [`LICENSE`](LICENSE) covers the **first-party application code only**:
`main/` (except the Espressif-derived files listed below), `tools/` and the documentation.

It does **not** cover the four Espressif-derived files in `main/`, listed below. Every other
third-party component is fetched by the IDF component manager at build time rather than
committed here, so this repository does not redistribute any of it.

This file exists rather than a scope note inside `LICENSE` because GitHub detects a
repository's licence by matching the text of that file. Extra prose inside it stops the match,
and the repository shows as "Other" instead of MIT — which is worse than the ambiguity the
note was trying to remove. Keep `LICENSE` as the unmodified MIT text and scope it here.

## Resolved at build time

Declared in [`main/idf_component.yml`](main/idf_component.yml), pinned to exact versions and
content hashes by [`dependencies.lock`](dependencies.lock), and fetched into
`managed_components/`, which is gitignored. **None of it is redistributed by this
repository** — each carries its own notice in the fetched copy, at the version the lock names.

| Component | Version | Licence | Notice, once fetched |
|---|---|---|---|
| [LVGL](https://lvgl.io/) | 8.4.0 | MIT | `managed_components/lvgl__lvgl/LICENCE.txt` |
| ESP LCD Touch | 1.2.1 | Apache-2.0 | `managed_components/espressif__esp_lcd_touch/license.txt` |
| ESP LCD Touch GT911 | 1.2.1 | Apache-2.0 | `managed_components/espressif__esp_lcd_touch_gt911/license.txt` |
| ESP WebSocket Client | 1.8.0 | Apache-2.0 | `managed_components/espressif__esp_websocket_client/LICENSE` |
| ESP-IDF | 5.5.2 | Apache-2.0 | supplied with the SDK |

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
release rather than the `.bin` alone. Not committing the sources changes nothing here —
distributing a binary is distribution regardless of where the sources that built it came from.
