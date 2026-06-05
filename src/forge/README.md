# forge-slicer — REST service for OrcaSlicer

Embedded HTTP server that exposes OrcaSlicer's profile manager and
slicing pipeline over REST. Designed to be driven by
[3DPrintForge](https://github.com/skynett81/3dprintforge) so the
dashboard can slice and submit jobs without spawning the CLI per
request.

## Status

| Phase | Endpoint | Status |
|---|---|---|
| 1 | `GET /api/health` | Wired |
| 1 | `GET /api/version` | Wired |
| 2 | `GET /api/profiles[?kind=...]` | Wired |
| 2 | `GET /api/profiles/{id}` | Wired |
| 2 | `GET /api/printers` | Wired |
| 3 | `POST /api/slice` | **Wired** — buffered JSON, single slice at a time |
| 3 | `GET /api/jobs` | Wired — list all jobs sliced this session |
| 3 | `GET /api/jobs/{id}/gcode` | Wired — download the gcode file |
| 3 | `POST /api/preview` | Returns 501 |
| 5 | `POST /api/slice/stream` | **Wired** — Server-Sent Events with live progress |

## PresetBundle injection

`list_profiles()` and `find_profile()` iterate the real OrcaSlicer
`PresetBundle`, but the host (GUI or headless CLI) is responsible for
handing it over via `forge_slicer::set_preset_bundle()`.

- **GUI mode**: call after GUI_Init completes:
  ```cpp
  forge_slicer::set_preset_bundle(&Slic3r::GUI::wxGetApp().preset_bundle);
  ```
- **Headless mode**: construct a `PresetBundle` from `data_dir()`
  using `PresetBundle::load_presets()` and pass its address.

Until injection happens the profile endpoints return empty arrays /
`null` — they never crash on a missing bundle.

## Build

```bash
cmake -B build -S . -DENABLE_FORGE_REST=ON
cmake --build build --target forge_slicer_rest
```

## Run

GUI mode (alongside the slicer):
```bash
orca-slicer --rest-port 8765 --rest-bind 127.0.0.1
```

Headless mode (REST only, no GUI — what 3DPrintForge will use):
```bash
orca-slicer --rest-port 8765 --rest-only
# Sends SIGINT (Ctrl-C) or SIGTERM to stop.
```

In `--rest-only` mode the binary skips GUI startup entirely, constructs
a `PresetBundle` directly from `data_dir()` (defaulting to
`$XDG_CONFIG_HOME/OrcaSlicer` or `$HOME/.config/OrcaSlicer`), and blocks
on a signal so the REST thread keeps serving.

Note: OrcaSlicer's CLI parser maps the underscore-form keys
(`rest_port`) registered in PrintConfig.cpp to dash-form flags on
the command line. Both `--rest-port` and `--rest_port` are *not*
accepted — only the dash form works.

## Verified endpoints

End-to-end verified on 2026-06-05 (headless mode):

```bash
$ orca-slicer --rest-port 8765 --rest-only
[forge-slicer] REST service listening on 127.0.0.1:8765
[forge-slicer] PresetBundle injected: 12 printers, 294 filaments, 34 processes
```

| Endpoint | Status | Sample |
|---|---|---|
| `GET /api/health` | 200 | `{"ok":true,"service":"forge-slicer",...}` |
| `GET /api/version` | 200 | `{"api":1,"version":"1.10.2-skynett.1"}` |
| `GET /api/profiles?kind=printer` | 200 | 12 printers (Default Printer + 11 MyKlipper variants) |
| `GET /api/profiles?kind=filament` | 200 | 294 filament presets |
| `GET /api/profiles?kind=process` | 200 | 34 process presets |
| `GET /api/profiles/{id}` | 200 | Full preset with 160-356 settings (URL-encode spaces) |
| `GET /api/profiles/NONEXISTENT` | 404 | `{"error":"profile not found","code":"ERR_PROFILE_NOT_FOUND"}` |

## Files

- `rest_server.cpp` — cpp-httplib server, endpoint handlers
- `CMakeLists.txt` — FetchContent for cpp-httplib, static lib target
- `INTEGRATION.md` — step-by-step wiring into `src/OrcaSlicer.cpp`

## Contract

The HTTP contract is owned by 3DPrintForge in
`Server/website/docs/FORGE_SLICER_API.md`. A Node mock at
`Server/tools/forge-slicer-mock.js` implements the full contract for
end-to-end testing without waiting for a C++ build.
