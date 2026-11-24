# Post-alpha.8 stabilization report

Final status: **POST_ALPHA8_STABILIZATION_BLOCKED**.

Development hygiene implementation and final canonical software validation passed. Actual product UX acceptance is blocked by Computer Use capture/input failures; Owner explicitly replied `暂时无法恢复，记录 BLOCKED`. No product UX fix, regression-free desktop claim, or alpha.9 readiness is asserted. Stop for Owner review.

## Scope and preserved state

Baseline HEAD and final HEAD: `c52398bef2738f71b927594424736a42da5184ab` on main. Initial git status was clean and git diff empty, recorded before changes. The independent patch contains only this task's 14 changed/new files; no pre-existing edits were present. No commit, reset, clean, force push, tag, release, version bump or release packaging occurred.

No product runtime source, HID, firmware, HardwareSlot, protocol, persisted configuration schema or major product architecture changed. The isolated app fixture used a dry-run daemon and local mock provider, with no hardware writes or external LLM request. User data, real .auraeffect files, owner configuration, screenshots from other tasks, credentials and unrelated directories were preserved.

## Disk results

Sizes below are logical GiB (2^30 bytes), not allocated disk blocks. Nested inventory rows are inclusive; do not add them together.

| Measurement | Before | After / result |
|---|---:|---:|
| Repository logical footprint | 29.736 GiB | 28.730 GiB before delivery ZIP |
| Scoped known generated-directory footprint | 24.839 GiB lower bound | 23.808 GiB; includes protected release/audit output, not reclaimable space |
| Explicit stale CMake cleanup | 1.363 GiB eligible in reviewed dry-run | 1,463,704,994 logical bytes deleted |
| G free space | 12,677,861,376 bytes / 11.807 GiB | 12,944,834,560 bytes / 12.056 GiB |
| C free space | 28,793,520,128 bytes / 26.816 GiB | 29,614,710,784 bytes / 27.581 GiB |
| Final canonical approximate native usage | — | 923,420,735 bytes / 0.860 GiB |
| Final canonical automatic cleanup | — | 834,974,296 bytes / 0.778 GiB |
| Final canonical native residual | — | 0.082 GiB |

Only `build/alpha4` (666,238,672 bytes), `build/automation-v2-stage2` (645,294,159) and `build/cleanup` (152,172,163) were deleted from historical output. Each passed resolved-path, reparse, inactivity, CMake source/output/project ownership, generated-file and age checks after dry-run and isolated tests. No historical release/evidence directory was deleted. Real worktrees removed: **0**.

Immediate free-space change during that deletion was negative (~47 MB) on ReFS; later readings improved. The final observed G delta is 266,973,184 bytes. This includes concurrent/repeated builds and delayed allocation changes; it is not a causal measurement of physically reclaimed bytes. The definitive cleanup amount is the individually measured logical deletion total. Four complete validation builds have retained additional outputs and evidence.

Accepted alpha.8 package and sidecar hashes were rechecked unchanged: ZIP `69B8AF8889164E0A4882899CE081B68E83921836649EEB3742234151BC2DC95A`, sidecar `FB9A68DD2807EF63287A842AE905C1C01743D557092FA6804F952C7EBCD1E5A1`. Global NuGet/npm/Codex caches were not cleaned or counted as repo-owned generated output. The 474 scoped Aura-named Temp roots (1.945 GiB total) were reported, not deleted.

## Maintained cleanup and local CI behavior

