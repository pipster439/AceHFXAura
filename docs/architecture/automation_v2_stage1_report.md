> HISTORICAL: Stage 1 evidence before Automation retirement. Frozen ABI evidence remains protected; legacy Automation config execution assertions have been retired.

# Automation v2 Stage 0 / Stage 1 acceptance report

Date: 2026-09-21. Approved baseline: `7b2e1c7887cf9c6f7e5a9601530b19b00282ed24`. Stage 0 and Stage 1 work is complete; stopped for Owner review before Stage 2. Changes are uncommitted in the working tree. No branch changes, commits or pushes were made.

## Scope and exact changes

- [Design baseline](automation_v2_design_spike.md) and [Owner summary](../../automation_v2_review_summary.md): marked approved for implementation; pinned explicit promotion from top-level `suppress_web_ui` to orchestration rule-level `dnd`. Preserve process, target and disclose ordering/precedence; unknown fields without semantic preservation block conversion. DND remains policy metadata, never an action. Future conversion fixtures cover both boolean values, omission and unknown fields.
- `include/engine/plugin_interface.h`: optional revision-1 lifecycle C function typedefs and explicit calling convention. `Effect`, `IGsiReader`, `EffectContext` layouts and virtual tables are unchanged; `AURA_PLUGIN_API_VERSION` remains 1. Studio generation/serialization is unchanged.
- `include/engine/plugin_manager.h`: normalized ABI metadata, immutable published generation metadata, optional lifecycle capabilities, generation-bound `TriggeredEffectInstance`, and `LoadPluginInstance` / `CreateEffectInstance`. Existing shared-pointer factory entry points remain available and delegate to these.
- `src/engine/plugin_manager.cpp`: validate ABI before invoking name/lifecycle/factory exports; prepare shadow generation before transactional publication; retain old generations on failed reload. A reload validates a create/destroy roundtrip before publication. Each effect deleter pins its exact generation through destruction; surviving weak pointers do not unnecessarily pin DLLs. Candidate modules remain loaded through exception-object destruction. Registry retirement/unloading occurs outside its mutex. Absolute canonical source identity and process-qualified shadow names support safe reload. All same-source aliases move together; exported-name changes retain previous aliases with diagnostics. Cross-source alias collisions reject the candidate without changing existing aliases; stale concurrent publications are rejected.
- `tests/test_plugin_abi.cpp`, `tests/fixtures/plugin_abi_fixture.cpp`: Windows DLL normalization, lifecycle metadata, ownership, alias, concurrent factory/reload and failure regressions. New synthetic DLLs build separately from frozen evidence.
- `tests/verify_stage0_fixtures.py`, `tests/fixtures/automation_v2_stage0/`: frozen binaries, hashes/provenance, representative configs and future conversion expectations. `tests/test_gsi_rules.cpp` exercises the captured configs through unchanged legacy parsing/evaluation.
- `CMakeLists.txt`: adds 19 synthetic DLL targets, the ABI test and fixture-integrity test, and supplies the frozen config path to the existing rule test. `.gitignore` permits only the specific frozen fixture DLL directory.

No Automation v2 execution, occurrence batches, ConditionNode semantics, overlay composition, authoring, retrigger scheduling, queue, stack, latch or Phase 4 API changes were implemented. The sidecar is ownership/factory substrate only; it does not execute lifecycle callbacks in production, apply opacity, enforce watchdogs or own an animation clock.

## ABI behavior matrix

| Version export | Result | Reload with a valid prior generation |
| --- | --- | --- |
| Absent (both recognized spellings absent) | Normalize to v1.0; diagnostic | Publish if factory/capability preparation succeeds |
| `1` | Normalize to v1.0 | Same |
| `0x00010000` | Normalize to v1.0 | Same; existing Studio DLL exercised |
| `0`, `2`, `0x00010001`, `0x00020000` | Reject, before name/lifecycle/factory invocation | Prior generation and aliases retained |
| Any other value | Reject by the same explicit whitelist | Prior generation retained |
| Version export throws / DLL cannot load / required factory export absent | Reject candidate | Prior generation retained |
| Factory throws or returns null; reload probe destroy throws | Reject candidate | Prior generation retained |

