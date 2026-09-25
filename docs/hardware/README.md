# Hardware data and research

| Material | Status |
|---|---|
| [calibrated_keymap.json](../../calibrated_keymap.json) | Runtime mapping; retain at repository root |
| [Keymap report](calibrated_keymap.md) | Newer export retained; duplicate differed only in timestamp |
| [Native HID implementation](../../src/aura/native_hid_backend.cpp) | Current MI_01 transport and packet construction |
| [Native HID tests](../../tests/test_native_hid.cpp) | Automated protocol checks, not physical acceptance |
| [M605 verified runtime protocol](M605_RUNTIME_PROTOCOL.md) | Alpha.5 Phase 1 actuation/Static Analog and Phase 2 per-key RT/Deadzone, mapping and runtime apply boundary |
| [Light Bar mapping](lightbar_mapping.md) / [JSON](../../lightbar_mapping.json) | Recorded scan contains zero independently identified Light Bar LEDs; do not present it as a completed independent mapping |
| [Light Bar research](LIGHTBAR_REVERSE_ENGINEERING_REPORT.md) | Historical HAL/MI_01/MI_04 investigation; hypotheses and device-specific observations remain research evidence |
| [HAL compatibility](../../src/aura/hal_compat.cpp) / [Python helper](../../tools/py/aura_hal.py) | Retained legacy diagnostics and compatibility gates |
| [Early investigations](../archive/README.md) | HAL reverse engineering, device verification and disputed earlier hypotheses |

The current MI_01 path does not provide independently mapped Light Bar control. Preserve research results even when an experiment is no longer part of the default backend. See [tools](../../tools/README.md) and [manual acceptance](../testing/MANUAL_TESTS.md).
