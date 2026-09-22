> HISTORICAL: pre-retirement implementation evidence. Current Automation supports V2 only; see docs/architecture/AUTOMATION_V2.md.

# Automation v2 Stage 4 — publication, reload and reconciliation

Status: implemented for Owner review; stop before Stage 5. Accepted Stage 3 was committed separately as `3f6524f` before Stage 4 edits. Stage 0/1 (`23a5961`) and Stage 2 (`3c32e06`) were not amended. The accompanying staged patch is against the Stage 3 checkpoint. Historical patch artifacts are not patch inputs.

## Runtime publication and ownership

The daemon render owner checks config changes and applies plugin publication commands before its next Automation evaluation/render frame. HTTP threads prepare DLL candidates and wait for a publication result; they do not mutate EffectEngine, AutomationEffectRuntime or legacy overlay collections. `PluginPublicationQueue` is a bounded 32-command registry publication mailbox, **not** an Automation action queue. A timed-out command is cancelled under its command lock and cannot publish later. Publication retains PluginManager's expected-generation and alias collision checks.

Config parsing/profile construction and RuleEngine's validated plan swap run synchronously between frames on the owner thread. This keeps config/plan publication coherent without introducing a second config worker or an independently mutable live plan. It can lengthen a frame; DLL shadow loading and candidate factory validation for explicit plugin reload occur on the request thread. Reconciliation and effect construction occur between frames, outside rendering calls, before new instances become visible. No framebuffer snapshot is restored.

Failed parsing/schema/reference validation keeps the previous RuleEngine generation. Failed DLL preparation, ABI/lifecycle validation, factory probe, collision, stale publication or publication timeout leaves the registry's prior generation active. Successful plugin publication changes future factories; it does not invalidate already accepted instances.

`TriggeredEffectInstance` retains its effect and its exact `PluginEntry` generation. Its lifecycle pointers come exclusively from that generation. The effect's shared_ptr deleter independently pins the same generation through `DestroyEffect`, with the accepted exception containment. A base effect also retains this generation through its deleter even after its temporary sidecar leaves scope. Existing transients never acquire callbacks from a newer DLL.

## Rule identity and reconciliation

The stable identity is the required V2 `id`. The semantic fingerprint is the canonical JSON string of parsed mode, enabled, DND, scope, condition and action, with composition/blend/priority/retrigger defaults normalized. It is an equality key, not a lossy numeric hash. Display name/description and other outer display metadata are excluded. GSI-dependent runtime identities additionally include effective `automation_freshness_ms`. Scope/condition syntax remains the accepted ConditionNode representation; this is not algebraic equivalence checking of arbitrary expressions.

The candidate plan preserves temporal memory only for matching enabled IDs/fingerprints; deleted, disabled or changed definitions lose memory immediately. Existing Stage 2 epoch, overflow, freshness and scope admission rebasing remains in force. Cosmetic edits preserve armed rising/event memory. New/changed rising definitions seed rather than firing for initial True.

`AutomationEvaluation` adds an authoritative reconciliation flag, presence of V2 records, and per-trigger-rule status: ID, semantic/action/recipe identity, referenced plugin generation, config order, enabled/persistent flags, explicit scope truth and continuing validity. The evaluation already carries config generation. RuleEngine computes these statuses from the same current immutable decision snapshot used for arbitration. Event admission batches retain their own immutable snapshots.

AutomationEffectRuntime consumes these results; it never evaluates ConditionNode. It removes deleted/disabled/changed instances and their interval metadata. An active transient is cancelled when its explicit scope is False/Unknown, or when its required telemetry becomes stale. A condition being false after event admission does not cancel a shot by itself. Unscoped foreground changes do not cancel. An independent True process branch can remain valid without telemetry, preserving three-valued semantics. The existing 3000-ms fresh / 3001-ms stale boundary and recovery rebasing are unchanged; no packet is required to reconcile expiry.

## Recipes, base and plugin replacement

Profile-backed effect identity is canonical recipe JSON excluding profile brightness, fps, keys and display labels; those are not part of a triggered base-effect recipe. Direct plugin references retain their reference identity and current generation ID. This reuses profile recipes and PluginManager, without a new effect asset registry.

