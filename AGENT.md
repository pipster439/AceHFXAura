# Contributor / agent guide

Use current code as the source of truth. Start with [README](README.md) and the [documentation index](docs/README.md). This file contains current engineering rules; the former chronological discussion is preserved in [AGENT history](docs/archive/AGENT_HISTORY.md).

## Current engineering facts

- `winui/` is the native desktop client. Home uses the daemon Control API; Lighting uses `/api/lighting/presets` and `/api/lighting/base` on loopback port 19897, including parameter schemas, draft/apply and revision conflict handling. It is no longer a Phase 1 placeholder.
- `winui/Pages/StudioPage.xaml.cs` hosts WebView2 against port 19898. `frontend/` builds the tracked `web/index.html`; both remain runtime dependencies.
- `src/main.cpp` owns the user-session daemon; do not turn it into a Session 0 Windows service. Foreground detection uses WinEvent hooks. Hardware writes stay off HTTP request threads.
- The configured default is `auto`: try Native HID first, then gated legacy HAL if needed. `native_hid` explicitly disables HAL fallback; `legacy_hal` explicitly selects diagnostic compatibility. Verify selection in `src/main.cpp`, `src/aura/aura_adapter.cpp` and the example config before changing documentation.
- Root `calibrated_keymap.json` is runtime data and a WinUI development-layout marker. Do not relocate it or plugin SDK headers for cosmetic cleanup. Hardware reports live in [docs/hardware](docs/hardware/README.md).
- Studio orchestration uses version 2. Preserve draft/publish separation, immutable plugin versions, ABI compatibility and failure rollback. See [Studio workflow](docs/studio/STUDIO_WORKFLOW.md).
- `tools/package_release.py` and both root wrappers package the legacy C++ launcher, not WinUI. See [packaging limits](docs/development/PACKAGING.md).

## Working rules

- Make minimal scoped changes; preserve user configuration and local state. Do not commit generated plugins, private configuration, proprietary ASUS binaries, or build output.
- Keep services loopback-only. Do not add game-memory reads/injection, silently change game folders, kill ASUS processes, or request elevation without a demonstrated need.
- Preserve native HID implementation, verified mapping data and research evidence. Historical reports contain disputed claims; API success is not proof of physical lighting.
- Keep Release assertions meaningful. Do not equate model replicas or static string checks with production execution.
- Update links and the relevant current guide when moving files. Archive completed investigation reports; do not add chronological reports to the repository root.
- Run the [test matrix](docs/testing/TESTING.md), report failures/skips separately, and distinguish automated checks from [physical acceptance](docs/testing/MANUAL_TESTS.md).

## Repository hygiene and audit artifact rules

- **Temporary workspace isolation**: All agent and audit temporary artifacts (repro scripts, logs, draft patches, intermediate reports, test scratch data) MUST only be written to `/audit_artifacts/` (or `/audit_repros/`).
- **No root pollution**: Strictly prohibit creating temporary reports (`*_audit.md`, `*_report.md`, `*_summary.md`), patches (`*.patch`), or source snapshots in the repository root.
- **No source duplication**: Strictly prohibit copying full or partial source trees into audit directories and committing them to Git.
- **Default exclusion**: `/audit_artifacts/` is ignored by `.gitignore` by default and must never enter Git version control.
- **Curation of long-term findings**: If an audit or research finding needs long-term retention, curate it into formal documentation under `docs/archive/reports/`, `docs/architecture/`, `docs/hardware/`, or other appropriate `docs/` subdirectories. Never commit raw audit workspaces.
- Refer to [Repository Hygiene Policy](docs/development/REPOSITORY_HYGIENE.md) for detailed guidelines.

## Local Windows toolchain and CI

- Local Windows validation uses the installed/default supported CMake generator. Do not explicitly request VS2022 merely to mirror CI.
- On a VS2026 machine, use VS2026 directly (for example, `-G "Visual Studio 18 2026"`).
- GitHub Actions intentionally pins `windows-2022` / VS2022 as an independent compatibility gate; it does not dictate the local generator.

## Candidate build versus local deployment

- Build/test/package output must not implicitly overwrite checkout-root executables or clear the user's runtime cache.
- Before deliberately replacing a local runtime, back up config/binaries and validate the actual config with the candidate daemon. Resolve migration-required records explicitly, then deploy compatible config and binaries together. Never leave a new binary paired with a known rejected config as a completed handoff.
