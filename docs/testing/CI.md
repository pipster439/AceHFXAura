# Continuous integration

GitHub Actions runs on pushes, pull requests, and manual dispatches.

- **Windows:** checks that local configuration and generated plugin sources are absent from Git, builds Release daemon/WebUI and native tests, runs CTest (including real plugin DLL loading/reloading), and WebUI/dry-run daemon entrypoint tests. It also runs .NET client tests and builds WinUI x64 Release. Windows installs Node/frontend dependencies before CMake so Studio publication/reconciliation fixtures are registered, then builds the default Release target including all test executables and fixture DLLs. It also runs the five Automation v2 daemon integration modules (evaluation, authoring, effect, reload/publication, retrigger).
- **Frontend:** installs the lockfile dependencies with `npm ci`, runs the Node test suite, and builds the single-page UI.

The Windows job builds its own binaries and passes that exact directory through `AURA_BIN_DIR`. Runtime tests use temporary configuration and application-data directories; daemon lifecycle tests use `--dry-run`.

The legacy single-file `Aura.exe` launcher does not embed an ASUS driver and does not package WinUI. Hosted CI does not run packaging or launcher acceptance. See [packaging limits](../development/PACKAGING.md). Physical lighting remains manual.

Run all three entrypoint suites locally after building the daemon/WebUI and packaging a current launcher:

```powershell
python -B tests/test_runtime_entrypoints.py
```

The frontend build writes `web/index.html`. CI builds only in its disposable checkout and does not publish or commit generated output.

Legacy Python replica/model experiments are excluded from CI. Their passing counts do not measure production behavior. See [TESTING.md](TESTING.md) for the distinction and remaining limitations.