| Change | Running transient | Persistent while_true / selected base |
| --- | --- | --- |
| Cosmetic/unrelated config | Retains object, clock and generation | Retains compatible object and clock |
| Profile recipe changes behind stable reference | Old recipe finishes; next admission resolves new recipe | Prepares fresh object and swaps once between frames |
| Successful referenced DLL publication | Old pinned generation/callbacks finish; next admission uses new | Prepares from new generation and swaps once |
| Replacement factory failure | Existing accepted instance stays valid | Keeps prior object; diagnoses failure and does not retry the same failed identity every frame |
| Failed DLL reload | Unchanged | Unchanged |
| Deleted/disabled/semantic rule edit | Cancels | Cancels rule instance and clears interval memory |

Persistent interval metadata remembers attempted recipe/generation, including completed or unavailable instances. An unchanged completed interval does not respawn; a newly published recipe/generation can receive one new construction attempt. A later False/Unknown interval re-arms normally. Missing resolution creates no layer, including no black Replace layer. Successful replacement is constructed before the old layer is retired.

Base reconciliation compares selected profile name, recipe, brightness/fps/key overrides and plugin generation. A matching selection does not restart because another config field changed. Failed construction keeps the last runnable base. The legacy-only branch deliberately retains its existing reset-on-reload behavior, as required by the approved compatibility policy.

## Studio publication format and terminal semantics

Studio now explicitly selects `continuous` or `one_shot`. The Blockly workspace format remains unchanged; its existing config record carries:

```json
{
  "publication": { "mode": "one_shot", "fade_out_ms": 250 },
  "applied_publication": { "mode": "continuous", "fade_out_ms": 0 },
  "applied_plugin_name": "previous_immutable_publication"
}
```

`publication` belongs to the editable draft. `applied_publication`, applied workspace/revision/plugin and profile reference change only after successful publication/config save. Missing publication metadata defaults to continuous, including old workspaces and the existing Orchestrator bulk publisher. Fade is explicit, bounded 0..60000 ms, and zero for continuous mode. No terminal inference from block count, black output, sequence syntax or duration occurs.

Continuous compilation keeps the original resumable sequence loop. One-shot uses the same program counter/wait machinery but its terminal instruction records completion intent instead of restarting. The terminal visual is rendered with finished=false. With an authored terminal fade, subsequent frames expose its scalar opacity; a zero-opacity frame occurs before finished=true on the following render. Without a fade, the terminal visual still gets a frame before completion. Rewinding elapsed time does not restart a terminal one-shot. JS preview uses the same explicit mode/fade contract.

Generated one-shots retain packed ABI v1 (`0x00010000`) and export the accepted calling convention:

```cpp
uint32_t AURA_PLUGIN_CALL AuraGetEffectLifecycleVersion(); // 1
uint32_t AURA_PLUGIN_CALL AuraIsEffectFinished(const aura::Effect*, uint64_t elapsed_ms);
float AURA_PLUGIN_CALL AuraGetEffectOpacity(const aura::Effect*, uint64_t elapsed_ms);
```

Opacity is a per-instance scalar: 1 before its defined terminal fade, then clamped to 0. Generated continuous effects export no optional lifecycle callbacks. Effect's vtable and old DLL loading are unchanged. Explicitly authored non-terminating loops are not inferred to finish; the existing Automation watchdog still applies.

## Publication transaction

1. Generate code for an immutable publication name and the selected mode.
2. Compile to a unique staging DLL. Compilation is serialized per web process; the final immutable name must not exist.
3. Atomically move the completed candidate into its immutable DLL name without overwrite. No active DLL is linked over or replaced in place.
4. Daemon prepares a shadow generation, normalizes ABI, probes factory/destructor, and for one-shot requires lifecycle revision 1 plus finished capability.
5. Daemon owner publishes the generation between frames and acknowledges success; web returns lifecycle verification for one-shot.
6. Studio performs its revision-checked config reference update. A changed local config snapshot/revision or server If-Match conflict rejects that save.

Compile/load/capability/config failure leaves the previous applied reference and runnable generation intact; the draft remains editable. A successful candidate followed by a failed config commit can leave an unused immutable DLL/registry entry. It does not change the old applied pair; orphan garbage collection is not part of this stage. Publication is not a cross-process filesystem transaction, and acknowledgement of config persistence is not a separate daemon activation acknowledgement.

