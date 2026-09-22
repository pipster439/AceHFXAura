> HISTORICAL: pre-retirement implementation evidence. Current Automation supports V2 only; see docs/architecture/AUTOMATION_V2.md.

# Automation v2 Stage 6 acceptance report

Status: implemented and verified; stopped after Stage 6. The reviewed Stage 5A checkpoint was committed separately as `ddde10f` (`Checkpoint accepted Automation v2 Stage 5A authoring and migration`). Stage 4 and earlier commits were not amended. This patch is relative to that Stage 5A checkpoint.

## Scope and code changes

Added `stack` and `queue` only for rising/event `trigger_effect` / `one_shot`. The shared daemon parser remains authoritative for authoring validation and runtime loading. State/profile actions, `while_true`, unknown policies and configurable advanced-policy limits are rejected. No latch, new trigger semantics, ABI change, composition mode, authoring UI redesign or audit remediation was implemented.

* `include/config/automation_limits.h`: shared fixed resource limits used by runtime and capabilities.
* `include/engine/automation_effect_runtime.h`, `src/engine/automation_effect_runtime.cpp`: prepared source, pending tokens, bounded admission/draining/reconciliation, counters; extracted the existing layer initialization into `MakeLayer` without changing envelope/watchdog/render behavior.
* `include/engine/plugin_manager.h`, `src/engine/plugin_manager.cpp`: generation preparation without creating an Effect, and construction from an owned exact generation.
* `src/config/rule_engine.cpp`, `src/main.cpp`: immutable recipe/plugin source preparation wired into the existing decision consumer.
* `src/config/automation_evaluation.cpp`, `src/config/automation_authoring.cpp`: authoritative policy validation and capability expansion.
* `tests/test_automation_retrigger.cpp`, `tests/fixtures/retrigger_fixture.cpp`, `tests/test_automation_retrigger_daemon.py`, `CMakeLists.txt`: deterministic production-runtime/DLL tests and real daemon burst/reload scenario. Existing authoring/conformance tests replace the explicitly obsolete unsupported-policy assertions.

## Stack lifecycle

Each accepted stack decision constructs a fresh layer immediately, with its own Effect, generation, lifecycle callbacks, start timestamp, watchdog, opacity and instance sequence. Four active layers per stack rule and 32 active V2 layers globally are hard bounds. Persistent V2 layers count toward the same global bound. A full bound consumes and drops the newest admission; existing layers are not evicted and nothing is replayed.

Transient composition remains ascending `(priority, rule_order, instance_sequence)`. A later admission for the same rule therefore renders later. Retiring one layer leaves its siblings intact. Persistent/legacy/transient class ordering and rendering math are unchanged.

## Queue token and admission model

The host-owned token contains rule ID, semantic identity, a copied action, rule order, original monotonic admission time and `PreparedEffectSource`. It contains no Effect, framebuffer, telemetry state or condition evaluator.

The source is either the immutable canonical profile recipe string or a private `shared_ptr<const PluginEntry>` pin. Builtin/profile recipes reuse the existing profile constructor at actual start. Plugin-backed profile recipes are prepared as plugin sources, not deferred plugin-name lookups. Missing profile recipes or unavailable plugin generations consume the admission without enqueuing or retrying later.

`PluginManager::PrepareEffectGeneration` reuses the accepted registered generation by alias/canonical path. For a cold source it shadow-loads and validates exports/ABI without calling the factory or publishing a registry entry. The resulting unpublished generation is owned only by prepared sources/instances; there is no new permanent generation cache. `CreateFromGeneration` invokes the existing generation-bound instantiation path only when starting the token. Raw unowned callback pointers are not part of the prepared-source interface.

A queue rule has at most one active layer and four pending tokens. All rules together have at most 64 pending tokens. If there is no active layer, no older token, and active capacity is available, an admission starts immediately. Otherwise it appends FIFO. Capacity overflow drops the newest admission, never an older token. A pending token pins the identity accepted at admission, even across subsequent recipe or DLL publication.

## Timing, draining and failures

TTL is fixed at 2000 ms measured from Automation admission: age 2000 is eligible, 2001 expires. Cosmetic reload does not reset that time. All expired/cancelled tokens are removed before draining and new admission. Global active pressure retains a valid FIFO head while its TTL continues to run. Different rules can make progress independently; a rule cannot bypass its own head.

After an active layer retires, its successor can start on the next `Consume` frame. Pending work is bounded to four construction attempts globally and one per rule per frame. A failed construction consumes that token permanently; later tokens remain pending. Actual instance start establishes lifecycle/envelope/watchdog elapsed zero; waiting time does not advance any of them. Failure never installs a Replace layer or changes the lower frame.

Diagnostics reuse the existing set bounded to 64 messages until reset. Fixed-size saturating counters record capacity drops, expired tokens, unavailable sources, factory failures and cancelled tokens through `GetCounters`; these are host runtime diagnostics, not a new HTTP telemetry contract. A thrown immediate preparation/construction error is reported as unavailable; a failed pending construction is reported as a factory failure.

## Reconciliation and ownership

The runtime consumes Stage 4 `RuleStatus` only. It does not evaluate any ConditionNode. Deleted/disabled/semantically changed rules and False/Unknown continuing scope cancel every associated active stack/queue layer and pending token. Required-GSI freshness uses the existing authoritative continuation status. Unchanged/cosmetic reload preserves layers, FIFO and admission times, while updating rule order. Unscoped foreground changes and an event-time WHEN becoming false do not cancel accepted work.

