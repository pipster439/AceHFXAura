"""Directory-based WinUI release. Building a candidate never deploys user data."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ASSETS = {
    "daemon": "aura_daemon.exe", "web": "aura_web_ui.exe", "keymap": "calibrated_keymap.json",
    "template": "config.example.json", "studio": "web/index.html",
    "sdk_effect": "include/engine/effect.h", "sdk_plugin": "include/engine/plugin_interface.h",
    "sdk_types": "include/aura/aura_types.h", "sdk_keymap": "include/aura/keymap.h",
}
EXTENSION_FILES = (
    "Pages/MagneticSwitchPage.xbf",
    "Pages/PluginsPage.xbf",
    "plugins/effect_hall_reactive.dll",
    "plugins/mcp/aura_mcp_server.py",
    "plugins/mcp/README.md",
    "plugins/bridges/wooting-analog-m605/install_plugin.ps1",
    "plugins/bridges/wooting-analog-m605/README.md",
    "deploy_aura.ps1",
    "deploy_aura.bat",
)


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def command(args, cwd=ROOT):
    subprocess.run([str(x) for x in args], cwd=cwd, check=True)


def find_cargo():
    cargo = shutil.which("cargo")
    if cargo:
        return cargo
    candidate = Path.home() / ".cargo/bin/cargo.exe"
    return str(candidate) if candidate.is_file() else None


def candidate_build_id(stage, files):
    # Include the managed publish output: public metadata changes must identify a new candidate.
    published = {p.relative_to(stage).as_posix(): digest(p) for p in sorted(stage.rglob("*")) if p.is_file()}
    return hashlib.sha256(json.dumps({"runtime_files": files, "published_files": published}, sort_keys=True).encode()).hexdigest()[:16]


def product_version(path, *, exact=False):
    """Read the actual Windows version resource; never trust a requested build version."""
    import ctypes
    from ctypes import wintypes
    version = ctypes.WinDLL("version", use_last_error=True)
    version.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    version.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    version.VerQueryValueW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.UINT)]
    ignored = wintypes.DWORD()
    size = version.GetFileVersionInfoSizeW(str(path), ctypes.byref(ignored))
    if not size:
        raise RuntimeError(f"Missing version resource: {path}")
    buffer = ctypes.create_string_buffer(size)
    if not version.GetFileVersionInfoW(str(path), 0, size, buffer):
        raise RuntimeError(f"Unreadable version: {path}")
    pointer = ctypes.c_void_p()
    length = wintypes.UINT()
    if not version.VerQueryValueW(buffer, "\\VarFileInfo\\Translation", ctypes.byref(pointer), ctypes.byref(length)) or length.value < 4:
        raise RuntimeError(f"Missing version translation: {path}")
    pair = ctypes.cast(pointer, ctypes.POINTER(wintypes.WORD))
    key = f"\\StringFileInfo\\{pair[0]:04x}{pair[1]:04x}\\ProductVersion"
    if not version.VerQueryValueW(buffer, key, ctypes.byref(pointer), ctypes.byref(length)):
        raise RuntimeError(f"Missing ProductVersion: {path}")
    value = ctypes.wstring_at(pointer)
    return value if exact else value.split("+")[0]


def _check_x64_pe(path, label):
    data = Path(path).read_bytes()
    if len(data) < 0x40:
        raise RuntimeError(f"Not an x64 PE: {label}")
    offset = int.from_bytes(data[0x3c:0x40], "little")
    if data[offset:offset + 6] != b"PE\0\0\x64\x86":
        raise RuntimeError(f"Not an x64 PE: {label}")


def verify_package(directory, *, require_extensions=False):
    directory = Path(directory)
    for name in (
        "Aura.exe", "Aura.dll", "Aura.deps.json", "Aura.runtimeconfig.json", "Aura.pri",
        "App.xbf", "MainWindow.xbf", "Pages/GameIntegrationPage.xbf", "Pages/SettingsPage.xbf",
        "Pages/StudioPage.xbf", "Assets/AppIcon.ico", "coreclr.dll", "Microsoft.UI.Xaml.dll",
        "CommunityToolkit.WinUI.Controls.SettingsControls.dll",
    ):
        if not (directory / name).is_file():
            raise RuntimeError(f"Incomplete self-contained WinUI output: {name}")
    if require_extensions:
        for name in EXTENSION_FILES:
            if not (directory / name).is_file():
                raise RuntimeError(f"Incomplete release extension/deployment asset: {name}")
        if (directory / "plugins/bridges/wooting-analog-m605/target").exists():
            raise RuntimeError("Forbidden Cargo target directory inside release package")
        _check_x64_pe(directory / "plugins/effect_hall_reactive.dll", "plugins/effect_hall_reactive.dll")
        wooting_dll = directory / "plugins/bridges/wooting-analog-m605/wooting_analog_plugin_aura_m605.dll"
        if wooting_dll.is_file():
            _check_x64_pe(wooting_dll, "plugins/bridges/wooting-analog-m605/wooting_analog_plugin_aura_m605.dll")

    dependencies = json.loads((directory / "Aura.deps.json").read_text(encoding="utf-8"))
    if not any(name.startswith("CommunityToolkit.WinUI.Controls.SettingsControls/")
               for name in dependencies.get("libraries", {})):
        raise RuntimeError("SettingsControls dependency missing from package manifest")
    payload = directory / "runtime-payload"
    manifest = json.loads((payload / "runtime-manifest.json").read_text(encoding="utf-8"))
    if manifest["schema_version"] != 1:
        raise RuntimeError("Unsupported runtime manifest")
    roles = {}
    paths = set()
    for item in manifest["files"]:
        path = item["path"]
        if "\\" in path or ":" in path or any(x in ("", ".", "..") for x in path.split("/")):
            raise RuntimeError("Unsafe manifest path")
        if path.casefold() in paths:
            raise RuntimeError("Duplicate runtime path")
        paths.add(path.casefold())
        if item["role"] in ASSETS:
            if item["role"] in roles:
                raise RuntimeError("Ambiguous role")
            roles[item["role"]] = path
        if digest(payload / path) != item["sha256"]:
            raise RuntimeError(f"Runtime hash mismatch: {path}")
    if roles != ASSETS:
        raise RuntimeError("Runtime roles do not match the release contract")
    if not (directory / "AsusPlatform.dll").is_file():
        raise RuntimeError("Incomplete self-contained WinUI output: AsusPlatform.dll")
    for path in (directory / "Aura.exe", directory / "Aura.dll", directory / "AsusPlatform.dll", payload / "aura_daemon.exe", payload / "aura_web_ui.exe"):
        if product_version(path, exact=True) != manifest["version"]:
            raise RuntimeError(f"Stale or mixed-version binary: {path}")
    for file in directory.rglob("*"):
        if file.name.lower() in ("aackbhal_x64.dll", "config.json", "portable.marker") or file.suffix.lower() == ".pdb":
            raise RuntimeError(f"Forbidden public package file: {file}")
    for name in ("aura_daemon.exe", "aura_web_ui.exe"):
        _check_x64_pe(payload / name, name)
    return manifest


def vc_runtime(vcvars):
    vc = Path(vcvars).parents[2]
    candidates = sorted((vc / "Redist/MSVC").glob("*/x64/Microsoft.VC*.CRT"), reverse=True)
    if not candidates:
        raise RuntimeError("MSVC redistributable CRT not found; install the C++ desktop workload")
    return list(candidates[0].glob("*.dll"))


def _stage_extensions_and_deploy(stage, build_dir):
    plugins_stage = stage / "plugins"
    plugins_stage.mkdir(parents=True, exist_ok=True)

    hall_plugin = build_dir / "Release" / "effect_hall_reactive.dll"
    if not hall_plugin.is_file():
        hall_plugin = ROOT / "plugins" / "effect_hall_reactive.dll"
    if not hall_plugin.is_file():
        raise RuntimeError("Missing built effect_hall_reactive.dll; build target effect_hall_reactive first")
    shutil.copy2(hall_plugin, plugins_stage / "effect_hall_reactive.dll")

    mcp_stage = plugins_stage / "mcp"
    mcp_stage.mkdir(parents=True, exist_ok=True)
    for fname in ("aura_mcp_server.py", "README.md"):
        shutil.copy2(ROOT / "plugins/mcp" / fname, mcp_stage / fname)

    wooting_root = ROOT / "plugins/bridges/wooting-analog-m605"
    wooting_stage = plugins_stage / "bridges/wooting-analog-m605"
    wooting_stage.mkdir(parents=True, exist_ok=True)
    for fname in ("install_plugin.ps1", "README.md"):
        shutil.copy2(wooting_root / fname, wooting_stage / fname)
    wooting_dll = wooting_root / "target/release/wooting_analog_plugin_aura_m605.dll"
    if wooting_dll.is_file():
        shutil.copy2(wooting_dll, wooting_stage / wooting_dll.name)
    else:
        print(f"[WARN] Wooting bridge DLL not built yet; omitted from package: {wooting_dll}")

    for deploy_file in ("deploy_aura.ps1", "deploy_aura.bat"):
        src = ROOT / deploy_file
        if not src.is_file():
            raise RuntimeError(f"Missing root deployment script: {src}")
        shutil.copy2(src, stage / deploy_file)


def build(version, generator, vcvars, build_dir=None, skip_build=False, skip_zip=False, skip_frontend=False):
    version = version.removeprefix("v")
    build_dir = Path(build_dir or ROOT / "build/winui-release").resolve()

    if not skip_frontend:
        # Always regenerate the frontend unless explicitly skipped.
        command(["npm.cmd", "ci"], ROOT / "frontend")
        frontend_output = build_dir / "frontend"
        command(["npm.cmd", "run", "build", "--", "--outDir", frontend_output], ROOT / "frontend")
        studio_html_source = frontend_output / "index.html"
    else:
        studio_html_source = ROOT / "web/index.html"
        if not studio_html_source.is_file() or studio_html_source.stat().st_size < 1000:
            raise RuntimeError(f"Missing or invalid prebuilt frontend for --skip-frontend: {studio_html_source}")
        print(f"Reusing checked-in frontend bundle (--skip-frontend): {studio_html_source}")

    if not skip_build:
        command(["cmake", "-S", ROOT, "-B", build_dir, "-G", generator, "-A", "x64"])
        command(["cmake", "--build", build_dir, "--config", "Release",
                 "--target", "aura_daemon", "aura_web_ui", "effect_hall_reactive", "--parallel", "2"])
        cargo = find_cargo()
        wooting_manifest = ROOT / "plugins/bridges/wooting-analog-m605/Cargo.toml"
        if cargo and wooting_manifest.is_file():
            command([cargo, "build", "--release", "--manifest-path", wooting_manifest])

    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    winui_artifacts = build_dir / "winui-artifacts"
    with tempfile.TemporaryDirectory(prefix="winui-stage-", dir=dist) as staging:
        stage = Path(staging)
        command(["dotnet", "publish", ROOT / "winui/Aura.WinUI.csproj", "-c", "Release", "-p:Platform=x64",
                 "-r", "win-x64", "--self-contained", "true", "-p:WindowsPackageType=None",
                 "-p:WindowsAppSDKSelfContained=true", "-p:DebugType=None", "-p:DebugSymbols=false",
                 "-p:IncludeSourceRevisionInInformationalVersion=false", f"-p:Version={version}",
                 "-p:UseArtifactsOutput=true", f"-p:ArtifactsPath={winui_artifacts}", "-o", stage])
        payload = stage / "runtime-payload"
        payload.mkdir()
        files = []
        for role, relative in ASSETS.items():
            source = (build_dir / "Release" / relative) if role in ("daemon", "web") else (studio_html_source if role == "studio" else ROOT / relative)
            target = payload / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            files.append({"role": role, "path": relative, "sha256": digest(target)})
        # App-local x64 VC runtime for native sidecars and MSVC-built plugins. Never vendor ASUS components.
        for source in vc_runtime(vcvars):
            shutil.copy2(source, payload / source.name)
            files.append({"role": "native_dependency", "path": source.name, "sha256": digest(source)})

        _stage_extensions_and_deploy(stage, build_dir)
        build_id = candidate_build_id(stage, files)
        manifest = {"schema_version": 1, "version": version, "build_id": build_id, "files": files}
        (payload / "runtime-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        shutil.copy2(ROOT / "LICENSE", stage / "LICENSE")
        (stage / "README_RELEASE.md").write_text(f"""# AceHFXAura v{version} — Windows 11 x64

