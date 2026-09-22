# Post-audit remediation: SEC-01 and SEC-02

Baseline: `cac205f` (accepted Automation v2 Stage 6). This patch fixes only the two accepted findings. It adds no Automation stage or feature, does not redesign PluginManager, and does not change process canonicalization. Existing untracked audit reports/reproduction artifacts were left untouched and are not included in this remediation patch.

## SEC-01: coherent admission context

`RuleEngine::EvaluateAutomation` now captures foreground once and selects one admission timestamp immediately after draining the input buffer, before constructing any packet/current snapshot. In production the monotonic clock is called once; tests supplying an explicit timestamp reuse that timestamp without reading the clock. All drained packet snapshots and the current reconciliation snapshot use those same immutable process/time values.

Each packet still supplies its own original immutable telemetry, occurrence list, receipt timestamp, source epoch and packet sequence. Packet order, the 3000 ms freshness boundary and the legacy evaluators are unchanged. Foreground means Automation admission time, not historical packet occurrence time.

The deterministic regression supplies a provider returning `cs2.exe` first and `desktop.exe` on subsequent calls. Two queued kill occurrences are admitted, the current profile selection and continuing scope remain `cs2.exe`, and the provider is called exactly once. A separate subsequent invocation with foreground `desktop.exe` rejects its occurrence, preserving admission-time semantics. The pre-existing capture-count assertion was updated from per-packet/current sampling to the explicitly requested one-per-invocation contract. The existing simulated telemetry-update-during-capture test remains passing.

## SEC-02: synchronous retirement after unlocking

`AutomationEffectRuntime::Apply`, `Consume` and `Clear` now declare local retirement ownership before the lock guard. Reverse local destruction order guarantees the lock guard unlocks first, including early return and exception unwinding.

* Individual retired layers/tokens are moved to local storage before erasing their moved-from container elements. Their shared generation/Effect owners move with them; vector/deque compaction cannot perform final release of the retired object.
* Whole-runtime clearing swaps layer/token containers into retirement storage under the lock. This includes the existing conservative non-authoritative rebuild path.
* Replacement constructs the new layer successfully, moves the old layer into retirement storage, then installs the replacement.
* Dequeued tokens remain in local retirement storage through their construction attempt, including failed attempts.
* Prepared sources and newly constructed instances have local ownership storage declared before the lock. Temporary layer initialization/insertion and failure handling therefore cannot drop the last returned source/instance owner under the runtime mutex. Queue insertion retains a source copy until unlock as well.
* The runtime destructor calls `Clear` while the mutex is still alive, so shutdown follows the same retirement discipline.

No reference into a changed runtime container escapes the locked section. References used while constructing queued work point to stable local deque storage. The original generation-bound Effect deleter, exception containment, callback ownership, PluginManager registry and PluginHandle implementation remain unchanged.

Container order, queue FIFO, counters, active/pending bounds, TTL, reconciliation status, envelopes, watchdogs and rendering order remain unchanged. Retired objects are no longer active in the containers before unlocking; their synchronous destruction completes before the runtime call returns. This changes the lock boundary, not the execution thread.

## Regression instrumentation and ownership evidence

The Stage 6 production-runtime test now wraps a real fixture PluginEntry with test-only final-deletion instrumentation and wraps its destroy function. Both Effect destruction and final generation deletion synchronously call `ActiveCount` and `PendingCount`, which acquire the same non-recursive runtime mutex. A retirement holding that mutex deadlocks the regression and fails the existing bounded CTest timeout. No production test hook or PluginManager change was added.

The observer records `GetCurrentThreadId` against the initiating caller. The final full-suite run recorded caller thread **72016**; every observed destruction matched that thread. No background worker was created. DLL unload and shadow-file removal remain synchronous on the calling/render thread: moving filesystem cleanup away from that thread is separate performance debt, intentionally not addressed here.

Coverage includes:

* Apply completion: first stack sibling retires without unloading the remaining sibling's generation, then the final sibling releases it.
* Consume scope cancellation, semantic reconciliation and conservative rebuild.
* Restart replacement, explicit Clear and runtime destructor/shutdown.
* Pending-only generation expiry, Unknown-scope cancellation, Clear and failed head construction.
* For final release: generation weak pointer expires, the shadow path disappears and `GetModuleHandleW` no longer finds that module before the API call has returned.

The old stack/queue tests remain intact, including FIFO, factory-attempt bounds, reload generation identity, recipe capture, capacity, TTL and stale cancellation. The new observation checks exercise real production runtime and DLL ownership, not a model of the implementation.

## SEC-03: unchanged LOW policy question

Whether dotted extensionless user input such as `Game.Shipping` should be accepted as shorthand for `Game.Shipping.exe` remains an unresolved LOW authoring policy question. No canonicalization code or acceptance policy was modified.

## Verification

All checks used the existing Release build at `build/automation-v2-stage2`, on Windows without physical hardware.

| Executed check | Final result |
|---|---|
| Full Release CMake build, incremental final build and changed snapshot-test rebuild | Passed |
| `ctest --test-dir build/automation-v2-stage2 -C Release --output-on-failure` | **14/14**, 2.44 s |
| Stage 1 ABI/factory/lifetime, included in CTest | **212 assertions** |
| Stage 2 conformance, included in CTest | **262 assertions** |
| Stage 3 lifecycle/composition, included in CTest | **115 assertions** |
| Stage 4 reconciliation/publication, included in CTest | **83 assertions** |
| Stage 5A authoring, included in CTest | **89 assertions** |
| Stage 6 retrigger/retirement, included in CTest | **96 assertions** |
| Daemon/web entrypoints plus Stage 2-6 daemon integration suites | **32/32**, 20.042 s |
| Frontend `npm test` | **36 passed, 3 existing toolchain/platform skips, 0 failed**, 1.116 s |
| Frontend `npm run build` | Passed, 3.21 s |
| `dotnet test tests/Aura.Tests/Aura.Tests.csproj --configuration Release` | **36/36**, 0 skipped, 64 ms reported test duration |

The Python integration invocation used `AURA_BIN_DIR=G:\Aura\build\automation-v2-stage2\Release`, `PYTHONPATH=tests`:

```text
python -m unittest test_runtime_entrypoints.TestDaemonEntrypoint test_runtime_entrypoints.TestWebUiEntrypoint test_automation_v2_daemon test_automation_effect_daemon test_automation_reload_daemon test_automation_authoring_daemon test_automation_retrigger_daemon -v
```

The first full test run exposed an indexing mistake in the new test: profile decisions were included when selecting the first two event decisions. The test was corrected to filter admitted decisions for its rule ID, then the full CTest suite passed. No production semantics or historical assertion was changed to accommodate that test mistake.

Logs: `build/post-audit-build.log`, `build/post-audit-final-build.log`, `build/post-audit-snapshot-build.log`, `build/post-audit-ctest.log`, `build/automation-v2-stage2/Testing/Temporary/LastTest.log`, `build/post-audit-entrypoints.log`, `build/post-audit-frontend-tests.log`, `build/post-audit-frontend-build.log`, `build/post-audit-dotnet.log`.

Deliverables are `post_audit_remediation.patch` (complete staged binary diff relative to `cac205f`) and `build/post-audit-git-status.txt`. Work stops after these two fixes. No commit or push was performed in this pass.
