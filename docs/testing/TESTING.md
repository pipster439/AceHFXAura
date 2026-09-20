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

## Current validation matrix

Run from the repository root unless specified:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python -B -m unittest discover -s tests -p "test_*.py" -v
dotnet test tests/Aura.Tests/Aura.Tests.csproj -c Release
dotnet build winui/Aura.WinUI.csproj -c Release -p:Platform=x64
cd frontend
npm ci
npm test
npm run build
```

Python discovery includes release/launcher checks that require a freshly packaged legacy `dist/Aura.exe`; absent or stale artifacts are not cleanup regressions and must be reported. Do not run packaging merely to satisfy these checks without accounting for its runtime-cache side effects. The CI-safe subset is `test_aura_hal`, `test_lightbar_probe`, `test_gsi_dictionary_blocks`, plus isolated Web/daemon entrypoint classes. Tests may skip daemon lifecycle cases when a real instance occupies the shared mutex or ports.

CTest registers `plugin_runtime`, `runtime_status`, `lighting_service`, `gsi_rules`, `native_hid`, and (when Python is found) `aura_hal_py`. Lighting tests exercise production service validation and parameter schemas; .NET tests exercise the production client. Report their results separately.

`test_com.cpp` and `test_diag_hook.cpp` remain CMake-built manual diagnostics, not CTest tests. The latter is interactive. `test_cs2_gsi.py` sends to live ports 19897/19898 and is an opt-in integration tool, not isolated CI. Do not run live probes or hardware calibration against a user's session as an automated check.

[Historical M1/M5 harnesses](../../tests/archive/README.md) are retained outside default discovery. `tests/reference_models/` remains non-production evidence. Neither is included in current coverage claims.
