# alpha.7 current closure: PACKAGE_CANDIDATE_AUDIT

**PACKAGE_CANDIDATE_PASS — NOT FINAL RELEASE.** Candidate `49fc2019c37099ca` was accepted on 2026-10-04.
Final source reconciliation, commit, push, exact-commit hosted CI and clean artifact rebuild are now Owner-authorized; tag/release/public publication remain pending Owner approval.

The candidate was built from an uncommitted cumulative worktree. Its SHA256 is `290bc0d08b36998b3119de955483923411dfdeb881b318d2e7631e4c7c155b71`.
Eight later FanWorker validation/diagnostic and regression-wiring inputs were explicitly authorized for alpha.7; they require fresh complete software validation and exact-commit rebuild. Candidate acceptance does not validate those later inputs.

Current related closure: [HardwareSlot](HARDWARE_SLOT_RELEASE_CLOSURE.md), [plugin reload](PLUGIN_RELOAD_ROOT_CAUSE.md), [ASUS command9](ASUS_COMMAND9_RELEASE_CLOSURE.md), [package candidate](PACKAGE_CANDIDATE_AUDIT.md).

HardwareSlot physical gates, candidate 1→5→1, packaged reconnect and CS2 smoke remain retained historical PASS evidence. Plugin fixture root cause is closed; production reload lifecycle was unchanged. Command IDs 1–8 remain unchanged, ID9 is additive and protocol version stays1.
Known limits: external unauthorized selector sender UNKNOWN; reconnect mismatch branch has software coverage only; NVM mechanism/wear UNKNOWN; no hidden hardware-bank authoring.

## HISTORICAL CHECKPOINT / SUPERSEDED — preserved investigation and acceptance record

All dated stop points and unexecuted-stage statements below describe their original checkpoint. They do not supersede the current status above or claim final hosted CI/runtime acceptance. Original evidence references remain retained locally.

# Alpha.7 public package candidate audit

Current checkpoint: **PACKAGE_CANDIDATE_PASS — NOT FINAL RELEASE**.
Recorded 2026-10-04T09:14:03.412234+00:00. Stop here for Owner review. No commit, push, tag, hosted release, or publication was performed.

## Conclusion and provenance

The two public metadata blockers are closed by package-only MSBuild publish properties. Fresh official packaging, actual final ZIP static verification, extracted WinUI startup, graceful shutdown, sanitized alpha.6 upgrade, HardwareSlot persistence/UI, shortest physical smoke, reconnect, and live CS2 usage all passed.

- HEAD: `d567de6326baa69a00500958f4b6109ec5cb1087`. This candidate was built from the **uncommitted cumulative alpha.7 worktree**; HEAD alone does not reproduce it.
- VERSION: `0.1.0-alpha.7`; unchanged during this task.
- New source freeze: 423 tracked/untracked build inputs; SHA256 `459a95b2898c8647a83fc686f28e1965d15f2fd7fec101fa17f0cd177a17568a`. `source-freeze.json`, inventories, and start/end dirty status retain provenance.
- Build ID: `49fc2019c37099ca`.
- Fresh native build: `build/package-alpha7-metadata-20261004-083950`; default official `tools/package_release.py`, no legacy, skip-build, clean, or reused ZIP.
- ZIP: `Aura-v0.1.0-alpha.7-windows-x64.zip`, 110629939 bytes.
- ZIP SHA256: `290bc0d08b36998b3119de955483923411dfdeb881b318d2e7631e4c7c155b71`; sidecar matches, unchanged after acceptance.
- Source hashes stayed stable during build and subsequent acceptance. No other Agent source changes were detected. Final extracted 569-file inventory stayed byte-identical after runtime.

## Historical blocked candidate

The prior candidate `9084e8502a331c94`, SHA256 `835f38901e4d68e842ec31402047223c48ab33d0c95694e049e8fce2629f159a`, was blocked for local RSDS PDB paths and Aura raw ProductVersion with `+d567de...`. Its historical ZIP/sidecar, extracted tree, report, freeze, logs and evidence remain retained. The ZIP was copied to `dist/history/metadata-blocked-20261004-083950/` before the new default ZIP was generated. The old report under `audit_artifacts/package-alpha7-candidate-20261004-0710/` is a historical checkpoint superseded by this report; its failure evidence is not rewritten.

## Cause, change, and regression boundary

Actual .NET SDK 10.0.401 property/target audit established `DebugType=portable` in ordinary Release (evaluated `DebugSymbols=false`). The Csc target receives that DebugType and PDB/intermediate metadata, resulting in managed CodeView RSDS paths. `Microsoft.NET.GenerateAssemblyInfo.targets` defaults `IncludeSourceRevisionInInformationalVersion=true`; `AddSourceRevisionToInformationalVersion` appends the initialized SourceRevisionId. AsusPlatform has no project Version and otherwise defaults to 1.0.0.