Extract the complete ZIP and either run **deploy_aura.bat** (recommended one-click setup for clean Windows 11 systems) or launch **Aura.exe** directly. Do not move only Aura.exe.

## One-click deployment (clean Windows 11)
Run `deploy_aura.bat` (or `deploy_aura.ps1`) in the extracted package root:
- Installs Aura to `%LOCALAPPDATA%/Programs/Aura` (or add `-Portable` for self-contained mode with `portable.marker`).
- Checks Microsoft Edge WebView2 Evergreen Runtime (pass `-InstallWebView2` to install automatically if missing).
- Seeds `plugins/effect_hall_reactive.dll`, `plugins/mcp/`, and `plugins/bridges/wooting-analog-m605/` into the active data directory without overwriting any existing `config.json`.
- Optional flags: `-ExportMcpConfig`, `-InstallWootingBridge`, `-DesktopShortcut`, `-AutoStart`, `-Launch`, `-Uninstall`.

## Data layout and upgrades
User config, plugins and client data live in `%LOCALAPPDATA%/Aura`; verified runtime assets are cached separately.
Close Aura using the tray Exit command before switching packages. Existing external daemons are not stopped by Aura.
For an explicitly portable data directory, create `portable.marker` beside `Aura.exe` before first launch (or deploy with `deploy_aura.bat -Portable`), or set `AURA_DATA_ROOT`.
To preserve an alpha.3 portable installation, select its existing data directory; preserve `config.json` AND `plugins/`.
No automatic migration from the current working directory, and no automatic Automation migration.

