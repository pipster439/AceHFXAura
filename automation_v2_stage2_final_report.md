> HISTORICAL: pre-retirement implementation evidence. Current Automation supports V2 only; see docs/architecture/AUTOMATION_V2.md.

# Automation v2 Stage 2 final report

Status: merge-ready within the approved Stage 2 scope; staged and uncommitted. Final audit and Release verification: 2026-09-21. Stop before Stage 3.

Base checkpoint: `23a596108cd8e237350bb12369ffd4be2e61bf5e`, the separately committed, accepted Stage 0/1 implementation. No amendment, Stage 2 commit, push or merge was performed.

## Scope and authority

Audited against [approved design](docs/architecture/automation_v2_design_spike.md) and [approved review decisions](automation_v2_review_summary.md). The review summary exists at the repository root; `docs/architecture/automation_v2_review_summary.md` does not exist. Their historical Stage 1 authorization text is superseded by the Owner's explicit Stage 2 instructions in this task. No design files were rewritten to expand authorization.

Stage 2 implements one internal evaluation plan with explicit legacy/V2 provenance, AutomationInputSnapshot, three-valued V2 conditions, state profile eligibility, persistent-effect eligibility decisions, rising/event admissions, bounded packet observations, independent Automation freshness, and transactional candidate-reference/schema validation. The daemon consumes profile/DND decisions; effect decisions do not create or run effects.

The finalization pass added this report and reran verification. No additional production or test changes were necessary after the accepted review fixes. [Detailed Stage 2 report](automation_v2_stage2_report.md) records the implementation files and earlier review hardening.

## Correctness audit

| Requirement | Verified implementation and evidence |
| --- | --- |
| One plan, no translated duplicate execution | `RuleEngine::LoadConfig` compiles one entry per source record. `EvaluateProfilesLocked` traverses that plan with provenance-specific compatibility handling. V2 profile records are skipped by the later effect-decision traversal. The daemon uses one returned profile/DND result instead of separate profile and suppression passes. Conformance checks entry counts, arbitration counts, order and absence of duplicate admissions. |
| Legacy overlays retain their executor | `LegacyOverlayExecutor` entries are skipped by profile evaluation; `GetEventOverlayRules` exports them once to existing main-loop OverlayManager bindings. The V2 temporal evaluator visits only AutomationV2 entries. Existing legacy rendering/lifecycle tests are unchanged. |
| Coherent V2 snapshot | The adapter publishes immutable telemetry/observations under its lock. A single drain captures latest state and ordered packets. Each decision batch captures foreground once and computes admission age/freshness once; all V2 scope/condition leaves recurse through the same const snapshot. V2 leaves never read live GsiState. Tests mutate foreground/GSI during capture/evaluation and verify coherent results and capture counts. |
| Compatibility boundary | Legacy boolean ConditionNode and GSI bindings still use their original live GSI interfaces and coercion/status behavior. The immutable/three-valued contract applies to V2; converting legacy evaluation to new semantics would violate the approved compatibility boundary. |
| Reference validation before publication | After constructing `new_profiles`, LoadConfig checks every AutomationV2 rule, including disabled rules. Activate Profile requires `action.profile`; profile_effect requires `effect.name` in the candidate map. Missing references log rule ID and missing candidate profile, set validation failure and return before swapping runtime profiles/plan. Tests verify rejection and continued selection from the previous plan, including targets that exist only in the old config. Plugin references are not loaded or resolved here. |
| Rising memory | Only observed valid False to True admits. Initial True, Unknown recovery, semantic edits, disabled/scope-entry periods and discontinuities seed/disarm. Unchanged rule reload retains memory. Health 20→10→8→20→10 produces exactly two admissions. |
| Event authority and ordering | Detector emission collects canonical occurrences before public pulse synchronization; telemetry snapshots exclude pulse/sequence fields. Separate packet observations survive until drain. Duplicate/non-transition packets produce no new occurrence; counter delta greater than one remains one occurrence. |
| Event witness | AND operates within one packet. NOT contributes no positive witness. Kill AND NOT headshot retains the kill witness, admits body-kill packets, rejects kill+headshot and unrelated damage. Process OR kill cannot admit unrelated damage solely from the process branch. |
| Overflow | The input buffer holds at most 256 observations, drops oldest, records a cumulative diagnostic and marks discontinuity. Evaluation discards the retained temporal backlog and seeds at latest state; subsequent new observations can admit. Tests check exact retained sequences and safe rebase. This is not an action queue. |
| Freshness | Snapshot age <=3000 ms is fresh by default; >3000 makes GSI leaves Unknown, not False. Process-only branches remain usable. Current-state reconciliation runs without new packets, and recovery True does not invent a rising edge. Legacy approximately 10 s status/connection checks and Effect IGsiReader behavior are unchanged. |
| AST bounds and DND | V2 roots count as depth 1; depth <=32 and total explicit scope+condition nodes <=256 per rule. Exact boundary tests accept 32/256 and reject 33/257 while retaining prior runtime. Omitted scope adds no node. Legacy depth-33 and 257-node cases remain accepted. Rule-level DND is allowed only on state Activate Profile; any DND key on Trigger Effect is rejected, including false. |

