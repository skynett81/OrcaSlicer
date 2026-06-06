# Sammenligning: upstream OrcaSlicer vs vår fork

**Dato:** 2026-06-06 02:00
**Upstream commit:** `f4d34219b8` (ren OrcaSlicer 2.4.0-dev, ingen Forge-endringer)
**Fork:** `/home/skynett81/Dev/Prosjekter/OrcaSlicer/build/src/3dprintforge-slicer`
**Upstream-bygg:** `/home/skynett81/Dev/Prosjekter/OrcaSlicer-upstream/build/src/orca-slicer`

Begge bygd fra samme dependencies, samme commit-utgangspunkt, samme miljø
(NVIDIA 610.x + Wayland, GDK_BACKEND=x11). 149 MB hver.

---

## Hovedfunn

> **Setup-wizard-problemet du så er IKKE noe vi har innført.**
> Upstream OrcaSlicer (ren, urørt) viser nøyaktig samme `Failed to create GBM
> buffer of size 1200x727: Ugyldig argument` — det er NVIDIA-driveren + Wayland
> som ikke gir webkit2gtk en gyldig GBM-buffer å rendre webview-en på.

---

## Side-by-side: hva som virker

| Funksjon                       | Upstream OrcaSlicer | Vår fork |
|--------------------------------|---------------------|----------|
| GUI starter opp                | ✅                  | ✅       |
| Splash/loading                 | ✅                  | ✅       |
| Hovedvindu vises               | ✅                  | ✅       |
| Topbar menu fungerer           | ✅                  | ✅       |
| 3D-viewport rendrer            | ✅ (OpenGL)         | ✅       |
| `Failed to create GBM buffer`  | ❌ (samme feil)     | ❌ (samme) |
| Webview (Home tab)             | ❌ blank            | ❌ blank |
| Setup Wizard (webkit2gtk)      | ❌ blank            | ❌ blank → erstattet med ForgeOnboardingDialog |
| Preset Bundle Studio (webkit)  | ❌ blank            | ❌ blank |
| Cloud login (webkit)           | ❌ blank            | ❌ blank → tom URL, dialog vises ikke |
| REST API                       | ❌ ikke implementert | ✅ 980 printere/5386 filament/2790 process |
| Slice via REST                 | ❌                  | ✅ ~76s estimat, gcode 146KB |
| 3DPrintForge integrasjon       | ❌                  | ✅ |
| Forge Library                  | ❌                  | ✅ (Phase 1+2) |
| 17 brands seeded               | ❌ tom system/      | ✅ 133 vendor-mapper kopiert til ~/.config/3DPrintForgeSlicer/system/ |

---

## Hva som er ENDRET i forken vs upstream

### 1. Rebrand (kosmetikk, kompatibilitet beholdt)
- `SLIC3R_APP_NAME = "3DPrintForge Slicer"`
- `SLIC3R_APP_KEY = "3DPrintForgeSlicer"`
- `~/.config/OrcaSlicer/` → `~/.config/3DPrintForgeSlicer/` (med automatisk migrering)
- Cloud-URL er blanket (var `cloud.orcaslicer.com`)
- About-dialog viser 3DPrintForge

### 2. Webkit-omgåelser (workarounds for det GBM-problemet)
- `ShowUserGuide()` → `ForgeOnboardingDialog` (native wxCheckListBox) istedenfor `GuideFrame` (webview)
- `config_wizard_startup()` returnerer `false` unconditionally — wizard kan
  fortsatt kjøres manuelt fra Help-menyen
- Forge-menu i topbar med "Choose Printers..." og "Forge Library..."

### 3. REST-tilkobling
- `--rest-port` + `--rest-only` CLI-flagg
- 9 endepunkter under `/api/`, brukes av 3DPrintForge dashboard

### 4. Auto-onboarding
- Ved første start kopieres alle 133 vendor-mapper fra `resources/profiles/` til
  `~/.config/3DPrintForgeSlicer/system/`
- 954 vendor-varianter blir auto-registrert i AppConfig

---

## Verifiserte tester nettopp

