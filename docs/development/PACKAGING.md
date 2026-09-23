# alpha.4 WinUI packaging

`package_release.bat` and `package_release.ps1` forward to `tools/package_release.py`. The default pipeline produces an unpackaged, self-contained **Windows 11 x64 WinUI ZIP**. `Aura.exe` is the GUI, never a daemon candidate. `--legacy` retains the historical C++ launcher under `dist/legacy/` for engineering use only.

## Build and output

```powershell
python tools/package_release.py
# Reuse a native build already compiled/tested with the same VERSION:
python tools/package_release.py --build-dir build --skip-build
# Verify an extracted candidate:
python tools/package_winui.py --verify dist/Aura-v<VERSION>-windows-x64-<build-id>
```

The default rebuilds frontend assets, builds the native sidecars, publishes WinUI, verifies required files/roles/hashes/x64 PE architecture, then emits a versioned directory, ZIP and `.sha256`. `--skip-build` skips only native compilation. `--skip-zip` retains the verified directory. There is no release publishing, local deployment, config migration or user-runtime deletion. Do not pass `--clean` to the new pipeline. A stale native build is detected by the packaged runtime version test, so use `--skip-build` only after rebuilding.

Build prerequisites: VS 2022/2026 C++ desktop workload + Windows SDK, CMake, Python, Node/npm, .NET 10 SDK. The installed Visual Studio supplies app-local x64 VC CRT redistributables. No ASUS proprietary binaries may enter the package.

```text
Aura-v<VERSION>-windows-x64-<build-id>/
  Aura.exe / Aura.dll / Aura.deps.json / Aura.runtimeconfig.json
  Aura.pri / App.xbf / MainWindow.xbf / Pages/*.xbf / Assets/
  .NET and Windows App SDK self-contained dependencies
  runtime-payload/
    runtime-manifest.json
    aura_daemon.exe
    aura_web_ui.exe
    config.example.json
    calibrated_keymap.json
    web/index.html
    include/engine/{effect.h,plugin_interface.h}
    include/aura/{aura_types.h,keymap.h}
    MSVC redistributable CRT DLLs
  LICENSE / README_RELEASE.md / checksums.json
```

Keep the complete directory together. Ordinary .NET publish omitted application PRI/XBF resources in the current SDK combination; the project explicitly includes them in publish output and packaging requires them.

## Data and update contract

Canonical data root is `%LOCALAPPDATA%/Aura`. `AURA_DATA_ROOT` explicitly overrides it. A `portable.marker` beside the GUI selects that directory as data root only when no override is set. ZIPs do not include the marker or a user `config.json`.

```text
<data-root>/
  config.json                  # durable user configuration; template copied only if absent
  plugins/                     # durable Studio sources, DLLs, shadow cache
  client-settings.json         # theme / tray preference, separate from daemon configuration
  logs/winui.log               # GUI exception log
  aura_daemon.log              # native log, working directory = data root
  WebView2/                    # persistent browser profile
  runtime/<version>-<build-id>/ # verified replaceable files from runtime-payload
```

Resolve performs no file mutations and never searches ancestors or current working directory. Prepare verifies payload SHA-256, copies to a unique staging directory, verifies again, then renames to its cache location. Existing caches are verified; corrupt files fail closed. Old version directories are retained, never removed while a process might use them. GUI preferences never mutate daemon config. Plugin paths/publication and config revision semantics remain unchanged.

Exit the old GUI from its tray before switching packages. The new GUI uses the same durable data and a separate version/build runtime cache. An existing external daemon is attached without ownership or automatic upgrade; stop/upgrade it using its original launcher. For alpha.3 portable data, preserve config **and plugins**, explicitly point to the existing data root, and inspect any absolute paths. No automatic Automation migration or path rewriting occurs. Standalone legacy daemon marker compatibility remains `%LOCALAPPDATA%/Aura/.daemon_running`; it is not authoritative ownership evidence.

Development is explicit: set `AURA_DEV_ROOT` to the checkout and optionally `AURA_DEV_BIN` to its native Release directory (default `<checkout>/build/Release`). Set a separate `AURA_DATA_ROOT`. The GUI passes explicit web/SDK roots. An unset dev variable must never fall back to a checkout or `G:/Aura`.

## Process contract

WinUI starts daemon suspended, assigns its private kill-on-close Job Object, then resumes. It retains the child process and gives it a unique instance ID/private shutdown event. The daemon similarly owns web through its job. Graceful GUI exit signals only its retained child; after a bounded timeout it may terminate that child. Closing the GUI job also prevents its child tree from becoming orphaned. Attaching an existing daemon grants no shutdown/kill right. A mutex without a compatible Control API prevents another spawn.

`CoreReady` requires Control API v1 and daemon identity. `StudioWebReady` additionally requires Web API v2 and the same daemon instance. A suppressed/missing web service does not make core/GSI offline. The two sidecars carry UTF-8 process manifests for Unicode data paths; this follows [Microsoft's process code-page guidance](https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page).

## Dependencies and signing

.NET and Windows App SDK are bundled; no source checkout is needed. Studio requires WebView2 Evergreen Runtime. Native Studio publishing additionally requires local x64 MSVC Build Tools and Windows SDK; drafts do not. The four SDK headers travel in the verified payload. No Plugin ABI change or native crash isolation is provided.

Current candidates are unsigned. SHA-256 establishes file consistency, not publisher identity or SmartScreen reputation. Do not claim signing, automatic updates, installer integration, or enterprise deployment. A Release can only be approved after [the release checklist](RELEASE_CHECKLIST.md), including manual Windows/keyboard/CS2 validation.
