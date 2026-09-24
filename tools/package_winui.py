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

def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()

def command(args, cwd=ROOT):
    subprocess.run([str(x) for x in args], cwd=cwd, check=True)

def product_version(path):
    """Read the actual Windows version resource; never trust a requested build version."""
    import ctypes
    from ctypes import wintypes
    version = ctypes.WinDLL("version", use_last_error=True)
    version.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    version.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    version.VerQueryValueW.argtypes = [ctypes.c_void_p,wintypes.LPCWSTR,ctypes.POINTER(ctypes.c_void_p),ctypes.POINTER(wintypes.UINT)]
    ignored=wintypes.DWORD(); size=version.GetFileVersionInfoSizeW(str(path),ctypes.byref(ignored))
    if not size: raise RuntimeError(f"Missing version resource: {path}")
    buffer=ctypes.create_string_buffer(size)
    if not version.GetFileVersionInfoW(str(path),0,size,buffer): raise RuntimeError(f"Unreadable version: {path}")
    pointer=ctypes.c_void_p(); length=wintypes.UINT()
    if not version.VerQueryValueW(buffer,"\\VarFileInfo\\Translation",ctypes.byref(pointer),ctypes.byref(length)) or length.value<4:
        raise RuntimeError(f"Missing version translation: {path}")
    pair=ctypes.cast(pointer,ctypes.POINTER(wintypes.WORD))
    key=f"\\StringFileInfo\\{pair[0]:04x}{pair[1]:04x}\\ProductVersion"
    if not version.VerQueryValueW(buffer,key,ctypes.byref(pointer),ctypes.byref(length)):
        raise RuntimeError(f"Missing ProductVersion: {path}")
    return ctypes.wstring_at(pointer).split("+")[0]

def verify_package(directory):
    directory = Path(directory)
    for name in ("Aura.exe", "Aura.dll", "Aura.deps.json", "Aura.runtimeconfig.json", "Aura.pri", "App.xbf", "MainWindow.xbf", "Pages/GameIntegrationPage.xbf", "Pages/SettingsPage.xbf", "Pages/StudioPage.xbf", "Assets/AppIcon.ico", "coreclr.dll", "Microsoft.UI.Xaml.dll", "CommunityToolkit.WinUI.Controls.SettingsControls.dll"):
        if not (directory / name).is_file():
            raise RuntimeError(f"Incomplete self-contained WinUI output: {name}")
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
            if item["role"] in roles: raise RuntimeError("Ambiguous role")
            roles[item["role"]] = path
        if digest(payload / path) != item["sha256"]:
            raise RuntimeError(f"Runtime hash mismatch: {path}")
    if roles != ASSETS:
        raise RuntimeError("Runtime roles do not match the release contract")
    for path in (directory/"Aura.exe", directory/"Aura.dll", payload/"aura_daemon.exe", payload/"aura_web_ui.exe"):
        if product_version(path) != manifest["version"]:
            raise RuntimeError(f"Stale or mixed-version binary: {path}")
    for file in directory.rglob("*"):
        if file.name.lower() in ("aackbhal_x64.dll", "config.json", "portable.marker"):
            raise RuntimeError(f"Forbidden public package file: {file}")
    for name in ("aura_daemon.exe", "aura_web_ui.exe"):
        data = (payload / name).read_bytes()
        offset = int.from_bytes(data[0x3c:0x40], "little")
        if data[offset:offset+6] != b"PE\0\0\x64\x86":
            raise RuntimeError(f"Not an x64 PE: {name}")
    return manifest

def vc_runtime(vcvars):
    vc = Path(vcvars).parents[2]
    candidates = sorted((vc / "Redist/MSVC").glob("*/x64/Microsoft.VC*.CRT"), reverse=True)
    if not candidates:
        raise RuntimeError("MSVC redistributable CRT not found; install the C++ desktop workload")
    return list(candidates[0].glob("*.dll"))