Only `tools/package_winui.py` and new `tests/test_package_metadata.py` changed:

1. Public publish adds `DebugType=None`, `DebugSymbols=false`, `IncludeSourceRevisionInInformationalVersion=false`, and `Version=<value from VERSION>` globally to the publish invocation/project references. No csproj, Directory.Build.props/targets or functional WinUI code changed. No hex patching/stripping was used.
2. The public verifier reads raw ProductVersion with `exact=True`, includes AsusPlatform.dll, and rejects PDB files case-insensitively. The helper's development normalization default is retained for existing development checks; the public gate never normalizes.
3. Candidate build_id includes managed publish file hashes as well as native payload, so metadata-only changes identify a new candidate and retain the prior candidate directory.
4. Five meaningful regression cases cover exact acceptance, commit suffix rejection on every product binary, missing AsusPlatform, PDB rejection, and managed metadata changing the candidate identity.

Ordinary Release properties were evaluated again and retain portable debug policy and Git-suffixed InformationalVersion. Package-only metadata changes do not alter normal development/CI behavior.

## Verification

| Gate | Result / evidence |
|---|---|
| Targeted isolated managed publish | PASS; three actual PE bytes, raw resources, assembly custom attributes and PE debug directory inspected |
| Packaging regressions | 5/5 PASS; metadata-tests.log |
| Protected local state regression | 1/1 PASS; local-state-tests.log |
| Source/ASUS call ownership static guards | PASS; static-source-guards.log |
| Canonical functional baseline | Recorded prior frozen CI 17/17 PASS, 0 NOT RUN; 07:10:47–07:17:26 UTC. Not rerun or represented as a new CI invocation. Union comparison proves only the two packaging/test inputs differ; ordinary functional inputs are unchanged. This follows the requested package-only regression boundary. |
| Formal staging verifier | PASS inside fresh official build, packaging.log |
| Formal extracted verifier | PASS on actual ZIP extraction |
| ZIP SHA/.sha256/CRC/path safety | PASS; no absolute or traversal member names, case-insensitive duplicates, symlink/reparse or unexpected file types |
| File checksums | PASS; checksums.json equals complete file-set except itself; every SHA256 matches |
| Required dependencies | PASS: Aura apphost/assembly/deps/runtimeconfig, Windows App SDK, SettingsControls, self-contained .NET, native daemon/web, keymap/frontend, VC runtime and manifest |
| Manifest | schema_version 1, version exact alpha.7, build_id 49fc2019c37099ca, all payload hashes match |
| Forbidden/private payload | PASS: no PDB, ASUS proprietary DLL, config.json, portable.marker, owner profiles, evidence/log/pcap/cache directories, local paths or credential findings |
| Final integrity after runtime | PASS; final-integrity.json |

Exact ProductVersion (raw, no split('+')):

| Extracted PE | ProductVersion | FileVersion | Managed InformationalVersion |
|---|---|---|---|
| Aura.exe | 0.1.0-alpha.7 | 0.1.0.0 | Apphost; application attribute is in Aura.dll |
| Aura.dll | 0.1.0-alpha.7 | 0.1.0.0 | 0.1.0-alpha.7 |
| AsusPlatform.dll | 0.1.0-alpha.7 | 0.1.0.0 | 0.1.0-alpha.7 |
| aura_daemon.exe | 0.1.0-alpha.7 | 0.1.0-alpha.7 | Native PE |
| aura_web_ui.exe | 0.1.0-alpha.7 | 0.1.0-alpha.7 | Native PE |

Aura.dll and AsusPlatform.dll have **zero CodeView entries**, only Reproducible debug metadata. Public ZIP contains zero PDB files. ASCII and UTF16 scans of actual bytes found zero Owner/local-development paths (`G:\Aura`, build root, Owner home, `F:\USBPcap`, audit paths).

The broad `obj\x64\Release` scan had 18 **reviewed upstream Microsoft** hits. They reference Microsoft's `C:\__w\1\s` build environment, not this computer. Native dependency SHA256 matches NuGet input; ReadyToRun managed dependency CodeView records are byte-identical to its NuGet input. Microsoft's apphost likewise retains its upstream `D:\a` build-agent PDB record. These public supplier build metadata strings were not modified or represented as Owner privacy leaks. Full hit/provenance evidence is retained in `upstream-metadata-classification.json`, `upstream-pe-debug.json`, and the unclassified scan. All actual Owner/local path findings remain zero.

## Runtime, upgrade, and physical acceptance

