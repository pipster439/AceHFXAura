> HISTORICAL: pre-retirement implementation evidence. Current Automation supports V2 only; see docs/architecture/AUTOMATION_V2.md.

# Automation v2 Stage 2 acceptance report

Status: implemented and staged for Owner review; stopped before Stage 3. Date: 2026-09-21.

## Checkpoint and scope

The accepted Stage 0/1 work was committed **before any Stage 2 edits** as `23a596108cd8e237350bb12369ffd4be2e61bf5e` (`Checkpoint Automation v2 Stage 0 fixtures and Stage 1 ABI substrate`). Its parent is the approved Phase 4 baseline `7b2e1c7887cf9c6f7e5a9601530b19b00282ed24`. That checkpoint was not amended. Stage 2 remains a separate staged, uncommitted change; nothing was pushed.

Authoritative design: [design spike](docs/architecture/automation_v2_design_spike.md), [Owner decisions](automation_v2_review_summary.md), and the accepted Stage 0/1 implementation. Studio defines rendering, Automation defines decisions, and GSI supplies observations. This change extends the existing RuleEngine and ConditionNode; it adds no second rule-engine service.

## Implemented changes

| Files | Change |
| --- | --- |
| `include/config/rule_engine.h`, `src/config/rule_engine.cpp` | Compile all four legacy record sources and new `model:"automation_v2"` orchestration records into one ordered `RulePlanEntry` vector. Entries carry source provenance, original array order and explicit compatibility policy. Config validation/publication retains the last valid plan on invalid input. |
| `src/config/automation_evaluation.cpp` | V2 AST parsing, three-valued snapshot evaluation with occurrence witnesses, shared profile/DND arbitration, per-rule rising/event memory, batch admission and freshness reconciliation. Legacy entry points delegate to the shared plan. |
| `include/gsi/automation_input.h` | Immutable telemetry/observation ownership and the decision snapshot contract, including process, epoch, sequence, monotonic receipt/admission times, age and freshness. |
| `include/gsi/gsi_adapter.h`, `src/gsi/gsi_adapter.cpp` | Observe existing detector emissions, normalize canonical occurrence IDs, retain up to 256 immutable packet observations and expose a single-consumer drain. Existing pulse/status/reader behavior is retained. |
| `src/main.cpp` | The owning daemon loop drains/evaluates Automation once per cycle and uses its profile/DND result. It captures foreground once per batch. Config reload also uses the decision path. V2 effect decisions are not sent to OverlayManager. |
| `tests/test_automation_v2.cpp`, `CMakeLists.txt` | A deterministic conformance target linked to the production rule/GSI sources; all existing historical test sources remain unchanged. |
| `tests/test_automation_v2_daemon.py` | A real daemon dry-run test verifies low-health profile selection followed by freshness fallback without another GSI packet. |

## One plan, with explicit legacy ownership

| Provenance | Compatibility policy / sole execution path |
| --- | --- |
| Legacy orchestration rules | Existing boolean ConditionNode evaluation; first eligible profile in orchestration array order; existing DND aggregation. |
| V2 orchestration records | Snapshot semantics; profile records share the same orchestration tier at their written array positions. Effect records produce decisions/admissions independently of the profile winner. |
| Legacy GSI bindings | Existing CS2/CSGO foreground gate, approximately 10 s status check, comparison/coercion behavior and binding order. |
| Top-level Application Rules | Existing process matching, order, profile selection and `suppress_web_ui` behavior. |
| Legacy event overlays | Plan entries have `LegacyOverlayExecutor` policy. `GetEventOverlayRules()` exports those entries exactly once to the existing OverlayManager binding path. The new evaluator does not emit duplicate legacy overlay admissions or replace their lifecycle. |

The old stored vectors remain compatibility/introspection data, not an additional evaluation pass. Profile arbitration iterates the compiled plan. Fallback/default selection retains its original order. New records are never persisted as translated legacy records. No Promote/Convert or automatic migration was added.

Legacy DND behavior remains independent of which matching orchestration profile wins: a matching orchestration DND rule can suppress, otherwise the first matching Application Rule supplies its suppression value. V2 profile-rule `dnd` participates in that policy and remains rule metadata, not an action. The initial V2 schema accepts rule-level `dnd` only for state → Activate Profile, the approved Phase 4 promotion path. Any `dnd` key on a trigger_effect rule (including `false`) rejects the candidate config; ignored DND metadata is not accepted.

