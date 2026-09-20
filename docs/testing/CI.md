# Continuous integration

GitHub Actions runs on pushes, pull requests, and manual dispatches.

- **Windows:** checks that local configuration and generated plugin sources are absent from Git, builds Release daemon/WebUI and native tests, runs CTest (including real plugin DLL loading/reloading), and WebUI/dry-run daemon entrypoint tests. It also runs .NET client tests and builds WinUI x64 Release. The explicit native build list includes `test_lighting_service`, which is registered with CTest.
- **Frontend:** installs the lockfile dependencies with `npm ci`, runs the Node test suite, and builds the single-page UI.

The Windows job builds its own binaries and passes that exact directory through `AURA_BIN_DIR`. Runtime tests use temporary configuration and application-data directories; daemon lifecycle tests use `--dry-run`.

The legacy single-file `Aura.exe` launcher does not embed an ASUS driver and does not package WinUI. Hosted CI does not run packaging or launcher acceptance. See [packaging limits](../development/PACKAGING.md). Physical lighting remains manual.

Run all three entrypoint suites locally after building the daemon/WebUI and packaging a current launcher:

```powershell
python -B tests/test_runtime_entrypoints.py
```

The frontend build writes `web/index.html`. CI builds only in its disposable checkout and does not publish or commit generated output.

Legacy Python replica/model experiments are excluded from CI. Their passing counts do not measure production behavior. See [TESTING.md](TESTING.md) for the distinction and remaining limitations.
