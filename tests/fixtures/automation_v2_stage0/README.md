> HISTORICAL: Automation configuration snapshots here are pre-retirement migration/rejection evidence, not supported runtime inputs. Frozen plugin binaries remain protected; the ABI integrity test verifies binaries independently.

# Stage 0 compatibility evidence

Captured before any Stage 1 build, from approved HEAD `7b2e1c7887cf9c6f7e5a9601530b19b00282ed24`. [manifest.json](manifest.json) records exact bytes, SHA-256, source paths, timestamps, categories and the bounded inventory. CMake must never rebuild or overwrite `binaries/`.

| File | Evidence category |
| --- | --- |
| binaries/prebuilt_abi1.dll | Unmodified pre-existing build/Release/plugin_fixture_v1.dll, exported raw version 1. This existed before this task; its original build commit is not independently proven. |
| binaries/prebuilt_stress_abi1.dll | Unmodified pre-existing plugins/effect_stress_test.dll, raw version 1. |
| binaries/prebuilt_studio_packed1.dll | Unmodified existing Studio-generated effect_studio_cs2_health_d6c40fe81cb54d87a84a.dll, raw version 0x00010000. |
| binaries/synthetic_unknown_abi.dll | Synthetic byte mutation of prebuilt_abi1.dll: changes only the version return immediate to 0x00020000. Not historical evidence. Patch offset/before/after bytes recorded in manifest. |
| binaries/synthetic_absent_version.dll | Synthetic export-name mutation of prebuilt_abi1.dll: AuraGetPluginApiVersion renamed to ZuraGetPluginApiVersion (export sort order retained). Neither recognized version export remains. Not historical evidence. |

No historical no-version-export plugin was available among 36 factory DLLs inspected under repository `plugins`, `build`, and `dist/plugins`. No claim of historical unversioned binary compatibility acceptance can be made from the synthetic sample. Existing prebuilt files were copied, never rebuilt and relabeled historical. Modern generated test DLLs belong in the build directory and are separate regression fixtures.

Config captures:

- configs/legacy_rules_gsi_bindings.json: byte copy of existing tests/fixtures/test_config.json.
- configs/legacy_orchestration_event_overlays.json: exact raw JSON literal extracted from baseline test_gsi_rules.cpp test 25; includes AST, dnd and legacy event_overlays. It intentionally references a nonexistent plugin to exercise the existing silent-placeholder behavior.
- configs/phase4_dnd_application_rules.json: explicitly derived from baseline test_automation_service.cpp kSampleConfig, with suppress_web_ui true/false added for acceptance coverage. This is a representative test config, not a captured user configuration. Its unknown custom_extra_tag must block later conversion absent a semantic mapping.
- promotion_expectations.json: future conversion contracts only; no Stage 1 authoring or conversion implementation is implied.

The binaries are project test/Studio effects, not ASUS HAL binaries. Exercise via the real PluginManager in an isolated temporary working directory, never overwrite installed user plugins/configs. Verify hashes before testing. Native plugin tests do not establish real-hardware lighting acceptance or isolation from malicious DLLs.
