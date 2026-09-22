> HISTORICAL: pre-retirement implementation evidence. Current Automation supports V2 only; see docs/architecture/AUTOMATION_V2.md.

# Automation v2 Stage 3 acceptance report

Status: implemented for Owner review; stop before Stage 4. Date: 2026-09-21.

## Checkpoint and scope

The reviewed Stage 2 work was committed **before any Stage 3 edits** as `3c32e06` (`Checkpoint accepted Automation v2 Stage 2 evaluation runtime`). Its parent is the unchanged accepted Stage 0/1 checkpoint `23a596108cd8e237350bb12369ffd4be2e61bf5e`. Stage 3 is a separate staged, uncommitted change; no amendment, push or merge was performed.

Authority: [design spike](docs/architecture/automation_v2_design_spike.md), [Owner decisions](automation_v2_review_summary.md), accepted Stage 0/1 and Stage 2, and the Owner's Stage 3 implementation instructions. The review-summary file lives at the repository root. Stage 3 adds basic effect lifecycle and composition only; RuleEngine remains the sole business-condition evaluator.

## Exact implementation

| Files | Implementation |
| --- | --- |
| `include/engine/automation_effect_runtime.h`, `src/engine/automation_effect_runtime.cpp` | Consume already-produced decisions; own one layer per rule, True-interval completion/failure memory, fresh restart, ignore-while-active, lifecycle dispatch, watchdog, bounded diagnostics and RGB composition. At most 32 active V2 instances; no pending action queue. |
| `include/config/rule_engine.h`, `src/config/automation_evaluation.cpp` | Decisions carry config order and evaluations carry a monotonic successful-config generation. Validate priority as signed 32-bit integer, one-shot watchdog as integer 1..60000, and optional compatibility envelope values before publication. No new WHEN semantics or scope evaluator. |
| `include/engine/effect.h`, `src/config/rule_engine.cpp` | Retain each accepted profile's serialized recipe and reuse `CreateEffectFromProfile` for fresh builtin objects. Plugin recipes use the existing plugin-name aliases and generation-bound factory. Profile brightness, key overrides and FPS are excluded from Trigger Effect construction. The Effect vtable is unchanged; serialized recipe storage introduces no JSON dependency into the packaged Studio SDK. |
| `include/engine/plugin_manager.h`, `src/engine/plugin_manager.cpp` | Add a host-effect sidecar factory for builtins and an optional quiet factory/load path used only by Automation, whose runtime emits bounded per-rule errors. Existing callers retain default diagnostics and identical ABI normalization/publication behavior. The accepted generation-pinning deleter, including normal destruction exception containment, is unchanged. |
| `include/engine/effect_engine.h`, `src/engine/effect_engine.cpp` | Compose current base/preview, V2 persistent, existing legacy overlay group, then V2 transient. `TickAt` exposes the same rendering path with an explicit clock for golden tests; production `Tick` delegates to it. |
| `src/main.cpp` | Feed normal and post-reload AutomationEvaluation results into the effect executor. Config/plugin binding rebuilds conservatively cancel V2 instances. Capture a runtime revision before evaluation so a completed rebuild invalidates in-flight old work. |
| New runtime test, synthetic lifecycle DLL, daemon integration test, `CMakeLists.txt` | Real DLL lifecycle/exception/generation tests, deterministic frames and rules-to-runtime tests, and a real dry-run daemon test proving persistent completion does not respawn. Existing historical tests and legacy OverlayManager source are unchanged. |

`state/while_true` creates once at True entry, advances without restarting, and removes on False/Unknown. Completion or construction failure marks that True interval consumed, preventing per-frame respawn/retry. False/Unknown re-arms the next True interval; rebuild also starts a new runtime interval.

Only admitted one-shot decisions start/retrigger. Restart constructs the replacement successfully before releasing the old instance; null or throwing factories retain the previous active object and its elapsed time. Ignore-while-active consumes the admission without resetting the object or retaining replay work. Active means still present in the runtime at admission; completion is discovered during the render pass.

Missing plugin/reference resolution returns no instance, including plugin-backed profile recipes: it never borrows the legacy black placeholder. A missing Replace therefore leaves the accumulated lower frame intact. Runtime diagnostics deduplicate `(rule ID, reason)` and retain at most 64 messages per rebuild. The quiet PluginManager path prevents repeated lookup/load/factory failures from bypassing this bound; regular loader/reload and existing destruction diagnostics retain their prior behavior. Failed resolution does not suppress future one-shot admissions or introduce retry queues.

## Ownership and callback containment

Ownership chain:

```text
AutomationEffectRuntime::Layer
  -> TriggeredEffectInstance
       -> shared_ptr<const PluginEntry> -> PluginHandle -> HMODULE
       -> shared_ptr<Effect>
            -> noexcept deleter capturing that same PluginEntry
                 -> that generation's DestroyEffect
```

