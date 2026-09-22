# Release candidate 0.1.0-alpha.3

Prepared 2026-09-22. Public alpha / experimental; no GitHub Release or release tag created.

## 1. Baseline and branch

- Repository: pipster439/AceHFXAura; intended existing branch: `main`.
- Baseline and initial local/remote main: `b2a021c7528d9ab06211a921136836363355262e` — Fix Automation snapshot coherence and safe plugin retirement.
- Before editing, ran `git status --short`, `git branch --show-current`, `git log --oneline -15`, `git tag --list`, `git diff`, `git diff --staged`.
- No tracked/index changes. Pre-existing untracked `audit_artifacts/`, `audit_repros/`, `automation_v2_red_blue_audit.md`, `post_audit_remediation.patch`, `post_audit_remediation_report.md` were preserved and excluded from all commits.
- Accepted Stage 0–6 and RED/BLUE remediation were not redesigned. No product feature or production runtime change was added.

## 2. Release-preparation commit

`61730f763abbbd338cfa89493c514b163e03e9c9` — Prepare v0.1.0-alpha.3 release candidate.

`72e8474dc44840f0c2a17e19a8146e3d166cb2ff` — Make runtime status concurrency test independent of host speed. This follow-up changes only a C++ test and its CTest timeout; packaged runtime inputs and candidate hashes are unchanged.

Staged paths and diff were inspected before committing. Normal fast-forward push to `origin main` succeeded; no amend, force push, release or tag operation. This final evidence report is a local delivery artifact generated after the push, not an additional source commit.

`d40cdaba5273f02df5bc5fd3f8a311ed3da20424` — Make lighting reload test timestamp precondition deterministic. Only the temporary-input precondition and its assertion changed; runtime/package inputs remain unchanged.

## 3. Original GitHub Actions failure