def build(version, generator, vcvars, build_dir=None, skip_build=False, skip_zip=False):
    version = version.removeprefix("v")
    build_dir = Path(build_dir or ROOT / "build/winui-release").resolve()
    # Always regenerate the frontend, even when the native build has just been tested separately.
    command(["npm.cmd", "ci"], ROOT / "frontend")
    command(["npm.cmd", "run", "build"], ROOT / "frontend")
    if not skip_build:
        command(["cmake", "-S", ROOT, "-B", build_dir, "-G", generator, "-A", "x64"])
        command(["cmake", "--build", build_dir, "--config", "Release", "--target", "aura_daemon", "aura_web_ui", "--parallel", "2"])
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="winui-stage-", dir=dist) as staging:
        stage = Path(staging)
        command(["dotnet", "publish", ROOT / "winui/Aura.WinUI.csproj", "-c", "Release", "-p:Platform=x64",
                 "-r", "win-x64", "--self-contained", "true", "-p:WindowsPackageType=None",
                 "-p:WindowsAppSDKSelfContained=true", "-o", stage])
        payload = stage / "runtime-payload"
        payload.mkdir()
        files = []
        for role, relative in ASSETS.items():
            source = (build_dir / "Release" / relative) if role in ("daemon", "web") else ROOT / relative
            target = payload / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            files.append({"role": role, "path": relative, "sha256": digest(target)})
        # App-local x64 VC runtime for native sidecars and MSVC-built plugins. Never vendor ASUS components.
        for source in vc_runtime(vcvars):
            shutil.copy2(source, payload / source.name)
            files.append({"role": "native_dependency", "path": source.name, "sha256": digest(source)})
        build_id = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()[:16]
        manifest = {"schema_version": 1, "version": version, "build_id": build_id, "files": files}
        (payload / "runtime-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        shutil.copy2(ROOT / "LICENSE", stage / "LICENSE")
        (stage / "README_RELEASE.md").write_text(f"""# AceHFXAura v{version} — Windows 11 x64

Extract the complete ZIP and launch **Aura.exe** (WinUI). Do not move only the EXE.
User config, plugins and client data live in %LOCALAPPDATA%/Aura; verified runtime assets are cached separately.
Close Aura using the tray Exit command before switching packages. Existing external daemons are not stopped by Aura.
For an explicitly portable data directory, create portable.marker beside Aura.exe before first launch, or set AURA_DATA_ROOT.
To preserve an alpha.3 portable installation, select its existing data directory; preserve config.json AND plugins/.
No automatic migration from the current working directory, and no automatic Automation migration.

WebView2 Evergreen Runtime is needed for Studio: https://developer.microsoft.com/microsoft-edge/webview2/
Studio native publishing also needs MSVC x64 C++ Build Tools and the Windows SDK; viewing/saving drafts does not.
.NET and Windows App SDK are bundled; Node/CMake/.NET SDK and a source checkout are not needed to run this package.
Native HID is preferred. ASUS proprietary DLLs are not included. Independent Light Bar and advanced Hall controls are not implemented.
This is an unsigned alpha candidate. SHA-256 verifies download integrity; it does not replace code signing or establish SmartScreen reputation.
Physical keyboard and live CS2 acceptance must be recorded separately before release.

## Third-party runtime components
Windows App SDK/.NET dependencies retain their upstream licenses. Microsoft VC runtime DLLs are redistributed from the installed Visual Studio Redist directory under the applicable Microsoft redistribution terms.
Project source: https://github.com/pipster439/AceHFXAura (GPL-3.0-only).
""", encoding="utf-8")
        verify_package(stage)
        checksums = {p.relative_to(stage).as_posix(): digest(p) for p in sorted(stage.rglob("*")) if p.is_file()}
        (stage / "checksums.json").write_text(json.dumps(checksums, indent=2) + "\n", encoding="utf-8")
        target = dist / f"Aura-v{version}-windows-x64-{build_id}"
        if target.exists():
            # Restrict removal to this generated candidate directory; never user runtime/config.
            if target.resolve().parent != dist.resolve(): raise RuntimeError("Unsafe staging destination")
            shutil.rmtree(target)
        shutil.copytree(stage, target)
    if not skip_zip:
        archive = dist / f"Aura-v{version}-windows-x64.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
            for path in sorted(target.rglob("*")):
                if path.is_file(): output.write(path, path.relative_to(target).as_posix())
        (dist / (archive.name + ".sha256")).write_text(digest(archive) + "  " + archive.name + "\n", encoding="utf-8")
        print(archive)
    print("Verified WinUI candidate:", target)
    return target

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--verify", type=Path, required=True)
    print(json.dumps(verify_package(parser.parse_args().verify), indent=2))