## Review hardening: candidate validation

After all candidate profiles have been built, every V2 rule (including disabled rules) is checked against `new_profiles`. An Activate Profile target must exist, and a Trigger Effect reference with `kind: profile_effect` must name an existing candidate profile. A reference present only in the old runtime is insufficient. Any missing reference rejects the candidate before publication and preserves the previous profiles, plan and temporal memory.

The follow-up reference review verified this check in `RuleEngine::LoadConfig`, after profile construction and before the `!valid` rejection/publication boundary; it is not merely a `ParseAutomationRule` syntax check. The explicit error identifies the rule ID and missing candidate profile. `CandidateValidation` rejects both missing-target reloads and checks the previous plan ID and selected profile after rejection. Plugin references bypass this config-local lookup.

Additional tests exercise the 3×3 combinations of foreground/target `cs2`, `cs2.exe` and `CS2.EXE` in V2 scopes and conditions, equivalent-name inequality, and nonmatching suffix text. The event tests verify `event.kill AND NOT event.headshot` admits body-kill packets, rejects kill+headshot packets, and cannot admit an unrelated damage packet merely because headshot is absent.

A `kind: plugin` reference remains a syntactically validated name in Stage 2. It does not cause plugin load/instantiation during this validation pass. **Stage 3 must validate actual plugin-generation resolution through the accepted generation-bound factory**, including missing/failed loads, before admitting an effect into its runtime lifecycle. No Stage 3 resolution behavior is implemented here.

V2 AST limits are **maximum depth 32** and **maximum 256 nodes per rule**, shared across explicit `scope` and `when.condition`. Each root has depth 1; each logical, comparison or occurrence node counts once. Omitted scope contributes no configured node; there is no synthetic wrapper counted between the two roots. The budget resets for each rule. Standalone V2 AST parsing applies the same limits to its single tree. Legacy ConditionNode parsing/evaluation has no new limits.

Boundary/rollback tests cover depth 32 accepted / 33 rejected in both trees; exactly 256 shared nodes accepted / 257 rejected with either tree consuming the larger share; a condition-only 256-node tree with omitted scope; independent budgets for multiple rules; profile typos, deleted candidate targets and disabled invalid references; DND presence on all effect modes for both boolean values; supported profile DND; and legacy depth-33 / 257-node trees remaining valid. Each oversized or missing-reference rejection checks that the prior plan and active profile remain available.

## Snapshot and condition contract

Each packet observation owns a `shared_ptr<const AutomationTelemetry>`. The adapter samples production receipt time, assigns packet order and publishes it after collecting the existing detector's occurrences under the same GSI lock. Sampling receipt time inside that lock prevents concurrent HTTP workers from creating a false backwards-clock epoch. It contains the flattened telemetry view for that packet, including existing field aliases, but excludes public pulse/sequence fields as V2 inputs. Vector position is an occurrence's ordinal within its packet.

A decision batch captures foreground once and combines it with that immutable telemetry and occurrence vector. Every V2 scope/condition leaf reads only the same `const AutomationInputSnapshot`; it cannot independently query a live GsiState. Production admission time is sampled after the input drain/foreground capture. Explicit monotonic times are injectable for deterministic tests. Event process predicates mean **foreground at admission**, not at gameplay occurrence time.

A final current-state snapshot reconciles profile/persistent eligibility and process/freshness changes on every daemon cycle, even with no new packet. V2 state decisions describe current eligibility; rising rules observe packet snapshots in order plus current reconciliation. Profile arbitration uses the current snapshot.

For V2 only:

- Missing, incompatible-typed, non-finite or stale telemetry returns Unknown. NOT Unknown remains Unknown. AND uses False dominance; OR uses True dominance. Scope and condition eligibility follow the same three-valued conjunction policy.
- Process leaves remain usable without telemetry, including independent successful process branches under OR. Executable comparison retains case folding and existing `.exe` equivalence.
- Valid same-type numeric comparisons retain the existing equality tolerance; valid string comparisons retain case-insensitive matching. New rules do not inherit legacy numeric-string coercion: an incompatible type is Unknown.
- Event leaves are allowed only in event conditions, never in scopes or state/rising conditions. Public `event.*` pulse fields and `event_sequence.*` are rejected as V2 state leaves.