- `tools/dev/storage-audit.py` inventories explicitly scoped roots read-only, reports logical bytes/file counts/oldest/newest, records errors and skips reparse points. `docs/DEV_STORAGE_AUDIT.md` contains the >100 MiB and requested-root inventory and ownership/classification guidance.
- `cleanup-generated.ps1` defaults to dry-run; real deletion requires `-Apply`. Supports `-DryRun`, `-KeepLast`, `-OlderThanDays`, `-IncludeFailedRuns`, `-Verbose`, JSON audit output and explicit reviewed legacy names.
- Automatic deletion is limited to direct children of `build/ci` (EPHEMERAL) and `artifacts/ci/runs` (SHORT_RETENTION) with matching owner/schema/repo/path/category/completed manifests. RELEASE/AUDIT and UNKNOWN are protected. Custom evidence, `kept`, unfinished runs and source/config/manual artifacts remain protected.
- Retention keeps the newest 3 per category/status plus every run with completion OR newest file <24h. Failed deletion requires explicit opt-in; canonical opts in while protecting current failure and keeping the newest/recent failed runs. Partial runs have a separate group. This is bounded long-term retention for eligible completed runs, not a fixed disk quota: high activity within 24h, unknown, protected or abandoned running output can exceed 3. Inspect audit decisions rather than assuming all build trees are reclaimable.
- Safety revalidates allowed paths, rejects traversal/root/reparse escapes, checks cwd/worktrees/live process references, and takes the same exclusive run lease immediately before literal file deletion. No recursive deletion glob or global git worktree prune is used. Inaccessible process inventory fails closed.
- Local canonical takes an exclusive workspace lock and marks the current build/log directories active. Low disk starts with safe old-run pruning, then refuses another full run below the estimated requirement; warnings/errors show free space, estimated requirement and cleanup command. Current historical native usage ~0.860 GiB gives a conservative **4 GiB minimum / 6 GiB warning** including restore/shared build allowance. It is an estimate, not a measured peak.
- Successful local CI removes current owned obj/tlog/pch/ilk only, reducing this run's native residual by about 90%; failures retain diagnostics and intermediates. Final native residual is ~0.082 GiB. Default local logs get unique run directories and a latest-summary pointer. Stage summaries show `running` during actions; the local wrapper preserves the owned default instead of overriding it.
- GitHub-hosted CI keeps its original artifact path and skips local retention/preflight. Existing 18 software stages and hardware-offline behavior are retained. No hosted run was dispatched during this task.
- `temporary-worktrees.ps1` is report-only by default and removes only registry-owned, completed, stale, inactive, fully clean worktrees under `build/dev-worktrees`; dirty/untracked/ignored/unknown worktrees are retained. The primary plus 9 historical registered worktrees remain unchanged. Historical release worktrees were not automatically adopted.

Three exact configure-time headers are path-and-SHA-256 allowlisted. Generator/ABI/review drift or a modified header is protected until that cleanup proof is reviewed. This affects deletion eligibility only, with no ABI/protocol edit. It fixes a real false-positive found by previewing actual canonical output; config/settings-named generated objects no longer masquerade as user configuration.

## Developer experience findings and fixes

| Finding | Change | Verification |
|---|---|---|
| Fresh per-run native trees accumulated heavyweight intermediates | Active/completed markers, success intermediate cleanup, eligible old-run retention | Real canonical storage records and isolated age/count/status tests |
| Default local logs were overwritten/shared, hampering failure recovery | Unique owned log roots, latest-summary pointer, explicit custom-root protection | Wrapper and summary tests; real canonical paths |
| Summary displayed NOT RUN during an active long stage | Persist `running` before executing action | Action reads actual intermediate summary in fixture |
| Real canonical generated headers / config_writer_util.obj were falsely protected | Exact generated-header byte hashes; narrowed object-name protection | Real output dry-run now KEEP, isolated generated/mutated-header deletion test |
| Historical worktree names do not prove temporary ownership | Positive registry/allowlist helper and report-only fallback | Owned clean removal + dirty/ignored/unknown protection fixtures |

No broad build-system redesign, duplicate script deletion, new product feature or unmeasured speedup claim was made. Dependency install, source verification and required suites remain unchanged. Local targeted commands are documented in the current CI guide; storage tools are discoverable there.

## Validation

- `git diff --check`: PASS; task-only patch reverse-apply check and exact file list checked at packaging. Forward check also passed against baseline HEAD using an isolated temporary Git index; the real index/worktree were not modified by that check.
- Cleanup fixture: **14/14 PASS**, including dry-run/no mutation, count+age, recent/failed retention, active PID/lease/relative commands, cwd/root/traversal/mismatched owner, reparse escape, real project/config/manual evidence, registered worktrees, low-disk refusal, success/failure lifecycle, generated headers/mutations and idempotent pruning. Fixtures do not operate on real release evidence.
- CI infrastructure: **13/13 PASS**; includes actual running-summary behavior, wrapper forwarding, permission and early-failure accounting.
- Final full canonical: **18/18 required stages PASS; 0 unexpected NOT RUN**. CTest **26/26 PASS**, including native suites and maintained cleanup/infrastructure suites. Daemon integration **59 tests PASS**; Aura.Tests **278 PASS**, including Studio software tests; WinUI build **0 warnings / 0 errors**.
- Frontend: **97 PASS / 1 explicit skip / 0 fail** out of 98. The existing portable g++ Studio compile check is unavailable on this Windows toolchain; canonical Windows native Studio generation/compile tests passed. No required stage was skipped.
- Relevant Gate A, enumeration, MTA triage and lighting-backend software compile/tests passed without live vendor COM/HID trials. This is software evidence only.

| Canonical evidence root | Overall | Required stages | Started UTC | Completed UTC |
|---|---|---:|---|---|
| `audit_artifacts/post_alpha8_stabilization/ci-after-cleanup` | passed | 18/18 | 2026-10-07T05:24:44.8043984Z | 2026-10-07T05:32:40.6449845Z |
| `artifacts/ci/runs/20261007-053544-83799d67` | passed | 18/18 | 2026-10-07T05:35:44.3795105Z | 2026-10-07T05:43:13.8336540Z |
| `artifacts/ci/runs/20261007-054850-871c292b` | passed | 18/18 | 2026-10-07T05:48:50.5256018Z | 2026-10-07T05:56:27.3499844Z |
| `artifacts/ci/runs/20261007-060134-f598d94d` | passed | 18/18 | 2026-10-07T06:01:34.7282290Z | 2026-10-07T06:09:02.3169685Z |