The runtime serializes construction, callbacks, retirement and rebuild clearing with its mutex. The sidecar's effect and metadata originate in one factory result. No lookup of current-generation callbacks occurs during Render/finished/opacity. The effect deleter independently pins the generation, including during replacement assignment, and the sidecar keeps generation ownership until destruction completes. Tests verify old objects still use old callbacks after registry replacement and after PluginManager destruction, then release the last old generation when retired.

Per capable-instance frame: compute one elapsed value, clear the temporary buffer, RenderWithContext (including when opacity will be zero), query finished with that same elapsed, retire without composition on finished, otherwise query opacity with the same elapsed, validate/clamp and compose. Finished must be 0 or 1; invalid status, NaN/Inf, or cooperative Render/finished/opacity exceptions retire only the affected instance before any of its pixels are copied. Lower layers remain intact. Finite opacity is clamped to [0,1]. Finished frames do not query opacity.

Normal shared_ptr destruction still uses the accepted `noexcept` deleter and catches plugin DestroyEffect exceptions. The fixture tests throwing after delete; the host cannot guarantee a misbehaving plugin freed its allocation if it throws before deletion. Containment claims apply to cooperative C++ exceptions in these V2 paths, not native access violations, deadlocks, terminate, or arbitrary memory corruption. Existing legacy/base-effect execution is not rewritten into a new isolation boundary.

## Lifecycle matrix actually exercised

| Capability / failure | Runtime behavior and test |
| --- | --- |
| Revision 1 + finished + opacity | Real Stage 3 DLL checks exact object generation and Render→finished→opacity elapsed/order. Opacity drives Alpha; finished removes without composing. |
| Revision 1 + finished only | Existing `abi_finished_only` DLL uses opacity 1 and retires at plugin completion. |
| No lifecycle | `abi_no_lifecycle` uses default LegacyEnvelope. |
| Unknown revision | `abi_lifecycle_unknown` uses LegacyEnvelope; no lifecycle callback execution. |
| Opacity only / exports without revision | `abi_opacity_only` and `abi_lifecycle_no_revision` use LegacyEnvelope. |
| Capable effect with compatibility values | Plugin opacity/completion wins; host attack/fade/duration is not multiplied into it. |
| Zero opacity | Stateful DLL still renders and advances, becoming visible on its next frame. |
| Finished status 99; NaN/Inf; Render/finished/opacity throw | Instance retires; lower frame unchanged. |
| Finite opacity outside [0,1] | Clamp to 0 or 1. |
| Factory null/throw during restart | Previous running instance remains. |
| Early finished while_true | Completed True interval does not respawn; Unknown→True re-arms. Real daemon test additionally observes this through GSI and main-loop rendering. |
| Destroy throws after deleting | Accepted normal deleter contains the exception; host remains alive. |

Non-capable one-shots default to `duration_ms=1200`, `fade_out_ms=400`, `attack_ms=0`. Explicit values are under `action.compatibility` with `lifecycle: legacy_envelope`; the runtime reuses `ActiveOverlay::ComputeWeight` exactly. Persistent fallback uses weight 1 and ignores duration/fade. No force-legacy double envelope is applied to capable plugins.

Capable one-shots default to a 5000 ms watchdog; configured one-shot `watchdog_ms` accepts 1..60000. A fallback envelope's implicit watchdog is `max(5000, duration_ms)`, so a 10-second legacy envelope is not cut to five seconds. An explicitly configured watchdog may shorten it. Persistent effects have no watchdog; schema rejects a configured one. Checks occur before and between calls, including elapsed wall time spent in returned callbacks. They cannot interrupt a hung call.

## Layer order and golden output

```text
current Base Profile -> V2 persistent -> unchanged legacy group -> V2 transient -> final RGB
```

Each V2 class sorts ascending `(priority, config rule_order, instance_sequence)`; higher priority paints later, but cannot cross class boundaries. Restart receives a new instance sequence. There is one instance per rule, so an equal-order cross-rule tie is normally only an injected test case; the final sequence tie-break is tested explicitly.

The default route is the existing entire RGB FrameBuffer; no new routing UI or per-pixel alpha ABI was added. Overlay covers an RGB pixel only if at least one channel is nonzero. Replace covers black as well. Alpha computes `(1-w)*L + w*E` on covered pixels; Additive clamps `L+w*E` to 255. Final conversion truncates to bytes, matching the legacy renderer's rounding convention. Replace+Additive and Overlay+Additive are equivalent for RGB; tested at fractional opacity including black pixels.

The HealthGradient golden test uses the production EffectEngine/AutomationEffectRuntime composition path with a test Effect reading live GsiState through IGsiReader. Health 100 produces base `(0,100,0)`; half-white flash yields `(127,177,127)`. At the same flash opacity, health changes to 20 and base becomes `(80,20,0)`, immediately yielding `(167,137,127)`. At expiration the frame is `(80,20,0)`. No framebuffer is captured at trigger time. HealthGradient is a test effect, not an added builtin or Studio generator feature.