## Bundled extensions and external integrations
- **Native plugins**: `plugins/effect_hall_reactive.dll` (Hall Analog v1 reactive sample). Studio native C++ publishing also requires local MSVC x64 C++ Build Tools and Windows SDK; viewing/saving drafts and `.aurair` programs do not.
- **External MCP server**: `plugins/mcp/aura_mcp_server.py` (stdlib stdio JSON-RPC 2.0 server, replacing the old built-in Studio AI chat panel; requires Python 3.8+ on host).
- **Wooting Analog SDK bridge**: `plugins/bridges/wooting-analog-m605/` reads shared memory `Local\\AuraM605HallAnalogV1`; run `install_plugin.ps1` as Administrator (or deploy with `-InstallWootingBridge`) to register under `C:\\Program Files\\WootingAnalogPlugins\\`.

## Runtime prerequisites and notes
- WebView2 Evergreen Runtime is required for Studio: https://developer.microsoft.com/microsoft-edge/webview2/
- .NET and Windows App SDK are bundled; CMake/.NET SDK/Node and a source checkout are not needed to run this package.
- Native HID is preferred (`MI_01` / `FF00` / `FFC0`). ASUS proprietary DLLs are not included. Independent Light Bar control is not implemented.
- This is an unsigned alpha candidate. SHA-256 verifies download integrity; it does not replace code signing or establish SmartScreen reputation.
- Physical keyboard, Wooting bridge and live CS2 acceptance must be recorded separately before final release.

