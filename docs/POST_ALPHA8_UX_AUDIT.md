# Post-alpha.8 product UX audit

## Conclusion

**POST_ALPHA8_STABILIZATION_BLOCKED** for desktop acceptance. No reproducible product UX issue was established, and no product UX fix is claimed. Computer Use could enumerate the actual Aura window but could not capture or activate it. A source review or automated fixture is not a replacement for the requested visible desktop dogfood.

## Actual session and evidence

- 2026-10-07 13:33:16 Asia/Shanghai: launched freshly built `winui/bin/x64/Release/net10.0-windows10.0.26100.0/win-x64/Aura.exe` with task-isolated data root `audit_artifacts/post_alpha8_stabilization/dogfood/initial`.
- Native/Web assets came from the first successful new canonical `build/ci/20261007-052444-f2d562ad`; binary hashes are retained in `initial-dogfood-binary-hashes.json`.
- The task started a separate owned `aura_daemon.exe --dry-run` with a named shutdown event and launched Aura alongside it. UI connection status was not verified. No real HID workflow, hardware setter, external AI request or owner configuration edit was performed.
- Normal product launch used no validation command, AutomationId, injected editor action or hidden UI fixture shortcut. A UUID loopback mock provider was available but no conversation or provider configuration was exercised.
- Computer Use returned one real window: title `Aura`, app executable path above, window ID 461872. GUI PID 6568, dry-run daemon PID 27632.
- First state capture failed: `IGraphicsCaptureItemInterop.CreateForMonitor failed: Could not capture the given monitor. (0x80070057)`.
- Refreshed the returned window selection and attempted activation once. Failed: `GetCursorPos failed: 拒绝访问。 (0x80070005)`.
- Stopped app input after failed recovery. Requested restoration of an unlocked, interactive desktop. The precise environmental cause remains **Unknown**; neither an Aura defect nor a locked desktop is inferred from these errors alone.
- Owner explicitly replied: `暂时无法恢复，记录 BLOCKED`. Desktop attempts ended under this instruction; pending workflows remain stabilization work, not alpha.9 development.
- Task fixture cleanup terminated only its retained GUI process and gracefully signaled its own dry-run daemon. This was **forced fixture cleanup**, not a successful graceful product shutdown test. Owned processes were absent afterwards; test ports were available.

Evidence prefix: `audit_artifacts/post_alpha8_stabilization/staging/`. See `evidence/computer-use-blocker.json`, `evidence/resource-initial.jsonl`, `manifests/dogfood-initial-session.json`, `manifests/initial-dogfood-binary-hashes.json`, and `manifests/owned-session-processes-after.json`. No screenshot could be delivered; no screenshot is fabricated.

Recovery followed [Computer Use SKILL.md](C:/Users/ROG/.codex/plugins/cache/openai-bundled/computer-use/26.930.31730/skills/computer-use/SKILL.md) and its required guidance: “Refresh the app/window selection and retry once; report the exact error if recovery fails.” The Owner's explicit BLOCKED instruction ended further desktop attempts.

## Acceptance blocker (not a product UX defect)

| Field | Observation |
|---|---|
| ID | ENV-CU-001 |
| Severity | S1 acceptance/workflow blocker; product severity not assigned |
| Confidence | High that capture/input failed; root cause Unknown |
| Repro | Launch isolated dry-run core and normal Aura; enumerate real window; request state capture; refresh returned selection; activate window |
| Expected | Window screenshot and usable target-bound mouse/keyboard input |
| Actual | Capture 0x80070057; activation GetCursorPos 0x80070005 |
| Screenshot/evidence | computer-use-blocker.json; returned-window metadata and resource sample |
| Suspected layer | Computer Use / Windows capture-input environment; unproven |
| Proposed resolution | Restore interactive desktop/session, retry supported Computer Use workflow, then complete the real product audit |
| Regression risk | No product code change proposed |

## Requested workflow coverage

`BLOCKED` means the workflow was not actually exercised. It is not PASS, a product defect, or a skipped software stage.

| # | Path | Result |
|---:|---|---|
| 1 | Cold app launch | PARTIAL: process and real window enumerated; pixels/startup UX unavailable |
| 2 | Major-page navigation | BLOCKED |
| 3 | Studio first-open | BLOCKED |
| 4 | New project | BLOCKED |
| 5 | Load preset | BLOCKED |
| 6 | Blockly editing | BLOCKED |
| 7 | Save | BLOCKED |
| 8 | Validation | BLOCKED |
| 9 | Test Bench | BLOCKED |
| 10 | AI Assistant empty state | BLOCKED |
| 11 | Loopback/mock AI conversation | BLOCKED; server only prepared, no request sent |
| 12 | Proposal / Diff / Apply / Undo | BLOCKED |
| 13 | AI error conversation | BLOCKED |
| 14 | Autosave / recovery | BLOCKED |
| 15 | Snapshots | BLOCKED |
| 16 | Command Palette | BLOCKED |
| 17 | .auraeffect export / import | BLOCKED; no owner .auraeffect touched |
| 18 | Diagnostics | BLOCKED |
| 19 | Settings | BLOCKED |
| 20 | Light / dark theme | BLOCKED |
| 21 | Narrow window | BLOCKED |
| 22 | Large window | BLOCKED |
| 23 | Long names | BLOCKED |
| 24 | Long errors | BLOCKED |
| 25 | Rapid navigation | BLOCKED |
| 26 | Repeated dialog open/close | BLOCKED |
| 27 | Several restarts | BLOCKED |
| 28 | Existing daemon / reconnect | PARTIAL: owned pre-existing dry-run core prepared; UI attachment and reconnect unverified |
| 29 | Invalid / missing project state | BLOCKED |
| 30 | Graceful shutdown | BLOCKED; forced owned-fixture cleanup only |

## Performance and resource observations

One launch sample at 13:33:40 Asia/Shanghai: Aura working set 183,341,056 bytes (~174.85 MiB), private bytes 113,582,080 (~108.32 MiB), 1,323 handles; daemon working set 12,124,160 and private bytes 2,891,776; Web sidecar working set 6,406,144 and private bytes 1,646,592. No WebView child or data profile existed in this sample because Studio was not opened. Task data footprint was 1,284 bytes / 6 files.

This single observation cannot establish idle steady state, a memory leak, monotonic growth, WebView profile hygiene, 10-cycle lifecycle reliability or daemon reconnect correctness. All requested interaction/resource comparisons and the final 20–30 minute dogfood remain unverified.

## Fixes and deferred work

- Product fixes: **none**, because reproduce → smallest fix → targeted test → Computer Use recheck could not be completed.
- No speculative UI issue list, localization fixes or count-driven polish was created.
- No new feature or hardware/protocol change was made. No item is assigned DEFER_TO_ALPHA9 solely because this environment failed; these are unfinished stabilization acceptance checks.
- Resume this audit with the new maintenance build and the same isolation boundaries once Computer Use capture/input works. Run all 30 paths, 10 launch/use/exit cycles and a measured 20–30 minute final acceptance session. Record discovered issues using S0–S4 and the full evidence fields before selecting fixes.