## Reload boundary and Stage 4 interface needs

Successful config publication changes config generation and cancels all V2 active/interval state at the next consume. The existing main config/plugin binding rebuild also clears V2 immediately. Successful plugin reload conservatively cancels V2; failed reload leaves the prior registry and running instances intact. A revision token rejects a decision computed across a completed rebuild. Base profile selection changes alone do not clear V2, so a transient reveals the current base frame.

Semantic-equivalent config preservation and recipe/plugin publication preservation are **not implemented**. Stage 4 must coordinate those transactions and decide which active instances retain their original generation. The pinning tests prove lifetime safety, not a claim that daemon hot reload currently preserves active transients.

Current Stage 2 one-shot output contains admissions, not a continuing per-rule cancellation/scope-validity stream. Consequently Stage 3 one-shots finish through their lifecycle/envelope/watchdog or explicit rebuild cancellation; they are not independently cancelled on later scope exit/GSI staleness. Persistent actions do receive current eligibility and cancel on False/Unknown, including stale-without-packet. Stage 4 needs explicit RuleEngine cancellation/reconciliation metadata for scoped transients. No second business evaluator was added in OverlayManager or the effect executor.

## Verification

Windows x64, MSVC Release, Python 3.13. Reused the isolated `build/automation-v2-stage2` directory, reconfigured and rebuilt against Stage 3 sources (directory name is historical).

| Suite | Final result |
| --- | --- |
| Daemon/web and all relevant C++ targets | Build passed |
| Full Release CTest | 11/11 passed |
| New Stage 3 runtime | 115 assertions passed |
| Unchanged Stage 2 conformance | 257 assertions passed |
| Plugin ABI/factory/lifetime | 212 assertions passed |
| Existing daemon/web entrypoints | 26/26 passed, no skips |
| Existing real-daemon freshness | 1/1 passed, no skips |
| New real-daemon lifecycle integration | 1/1 passed, no skips |
| Combined runtime Python run | 28/28 passed, no skips |

CTest includes plugin_runtime, plugin_abi, frozen Stage 0 fixture integrity, runtime_status, lighting_service, automation_service, legacy gsi_rules/overlays, native_hid, Python HAL, automation_v2 and automation_effect_runtime. No historical tests were weakened or rewritten. New lifecycle DLLs are explicitly synthetic test targets; packaging selects production executables/SDK assets and does not copy these DLL targets. Historical DLL fixtures were not rebuilt.

Commands:

```powershell
cmake -S . -B build/automation-v2-stage2 -A x64
cmake --build build/automation-v2-stage2 --config Release --target aura_daemon aura_web_ui test_automation_effect_runtime test_automation_v2 test_gsi_rules test_plugin_runtime test_plugin_abi test_runtime_status test_lighting_service test_automation_service test_native_hid --parallel 4
ctest --test-dir build/automation-v2-stage2 -C Release --output-on-failure
# From tests/:
$env:AURA_BIN_DIR='G:/Aura/build/automation-v2-stage2/Release'
$env:PYTHONIOENCODING='utf-8'
python -m unittest test_runtime_entrypoints.TestDaemonEntrypoint test_runtime_entrypoints.TestWebUiEntrypoint test_automation_v2_daemon test_automation_effect_daemon -v
```

Logs: `build/stage3-final-build.log`, `build/stage3-ctest.log`, `build/stage3-runtime.log`, and CTest `Testing/Temporary/LastTest.log`. Existing compiler/deprecation warnings and deliberate synthetic exception-export warnings are not claimed as warning-free. The three previously baseline-reproduced launcher failures were outside this daemon/web run; no full launcher green claim. Real hardware and a live CS2 session were not tested.

## Deviations, exclusions and artifact

No unapproved runtime capability was added. The permitted conservative rebuild cancellation and explicitly deferred transient cancellation metadata are described above. A failed persistent construction is consumed for its True interval rather than retried each frame; this prevents unbounded factory/load work and re-arms on False/Unknown or rebuild. Diagnostics are aggregate rule-level failures on the Automation quiet loader path; regular loader diagnostic detail remains available through normal load/reload calls.

Not implemented: Stage 4 semantic preservation, Studio one-shot publication/generation, Promote/Convert, Automation authoring UI, stack, queue, latch, Phase 4 API redesign, per-pixel alpha ABI, plugin process isolation, or a second builtin/effect registry.

`automation_v2_stage3.patch` is the complete staged binary-capable diff against the separate accepted Stage 2 checkpoint. Prior generated patches and this patch itself are excluded from staging. Stage 3 remains uncommitted for Owner review. Stop before Stage 4.