## Compatibility guarantees and limits

Existing top-level rules, gsi_bindings, legacy orchestration rules and event_overlays retain their precedence, parsing and executor policies. V2 profile records share the orchestration tier at config order. No persisted translation, automatic promotion, Phase 4 API change or second engine was introduced. Process matching tests cover `.exe`/no suffix and case in both directions, inequality and nonmatching suffix text. Existing historical tests were not weakened or rewritten.

Captured Stage 0 legacy/Application Rule/orchestration fixtures, the frozen fixture integrity check, real PluginManager DLL tests and ABI tests passed. No legacy behavior differences were found in this audit or tested fixtures; this is not a claim of exhaustive equivalence for every possible configuration. Legacy getters intentionally retain their original mutable-read semantics.

## Exact final verification

Environment: Windows x64, MSVC Release, Python 3.13, isolated `build/automation-v2-stage2` directory.

| Run | Result |
| --- | --- |
| Daemon, web UI and relevant C++ test targets build | Passed, exit 0 |
| Full CTest Release | 10/10 passed, 0 failures |
| Automation v2 conformance (included in CTest) | 257 assertions passed |
| Plugin ABI/factory/lifetime (included in CTest) | 212 assertions passed |
| Existing daemon/web entrypoints | 26/26 passed, no skips |
| Real daemon dry-run freshness test | 1/1 passed, no skips; low-health selection expires without another packet |
| Combined Python runtime run | 27/27 passed, no skips |

CTest executed `plugin_runtime`, `stage0_fixture_integrity`, `plugin_abi`, `runtime_status`, `lighting_service`, `automation_service`, `gsi_rules`, `native_hid`, `aura_hal_py`, and `automation_v2`. This includes the existing legacy GSI/overlay regression suite and the 25-test Python HAL suite.

```powershell
cmake --build build/automation-v2-stage2 --config Release --target aura_daemon aura_web_ui test_automation_v2 test_gsi_rules test_plugin_runtime test_plugin_abi test_runtime_status test_lighting_service test_automation_service test_native_hid --parallel 4
ctest --test-dir build/automation-v2-stage2 -C Release --output-on-failure
# From tests/:
$env:AURA_BIN_DIR='G:/Aura/build/automation-v2-stage2/Release'
$env:PYTHONIOENCODING='utf-8'
python -m unittest test_runtime_entrypoints.TestDaemonEntrypoint test_runtime_entrypoints.TestWebUiEntrypoint test_automation_v2_daemon -v
```

Logs: `build/stage2-finalization-build.log`, `build/stage2-finalization-ctest.log`, `build/stage2-finalization-runtime.log`, and `build/automation-v2-stage2/Testing/Temporary/LastTest.log`. Generated logs/build outputs are not included in the patch.

The three known launcher entrypoint failures previously reproduced on the original baseline were not part of this daemon/web run; launcher source/packaging are unchanged. No full launcher-suite green claim is made. Real hardware and live CS2 behavior were not tested.

## Excluded scope and deferred limitations

Stage 3 owns actual generation-bound plugin/effect resolution, fresh effect instances, active-instance reconciliation, lifecycle callback execution, restart/ignore behavior, watchdogs and Overlay composition/Alpha/Additive rendering. These are not implemented here. Finished/opacity callbacks remain discovery metadata only. Stage 2 returns effect eligibility/admissions and the daemon discards them; it does not accumulate pending actions.

Studio one-shot generation/runtime, Automation authoring UI, Promote/Convert and Phase 4 API redesign are excluded. Stack, queue and latch remain deferred. No Effect vtable or PluginManager behavior changed in Stage 2.

Packet buffering is count-bounded, not a gameplay event log: aggregate kill deltas remain one occurrence, overflow intentionally loses temporal admissions, and admission-time foreground cannot prove event-time foreground. Telemetry-copy cost has not been benchmarked in a live game. Profile names remain config identifiers, not rename-proof asset IDs. Plugin reference syntax can be accepted without an installed plugin; Stage 3 must validate generation resolution before creating effects.

## Deviations and merge artifact

No functional deviation from the Owner-approved Stage 2 scope was identified. One nonfunctional implementation detail differs from the design wording: process strings are captured once but case folding/suffix equivalence is applied during leaf comparison rather than normalizing once during capture; all leaves still use the same captured value, with explicit canonicalization coverage. Legacy overlay delegation is the intentional compatibility path, not a new V2 lifecycle implementation.

`automation_v2_stage2_final.patch` is the complete staged binary-capable diff against checkpoint `23a5961`, including both Stage 2 reports and all implementation/tests. Earlier Stage 1/2 patch artifacts and the final patch itself are excluded from staging to avoid embedding generated patches inside the source patch. Staged whitespace and reverse-apply index checks must pass before delivery. Stage 2 remains uncommitted for merge review; Stage 3 has not started.
