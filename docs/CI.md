# Continuous integration

GitHub Actions runs on pushes, pull requests, and manual dispatches.

- **Windows:** checks that local configuration and generated plugin sources are absent from Git, builds Release daemon/WebUI and native tests, runs CTest, Python effect tests, and WebUI/dry-run daemon entrypoint tests.
- **Frontend:** installs the lockfile dependencies with `npm ci`, runs the Node test suite, and builds the single-page UI.

The Windows job builds its own binaries and passes that exact directory through `AURA_BIN_DIR`. Runtime tests use temporary configuration and application-data directories; daemon lifecycle tests use `--dry-run`.

The single-file `Aura.exe` launcher embeds an ASUS driver which is not in the repository. Packaging and launcher entrypoint tests require the local driver and are not run on hosted CI. CI does not validate physical lighting or driver behavior.

Run all three entrypoint suites locally after building the daemon/WebUI and packaging a current launcher:

```powershell
python -B tests/test_runtime_entrypoints.py
```

The frontend build writes `web/index.html`. CI builds only in its disposable checkout and does not publish or commit generated output.