Recipe/plugin publication leaves already accepted transient work and pending sources intact. Future admissions prepare the new recipe/generation. Existing restart, ignore-while-active and persistent replacement paths retain their accepted Stage 3/4 behavior; restart/ignore are not implemented through the queue.

Ownership is token -> prepared source -> PluginEntry -> PluginHandle/HMODULE. At start, the constructed TriggeredEffectInstance and its existing shared Effect deleter retain that same generation before the token is released. Lifecycle callbacks come from that instance's generation. The existing deleter contains DestroyEffect exceptions and retains the generation through destruction; it was not modified here. Expiry, cancellation, failed start and shutdown release token pins. There is no retained historical generation list. Cold generations may be independently prepared, but their ownership remains bounded by active/pending capacities.

Tests observe factory counters to prove pending preparation creates no Effect, use differing DLL markers/callback state to prove old/new generation separation, and verify weak-generation expiry, shadow deletion and absence of the old module handle after the final old owner retires. Separate tests cover pending expiry, scope cancellation and runtime clear releasing pins. Synthetic fixture DLLs are test targets only; release packaging inputs were not changed to include them.

## Authoring contract

The existing capabilities endpoint now returns:

```json
{
  "retrigger": ["restart", "ignore_while_active", "stack", "queue"],
  "stack": {"max_active_per_rule": 4},
  "queue": {"max_pending_per_rule": 4, "max_pending_global": 64, "pending_ttl_ms": 2000},
  "global": {"max_active_v2": 32}
}
```

The Stage 5A JSON authoring client remains sufficient. API tests validate/create/delete both policies, reject them for while_true, reject attempted TTL configuration and reject an actually unknown policy. Revision checks, migration, provenance, shadowing and Phase 4 CRUD contracts are unchanged.

## Executed validation

All results below are from Release binaries on Windows, without physical hardware.

| Check | Result |
|---|---|
| Full Release CMake build; final changed test target rebuild | Passed |
| Full `ctest --test-dir build/automation-v2-stage2 -C Release --output-on-failure` | **14/14**, 2.36 s |
| Stage 1 ABI/factory/lifetime suite, included above | **212 assertions** |
| Stage 2 conformance, included above | **255 assertions** |
| Stage 3 runtime/composition, included above | **115 assertions** |
| Stage 4 reconciliation/publication, included above | **83 assertions** |
| Stage 5A authoring, included above | **89 assertions** |
| New Stage 6 retrigger production-runtime suite, included above | **68 assertions** |
| Daemon/web entrypoints and Stage 2/3/4/5A/6 real-daemon suites | **32/32**, 19.311 s |
| Frontend `npm test` | **36 passed, 3 existing platform/toolchain skips, 0 failed** |
| Frontend `npm run build` | Passed, 6.59 s |
| `dotnet test tests/Aura.Tests/Aura.Tests.csproj --configuration Release` | **36/36**, 0 skipped |

The 14 CTest entries also include existing plugin runtime, frozen Stage 0 evidence, runtime status, Phase 4 profile/rule, native HID and legacy regressions. The Stage 2 assertion count changed only because the obsolete stack/queue rejection cases were replaced by an unknown-policy rejection; positive/invalid-context assertions now live in the authoritative authoring tests. No historical behavior assertions were weakened.

The Python command used `AURA_BIN_DIR=G:\Aura\build\automation-v2-stage2\Release`, `PYTHONPATH=tests`, and:

```text
python -m unittest test_runtime_entrypoints.TestDaemonEntrypoint test_runtime_entrypoints.TestWebUiEntrypoint test_automation_v2_daemon test_automation_effect_daemon test_automation_reload_daemon test_automation_authoring_daemon test_automation_retrigger_daemon -v
```

The new real daemon scenario records two independent kill packets creating two instances before the next render callback, successfully reloads the fixture DLL, observes old/new generations rendering concurrently, then verifies semantic config editing stops all of those instances. It runs with the real detector, decision loop, plugin manager and HTTP reload/config path in dry-run mode. Deterministic C++ tests separately cover queue bursts, FIFO, both TTL boundaries, global pressure, bounded failed starts, immutable profile recipes, exact DLL ownership and stale reconciliation without another packet.

Logs: `build/stage6-build.log`, `build/stage6-final-build.log`, `build/stage6-ctest.log`, `build/automation-v2-stage2/Testing/Temporary/LastTest.log`, `build/stage6-entrypoints.log`, `build/stage6-frontend-tests.log`, `build/stage6-frontend-build.log`, `build/stage6-dotnet.log`.

## Deviations and remaining scope

No concrete blocking defect in accepted Stages 0-5A was discovered. Pinning without construction required the explicitly authorized small prepared-source extension; existing ABI/publication behavior was not redesigned. The chosen four-attempt/one-per-rule pending drain budget makes the required bounded failure work explicit. No deviation from the Stage 6 contract is intended.

Native plugin hangs/process faults remain outside the accepted in-process ABI guarantees, as before. No physical-device behavior is claimed. Latch, arbitrary action queues, configurable queue limits, dedicated UI polish and post-audit work remain unimplemented. Stage 6 is ready for review; no later stage has begun.
