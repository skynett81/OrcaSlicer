# Filament Inventory API (consumed by the slicer)

3DPrintForge Slicer can read a **filament spool inventory** from an external
server to power three on-mission features:

- **"Not enough filament" warning** in the G-code preview (per-filament needed
  grams vs. matched spool stock).
- **Real cost** on the multi-colour waste badge (priced from the matched spool's
  cost-per-gram, in the provider's currency).
- **Real spool colours** offered as quick-pick swatches in the filament colour
  picker.

The slicer depends on this **contract**, not on any single product. Two providers
ship today (3DPrintForge dashboard, Spoolman); any server matching one of the
shapes below works.

## Decoupling guarantees

- **Off by default.** With no inventory provider configured, the slicer makes
  **no network call** and behaves exactly as a standalone OrcaSlicer fork — the
  three features above silently do nothing.
- **Loose coupling.** Fetches run on a worker thread (once per distinct slice),
  never block the UI, and fail silently. An unreachable server yields an empty
  inventory → every filament is "unknown" → nothing is shown (the warning appears
  only on a *real matched shortage*).
- **Provider-neutral core.** Matching/sufficiency/cost logic lives in
  `libslic3r` (`ForgeSpool`, `parse_*`, `match_filaments_to_spools`) and is unit
  tested independently of any server.

## Configuration (`AppConfig`)

| Key | Meaning |
|-----|---------|
| `inventory_provider` | `"3dprintforge"` \| `"spoolman"` \| `""` (off) |
| `inventory_url` | base URL of the inventory server (e.g. `http://localhost:3000`) |
| `inventory_token` | optional bearer token |

Backward compatibility: if `inventory_provider` is unset but a legacy
`forge_dashboard_url` / `forge_server_url` is present, the provider resolves to
`3dprintforge` automatically.

> Use an `http://` URL the API answers directly on. The embedded HTTP client is
> plain HTTP (no TLS); for 3DPrintForge use the HTTP port (e.g. `:3000`), which
> serves `/api/*` with 200 (no forced HTTPS redirect for API paths).

## Provider: 3DPrintForge

- **Spools** — `GET {url}/api/inventory/spools?archived=0` → JSON array; each item
  (fields the slicer reads): `id`, `material`, `color_name`, `color_hex`
  (no leading `#`), `remaining_weight_g`, `initial_weight_g`, `cost`, `density`,
  `vendor_name`, `profile_name`, `location`, `ams_unit`, `ams_tray`, `archived`.
- **Currency** — `GET {url}/api/currency` → `{ "active": "NOK",
  "supported": [{ "code": "NOK", "symbol": "kr", ... }] }`. The active code's
  symbol is shown on the waste-badge cost.

Parser: `parse_forge_spools()` / `parse_forge_currency()` (libslic3r).

## Provider: Spoolman

- **Spools** — `GET {url}/api/v1/spool` → JSON array; each spool: `id`,
  `remaining_weight`, `used_weight`, `location`, `archived`, and a nested
  `filament`: `name`, `material`, `color_hex` (no `#`), `density`, `weight`
  (full-spool grams), `price` (full-spool price), nested `vendor.name`.
- **Mapping onto the slicer model**: `cost ← filament.price`,
  `initial_g ← filament.weight`, `color_name ← filament.name`,
  `remaining_g ← remaining_weight`. So `cost_per_gram = price / weight`.
- **Currency**: not fetched today → the waste cost shows without a symbol
  (a bare number). (Spoolman exposes a currency setting; wiring it is optional
  future work.)

Parser: `parse_spoolman_spools()` (libslic3r).

## Adding a new provider

1. Add a `parse_<provider>_spools()` (and optionally currency) parser in
   `src/libslic3r/ForgeSpool.{hpp,cpp}` mapping the server's shape onto
   `ForgeSpool` / `ForgeCurrency`; unit-test it in
   `tests/libslic3r/test_forge_spool.cpp`.
2. Add a fetch branch in `src/slic3r/Utils/InventoryProvider.cpp`
   (`fetch_inventory_spools` / `fetch_inventory_currency`).
3. No GUI changes needed — the waste badge, warning, and colour picker all go
   through `inventory_config()` + `fetch_inventory_*`.
