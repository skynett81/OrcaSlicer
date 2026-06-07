# Mixed Color / "Full Spectrum" — 2-colour verification guide

Quick manual test to confirm the ported Mixed-Color feature works end-to-end and
that the slicing-side channel-merge convention is correct. The feature is **gated**:
with no mixed filaments defined it is a strict no-op, so this guide is the only way
to exercise the new code paths.

## 0. Build

```bash
cmake --build build --config RelWithDebInfo --target ForgeSlicer
```

## 1. Set up a 2-colour scene

1. Launch and open any model (a 20 mm cube is ideal):
   ```bash
   ./build/src/3dprintforge-slicer /path/to/cube.stl
   ```
2. In the **Prepare** tab sidebar, **Filament** section: click the **+** (add filament)
   so there are **two** physical filaments. Give them clearly different colours
   (e.g. filament 1 = red `#FF0000`, filament 2 = blue `#0000FF`).

## 2. Create a mixed (virtual) filament

1. In the **Filament** title bar, click **"Mixed…"**.
2. In the dialog, pick component **A = 1**, **B = 2**, mix ≈ **50 %** (a basic
   LayerCycle blend). Accept with **OK**.
3. The mixed filament is now stored in the project (`mixed_filament_definitions`
   in the project config) and rebuilt into the `MixedFilamentManager` on the next
   slice via `Print::apply`.

> Sanity check: re-open the **"Mixed…"** dialog or save+reopen the project — the
> mixed row should persist.

## 3. Paint a region with the virtual filament

1. Open the **MMU paint** gizmo (paint/segmentation tool, shortcut `Ctrl+N`).
2. The colour palette should now show an **extra swatch** after the two physical
   filaments — that is the virtual mixed filament (ID 3 on a 2-filament printer).
3. Select it and paint a clearly identifiable region (e.g. the top half of the cube).

> If the extra swatch does **not** appear, `enabled_count()` is 0 — re-check step 2
> (the dialog must have added a *custom* row).

## 4. Slice and inspect the G-code

1. Click **Slice plate**.
2. Export / open the G-code and look at the painted region across layers.

**What correct LayerCycle mixing looks like:** the painted region must alternate
between the two **physical** extruders layer-by-layer (T0, T1, T0, T1 … for a 1:1
ratio). Quick checks:

```bash
# Count tool changes — a mixed 1:1 region should produce many, ~1 per layer pair.
grep -cE '^T[0-9]' exported.gcode

# See the tool sequence; for a 50/50 blend it should flip between the two tools.
grep -nE '^T[0-9]' exported.gcode | head -40
```

For a Bambu target the tool change is `M620`/`M621` + filament-change macro rather
than a bare `Tn`; grep those instead.

**This confirms the one unverified engine assumption** — the channel-merge in
`apply_mm_segmentation` (PrintObjectSlice.cpp): virtual channel index == 1-based
filament id, merged into `channels[physical]`. If the painted region prints with a
*single* tool (no alternation) or the wrong tool, adjust the merge target /
`virtual_id` mapping in the gated block at the top of `apply_mm_segmentation`.

## 5. Regression check (must pass)

Slice the **same model with NO mixed filament / no virtual paint**. The G-code must
be **byte-identical** to a build without this feature — because every new path is
gated behind `mixed_filament_manager().enabled_count() > 0`. If a plain
multi-material or single-colour slice changed, that is a regression to fix before
anything else.

```bash
# Optional: diff a plain MM slice against a pre-feature baseline.
diff <(grep -vE '^;' before.gcode) <(grep -vE '^;' after.gcode)
```

## Notes / current limits

- Only **LayerCycle** cadence is wired in the engine. Advanced modes (Local-Z
  sub-layer subdivision, same-layer pointillisme, gradient ramps) are ported in the
  data model + dialog but **not yet executed** by the slicer.
- The mixed-filament gradient *swatch* rendering in the gizmo is flat colour for now
  (the gradient bitmap is cosmetic and not yet wired).
- See `memory/forge_slicer_snapmaker_parity.md` for the full phase map and commits.