Existing unprefixed `GetPluginApiVersion`, `CreateEffect`, `DestroyEffect`, `GetEffectName` aliases remain supported. ABI version normalization does not assert compatibility across arbitrary C++ compilers, architectures or runtime-library choices.

Optional lifecycle signatures use `extern "C"` / `__cdecl`:

```cpp
uint32_t AuraGetEffectLifecycleVersion();
uint32_t AuraIsEffectFinished(const aura::Effect*, uint64_t elapsed_ms);
float AuraGetEffectOpacity(const aura::Effect*, uint64_t elapsed_ms);
```

| Optional exports | Effective metadata |
| --- | --- |
| Revision 1 + finished + opacity | Both callbacks bound to the creating generation |
| Revision 1 + finished only | Finished callback; future host opacity defaults to 1 |
| Opacity only, missing revision, or unknown revision | No effective callbacks; diagnostic; future legacy-envelope fallback |
| No lifecycle exports | Accepted ABI-v1 effect; no lifecycle capabilities |

Callbacks must be serial, nonblocking and nonthrowing. Later lifecycle work must validate result ranges and watchdog behavior; Stage 1 only probes capabilities and verifies pairing in tests. Native access violations/hangs are not contained by this substrate.

## Compatibility evidence actually exercised

Capture preceded all builds. [Manifest](../../tests/fixtures/automation_v2_stage0/manifest.json) records SHA-256, sources, timestamps and mutation details; [provenance](../../tests/fixtures/automation_v2_stage0/README.md) distinguishes pre-existing and synthetic evidence. Integrity is a prerequisite for the ABI CTest.

| Frozen sample | Actual exercise |
| --- | --- |
| `prebuilt_abi1.dll` | Unmodified pre-existing local DLL returning 1: load/create/render/destroy |
| `prebuilt_stress_abi1.dll` | Unmodified existing plugin returning 1: load/create/context render/destroy |
| `prebuilt_studio_packed1.dll` | Unmodified existing Studio DLL returning `0x00010000`: load/create/context render/destroy |
| `synthetic_absent_version.dll` | Recorded export-name mutation: load/create/render/destroy through absent-version fallback |
| `synthetic_unknown_abi.dll` | Recorded version-immediate mutation to `0x00020000`: rejection |
| Three config fixtures | Legacy rules/GSI bindings, orchestration/event overlays and Phase 4 DND parsed/evaluated by existing RuleEngine; 12 added assertions |
| Future promotion expectations | JSON/contract consistency verified; no conversion implementation or runtime acceptance claimed |

No historical DLL without a version export was found among the 36 inspected factory DLLs. Synthetic no-version coverage does **not** close that historical evidence gap. Original build commits of pre-existing local binaries are not independently proven. None was rebuilt and relabeled historical.

Additional freshly built synthetic DLLs cover optional capabilities, unprefixed aliases, unknown version spellings, malformed required exports, factory/version/destroy exceptions, alias changes/collisions and concurrent reload. Lifetime tests prove destruction before DLL unload, copied-out effect ownership, metadata pinning, exact old/new callback pairing and shadow cleanup.

## Validation results

Windows x64, MSVC 19.51 / Visual Studio 18, Release configuration, Python 3.13. Isolated build: `build/automation-v2-stage1`.

| Validation | Result |
| --- | --- |
| Full CTest | **9/9 passed**: plugin_runtime, stage0_fixture_integrity, plugin_abi, runtime_status, lighting_service, automation_service, gsi_rules, native_hid, aura_hal_py |
| New ABI/factory/lifetime test | **212 assertions passed** |
| Existing runtime entrypoint suite against Stage 1 binaries | **28/31 passed; 3 launcher failures**, no skips |
| Exact approved baseline, independently extracted/built, same complete entrypoint suite | **28/31 passed; same 3 failures**, no skips |

Both entrypoint runs pass all 15 daemon and 11 web tests, plus 2 launcher tests. The identical failing launcher tests are:

- `test_launcher_default_config_creation_when_template_available`
- `test_launcher_default_config_skip_existing_preserves_custom_file`
- `test_launcher_explicit_existing_config_loaded_without_cwd_pollution`

Each fails its daemon-log lookup assertion. Unchanged launcher code runs the official bundle under `%LOCALAPPDATA%/Aura`; these tests look for the log under the launch working directory (and default-config cases also expect working-directory behavior). Exact-baseline reproduction establishes these are not Stage 1 regressions. This report does not claim that every subsequent assertion in these failing tests passed. Launcher source and existing entrypoint tests were not changed to make this suite green.

Reproduction:

```powershell
cmake -S . -B build/automation-v2-stage1 -A x64
cmake --build build/automation-v2-stage1 --config Release --parallel 4
ctest --test-dir build/automation-v2-stage1 -C Release --output-on-failure
# From tests/, after producing the test bundle with the existing packaging helper:
$env:AURA_BIN_DIR='G:/Aura/build/automation-v2-stage1/Release'
python -m unittest test_runtime_entrypoints -v
```

The launcher test bundle uses the existing `build_single_exe` packaging helper with isolated source/staging directories and the newly built daemon/web executables. Full release packaging and runtime-cache cleanup were not run. The exact baseline was extracted under `build/automation-v2-stage1/baseline-source` using `git archive`; the current checkout/branch was not replaced.

Local logs (generated, not source-controlled): `build/automation-v2-stage1/ctest-stage1.log`, `Testing/Temporary/LastTest.log`, `runtime-entrypoints.log`, `runtime-entrypoints-baseline.log`, and the corresponding build logs. Existing compiler/deprecation and Python resource warnings occurred; this is not a warning-free-build claim. **NOT VERIFIED ON REAL HARDWARE** or live CS2.

## Review clarification: destruction, ownership, publication and packaging

These findings describe the current review patch; no production behavior was changed to prepare the artifact.

### 1. Normal shared_ptr destruction and plugin exceptions

Yes: a misbehaving plugin's `DestroyEffect` export can throw during normal production destruction, not just reload validation. Both legacy and sidecar factory APIs use the same deleter in `PluginManager::Instantiate`. Its exact containment is:

```cpp
auto effect = std::shared_ptr<Effect>(raw, [owner = generation, destruction_failed](Effect* p) mutable noexcept {
    try {
        owner->destroy_fn(p);
    } catch (...) {
        if (destruction_failed) *destruction_failed = true;
        // A shared_ptr deleter must never propagate, including when
        // formatting or writing the diagnostic itself fails.
        try {
            LOG_ERROR("[PluginManager] Destroy export threw for " << owner->effect_name);
        } catch (...) {
        }
    }
    // Release after DestroyEffect returns, even if weak_ptr<Effect> survives.
    owner.reset();
});
```

For normal production destruction `destruction_failed` is empty: the exception is caught, logging is attempted inside a second catch-all, and no plugin C++ exception escapes the deleter. For the reload probe, the optional flag additionally prevents publication. The generation remains pinned until after the catch handler and exception object are destroyed. The host does not retry deletion or assume the plugin freed every allocation before throwing; a broken plugin can leak. This does not contain native faults, explicit termination, or a throw from a plugin function/destructor declared `noexcept` (which terminates inside the plugin before a host catch can run). The throwing-destroy regression exercises the common deleter via the reload probe; it is not a separate dedicated normal-destruction test.

### 2. Exact ownership chain and its API boundary

`PluginEntry` is the Stage 1 implementation's generation object (there is no separate class named `PluginGeneration`). The owning chains are:

```text
registry alias -> shared_ptr<const PluginEntry>
TriggeredEffectInstance.generation_ -> shared_ptr<const PluginEntry>
TriggeredEffectInstance.effect_ -> shared_ptr<Effect>
                                  -> deleter.owner -> shared_ptr<const PluginEntry>
PluginEntry -> lifecycle function pointers
PluginEntry.handle -> shared_ptr<PluginHandle> -> HMODULE + shadow DLL path
```

