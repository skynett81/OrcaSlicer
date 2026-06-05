# forge-slicer — REST service for OrcaSlicer

Embedded HTTP server that exposes OrcaSlicer's profile manager and
slicing pipeline over REST. Designed to be driven by
[3DPrintForge](https://github.com/skynett81/3dprintforge) so the
dashboard can slice and submit jobs without spawning the CLI per
request.

## Status

| Phase | Endpoint | Status |
|---|---|---|
| 1 | `GET /api/health` | Skeleton |
| 1 | `GET /api/version` | Skeleton |
| 2 | `GET /api/profiles[?kind=...]` | Skeleton (stub returns `[]`) |
| 2 | `GET /api/profiles/{id}` | Skeleton (stub returns 404) |
| 2 | `GET /api/printers` | Skeleton |
| 3 | `POST /api/slice` | Returns 501 |
| 3 | `POST /api/preview` | Returns 501 |
| 4 | `GET /api/jobs/{id}/gcode` | Not wired |

## Build

```bash
cmake -B build -S . -DENABLE_FORGE_REST=ON
cmake --build build --target forge_slicer_rest
```

## Run

```bash
orca-slicer --rest-port 8765 --rest-bind 127.0.0.1
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
