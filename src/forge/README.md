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
| 2 | `GET /api/profiles[?kind=...]` | Wired — needs `set_preset_bundle()` call from host |
| 2 | `GET /api/profiles/{id}` | Wired — needs `set_preset_bundle()` call from host |
| 2 | `GET /api/printers` | Wired — needs `set_preset_bundle()` call from host |
| 3 | `POST /api/slice` | Returns 501 |
| 3 | `POST /api/preview` | Returns 501 |
| 4 | `GET /api/jobs/{id}/gcode` | Not wired |

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

```bash
orca-slicer --rest-port 8765 --rest-bind 127.0.0.1
```

Note: OrcaSlicer's CLI parser maps the underscore-form keys
(`rest_port`) registered in PrintConfig.cpp to dash-form flags on
the command line. Both `--rest-port` and `--rest_port` are *not*
accepted — only the dash form works.

## Verified endpoints

After building with `-DENABLE_FORGE_REST=ON` (verified 2026-06-05):

```bash
$ orca-slicer --rest-port 8765 &
[forge-slicer] REST service listening on 127.0.0.1:8765

$ curl -s http://127.0.0.1:8765/api/health
{"ok":true,"service":"forge-slicer","started_at":"...","upstream":"OrcaSlicer 2.3.1","version":"1.10.2-skynett.1"}

$ curl -s http://127.0.0.1:8765/api/version
{"api":1,"version":"1.10.2-skynett.1"}

$ curl -s 'http://127.0.0.1:8765/api/profiles?kind=printer'
{"profiles":[]}     # empty until set_preset_bundle() is wired in
```

## Files

- `rest_server.cpp` — cpp-httplib server, endpoint handlers
- `CMakeLists.txt` — FetchContent for cpp-httplib, static lib target
- `INTEGRATION.md` — step-by-step wiring into `src/OrcaSlicer.cpp`

## Contract

The HTTP contract is owned by 3DPrintForge in
`Server/website/docs/FORGE_SLICER_API.md`. A Node mock at
`Server/tools/forge-slicer-mock.js` implements the full contract for
end-to-end testing without waiting for a C++ build.