## Compatibility and scope

Legacy event overlays retain their executor, ClearBindings reload policy, pulses, layer placement and envelope math. Legacy-only base reload behavior is retained. Phase 4 Application Rules and DND API behavior are untouched. Existing ABI fixtures and Studio generated DLLs remain usable without republishing. Synthetic/generated test DLLs are not added to release packaging.

No Automation authoring UI, Promote/Convert, stack, action queue, latch, per-pixel alpha, process isolation, script actions, second evaluator or Phase 4 redesign was added. The Studio controls describe HOW TO RENDER only.

## Validation

Commands use `build/automation-v2-stage2` (the existing isolated validation build) with Release binaries. Results below are from the final build/run. No historical assertion was removed or weakened. The synthetic lifecycle fixture gained generation-specific tracing and an optional completion constant; its existing defaults remain unchanged.

| Suite | Result |
| --- | --- |
| Full Release CTest | 12/12 passed |
| Stage 1 Plugin ABI/factory/lifetime | 212 assertions, passed; frozen fixture integrity and real DLL runtime suite also passed |
| Stage 2 conformance | 257 assertions, passed |
| Stage 3 runtime/composition | 115 assertions, passed |
| Stage 4 reconciliation/publication | 83 assertions, passed |
| Existing daemon/web entrypoints + Stage 2 freshness + Stage 3 completion | 28/28 passed |
| New real-daemon reload/publication integration | 2/2 passed |
| Frontend `npm test` | 35 total: 32 passed, 3 existing portable g++ tests skipped on this Windows environment, 0 failed |
| Production frontend `npm run build` | Passed; regenerated `web/index.html` |

Native MSVC fixtures generated by the actual Studio transpiler exercise old/default continuous looping, explicit continuous looping, one-shot wait/terminal behavior, lifecycle negotiation, opacity fade, zero frame and no implicit restart. These run in the C++ suite despite the portable g++ skips. Node/frontend dependencies enable this additional test target; production native configuration does not require Node.

The final combined Python run passed all 30 tests in 19.167 seconds; full CTest passed in 2.41 seconds. Local logs: `build/stage4-full-build.log`, `build/stage4-ctest.log`, `build/stage4-entrypoints.log`, `build/stage4-frontend-tests.log`, and `build/stage4-frontend-build.log`. MSVC warnings were non-fatal. No physical hardware acceptance is claimed.

The new daemon integration uses real Release daemon/web processes, actual MSVC compilation, HTTP reload acknowledgement, immutable DLL hash checking, and config If-Match persistence. It proves old transient/new persistent coexistence and semantic cancellation, plus compile failure, unknown ABI rejection, successful lifecycle publication, refused DLL overwrite and rejected stale config revision. Hardware output and live CS2 gameplay were not exercised.

Reproduction:

```powershell
cmake -S . -B build/automation-v2-stage2 -A x64
cmake --build build/automation-v2-stage2 --config Release --parallel 4
ctest --test-dir build/automation-v2-stage2 -C Release --output-on-failure
$env:AURA_BIN_DIR='G:\Aura\build\automation-v2-stage2\Release'
$env:PYTHONPATH='tests'
python -m unittest test_runtime_entrypoints.TestDaemonEntrypoint test_runtime_entrypoints.TestWebUiEntrypoint test_automation_v2_daemon test_automation_effect_daemon test_automation_reload_daemon -v
cd frontend
npm test
npm run build
```

## Remaining interface needs and limitations

Stage 5 can consume stable rule IDs, profile/plugin references, explicit publication metadata and the established revision checks. Promotion still needs its separately approved process/profile/DND mapping and unknown-field refusal; none is implemented here. Runtime status currently diagnoses replacement failures through bounded logging, without a new authoring/status API.

Config preparation and replacement construction are synchronous between frames; large configs or slow native factories can delay rendering. Native hangs/access violations remain outside C++ exception containment. The existing one-shot watchdog must be configured to accommodate an intentionally long Studio sequence/fade. Identity comparison is canonical structural comparison, not proof that differently authored recipes are behaviorally equivalent.

There is no intentional semantic deviation from the approved Stage 4 scope. The owner-thread synchronous config publication and optional Node-dependent test target are implementation choices described above. No Stage 5 work was begun.
