# alpha.4 client contract

This release productizes the existing WinUI client. Native HID, Automation v2 evaluation/event/retrigger behavior, EffectEngine, PluginManager and Plugin ABI v1 remain the alpha.3 baseline. See [packaging](PACKAGING.md) for deployment and ownership; see [release checklist](RELEASE_CHECKLIST.md) for release gates.

## Authoritative routes

| Owner | Route | Client use / contract |
|---|---|---|
| daemon :19897 | GET `/api/runtime/status` | Existing Control API v1; additive `identity` (service, PID, instance ID, product version, config path), `studio_web.suppressed`, `gsi.source` |
| daemon :19897 | GET `/api/lighting/presets`, GET/PATCH `/api/lighting/base` | Existing schema, sparse patch and expected revision semantics; no new config mutable surface |
| daemon :19897 | GET/PATCH `/api/lighting/global` | API v1; effective global `fps` (default 25, range 10–100) and revision; PATCH accepts only integer `fps` and required `expected_revision`, preserves profile overrides and other fields, uses shared config lock/atomic writer; 400 invalid, 409 stale |
| daemon :19897 | GET `/api/gsi/current` | Existing fields plus `gsi_api_version: 1`, instance/source/source_epoch and owner-evaluated freshness; source and fields captured under the same source lock |
| daemon :19897 | GET/POST `/api/gsi/simulation` | Existing daemon command queue/evaluation path; additive API/instance identity, POST still 202 with queue sequence; WinUI waits for same-instance applied_sequence |
| web :19898 | GET `/api/status` | Existing Web API v2 plus product_version/daemon_instance_id; Studio must match the current core instance |
| web :19898 | GET `/api/gsi/cfg` | Existing template/detected fields retained; additive `gsi_api_version: 1`, `paths[]` with path/exists/template_match/revision |
| web :19898 | POST `/api/gsi/install-cfg` | Existing target whitelist and loopback/Origin protection; optional expected_cfg_revision adds 409 conflict; staged atomic file replacement |

Web GSI current/simulation proxy routes remain compatible. The native page reads runtime/current/simulation directly from the core; web unavailability affects cfg management and Studio, not those core features. API versions are contract versions, not product release numbers. WinUI rejects missing/incompatible new GSI contracts and never invents missing player values.

## GSI configuration

Steam discovery includes the installed HKCU Steam path and libraryfolders.vdf candidates. The UI chooses only backend-provided candidates, shows missing/matching/different/unreadable status, and requires a ContentDialog confirmation for installation. It sends the selected backend revision; it never writes game files or implements its own path whitelist. The web service serializes its cfg installs, checks revision, writes/flushed a distinct temporary file, then replaces the target atomically. An external editor is not coordinated by the server mutex; the revision is a precondition check, not an OS-wide transaction with external writers. Permission/replace failure preserves the old file.

## Simulation and UI state

Simulation remains a global core state. Navigating away does not disable it silently. REAL/SIMULATION labels come from the backend, Online and Automation freshness remain separate. A POST queue acknowledgement is not application confirmation. The client waits for the reported sequence in the same daemon instance, never retries an increment POST automatically, and reports uncertainty on timeout/restart. The existing source switch clears/rebases telemetry and event history; no synthetic event endpoint or new retrigger mode is introduced.

Game Integration owns data-source observation, cfg install and simulation inputs. Rules and effect authoring remain in Studio / Automation. Home and Game Integration cancel obsolete polling. Lighting retains its local draft while navigating, disables edits during save, and rereads an uncertain cancelled write on reentry. Transient notifications overlay the viewport and clear on unload; success notices auto-close. Native pages share a left-aligned 1200-DIP maximum, fixed visible vertical scrollbar, and content-width-driven responsive layout. Simulation remains visible above collapsed advanced diagnostics. Lighting labels runtime FPS as a target, and global FPS edits require explicit draft resolution before a separate confirmed write. Settings persists only client preferences separately from daemon config. Studio retains one cached WebView/editor; web unavailability is reported without automatically replacing its draft. Exit closes WebView, tray and window subclass handlers.

