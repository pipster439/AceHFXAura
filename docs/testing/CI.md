# Continuous integration

GitHub Actions runs on pushes, pull requests, and manual dispatches.

- **Windows:** checks that local configuration and generated plugin sources are absent from Git, builds Release daemon/WebUI and native tests, runs CTest (including real plugin DLL loading/reloading), and WebUI/dry-run daemon entrypoint tests. It also runs .NET client tests and builds WinUI x64 Release. Windows installs Node/frontend dependencies before CMake so Studio publication/reconciliation fixtures are registered, then builds the default Release target including all test executables and fixture DLLs. It also runs the five Automation v2 daemon integration modules (evaluation, authoring, effect, reload/publication, retrigger).
- **Frontend:** installs the lockfile dependencies with `npm ci`, runs the Node test suite, and builds the single-page UI.

The Windows job builds its own binaries and passes that exact directory through `AURA_BIN_DIR`. Runtime tests use temporary configuration and application-data directories; daemon lifecycle tests use `--dry-run`.

Windows CI also verifies GSI cfg contracts, publishes the default self-contained WinUI ZIP, checks its file manifest/version resources, and runs the packaged native runtime outside checkout assets. `Aura.exe` is the GUI; the legacy launcher is only available with `--legacy`. DesktopSmoke creates a real WinUI window and is excluded from hosted CI; run it locally against the final package with `AURA_PACKAGE_DIR`. Physical lighting and visual desktop acceptance remain manual. See [packaging](../development/PACKAGING.md).

The historical three-entrypoint command additionally requires an explicitly built legacy launcher (not the alpha.4 GUI):

```powershell
python -B tests/test_runtime_entrypoints.py
```

The frontend build writes `web/index.html`. CI builds only in its disposable checkout and does not publish or commit generated output.

Legacy Python replica/model experiments are excluded from CI. Their passing counts do not measure production behavior. See [TESTING.md](TESTING.md) for the distinction and remaining limitations.