## Pairings, temporal memory and occurrences

Accepted pairings are state → Activate Profile, state → Trigger Effect / while_true, and rising/event → Trigger Effect / one_shot. Unsupported pairings, stack, queue, latch activation, invalid operators and duplicate V2 IDs reject the candidate config and retain the previous plan. Restart and ignore_while_active are accepted **metadata only**; no retrigger effect runtime exists yet.

Rising admissions require an observed valid False → True. Initial True, Unknown → True, scope entry, re-enable, source restart, overflow and semantic edits seed without firing. Unknown disarms the previous truth. Unchanged config reload retains matching rule memory; semantic fingerprints exclude display name/description, and deleted rules lose their memory. A freshness-policy change clears temporal memory. A receipt timestamp of zero is valid, not an uninitialized sentinel.

Event conditions use only detector occurrences from the admitted packet. AND means same-packet co-occurrence. Successful expressions require a positive occurrence witness: NOT contributes none; OR only retains witnesses from true branches. Thus NOT event alone and a process-only successful branch of `(process == cs2 OR event.kill)` cannot admit on an unrelated damage packet. A kill-counter delta greater than one stays one detector occurrence. Truthful aliases such as `event.flash` normalize through the adapter mapping; the legacy synthetic `event.round_mvp` alias is not invented as a V2 MVP occurrence.

The input buffer retains all packet observations, including those without events, so intermediate rising-state transitions are observable. It is bounded at **256**, drops oldest on overflow, increments a cumulative diagnostic and marks discontinuity. On drain after overflow, temporal rules conservatively seed from the latest snapshot and discard retained backlog; state eligibility still reconciles immediately. The evaluation returns `dropped_input_batches`/`rebased` and logs overflow. These are input observations, not pending actions.

Inputs are drained once globally and offered independently to rules; no rule steals an occurrence. Returned admissions are ordered by epoch/packet sequence, then config order within a packet. Startup, scope entry and config edits discard pre-entry observations for affected rules. Source epochs advance on explicit Clear, a greater-than-10-second receipt gap, or a regressing injected receipt clock; shorter gaps exceeding the Automation freshness policy also disarm/rebase affected temporal rules. No old occurrence is reconstructed from pulse booleans or counter deltas.

## Freshness

`orchestration.automation_freshness_ms` defaults to **3000**, must be a positive integer, and applies only to V2. Age 3000 is fresh; 3001 is stale. Each event batch uses its own receipt timestamp, never a newer packet's freshness.

No-packet expiry produces ineligible/Unknown GSI-dependent persistent decisions and disarms rising state; independent process eligibility can remain True. On recovery, state eligibility may return immediately, while rising seeds and the first recovered occurrence batch after a freshness gap is not replayed. Subsequent valid transitions/occurrences can admit normally. No rendered layer, action queue or latch is manipulated in Stage 2.

Legacy `IsActive`, connection/status timing, boolean ConditionNode evaluation, GSI bindings, event overlays and Effect IGsiReader rendering semantics were not changed.

## Exact verification results

Environment: Windows x64, MSVC Release, isolated build directory `build/automation-v2-stage2`, Python 3.13.

| Run | Result |
| --- | --- |
| Production daemon/web and all relevant C++ test targets built | Passed |
| Full registered CTest suite | **10/10 passed**, 0 failures (8 C++ executables plus frozen-fixture integrity and Python HAL suite) |
| New Automation v2 conformance test | **257 assertions passed**, including candidate-reference, process canonicalization, event witness, DND and AST-boundary review regressions |
| Existing Plugin ABI test | **212 assertions passed**; prebuilt and synthetic Stage 0 fixture integrity passed |
| Existing daemon/web entrypoint tests | **26/26 passed**, no skips |
| New real daemon dry-run GSI/freshness test | **1/1 passed**, no skips; low-health selection and no-new-packet fallback observed |
| Combined runtime Python run | **27/27 passed**, no skips |