## Automated validation

```powershell
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
$env:AURA_INTEGRATION_BIN = (Resolve-Path build/Release).Path
dotnet test tests/Aura.Tests/Aura.Tests.csproj -c Release --filter 'TestCategory!=DesktopSmoke'
dotnet build winui/Aura.WinUI.csproj -c Release -p:Platform=x64
python tools/package_release.py --build-dir build --skip-build
$env:AURA_PACKAGE_DIR = '<absolute path to final extracted candidate>'
# Run from an interactive desktop with no user Aura instance; never interrupt one for testing:
dotnet test tests/Aura.Tests/Aura.Tests.csproj -c Release --filter 'TestCategory=DesktopSmoke'
$env:AURA_BIN_DIR = $env:AURA_INTEGRATION_BIN
Push-Location tests
python -B -m unittest -v test_winui_productization
Pop-Location
```

The desktop smoke loads the actual packaged GUI/XAML, redirects a secondary launch, closes normally and checks an externally started dry-run core remains alive. It is not a visual layout, WebView editing, real keyboard or live CS2 test. Native process tests use isolated data roots (including Unicode paths), private events and actual jobs. The package smoke verifies binary product versions/hashes, starts package-only native assets, compiles against the packaged SDK, requires daemon-confirmed load and tests simulation. Existing tests cover full Studio publication/config rollback and Automation regression separately. Hosted CI excludes DesktopSmoke and does not publish a release.

## UI validation gate

`tests/test_ui_layout.py` is explicitly opted into with `AURA_UI_EXE` and `AURA_UI_VALIDATION_DIR`, on an interactive desktop with no existing Aura core. It launches an isolated dry-run core and requires matching process identity before starting the GUI. The GUI test entry measures sibling/viewport geometry during expansion and overlay notifications, checks horizontal overflow and navigation changes, and saves XAML-rendered images. It runs only when both the test flag and output environment variable are present. These images do not capture the desktop, native non-client area or compositor effects. Actual host DPI is recorded; other DPI settings, real keyboard/CS2 and user interaction acceptance remain separate gates.

## Embedded Blockly Studio

The browser root `/` remains the complete standalone Web UI with its navigation and `aura-theme` localStorage setting. WinUI loads `/?host=winui&tab=studio&theme=dark|light`; `tab=automation` instead initially selects Studio's existing Automation workspace. Only these two tab values are accepted in embedded mode; an unknown tab starts Effect Studio. The WebView URL is constructed by `EmbeddedStudioNavigation` and uses only the loopback web service on port 19898.

Embedded mode renders the existing `Studio` component without the Web product sidebar, keyboard stage, other product pages or outer scroll container. Studio's own work list and inspector remain available. Below 1000 DIP of actual WebView content width they become optional overlay panels, leaving the Blockly canvas usable at 600 DIP. A container resize observer resizes the Effect Blockly workspace after native pane/window changes.

Initial theme comes from the WinUI query parameter. The only host message is WinUI → Web `{ "type": "theme_changed", "theme": "dark" | "light" }`; it changes presentation only and never writes the standalone browser's stored theme. No Blockly/config/compile/publication payload crosses WebMessage. All authoring, preview, Automation and publication operations retain their existing HTTP APIs. WinUI keeps one cached StudioPage/WebView until actual app exit. A suppressed or unavailable web service displays status over the retained editor; reconnection reload is explicit.

`test_embedded_studio.py` is opt-in with `AURA_STUDIO_EXE` and `AURA_STUDIO_VALIDATION_DIR` on an interactive desktop without an existing Aura instance. It starts an isolated dry-run core, exercises the packaged WebView, saves WebView2 captures and checks a 50-reentry navigation run. It must not be run against a user's live lighting session.
