# Developer and research tools

- `package_release.py`: official self-contained WinUI x64 ZIP packaging; root wrappers invoke the same entrypoint. Only explicit `--legacy` selects the historical C++ launcher. See [packaging](../docs/development/PACKAGING.md).
- `gui_calibrator.py` / `run_gui_calibrator.bat`: interactive hardware calibration. Uses shared `py/aura_hal.py` and can open `lightbar_probe.py`.
- `lightbar_probe.py` / `run_lightbar_probe.bat`: retained HAL-era Light Bar research, with GUI and diagnostic CLI. These can interact with real hardware/services; they are not unattended CI tests.
- `set_per_key.py`: manual per-key HAL diagnostic.
- `e2e_key_test.py`: interactive key-input/daemon diagnostic; can manipulate foreground focus and generate inputs.
- `py/aura_hal.py`: shared legacy HAL library used by retained probes and Python tests; not the daemon's default Native HID backend.

Default Markdown exports go to `docs/hardware/`; JSON mapping data stays at the root. Explicit custom Light Bar export paths still write Markdown beside the requested JSON. Physical probe logs are local ignored files, not automatic acceptance records.