The CTest names were `plugin_runtime`, `stage0_fixture_integrity`, `plugin_abi`, `runtime_status`, `lighting_service`, `automation_service`, `gsi_rules`, `native_hid`, `aura_hal_py`, `automation_v2`. The existing Python HAL suite contained 25 tests. The unchanged `gsi_rules` suite exercised the captured Stage 0 configs as well as its prior regression cases.

Conformance includes concurrent production-ingress timestamp/sequence ordering and deterministic scenarios for health 20→10→8→20→10 (exactly two rising admissions), initial low health, Unknown/recovery seeding, 3000/3001 boundaries, no-packet expiry, custom freshness, disconnected process branches, NOT missing, false scope dominance, immutable snapshots despite live mutations, foreground capture counts, two packets before one frame, duplicates, kill/headshot conjunction and negation, positive witnesses, packet-specific conditions/scopes, expired-batch freshness isolation, counter reset, source restart, exact 256-entry overflow/drop-oldest behavior, global admission ordering, config-order arbitration, semantic edits, unchanged reload, disable/re-enable, scope re-entry, invalid-config retention, and legacy coercion/NOT/unknown-metadata behavior.

Commands:

```powershell
cmake -S . -B build/automation-v2-stage2 -A x64
cmake --build build/automation-v2-stage2 --config Release --target aura_daemon aura_web_ui test_automation_v2 test_gsi_rules test_plugin_runtime test_plugin_abi test_runtime_status test_lighting_service test_automation_service test_native_hid --parallel 4
ctest --test-dir build/automation-v2-stage2 -C Release --output-on-failure
# From tests/:
$env:AURA_BIN_DIR='G:/Aura/build/automation-v2-stage2/Release'
python -m unittest test_runtime_entrypoints.TestDaemonEntrypoint test_runtime_entrypoints.TestWebUiEntrypoint test_automation_v2_daemon -v
```

Logs from the review reruns: `build/stage2-review-build.log`, `build/stage2-reference-review-build.log`, `build/stage2-ctest.log`, `build/automation-v2-stage2/Testing/Temporary/LastTest.log`, `build/stage2-runtime-final.log`. The follow-up added tests only and rebuilt `test_automation_v2`; all CTest and daemon/web/freshness runs were repeated. Generated logs/binaries are not included in the source patch. Existing compiler/deprecation and Python resource warnings are not presented as a warning-free acceptance claim.

The three known launcher test failures reproduced on the original baseline during Stage 1 were not repaired, weakened or rerun here. Launcher source/packaging are unchanged; this report does not claim a green full launcher suite. **NOT VERIFIED ON REAL HARDWARE** or a live CS2 session.

## Legacy differences, deviations and remaining boundary

No legacy behavior difference was found on the captured fixtures or the existing regression suites. The compatibility review specifically caught and preserved ignored non-string legacy `model` metadata: only the exact string `automation_v2` opts in. Legacy GSI bindings continue to use `operator` and retain numeric-string coercion. Historical tests were not rewritten.

No requested scope was deferred or expanded. Keeping legacy overlays delegated through a compatibility-tagged plan entry is intentional: replacing their executor would change legacy lifecycle/composition and exceed Stage 2. The observation buffer is packet-count bounded, not a new action queue; telemetry-copy cost has not been benchmarked on a live game.

Stage 3 still owns effect instantiation from admissions, active-instance reconciliation/cancellation, restart/ignore execution, watchdogs, lifecycle callback invocation and composition. In Stage 2 the daemon consumes the profile/DND result, while V2 effect eligibility/admissions are returned by the evaluator and discarded at the main-loop boundary. There is no hidden effect execution or accumulated action queue. Finished/opacity callbacks remain uninvoked by production; no Effect vtable, PluginManager, OverlayManager renderer, Studio generator, authoring UI, Promote/Convert, stack/queue/latch, or Phase 4 API implementation was changed.

## Review artifact

`automation_v2_stage2.patch` is the complete staged binary-capable diff against checkpoint `23a5961`. It includes this report and all new source/tests. The earlier `automation_v2_stage1.patch` remains a separate untracked review artifact and is not mixed into Stage 2. Neither patch includes itself. Stage 2 is staged but not committed; stop here for Owner review before Stage 3.
