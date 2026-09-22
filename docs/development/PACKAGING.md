# Packaging status: legacy launcher

`package_release.bat` and `package_release.ps1` both delegate to [tools/package_release.py](../../tools/package_release.py). They are shell conveniences, not separate packaging implementations.

The pipeline builds the C++ daemon and Web server, embeds runtime assets and plugin SDK headers into the legacy `Aura.exe` launcher, and emits a portable ZIP. It does **not** build or package the WinUI client. Public packages must not contain ASUS proprietary DLLs. Native HID does not require them; explicit legacy compatibility or auto fallback uses installed, gated ASUS components.

This cleanup retains the pipeline because launcher/runtime tests and existing release workflows still depend on it. It does not certify a WinUI release or redesign installation.

Known follow-up work before shipping WinUI:

- Define WinUI distribution, runtime dependencies and daemon/Web/SDK asset layout; validate source, portable and installed startup.
- Separate build output from user runtime-cache maintenance. The existing packaging script deletes `%LOCALAPPDATA%/Aura/runtime`; `--clean` clears the build directory, while a mismatched generator is refused; do not run it against an active user runtime merely to validate documentation.
- Validate packaged Studio publishing, upgrade/migration and graceful exit on actual Windows hardware.

Use the [release checklist](RELEASE_CHECKLIST.md). Automated CI does not publish packages or prove keyboard behavior.

For isolated candidate validation, set `LOCALAPPDATA` to an empty validation directory before invoking the canonical script, so packaging cannot clear a user runtime. Inspect ZIP entries and extracted embedded resources; run the launcher with `--dry-run` outside the checkout. Portable configuration lives beside Aura.exe; standalone configuration lives in `%LOCALAPPDATA%/Aura/config.json`.
