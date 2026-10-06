# AceHFXAura alpha.8 Studio Polish

Status: **ALPHA8_STUDIO_FEATURE_COMPLETE**

Goals 10–13 implementation and local automated gates passed. Owner real-provider acceptance remains pending; no alpha.8 packaging/release was started.

## Checkpoint and boundaries

ALPHA8_AI_MVP_COMMIT: `616c05af8efcdfffab571a8039e4ad9b11aa877f`.
ALPHA8_TOOLING_COMMIT: `6070e6232c5eab3c76f86ac4155468d03a4f92da` (`feat: add Studio test bench and recovery tooling`).
Before feature edits: working tree matched all 29 Tooling source hashes and all 439 accepted canonical input hashes; diff check clean. Targeted checkpoint validation: C# Studio 45/45, frontend Studio/tooling 54 PASS / 1 pre-existing skip. Local hook changed no build input; working tree clean after checkpoint. No push/tag/release.
This task-only patch starts at the Tooling checkpoint and excludes the pre-existing Phase 2 work that was checkpointed.

## Goal 10 — Project bundle

Source-only `.auraeffect`, ZIP schema 1 with exactly `manifest.json` and `project.json`; no extraction or executable assets. Project includes logical name, Blockly serialization and playback publication settings, without applied/published authority. Manifest has schema_version, name, optional author, description, tags, capabilities and actual created_with_version. VERSION remains unchanged; a development checkout can legitimately stamp the current alpha.7 version until release work is separately authorized.
Limits: archive 512 KiB, manifest 16 KiB, project member 264 KiB with draft graph 256 KiB, exactly 2 members. Declared and actually read decompressed bytes are bounded. Fixed names reject traversal/absolute paths, case duplicates, links/reparse/directory members, EXE/DLL/scripts and extra members. Duplicate JSON fields, invalid schemas, secrets/path-bearing metadata/graph and runtime records are rejected. No arbitrary path is accepted from WebView. Export uses a native owner-chosen file picker; import parses/validates before summary, then explicit confirmation creates a uniquely named draft. Existing projects cannot be overwritten by bundle import. Neither import nor export calls native compilation or Publish.
Capabilities remain graph-derived by the existing JS semantics and are compared exactly before summary/export; the C# ZIP service validates their allowed structure without inventing a second graph semantics implementation. Unsupported graphs are refused by the existing C++ transpiler/JS simulation validation. Block IDs are regenerated for sharing; variable IDs/references, values, layout and lifecycle survive. Old array-shaped workspaces are normalized on export. API config/keys, journals, snapshots, recent list, diagnostics and build output have no export route.

## Goal 11 — Command palette

Ctrl+Shift+P is wired in WebView and the native Page KeyboardAccelerator. WebView keyboard interaction passed; the supplemental native-focus shortcut check was not executed because the desktop tool rejected foreground ownership. Thirteen commands: New Effect, Open, Save, Validate, Test Bench, Run Scenario, Ask AI, Build, Publish, Restore Snapshot, Export/Import Bundle, Diagnostics. Chinese/English simple multi-term search, arrows, Enter, Esc, focus restoration, empty state, visible disabled reasons. One retained command dispatcher handles native buttons and palette events. Existing document actions own the behavior; native host-only actions open the existing assistant/diagnostics. Publish stays an explicit command and uses normal validation/snapshot/build/publication flow. Critical commands are disabled for busy, recovery, wrong work type and invalid validation; execution rechecks the same gates. Build/Publish validate the current graph before compilation.

## Goal 12 — UX and DPI

Test Bench separates time/execution, simulated inputs and assertion state into small cards. No new renderer/timeline. Keyboard visualization scales down to fit narrow viewports; long effect names wrap. Snapshot select stays within its available width. Native validation summaries have bounded scrolling and the manual checklist wraps.
Matrix: Light/Dark × 840/1520 DIP windows × emulated WebView deviceScaleFactor 1.25/1.5. Check actual visible bench bounds, all 68 key bounds, palette bounds; capture each surface. Current native XamlRoot RasterizationScale is 1.5 (actual 150% desktop environment). 125% native monitor acceptance is deferred; only its WebView viewport/device scale is emulated. No monitor/system DPI setting was changed. Long imported name, native long model text and long AI error layout are exercised; existing recovery/snapshot/recent/empty states are retained.
The initial CSS-zoom experiment was discarded after screenshot review found viewport-unit scaling/cropping. It was replaced by DevTools viewport/deviceScaleFactor emulation plus stricter visible-width assertions, then rerun. Stale viewport measurements and XAML-only captures that omitted owned popups were also corrected: final captures wait for stable native dimensions and use PID-scoped WinApp whole-window screenshots for native overlays.

## Goal 13 — Owner real-provider manual acceptance