### Health endpoint
```json
{
  "ok": true,
  "service": "forge-slicer",
  "started_at": "2026-06-05T23:57:36Z",
  "upstream": "3DPrintForge Slicer 2.4.0-dev",
  "version": "1.10.2-skynett.1"
}
```

### Slice end-to-end (hotend.stl, Bambu P2S 0.4)
```json
{
  "ok": true,
  "job_id": "13337799f313b8ce",
  "gcode_path": "/tmp/forge-slicer/13337799f313b8ce.gcode",
  "gcode_size": 145610,
  "estimated_time_s": 76.09,
  "filament_used_g": [0.1117]
}
```

### Profiler tilgjengelig
- Printer: 980 (inkl. Bambu P2S 0.2/0.4/0.6/0.8, Snapmaker U1 0.4/0.4+0.6/0.6)
- Filament: 5386
- Process: 2790

---

## Hvorfor "ting virket før" men "ikke virker nå"

Du sa flere ganger: *"ting virket jo med en gang når vi begynte på prosjektet"*.

Min vurdering: det som "virket" var sannsynligvis at du hadde en eldre OrcaSlicer
installasjon med ferdig oppsett (vendor-profiler installert, AppConfig med valgte
printere, ingen onboarding-trigger). Når vi rebrandet til
`~/.config/3DPrintForgeSlicer/` startet vi med tom datadir og webview-wizarden
ble trigget — som er blank på NVIDIA Wayland både i forken OG i upstream.

Fixene som faktisk er gjort:

1. **Auto-migrering** fra `~/.config/OrcaSlicer/` ved første start (utløser ikke
   wizard hvis du allerede har oppsett der).
2. **Auto-seeding** av alle 133 vendor-profiler — så system/ er aldri tom.
3. **ForgeOnboardingDialog** som native wx-replacement når brukeren *vil* velge
   printere.

---

## Konkret handlingsliste

### Det som er reelt brutt og må fikses
1. **Errors i Qidi profile-fil**:
   ```
   /system/Qidi/process/fdm_process_n_common.json contains incorrect keys:
   first_x_layer_fan_speed, which were removed
   ```
   Dette er upstream-data fra `resources/profiles/Qidi/` — bør rapporteres til
   upstream eller patches lokalt. Ikke fatal.

2. **`calc_exclude_triangles: Unable to create exclude triangles`** —
   skjer når en printer mangler `bed.stl`/`bed_texture.png`. Cosmetic.

### Det som er environmental (ikke vår feil)
- GBM buffer-feil ved webkit2gtk → bytt til X11 hele veien eller
  installer Wayland-versjon av nvidia-drivers, eller bruk `WEBKIT_DISABLE_DMABUF_RENDERER=1`.

### Bekreftet OK i forken
- REST API (alle endepunkter testet)
- Slicing (gcode generert, tid estimert, filament målt)
- Vendor seed (133 mapper)
- Variant register (954 stk)
- Auto-migrering fra OrcaSlicer datadir
- Snapmaker U1 + Bambu P2S printere er tilgjengelige
- Forge-menu i topbar, native ForgeOnboardingDialog, ForgeLibraryDialog

### Anbefalt neste tiltak
**FIX FUNNET OG IMPLEMENTERT (02:02):**

Lagt til i `~/.local/bin/3dprintforge-slicer`:
```bash
export WEBKIT_DISABLE_DMABUF_RENDERER=1
export WEBKIT_DISABLE_COMPOSITING_MODE=1
```

**Resultat etter fix:**
- GBM buffer-feil: 0 (var 4–6 per oppstart)
- Webkit-feil: 0
- Logg viser `main frame firstly shown`, `finished the gui app init`,
  `post_init, finished init opengl`, og 4 × `add script message handler for wx`
  — alle webview-er er nå instansiert riktig

Når du tester i morgen burde Home-fane, Setup Wizard, Preset Bundle Studio
og Cloud-login alle rendre korrekt. Hvis Setup Wizard nå virker, kan vi:
1. Revertere `config_wizard_startup → return false` til upstream-oppførsel
2. Beholde `ForgeOnboardingDialog` som *manuelt* alternativ via Forge-menu
3. Beholde auto-migrering og auto-seed som de er

Hvis det fortsatt feiler, har vi minst native ForgeOnboardingDialog som fallback.