[Baseline run 35628991913](https://github.com/pipster439/AceHFXAura/actions/runs/35628991913): Frontend passed; Windows Build passed; CTest failed, 6/13 passed (46%). Actual failed/not-run tests:

- `stage0_fixture_integrity`: SHA-256 assertion for `configs/legacy_rules_gsi_bindings.json`.
- `plugin_abi`: not run because its required integrity fixture failed; its executable was also omitted from the Build target list.
- `automation_service`, `automation_v2`, `automation_authoring`, `automation_retrigger`, `automation_effect_runtime`: required EXEs not found.
- Subsequent Python/.NET/WinUI steps were not reached.
- `automation_reconciliation` was not registered at all: Windows had not installed frontend dependencies before CMake configure.

## 4. Frozen Stage 0 investigation

File: `tests/fixtures/automation_v2_stage0/configs/legacy_rules_gsi_bindings.json`.

| Evidence | Value |
| --- | --- |
| Current local SHA-256 | `f9e1255c14b2e5a45c161debed41d345744a5f64dc5f127d735490b303ebd6c8` |
| Frozen manifest SHA-256 | `f9e1255c14b2e5a45c161debed41d345744a5f64dc5f127d735490b303ebd6c8` |
| HEAD Git blob SHA-256 | same; 1981 bytes, LF |
| SHA-256 after LF → CRLF checkout conversion | `373c12b9094a8a5825b95ec989621404d4292d5440e77a64843b8c5dc3350895`; 2081 bytes |
| Last path change | `23a596108cd8e237350bb12369ffd4be2e61bf5e`, 2026-09-21 11:23:30 +08:00 |
| Original capture baseline | `7b2e1c7887cf9c6f7e5a9601530b19b00282ed24` |

History shows the file was added at Stage 0 as a byte copy of `tests/fixtures/test_config.json`; no later accepted Automation commit changed this path. No semantic/content change was found; `git show 7b2e1c7:tests/fixtures/test_config.json` is byte-identical to the frozen HEAD blob. The fixture is still frozen historical compatibility evidence, not a modern authoring fixture. The original repository lacked `.gitattributes`; Windows automatic checkout line-ending conversion changes the hashed bytes. The remote log reports the assertion/path, not the observed remote digest; the CRLF digest above is the independently reproduced isolated checkout result, not a digest printed by CI.

Remediation preserves the original bytes: `.gitattributes` marks frozen `configs/**` and `binaries/**` as `-text`. No manifest hash update, fixture replacement, historical binary rebuild or semantic migration. An isolated fresh checkout with `core.autocrlf=true` and the new attributes passed the complete frozen verifier. The local working index was untouched during that checkout test. A separate temporary Git repository reproduced the exact unprotected 2081-byte CRLF checkout/hash, then restored the original 1981-byte LF checkout/hash solely by applying -text attributes.

## 5. Missing executable / registration root causes

The old workflow explicitly built seven historical targets and then ran the full CTest configuration. New executable targets and dependency DLLs were therefore absent. The default CMake Release target is the maintainable solution: all registered executables and their fixture dependencies are ordinary default targets. Manual diagnostics are built but remain unregistered/unexecuted; no tests were removed.

## 6. Exact fixes applied

1. Windows installs Node 22 and `npm ci` before configure, enabling real Stage 4 generated fixtures/reconciliation.
2. Build uses `cmake --build build --config Release --parallel 2` (ALL_BUILD).
3. Python entrypoint step also runs the five accepted Automation daemon integration modules; same explicit `AURA_BIN_DIR`.
4. Existing .NET tests, WinUI Release build and separate Frontend `npm ci`/test/build remain enabled. Windows job budget increased to 30 minutes for the expanded complete build.
5. Clean local full build exposed a further CMake race: three fixture projects independently ran the shared multi-output generator, producing `EBUSY ... studio_stage4/studio_old.cpp` / MSB8066. A single `studio_publication_sources` target now owns generation; all three DLL targets depend on it. A second entirely empty build directory verified the fix under parallel build.

6. First post-push CI exposed a pre-existing runtime_status test defect: it demanded >100 writes in a fixed 150 ms window. The shared runner did not reach the arbitrary throughput threshold (test_runtime_status.cpp:95), though all other CTests passed. Replaced it with a synchronized start, fixed 16,000 reads / 4,000 writes, cross-field snapshot consistency checks and final-update verification, plus a 30 s deadlock timeout. No production RuntimeStatusStore change, skip or weakened throughput threshold. Rebuilt the affected test in the validated directory: 20 consecutive runs passed, then full CTest again passed 14/14 (2.29 s).

7. Second post-push CI passed runtime_status but failed lighting_service test 9: UpdateProfile returned 200, CheckAndReload returned false, brightness stayed old. CheckAndReload is explicitly FILETIME-based; the test had no guaranteed timestamp separation between immediate writes. Timestamp aliasing is the code-based diagnosis (the failing run did not log exact FILETIME values). The test now backdates only its temporary input before the initial load, asserts the actual atomic writer changes mtime, and retains both reload and brightness assertions. No production change or post-write touch to force success. Rebuilt test: 20/20 repetitions pass, then full CTest 14/14 (2.31 s). After both CI timing fixes, every CTest was additionally repeated five times with zero failures (11.37 s total); this is stability evidence, not 70 distinct suites.

## 7. Clean local validation

Local environment: Windows 11 (build 26220), MSVC 19.51 / VS 2026, Windows SDK 10.0.26100.0, CMake 4.3.1-msvc1, Python 3.13.5, Node 22.18.0, npm 11.19.0, .NET SDK 10.0.401.

The exact requested VS 2022 configure was attempted at `build/release-alpha3-clean` and failed with MSB8020: v143 is not installed. No VS 2022 parity claim is made. VS 2026 was used as the practical installed local toolchain; GitHub Actions retains VS 2022/windows-2022 as the independent release gate.

Successful empty-directory commands:

```powershell
npm --prefix frontend ci
cmake -S . -B build/release-alpha3-validated -G "Visual Studio 18 2026" -A x64
cmake --build build/release-alpha3-validated --config Release --parallel 2
ctest --test-dir build/release-alpha3-validated -C Release --output-on-failure
```

| Gate | Result |
| --- | --- |
| Empty-directory default Release build | PASS; no inherited stage2 outputs |
| CTest | 14/14 PASS, 0 failed; 3.32 s |
| Web/daemon entrypoints + five Automation daemon modules | 32/32 PASS, no skips; 19.457 s |
| Python HAL / lightbar logic / GSI dictionary helpers | 39/39 PASS; not hardware evidence |
| `dotnet test tests/Aura.Tests/Aura.Tests.csproj -c Release` | 36/36 PASS; MSTEST0037 suggestions remain |
| `dotnet build winui/Aura.WinUI.csproj -c Release -p:Platform=x64` | PASS, 0 warnings/errors |
| Frontend `npm ci` | PASS; npm reported existing esbuild install-script policy warning |
| Frontend `npm test` | 36 passed, 3 skipped, 0 failed (39 total) |
| Frontend `npm run build` | PASS; regenerated tracked HTML unchanged |
| Windows-style frozen checkout | PASS, all 8 hashed binary/config fixtures |
| `git diff --check` / staged check | PASS |

Native build retains existing compiler warnings (for example unused arguments, shadowed locals and `_wgetenv` deprecation); no warnings were relabeled failures or suppressed. Local frontend skips are the three g++-dependent native harnesses (g++ unavailable); they are not counted as passes.

CTest suites: plugin_runtime, plugin_abi, runtime_status, lighting_service, automation_service, gsi_rules, native_hid, stage0_fixture_integrity, aura_hal_py, automation_v2, automation_authoring, automation_retrigger, automation_effect_runtime, automation_reconciliation.

Integration command, from `tests/`, with `AURA_BIN_DIR=G:\Aura\build\release-alpha3-validated\Release` and `PYTHONUTF8=1`:

```powershell
python -B -m unittest -v test_runtime_entrypoints.TestWebUiEntrypoint test_runtime_entrypoints.TestDaemonEntrypoint test_automation_v2_daemon test_automation_authoring_daemon test_automation_effect_daemon test_automation_reload_daemon test_automation_retrigger_daemon
```

Logs remain local under ignored `build/release-alpha3-*`; no logs or temporary tests are committed. Full Python discovery was not run. This report claims only the explicit suites above and isolated packaged acceptance below; opt-in live-CS2/hardware tools were not executed.

## 8. Final GitHub Actions

[First release-prep run 35680645662](https://github.com/pipster439/AceHFXAura/actions/runs/35680645662), commit `61730f763abbbd338cfa89493c514b163e03e9c9`: Frontend PASS; Windows full Build PASS, CTest 13/14. All original fixture/target problems were resolved; runtime_status failed only its 150 ms throughput assertion. Later steps were not reached. Follow-up fix is described above.

[Second validation run 35681444434](https://github.com/pipster439/AceHFXAura/actions/runs/35681444434), commit `72e8474dc44840f0c2a17e19a8146e3d166cb2ff`: Frontend PASS (39/39, zero skips, production build PASS); Windows Build PASS, CTest 13/14 (lighting_service timestamp precondition failure); runtime_status and all Automation suites PASS. Python/.NET/WinUI steps not reached.

[Final validation run 35682312063](https://github.com/pipster439/AceHFXAura/actions/runs/35682312063), commit `d40cdaba5273f02df5bc5fd3f8a311ed3da20424`: **SUCCESS**, completed 2026-09-22 03:26:27 UTC (11:26:27 Asia/Shanghai).

| Required job / step | Final result |
| --- | --- |
| Frontend job | PASS; 39/39 tests, 0 skipped; npm ci and production build PASS |
| Windows default Release build (VS 2022/windows-2022) | PASS |
| CTest | PASS, 14/14, including frozen integrity, ABI and reconciliation |
| Web/daemon + Automation Python integration | PASS, 32/32, no skips, 23.227 s |
| .NET Aura.Tests | PASS, 36/36, 0 skipped |
| WinUI x64 Release | PASS, 0 warnings/errors |
| Windows job overall | PASS, 13 min 43 s |

Final job summaries and logs were inspected, not inferred from build success. Logs are retained locally as build/release-alpha3-final-ci.json and build/release-alpha3-final-ci.log.

## 9. Version and public documentation

- `VERSION` is now `0.1.0-alpha.3`; changed after clean local validation passed.
- Intended future tag: `v0.1.0-alpha.3`, NOT created/pushed.
- Existing alpha.2 tag remains `9719fb4949ea08d1f8746b512b42adbe22aa3edf`.
- Canonical Python packaging and both wrappers read VERSION; EXE FileVersion/ProductVersion and ZIP/readme derive from it. Numeric Windows version is the existing 0.1.0.0 scheme; prerelease identity is in the string version. Private frontend package version 2.0.0 is not a duplicate release source.
- CHANGELOG adds Automation model/policies, lifecycle/composition, authoring/promotion, publication/reconciliation and accepted reliability fixes since alpha.2.
- README distinguishes legacy event coalescing from v2 batches/retrigger, profile arbitration from effect evaluation, 3 s decision freshness from ~10 s status, explicit conversion and generation ownership. Current Automation reference and testing docs updated; original design spike marked historical, not rewritten as a new architecture.
- Owner manual checkboxes remain unchecked; no generated stage report, scratch plugin, patch, local config or proprietary asset entered the commit.

Additional release audit: all 61 local file links in changed documentation resolve; no forbidden generated/audit/patch paths entered the diff. Baseline-tracked historical patch files remain untouched. Four packaged SDK headers form a complete internal include set, and the version consumer rejects alpha.2 overrides. All 14 configured test commands and 30 generated fixture DLLs exist in the validated Release output. Runtime/package inputs are unchanged by both test-only follow-ups. V2 authoring resides in Web AutomationAuthoring; the WinUI Automation page still uses the legacy Application Rule CRUD API.

## 10. Packaging and artifact inventory

Canonical command: `python tools/package_release.py`, with validation-only `LOCALAPPDATA=G:\Aura\build\release-alpha3-package-appdata` and `PYTHONUTF8=1`. Exit 0. Used detected VS 2026, rebuilt production daemon/Web through supported `build/Release` packaging flow; did not replace that flow or rely on stale binaries. No user runtime cache was cleared.

| Artifact | Size (bytes) | SHA-256 |
| --- | ---: | --- |
| `dist/Aura.exe` | 4,099,584 | `6f2a1aa09d86cd08ed1ff7dd100a6ac800e5fbea06e7c7cec99002ac338183f8` |
| `dist/Aura-v0.1.0-alpha.3-windows-x64.zip` | 1,538,438 | `37e1ab8c70eea3e6a32a38343a0c2bade07f5c2f08ad763afeb4f094b527f5db` |

EXE FileVersion and ProductVersion: `v0.1.0-alpha.3`. ZIP README identifies the same version. Both artifacts are ignored and uncommitted. Old ZIPs and unrelated local runtime files remain in dist; only the two named artifacts are candidates, not the directory as a whole.

## 11. Package contents / proprietary assets

ZIP exact eight-entry allowlist verified:

```text
Aura.exe
README.txt
config.example.json
calibrated_keymap.json
include/engine/effect.h
include/engine/plugin_interface.h
include/aura/aura_types.h
include/aura/keymap.h
```

ZIP EXE bytes equal dist/Aura.exe. Embedded RCDATA IDs exactly `[101, 102, 104, 105, 106, 110, 111, 112, 113]`: daemon, Web server, keymap, config example, HTML and four SDK headers. Extracted resources were byte-compared to packaging inputs; runtime inventory contained only those assets plus the launcher stamp before startup.

PASS: no AacKbHal_x64.dll / ASUS proprietary binaries, user config.json, generated source scratch, test fixture DLLs, logs, patches, audit files or development-only files in the candidates. Runtime daemon/Web/HTML/keymap/config template/SDK are present. No WinUI client is bundled. `dumpbin /dependents` confirms launcher imports only Windows system DLLs; embedded daemon/Web additionally require x64 MSVCP140.dll, VCRUNTIME140.dll, VCRUNTIME140_1.dll and the Windows UCRT. These Microsoft runtime prerequisites are not bundled. Smoke passed on the installed MSVC host, not a clean OS lacking those runtimes.

## 12. Packaged runtime smoke

PASS, for both copied standalone EXE and extracted portable ZIP in temporary directories outside the repository, each with isolated LOCALAPPDATA and a foreign working directory:

- `Aura.exe --help` returns 0 and exposes forwarded dry-run options; PE version metadata checked separately (no invented version CLI).
- `Aura.exe --dry-run` extracts and starts real daemon/Web; status reports `dry_run=true`, active backend `dry_run`, configured backend `auto`.
- Studio root HTTP returns 200 with 1,511,218-byte local HTML; `/api/status` and `/api/runtime/status` return 200.
- Packaged SDK discovery reports publication ready and its resolved include path is asserted to be inside the isolated package directory. Actual MSVC compiles current generated continuous and one-shot code, loads through daemon; one-shot lifecycle verification succeeds.
- Revision-checked config publication succeeds and runtime active_profile becomes candidate_smoke; subsequent invalid C++ fails with HTTP 400, configuration bytes remain unchanged, and runtime active_profile still equals candidate_smoke. Clean integration separately covers ABI rejection, immutable DLL reuse rejection and stale-revision rollback.
- Portable config resolves beside the copied EXE; standalone config resolves in isolated LOCALAPPDATA/Aura/config.json. No config is created in foreign CWD.
- Normal startup/compiler SDK resolution uses the extracted package, not a source checkout. Generated C++ supplied by the smoke driver is test input only, not a runtime dependency.
- Test-owned processes stopped; no hardware backend was invoked.

An initial smoke-driver request incorrectly required one-shot lifecycle exports from a continuous plugin; the runtime correctly rejected it. Driver corrected to match real publication modes, then both complete scenarios passed; no product code changed for this negative result.

## 13. Owner manual gates — all PENDING

Record candidate SHA-256, tester, firmware/environment, date and outcome. Automated checks do not mark any item PASS.

- [ ] Native HID: launch, base lighting, preview, stop-preview restore, daemon restart recovery, USB reconnect.
- [ ] Studio: save draft, publish, republish/hot reload, failed publish keeps previous effect on keyboard.
- [ ] Automation v2 on keyboard: persistent low-health-style rule with mock/manual telemetry where practical, one-shot event, restart, stack or queue burst.
- [ ] Live CS2: GSI online, low-health persistent layer, kill one-shot, foreground exit clears scoped work, return/reconnect does not replay stale events.

Owner must complete these or explicitly waive them for this alpha before authorizing the tag. See docs/development/RELEASE_CHECKLIST.md and docs/testing/MANUAL_TESTS.md.

## 14. GitHub Release notes draft — v0.1.0-alpha.3

### Highlights

Experimental **pre-release / alpha** for ROG Falchion Ace HFX. Adds the accepted Automation v2 authoring and effect lifecycle work, with bounded retrigger behavior and safer publication/reload.

### Automation v2

- Unified state / rising / event rules: state activates profiles or runs continuous effects; rising/event triggers one-shot effects.
- Explicit restart, ignore_while_active, stack and queue policies with bounded capacity and queue age.
- Web authoring capabilities/CRUD, legacy provenance and coexistence, shadowing acknowledgement, and explicit Application Rule → V2 Promote/Convert. WinUI Automation continues to expose legacy Application Rule CRUD.

### Studio/runtime improvements

- Explicit continuous vs one-shot publication and finished/opacity lifecycle support.
- Generation-bound plugin instances; Alpha/Additive blending and Replace/Overlay composition with deterministic persistent/transient layer order.
- Transactional publication/config updates and generation-safe hot reload retain old running work safely.
- Cosmetic reloads preserve compatible work; semantic changes, scope loss and stale GSI reconcile/cancel affected active or pending work. Reconnect does not replay stale events.

### Reliability fixes

- Immutable Automation admission snapshots keep conditions, events and foreground scope coherent.
- Plugin generations retire outside the Automation runtime mutex.
- Expanded implementation-backed regression tests and complete Windows CTest/Studio fixture build coverage.

### Requirements

Windows 11 x64 and ROG Falchion Ace HFX, with compatible x64 Microsoft Visual C++ v14 runtime (daemon/Web import MSVCP140 and VCRUNTIME140; the package does not include that runtime). Default `auto` prefers `native_hid`; explicit `native_hid` disables legacy fallback. **ASUS proprietary HAL DLL is NOT bundled**. Only legacy/auto fallback uses locally installed, compatibility-gated ASUS components.

Studio native plugin publication requires **MSVC Build Tools / Visual Studio C++ Desktop workload**, x64 tools and Windows SDK. Editing/saving drafts does not require native compilation.

This distribution contains the **legacy C++ launcher, daemon and Web Studio**. **WinUI is not packaged**; its Release source build is validated separately.

### Known limitations

Independent Light Bar control is not implemented. Historical plugin DLLs are not cleaned up automatically. Native plugin crashes/hangs are not isolated. Real CS2 fields depend on game mode/state; observed events are not a lossless gameplay log. Windows 10 is not formally validated. Automated dry-run testing is not real-keyboard/live-CS2 acceptance; Owner manual candidate acceptance remains pending at draft time.

### Upgrade notes

Back up config.json and plugins before replacing the application. Portable config lives beside Aura.exe; standalone config lives in `%LOCALAPPDATA%/Aura/config.json`. Preserve published historical DLLs still referenced by your config/work.

Legacy rules retain compatibility behavior; opening Automation does not silently migrate them. Use explicit Promote/Convert and review shadowing/precedence. Existing workspaces keep continuous semantics unless explicitly published as one-shot. V2 GSI decisions default to 3-second freshness, separately from the roughly 10-second connection indicator. Update daemon/Web/Studio together; do not mix old binaries and new UI assets.

## 15. Final git status

```text
?? audit_artifacts/
?? audit_repros/
?? automation_v2_red_blue_audit.md
?? post_audit_remediation.patch
?? post_audit_remediation_report.md
?? release_candidate_0.1.0-alpha.3_report.md
```

No tracked or staged changes. The first five entries predate release preparation; only this local final report is newly untracked. Source changes are committed and pushed. Remote main matches d40cdaba5273f02df5bc5fd3f8a311ed3da20424; alpha.2 is unchanged and alpha.3 is absent.

## 16. Release decision

**NOT READY — Owner hardware/CS2 candidate smoke has not been completed or explicitly waived.**

The candidate is prepared and suitable for Owner review: clean local validation, final GitHub Actions, package inspection and isolated packaged-runtime smoke all pass. There are no remaining known automated release blockers. This is not authorization to tag or publish; the sole remaining release gate is Owner manual acceptance or an explicit alpha waiver.

No release or alpha.3 tag exists. Do not authorize tagging until local/remote/package gates and Owner smoke or explicit waiver are complete.