A visible opt-in checklist lives in the native AI panel. It performs no request by itself. Each AI request in this mode opens a Cancel-default confirmation, sends once, and never automatically Applies/Publishes or retries. Repair is disabled in manual mode; provider 429/timeout errors state that no automatic retry occurred. An in-flight confirmation cannot be opened twice.
Owner checklist (real provider NOT RUN):

- A. Open AI service settings, enter Base URL/model/key, manually Test Connection; this can incur provider charges. Keys remain in Windows Credential Manager; saved keys are never echoed.
- B. Enable manual mode, choose Explain current effect, manually send once and inspect the explanation.
- C. Choose Modify current effect: “把当前效果的速度提高一点，不改变颜色。”
- D–E. Inspect typed proposal and Diff: only the expected period/decay number changes; RGB/colors are unchanged. Failed schema/range/simulation validation must prevent Apply.
- F–G. Explicitly Apply to draft, open Test Bench, run relevant deterministic scenarios.
- H. Undo or restore the pre-AI snapshot. Both remain drafts; published/runtime authority must stay unchanged.
- I. Verify the owner key is absent from logs, diagnostics, saved project, exported bundle, journals and snapshots. Do not paste keys into the prompt or graph. Automated coverage uses synthetic keys only; it does not certify a real provider's behavior.
- Record 429/timeout if encountered; retry only through a new explicit owner request. Owner results should be recorded separately before any alpha.8 release work.

## AI context final review

Only effective numeric project nodes (inactive fallback shadows excluded), graph capability manifest, the single selected preset's metadata for generation, and at most selected relevant diagnostic lines. Modify/Explain/Error Analysis send no preset catalog. Native strict context allows at most one preset, rejects extra fields, and redacts the exact credential and local paths before HTTP. No repository, arbitrary logs, local paths, hardware serial or unrelated GSI fields are fetched for model context. A generated proposal must use the selected preset. The local desktop mock checks time-only capability, empty GSI fields and empty preset metadata for Modify.

## Validation and evidence

| Gate | Accepted result |
| --- | --- |
| Goal 10 | PASS: source roundtrip/current presets/old arrays, malformed manifest and claims, traversal/absolute/executable entries, case duplicates/link flags, compressed bomb/size bounds, secret exclusion, independent draft authority |
| Goal 11 | PASS: 13-command discovery, bilingual filtering, ArrowDown/Enter/Esc in actual palette, disabled safety, retained dispatcher/no duplicate execution |
| Goal 12 | PASS for tested matrix: narrow/wide Light/Dark, emulated WebView 125%/150%, actual native 150%, long project/model/error and recovery/snapshot/recent states; native 125% remains manual |
| Goal 13 | PASS for manual harness, strict context and mock security/no-retry coverage; real-provider A–I deliberately NOT RUN |
| Canonical CI | **18/18 required stages PASS; 0 unexpected NOT RUN** |
| CTest | 25/25 PASS |
| .NET | Aura 239 + AsusPlatform 84 + FanTypeLibValidator 23 = 346 PASS; 0 skipped |
| Frontend | 89 PASS / 0 FAIL / 1 existing skip (portable generated-native-frame harness); total 90 |
| WinUI x64 Release | 0 errors / 0 warnings |
| Isolated desktop smoke | 20 phases PASS: crash 12, recovery 5, clean restart 3; all error=null |

Accepted canonical run: `audit_artifacts/alpha8_polish/canonical-verified`, build `build/ci/20261005-222504-21392332`, finished 2026-10-05T22:32:34Z. All 468 frozen build inputs still match their hashes after verification. `accepted-verified-ci.json`, `ci-verified-input-hashes.json`, `build-output-hashes.json` bind source, tests and binaries. Local CI is not hosted CI or physical acceptance.

Desktop run: `audit_artifacts/alpha8_polish/smoke-verified`. Native daemon `aura_daemon.exe` runs dry-run; GUI is current `winui/bin/x64/Release/net10.0-windows10.0.26100.0/win-x64/Aura.exe`. Isolated AURA_DATA_ROOT and the existing CLI/offline fixture marker are required. The crash is a guarded fixture self-exit (code 23); user processes are never terminated. Restart offers recovery, explicit restore and mock AI Apply generate a pre-AI snapshot, snapshot restore changes only draft, clean save/restart shows no false recovery. Export/import uses actual bounded native ZIP codec; import confirmation is explicit. Palette Validate uses the same retained action. Test Bench runs key tap, health 100→50→10 and pause/step/reset through existing simulation. Publish snapshot is verified with an intercepted failing compile fixture; no actual plugin compilation/reload or automatic publication occurs. Existing published/applied/default/profile/orchestration records remain unchanged.

Mock provider receives one request, only relevant numeric nodes/time capability, no unrelated GSI fields or preset catalog for Modify. Synthetic provider credential is absent from exported bundle and persistence files. 429/500 coverage proves a single request with no retry. Native manual-mode screenshot invokes no provider. Interactive owner-selected file pickers and real paid provider are not claimed by this fixture.