`PluginHandle` unloads with `FreeLibrary` only when its last owner is released. A sidecar destroys its effect before its generation member; a copied-out effect independently pins the same generation in its deleter. A retained generation pointer alone also pins the DLL and its callback addresses. Reload replaces registry references, not existing objects' owners or callbacks.

The guarantee applies while callers retain the sidecar or owning generation pointer. Raw callback pointers are exposed metadata and are copyable: C++ cannot prevent a caller from extracting a bare pointer, dropping every owner, then calling it. Such use is outside this API's lifetime contract. Likewise the API does not type-enforce matching an arbitrary Effect to a callback. Tests validate the supported generation-bound usage. Future lifecycle consumers must retain the generation/sidecar through each callback and use its paired Effect; the current API is not an owning callable wrapper. It would be inaccurate to claim unconditional safety for arbitrarily detached raw pointers.

### 3. No partial registry publication

`ReloadPlugin` prepares/validates a candidate and completes its create/destroy probe before calling `PublishGeneration`. Every failure up to that point returns without writing `plugins_`.

`PublishGeneration` holds `mutex_` while collecting the full alias set, checking `current != expected_previous`, and checking all cross-source alias collisions. Either rejection returns before registry mutation. It then copies `plugins_` into local `next`, applies every alias to that local map, and performs the sole publication operation `plugins_.swap(next)`. Allocation failures before the swap leave the original map intact. Registry readers use the same mutex; they see the old or the complete new map. Retired references are released outside the lock. Candidate export side effects are not rolled back, but they are not partial registry writes. Existing tests cover failed reload, alias collision preservation and concurrent factory/reload consistency; stale-candidate rejection follows the explicit under-lock comparison, not a claim of a separately forced stale-race fixture.

### 4. Discovery versus execution

Production `src/` only obtains `AuraIsEffectFinished` / `AuraGetEffectOpacity` addresses with `GetProcAddress`, negotiates capability metadata and stores them. Neither callback is invoked by the production render path or reload probe in Stage 1. `AuraGetEffectLifecycleVersion()` **is** called during loading to negotiate the optional revision. Finished/opacity invocations currently occur in `tests/test_plugin_abi.cpp` to validate generation pairing; this is not production lifecycle execution.

### 5. Synthetic fixtures and release packaging

The synthetic `abi_*` DLLs are CMake test dependencies of `test_plugin_abi`; an unqualified full build can produce them in the build output directory. They are not dependencies of `aura_daemon` or `aura_web_ui`. `tools/package_release.py::ensure_binaries` builds those two production targets explicitly. `build_single_exe` embeds an explicit list of daemon/web/config/keymap/web/SDK files, with no fixture DLL input. `build_portable_zip` likewise writes explicit release files, not a wildcard of the Release directory. Consequently neither newly built synthetic DLLs nor frozen Stage 0 DLLs enter the repository's release packaging pipeline. This is verified from the current target graph and packaging source, not a claim that a fresh full release archive was generated for this review.

## Stage 2 disposition

No implementation-scope expansion. Factory/destroy preflight is an intentional reload validation step; constructors must tolerate the extra probe instance. Unknown ABI rejection and cross-source alias collision rejection deliberately tighten prior permissive behavior, as required by the approved design. Missing/partial lifecycle capabilities do not break old DLL loading.

Stage 2 must not begin in this task. No failing Stage 1 substrate regression remains, but Owner review should explicitly dispose of these acceptance limits before proceeding:

1. The full existing entrypoint suite is not green. Its three failures reproduce on the approved baseline; align launcher tests in a separate scoped change or explicitly accept the baseline exception.
2. Historical unversioned DLL evidence is unavailable. Synthetic compatibility tests pass; obtain an authentic sample if historical acceptance is a release gate.
3. Accept the generation/factory substrate and its reload probe/alias policy before building decision execution on it. Later stages still owe callback result validation, watchdogs, coherent input snapshots, freshness and composition acceptance; none is established by these tests.
