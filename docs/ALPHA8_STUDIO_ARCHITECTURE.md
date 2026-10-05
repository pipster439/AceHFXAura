# alpha.8 Studio architecture audit

Baseline: `c8a0fdd1228a746dae185c93fb48e4510e70929f`; clean tracked working tree. Goal 0 reviewed navigation, retained WebView2, effect/automation editors, preview, publication, web compiler endpoints, configuration coordinator, runtime supervisor, diagnostics and current test harnesses.

## Current data flow and authority

`MainWindow` navigation retains `StudioPage` (`NavigationCacheMode.Required`). `EmbeddedStudioNavigation` opens loopback port 19898 with host/tab/theme parameters. React `App` loads configuration and coordinates serialized configuration saves. `Studio` selects named effects or Automation; `EffectStudio` owns the live Blockly workspace, publication metadata, generated JS/C++ and session draft. Config `blockly_effects[name]` stores the draft; `applied_blockly_json`, `applied_revision`, `applied_publication` and `applied_plugin_name` describe the last publication. Editing a draft never changes the published profile reference. Unknown existing config fields survive `effectConfig` merges.

## Native shell / retained web boundary

Move project selection and New/Open/Save, preview toggle, validation, build/publish commands and status/diagnostic summaries to a native adaptive CommandBar. Keep Blockly canvas/toolbox, serialization, lifecycle editing, Automation authoring, simulation controls and keyboard visualization in WebView2. Existing dialogs/import semantics can be invoked by the shell. Browser entry keeps its own web controls; embedded entry hides duplicate command surfaces. A bounded, origin-checked typed message bridge routes native UI commands to the existing owner and returns status. No second native project model.

## Build and publish boundary

`stageEffect` checks runtime/SDK/MSVC, transpiles a unique immutable plugin, calls `/api/compile_effect`, calls `/api/reload_plugin` and requires daemon acknowledgment before `effectConfig` updates references through `ConfigSaveCoordinator`. Retain that path. Add compile-only build using the same generated source and compile endpoint, with no reload/config change. AI has no publish operation. Existing runtime/plugin tests use isolated dry-run daemons.

## Simulation boundary

JS transpilation renders 68 preview keys using mock GSI and the monotonic preview clock. Existing explicit preview path posts frames via `/api/preview`. Automation simulation uses the daemon simulation API with owner-thread acknowledgment. AI proposal simulation runs locally without posting frames or mutating the current workspace; unattended desktop validation must verify a dry-run runtime before any editor startup. No HID/protocol changes.

## Safest AI integration

Native C# HTTP provider and Windows protected credential storage keep the API key outside web/config/diagnostic exports. Only explicit user requests may invoke a provider. A allowlisted context snapshot includes typed effect fields, supported preset/node capabilities and selected sanitized error text. The provider returns JSON proposals; local strict parsing rejects unknown fields/actions, paths, code and oversized data. AI cannot access shell/files/git/process/HID/native publication. The web editor validates a candidate in a disposable workspace, displays field/node before/after changes and local preview, then applies only on a user click. Revision matching prevents stale proposals and Undo preserves ordinary edits.

## Proposed files / classes

- `Services/StudioShellModel.cs`: bounded host status and typed command serialization.
- `utils/studioHost.js`: web command listener and status snapshots.
- `StudioPage`: adaptive native shell and assistant panel.
- `Services/StudioLlm*.cs`: settings, credential store, bounded HTTP provider and safe context contracts.
- `utils/studioAssistant.js`: strict proposal parsing, validation, diff and one-step undo.
- `blockly/presets.js`: reuse existing assets with capability metadata.

## Test strategy

Per-goal gates: linked C# service/model tests in Aura.Tests, frontend Node tests against real Blockly/transpilers, mock HTTP provider failure/cancellation/size/redaction tests, WinUI x64 compilation and existing Studio regression. Validate retained editor behavior in isolated desktop harness including dark/light and narrow widths. Final canonical `tools/ci/run-ci.ps1` must complete every required stage. Hardware tests remain fixture-only; no live external LLM requests. Evidence captures distinguish automated software and desktop smoke; no physical acceptance claimed. Preserve baseline changes; deliver task-only patch separately from a small staging-built evidence archive.