## Third-party runtime components
Windows App SDK/.NET dependencies retain their upstream licenses. Microsoft VC runtime DLLs are redistributed from the installed Visual Studio Redist directory under the applicable Microsoft redistribution terms.
Project source: https://github.com/pipster439/AceHFXAura (GPL-3.0-only).
""", encoding="utf-8")
        verify_package(stage, require_extensions=True)
        checksums = {p.relative_to(stage).as_posix(): digest(p) for p in sorted(stage.rglob("*")) if p.is_file()}
        (stage / "checksums.json").write_text(json.dumps(checksums, indent=2) + "\n", encoding="utf-8")
        target = dist / f"Aura-v{version}-windows-x64-{build_id}"
        if target.exists():
            # Restrict removal to this generated candidate directory; never user runtime/config.
            if target.resolve().parent != dist.resolve():
                raise RuntimeError("Unsafe staging destination")
            shutil.rmtree(target)
        shutil.copytree(stage, target)
    if not skip_zip:
        archive = dist / f"Aura-v{version}-windows-x64.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
            for path in sorted(target.rglob("*")):
                if path.is_file():
                    output.write(path, path.relative_to(target).as_posix())
        (dist / (archive.name + ".sha256")).write_text(digest(archive) + "  " + archive.name + "\n", encoding="utf-8")
        print(archive)
    print("Verified WinUI candidate:", target)
    return target


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--verify", type=Path, required=True)
    print(json.dumps(verify_package(parser.parse_args().verify, require_extensions=True), indent=2))