The last run `artifacts/ci/runs/20261007-060134-f598d94d` validates the frozen final implementation including the real-output retention compatibility fix. Earlier runs are supporting/transitional evidence, not substituted for the last run. Final source hashes are checked unchanged before packaging.

### Final canonical storage telemetry

| Field | Bytes / count |
|---|---:|
| Free before | 13,698,297,856 |
| Free after start prune | 13,698,277,376 |
| Scoped generated before | 1,190,022,372 |
| Approximate build before intermediate cleanup | 923,420,735 |
| Free after lifecycle | 12,638,609,408 |
| Scoped generated after | 1,279,106,770 |
| Automatically reclaimed logical bytes | 834,974,296 |
| Retained unique run names (includes protected/unknown) | 6 |
| Retained artifact directory entries | 9 |
| Cleanup errors | 0 |

`cleanup-start.json` and `cleanup-end.json` include exact paths, sizes and reasons. Real recent outputs are correctly retained; no timestamp was falsified to delete real evidence. In isolated aged fixtures, 7 passed + 7 failed runs prune to 3 of each, and a second prune deletes 0 bytes. That proves eligible long-term count/age enforcement, while four actual success runs prove heavyweight intermediate cleanup. No filesystem watcher or measured-peak assertion is used.

## Product UX and resource audit: BLOCKED

Computer Use identified the actual Aura window, but capture failed with `IGraphicsCaptureItemInterop.CreateForMonitor ... 0x80070057`; refreshed window selection/activation then failed `GetCursorPos ... 拒绝访问 ... 0x80070005`. Recovery was attempted once and inputs stopped. Owner cannot restore interaction now and requested BLOCKED. Root cause remains Unknown; no Aura defect is inferred.

All 30 required routes are individually recorded in `docs/POST_ALPHA8_UX_AUDIT.md`; only process launch and prestarted dry-run fixture preparation have partial evidence. No Studio navigation/edit/save/AI/Diff/Apply/Undo/Test Bench/theme/dialog/recovery/reconnect workflow was exercised. Product issues discovered: **0 assessed**, not proof of zero issues; product fixes: **0**. No invented alpha.9 backlog was created.

A single Aura launch sample had ~174.85 MiB working set / ~108.32 MiB private bytes and 1,323 handles. Daemon/Web sidecar samples and the 1,284-byte isolated data root are recorded. No Studio/WebView interaction occurred. A single sample cannot establish leaks, steady idle state, monotonic growth, resource stability or lifecycle reliability. Owned fixture processes were cleaned up; no residual owned process or occupied test port was detected. GUI cleanup was forced and is not graceful product shutdown acceptance.

Unresolved required work: actual visible UI exploration, high-confidence issue reproduction/fixes/rechecks, 10 launch-use-exit cycles, repeated interaction resource comparisons, graceful shutdown/reconnect acceptance, and the final measured 20–30 minute Computer Use dogfood. Resume these stabilization checks when the interactive desktop is available. No alpha.9 planning/development starts under this result.

## Evidence and handoff

- Formal reports: `docs/DEV_STORAGE_AUDIT.md`, `docs/POST_ALPHA8_UX_AUDIT.md`, this report.
- Task evidence staging: `audit_artifacts/post_alpha8_stabilization/staging/`, including baseline inventory/drives/HEAD, reviewed cleanup previews/apply decisions, protected hashes, worktree inventory, Computer Use blocker, resource sample, final source hashes, test logs and canonical storage/stage records.
- Independent patch: `audit_artifacts/post_alpha8_stabilization/delivery/POST_ALPHA8_STABILIZATION.patch`.
- Curated evidence ZIP: `audit_artifacts/post_alpha8_stabilization/delivery/POST_ALPHA8_STABILIZATION_EVIDENCE.zip`; checksum is the external `.zip.sha256` file. No screenshots could be captured; `screenshots/UNAVAILABLE.txt` states this explicitly.
- ZIP built only from independent staging; no delivery directory, prior ZIP, dependency/cache/build trees, binaries, credentials or source copy included. Patch is outside the ZIP. Per-file hashes, CRC, membership/duplicate/nesting and file allowlist verification are recorded; `DELIVERY_VERIFICATION.json` is outside the ZIP because it verifies that final archive.

**Owner review required for handoff; final status remains POST_ALPHA8_STABILIZATION_BLOCKED.**