Evidence package uses only the final accepted CI and smoke plus checkpoint receipts. Package paths: `logs/`, `manifests/`, `hashes/`, `screenshots/`, `evidence/`. Screenshot set includes bundle summary, palette, bench, recovery/editor state, native manual panel/settings/error and the 8 viewport/DPI combinations with palette views. `visual-review.json` records image review. Fixture bundle is represented by its two small decoded JSON members; no archive is nested.

Deliverables (repository-relative):
- Report: `docs/ALPHA8_STUDIO_POLISH_REPORT.md`.
- Task-only patch: `audit_artifacts/alpha8_polish/delivery/ALPHA8_STUDIO_POLISH.patch`, baseline `6070e6232c5eab3c76f86ac4155468d03a4f92da`. Temporary private Git indexes validate application/blob equality without changing the real index.
- Evidence: `audit_artifacts/alpha8_polish/delivery/ALPHA8_STUDIO_POLISH_EVIDENCE.zip`.
- External SHA-256 files accompany patch and ZIP. `delivery-validation.json` records duplicate/nested/archive/hash/credential checks. Staging is a fresh sibling directory, never output or an earlier delivery package.

## Issues, risks and deferred work

- Real external LLM acceptance is deliberately pending Owner; no real HID or external provider was used. Mock success cannot establish paid model quality or service behavior.
- Native-button-focus Ctrl+Shift+P acceptance remains manual: two supplemental attempts were blocked by the desktop foreground ownership guard before sending any keys. No product shortcut failure was observed; no guard was bypassed. WebView navigation/Enter/Esc passed in the accepted fixture.
- Native 125% DPI and screen-reader/full manual pointer acceptance remain outside the automated matrix. Native 150% and WebView emulation are distinguished above.
- Native file picker paths are owner-selected in production; automated smoke injects isolated byte picker delegates behind the existing CLI/offline/data-root fixture guard. Real interactive file-picker acceptance is not claimed.
- Sharing annotations (author/description/tags) are validated and shown in the import summary, but not added to the existing durable config schema. They must be entered again on re-export; source/layout/lifecycle roundtrip is retained.
- No optional project assets in schema 1. Sharing is source-only, not a compiled plugin container; unsupported custom graphs cannot bypass validation.
- Foreground Test Bench state remains an Automation boundary; no duplicate effect trigger/renderer or legacy event Boolean pulse is added. AI remains bounded preset/numeric proposals, not arbitrary topology or C++.
- Existing compiler/analyzer warnings are retained and reported by CI; no blanket suppression or test skip was added.
- A first developmental smoke launched an obsolete AppX executable rather than current canonical Aura.exe and failed before fixture startup (Windows App SDK activation). It was discarded, the current assembly path was verified, and the current build was used thereafter. This was an artifact selection error, not a passing run.
- No release/package, cloud sync, marketplace, hardware/firmware/HID, autonomous tools, new provider ecosystem or alpha.9 work. Final feature changes remain uncommitted for review. Stop after deliverables.

## Phase 3 changed files

- `frontend/src/components/EffectStudio.jsx`
- `frontend/src/components/KeyboardVisualizer.jsx`
- `frontend/src/components/Studio.jsx`
- `frontend/src/components/StudioCommandPalette.jsx`
- `frontend/src/components/StudioTestBench.jsx`
- `frontend/src/utils/studioAssistant.js`
- `frontend/src/utils/studioBundle.js`
- `frontend/src/utils/studioHost.js`
- `frontend/tests/effect-capabilities.test.mjs`
- `frontend/tests/studio-bundle.test.mjs`
- `tests/Aura.Tests/StudioBundleTests.cs`
- `tests/Aura.Tests/StudioLlmTests.cs`
- `tools/ci/run-studio-polish-smoke.py`
- `winui/Pages/StudioPage.Assistant.cs`
- `winui/Pages/StudioPage.Bundle.cs`
- `winui/Pages/StudioPage.xaml`
- `winui/Pages/StudioPage.xaml.cs`
- `winui/Services/StudioAssistantContracts.cs`
- `winui/Services/StudioLlmProvider.cs`
- `winui/Services/StudioProjectBundle.cs`
- `winui/Services/StudioShellModel.cs`
- `winui/Validation/StudioAiValidation.cs`
- `winui/Validation/StudioPolishValidation.cs`
- `winui/Validation/StudioToolingValidation.cs`
- `docs/ALPHA8_STUDIO_POLISH_REPORT.md`

24 implementation/test files plus this report. New changes remain uncommitted. The only commit created in this task is the authorized Tooling checkpoint. STOP: wait for Owner real-LLM smoke; no push/tag/release/package.