- Launched actual final ZIP extraction outside checkout: `C:\Users\ROG\AppData\Local\Temp\AuraAlpha7MetadataCandidate-20261004-final\Aura.exe`. SDK, Node, CMake, dotnet SDK and development variables removed from process PATH/environment; working directory outside checkout. System WebView2 Runtime retained. Loaded Aura/coreclr/AsusPlatform modules came from the extracted directory.
- Clean empty AURA_DATA_ROOT: default valid config and device-profiles created; WinUI visibly opened, daemon/web sidecars healthy, product version alpha.7, cached runtime ID 49fc2019c37099ca. Daemon path/cache hashes prove candidate payload origin rather than checkout/bin. Screenshots and API identity/process evidence retained.
- Clean, upgrade, and final normal window-close paths used normal packaged graceful lifecycle; all Aura/daemon/web children exited, no orphan or persistent test writer. `.daemon_running` is an existing intentionally global PID/time presence marker while daemon runs; it was removed by normal shutdown. Final hash-only audit proves all 857 protected Owner/checkout files and cache directories equal the pre-task state. No real config/profile contents were copied.
- Upgrade source is a synthetic sanitized alpha.6-era configuration plus an existing public test plugin binary. Existing config, plugins, HostManaged Profile, cs2 automation binding, client settings, and unknown extensions stayed byte-identical; no destructive migration. This does not claim a run of the alpha.6 binary or a copy of Owner's store.
- HardwareSlot1/5 fixture in a separate isolated root saved through production API and reloaded after actual GUI/daemon restart. Backend/slots/unknown extensions retained; Profiles UI displays board-slot mode and hides HostManaged magnetic editor. Disconnected diagnostics show target1, current unknown and FirmwareBank; physical diagnostics confirm actual/current/target matches. The separate global magnetic page was only inspected, not used to write settings.
- Physical smoke: only extracted Aura writer, Native HID active, ASUS HAL absent when Native available. Existing banks only, no authoring. 1→5→1 was verified by BasicInfo and 3 exact typed 51 00 selectors. Magnetic transactions/replay, 50 55 and C0 81 OUT = 0. Direct Owner confirmation: A shallow/deep and red/rainbow switched together normally. **PACKAGED_HARDWARE_SMOKE_PASS**.
- Reconnect: explicit preparation to slot5, Owner unplug/3-second wait/replug, stale transport discarded and opened generation1→3, first post-reconnect vendor OUT BasicInfo (12 00), actual5==desired5, active reconfirmed, selector delta0, magnetic replay0, C0 81=0, daemon healthy. Entire reconnect capture includes one selector during the explicit pre-unplug preparation; scoped post-reconnect selector count is zero. Short physical disconnect preceded the polling script; USB re-enumeration plus production generation history independently prove the lifecycle.
- Real CS2: Owner enabled the fixture's existing cs2.exe→HW Slot5 binding and launched actual cs2.exe/foreground. Three actual snapshots show MatchedRule, activation succeeded, active/actual/desired5, FirmwareBank, Clean, no ManualHold residue, and no ERROR/FATAL after CS2 foreground. No simulation packets, new game-event rules or formal match. This is the requested lightweight live foreground/Profile usage smoke, not full in-match event coverage.

Automatic approval rejected the combined tool operation enabling the isolated binding and launching Steam with `blocked by policy`, without a more specific reason. It did not execute. Owner performed those existing UI/game actions; subsequent read-only live acceptance passed. No alternate launch/mutation tool was used to bypass that rejection.

## Risks, limitations, and next steps

No package/runtime/upgrade/hardware/CS2 blocker remains. HardwareSlot, M605, lighting ownership, reconnect implementation, plugin reload/lifecycle, cooling semantics and IPC/WinUI functional code were not modified. Prior physical gates remain valid; plugin reload fixture closure and additive IPC9 compatibility are unchanged.

Known external unauthorized selector sender remains **UNKNOWN**. Fresh BasicInfo observed slot6 before explicit reconnect preparation; Aura did not auto-reclaim. No auto-reclaim policy remains. Historical reconnect mismatch physical branch remains only software PASS. NVM mechanism remains UNKNOWN; this audit makes no new persistence claim. Vendor services were present and passive USB capture cannot establish sender identity. Candidate is unsigned alpha; WebView2 is a runtime dependency, and Studio native publishing still needs MSVC/SDK as documented.

Stop at **PACKAGE_CANDIDATE_PASS, NOT FINAL RELEASE**. Next work requires Owner review, reconciling/final source freeze, approved commit/push, exact final commit hosted Windows CI, reproducing final artifacts from that commit, and then tag/release. None was done here.

## Deliverables and important evidence

- Public candidate ZIP and .sha256: `dist/`.
- This report, final `package-manifest.json` inventory/checksums, source freeze/status, PE metadata, scans, regression/build logs, sanitized runtime proof, screenshots and privacy-filtered hardware evidence: this audit directory.
- `public-package-metadata-task-only.patch`: generated against the exact task-start package script snapshot and an absent new test file, excluding all pre-existing cumulative changes.
- Delivery ZIP contains only requested candidate plus report/patch/relevant evidence; no dependency trees, compiler outputs, owner configs, browser caches or private hash inventories.
