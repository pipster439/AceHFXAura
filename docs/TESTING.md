# Test evidence and limitations

Report each suite separately. A CTest entry can contain many checks; a Python method can contain none that exercise production code. Do not add these numbers together as feature coverage.

## Implementation-backed regression checks

- `test_plugin_runtime` links the production `src/engine/plugin_manager.cpp`. CMake builds two real fixture DLLs. The test calls the ABI export (the current contract is integer **1**, not `0x00010000`), loads through `PluginManager`, checks rendered bytes, overwrites the original DLL while an old instance lives, reloads v2, verifies that the old instance still renders v1, rejects a corrupt replacement while retaining v2, and checks shadow-file cleanup. Checks remain enabled in Release builds.
- `test_gsi_rules` links production rule/effect/GSI/WebServer code. CTest runs it with controlled fixtures.
- `test_runtime_entrypoints.py` starts actual EXEs with isolated runtime directories. Daemon lifecycle cases use dry-run. Existing instances/port conflicts may cause skips, which must be reported separately.
- `frontend/tests` exercises production JavaScript and includes native harnesses when a compiler is available. Report skips, not a blanket pass count.
- `aura_hal_py` contains Python helper checks. It is not evidence of physical lighting correctness.

## Retired misleading suites

The former `test_e2e_11_effects.py` and `test_e2e_blockly_system.py` primarily tested their own Python replicas or platform primitives. They are retained under `tests/reference_models/` as explicitly non-production experiments, excluded from default discovery and CI. Their old claims of 135/168 end-to-end checks are withdrawn. Known tautologies for ABI, context fields, path reassignment, mock engine swaps, and file operations have been removed. The remaining model experiments have not been certified as meaningful regression tests.

`test_scratch_blocks_and_library.py` was removed: its three cases checked locally manipulated dictionaries, a hardcoded C++ string, and a local list comprehension. Real frontend tests are the appropriate replacement for those claims.

## Running the plugin regression

```powershell
cmake -S . -B build
cmake --build build --config Release --target test_plugin_runtime
ctest --test-dir build -C Release -R plugin_runtime --output-on-failure
```

The fixture DLLs validate the plugin manager contract, not the Studio code generator or all plugin ABI compatibility policies. Physical keyboard behavior is outside CI. Do not claim those behaviors are covered by this test.

## Verification of the test itself (2026-09-16)

The production implementation passed `plugin_runtime`. In an isolated copy under the ignored build directory, `PluginManager::ReloadPlugin` was deliberately changed to return `true` immediately, without loading or publishing a new DLL. The same regression executable then exited with code 1 and `DLL rendered the wrong version`. The repository's production source was not modified. This demonstrates detection of a no-op reload; it is not a comprehensive mutation-coverage result.
