# WinUI development launch

```powershell
pwsh ./tools/dev/run-winui.ps1 -IsolatedData
pwsh ./tools/dev/run-winui.ps1 -IsolatedData -Restart
pwsh ./tools/dev/run-winui.ps1 -Configuration Release -IsolatedData
```

Defaults: Debug, x64, incremental build. The script configures `build/dev-winui`, builds only
`aura_daemon` and `aura_web_ui`, builds WinUI and launches the evaluated MSBuild output.
It does not run CI, rebuild frontend assets, publish, register Appx packages, create ZIP/MSIX
files, change versions or write `dist/`. Existing checked-in `web/index.html` is used by Studio;
frontend source changes need the usual frontend build separately.

The current production project is already **unpackaged**, self-contained
(`WindowsPackageType=None`, `EnableMsixTooling=false`). `Package.appxmanifest` is an inactive
template. Direct executable activation matches the release directory deployment, including
its unpackaged AppInstance key, PRI/XBF resources and daemon lifecycle. No development
package identity or registration is needed. `dotnet run` can build/start this unpackaged
project, but alone does not build/provide the native sidecars or select the development
runtime layout; use this entrypoint for a complete launch. Packaged activation/AUMID and
package-local storage are not part of the current release contract either.

## Data and ownership

`-IsolatedData` uses persistent, ignored `build/dev-winui/data` via `AURA_DATA_ROOT`.
It never copies real configuration or Profiles. Without this switch, an existing
`AURA_DATA_ROOT` is respected, otherwise normal `%LOCALAPPDATA%/Aura` is used: edits there
affect normal user data, and enabled automation follows existing product behavior.
Use isolated data for safe UI iteration. Fresh isolated settings are seeded by the existing
runtime preparer from the repository template, not by a second configuration writer.

`AURA_DEV_ROOT` and `AURA_DEV_BIN` select this checkout and this native build.
Environment overrides are restored after spawning the app; its daemon inherits them.
The launcher checks Aura processes, daemon processes and port 19897 before building.
External processes must be exited normally by their owner. `-Restart` only requests normal
window close for a recorded development UI with matching PID, executable and start time,
and verifies daemon parent, executable and runtime instance. If close minimizes to tray,
choose **Exit** from the Aura tray menu and rerun. No force kill or quarantine action exists.

`build/dev-winui/launch.json` contains local ownership evidence and the last launch timing.
It is not a portable diagnostic export. A successful launch requires the health endpoint to
identify the actual child at the expected development binary path.

## Options

| Option | Behavior |
|---|---|
| `-Configuration Debug\|Release` | Debug default; Release for local smoke, not release evidence |
| `-Platform x64` | Current supported launcher platform |
| `-NoBuild` | Reuse existing outputs; no promise they include subsequent source edits |
| `-NoLaunch` | Build and resolve outputs without launching |
| `-Restart` | Request normal shutdown of proven owned development UI before rebuilding |
| `-IsolatedData` | Separate persistent development data |
| `-Verbose` | Show commands via PowerShell common parameter |

No clean option is provided. Build/bin/obj and data are never deleted. An already running
app must exit before rebuild to avoid executable locks or single-instance redirection.
No watcher is added: debugger-driven XAML Hot Reload remains a separate IDE workflow.
CI environments may build with `-NoLaunch` but are prohibited from launching the app.
Launcher orchestration tests use fake processes and commands:
`python -B -m unittest discover -s tests -p test_winui_dev_launcher.py -v`.

## Validation and release

Full software validation: `pwsh ./tools/ci/run-ci.ps1` (see [CI](CI.md)).
Release: existing [packaging workflow](PACKAGING.md). Development launch skips release
inventory, hashes, archive assembly and release smoke. It does not establish hardware PASS.
