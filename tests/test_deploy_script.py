"""End-to-end tests for clean-machine deployment (deploy_aura.ps1 / deploy_aura.bat)."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

REPO_ROOT = Path(__file__).resolve().parents[1]
DEPLOY_PS1 = REPO_ROOT / "deploy_aura.ps1"
DEPLOY_BAT = REPO_ROOT / "deploy_aura.bat"
VERSION = (REPO_ROOT / "VERSION").read_text(encoding="utf-8").strip().removeprefix("v")


def sha256_file(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def run_ps51(args, *, cwd=REPO_ROOT, env=None):
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    cmd = [
        "powershell.exe",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(DEPLOY_PS1),
        *args,
    ]
    return subprocess.run(
        cmd,
        cwd=str(cwd),
        env=merged_env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def create_fixture_package(root: Path, *, corrupt_zip_sha: bool = False):
    pkg = root / f"Aura-v{VERSION}-windows-x64-fixture01"
    (pkg / "runtime-payload/web").mkdir(parents=True, exist_ok=True)
    (pkg / "Assets").mkdir(parents=True, exist_ok=True)
    (pkg / "plugins/mcp").mkdir(parents=True, exist_ok=True)
    (pkg / "plugins/bridges/wooting-analog-m605").mkdir(parents=True, exist_ok=True)

    (pkg / "Aura.exe").write_bytes(b"fixture-aura-exe")
    (pkg / "Assets/AppIcon.ico").write_bytes(b"fixture-ico")
    shutil.copy2(DEPLOY_PS1, pkg / "deploy_aura.ps1")
    shutil.copy2(DEPLOY_BAT, pkg / "deploy_aura.bat")

    manifest_files = []
    for role, rel, payload in (
        ("daemon", "aura_daemon.exe", b"fixture-daemon"),
        ("web", "aura_web_ui.exe", b"fixture-web"),
        ("keymap", "calibrated_keymap.json", b"{}"),
        ("template", "config.example.json", b'{"default_profile":"desktop"}\n'),
        ("studio", "web/index.html", b"<!doctype html><title>Aura</title>"),
    ):
        target = pkg / "runtime-payload" / rel
        target.write_bytes(payload)
        manifest_files.append({"role": role, "path": rel, "sha256": sha256_file(target)})
    (pkg / "runtime-payload/runtime-manifest.json").write_text(
        json.dumps({"schema_version": 1, "version": VERSION, "build_id": "fixture01", "files": manifest_files}, indent=2),
        encoding="utf-8",
    )

    (pkg / "plugins/effect_hall_reactive.dll").write_bytes(b"fixture-hall-dll")
    shutil.copy2(REPO_ROOT / "plugins/mcp/aura_mcp_server.py", pkg / "plugins/mcp/aura_mcp_server.py")
    shutil.copy2(REPO_ROOT / "plugins/mcp/README.md", pkg / "plugins/mcp/README.md")
    shutil.copy2(
        REPO_ROOT / "plugins/bridges/wooting-analog-m605/install_plugin.ps1",
        pkg / "plugins/bridges/wooting-analog-m605/install_plugin.ps1",
    )
    (pkg / "plugins/bridges/wooting-analog-m605/wooting_analog_plugin_aura_m605.dll").write_bytes(b"fixture-wooting-dll")

    checksums = {
        p.relative_to(pkg).as_posix(): sha256_file(p)
        for p in sorted(pkg.rglob("*"))
        if p.is_file()
    }
    (pkg / "checksums.json").write_text(json.dumps(checksums, indent=2), encoding="utf-8")

    zip_path = root / f"Aura-v{VERSION}-windows-x64.zip"
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for p in sorted(pkg.rglob("*")):
            if p.is_file():
                zf.write(p, p.relative_to(pkg).as_posix())
    sha_hex = ("0" * 64) if corrupt_zip_sha else sha256_file(zip_path)
    (root / f"{zip_path.name}.sha256").write_text(f"{sha_hex}  {zip_path.name}\n", encoding="utf-8")
    return pkg, zip_path


class CleanMachineDeploymentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="aura-deploy-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def _package_sources(self):
        env_dir = os.environ.get("AURA_PACKAGE_DIR")
        env_zip = os.environ.get("AURA_PACKAGE_ZIP")
        if env_dir and env_zip and Path(env_dir).is_dir() and Path(env_zip).is_file():
            return Path(env_dir), Path(env_zip)
        return create_fixture_package(self.root / "src")

    def test_deploy_from_zip_idempotent_preserves_config_and_custom_plugins(self):
        _, zip_path = self._package_sources()
        install_dir = self.root / "ProgramFiles/Aura"
        data_dir = self.root / "AppData/Aura"

        res = run_ps51([
            "-PackagePath", str(zip_path),
            "-InstallDir", str(install_dir),
            "-DataDir", str(data_dir),
            "-NoStartMenuShortcut",
            "-SkipPrereqCheck",
            "-ExportMcpConfig",
            "-VerifyChecksums",
        ])
        self.assertEqual(res.returncode, 0, res.stdout)
        self.assertTrue((install_dir / "Aura.exe").is_file())
        self.assertTrue((install_dir / "runtime-payload/runtime-manifest.json").is_file())
        self.assertTrue((install_dir / "deploy_aura.ps1").is_file())
        self.assertTrue((install_dir / "deploy_aura.bat").is_file())
        self.assertFalse((install_dir / "portable.marker").exists())

        self.assertTrue((data_dir / "plugins/effect_hall_reactive.dll").is_file())
        self.assertTrue((data_dir / "plugins/mcp/aura_mcp_server.py").is_file())
        self.assertTrue((data_dir / "plugins/bridges/wooting-analog-m605/install_plugin.ps1").is_file())

        mcp_cfg = json.loads((data_dir / "aura-mcp-config.json").read_text(encoding="utf-8"))
        aura_entry = mcp_cfg["mcpServers"]["aura"]
        self.assertEqual(
            Path(aura_entry["args"][0]).resolve(),
            (data_dir / "plugins/mcp/aura_mcp_server.py").resolve(),
        )

        # Simulate user-customized config and user-compiled plugin before re-deploying.
        user_cfg = data_dir / "config.json"
        custom_bytes = b'{"default_profile":"custom_user","fps":60}\n'
        user_cfg.write_bytes(custom_bytes)
        custom_plugin = data_dir / "plugins/effect_user_custom.dll"
        custom_plugin.write_bytes(b"custom-user-plugin")

        res2 = run_ps51([
            "-PackagePath", str(zip_path),
            "-InstallDir", str(install_dir),
            "-DataDir", str(data_dir),
            "-NoStartMenuShortcut",
            "-SkipPrereqCheck",
        ])
        self.assertEqual(res2.returncode, 0, res2.stdout)
        self.assertEqual(user_cfg.read_bytes(), custom_bytes)
        self.assertEqual(custom_plugin.read_bytes(), b"custom-user-plugin")

    def test_corrupt_zip_sha256_rejected_before_install(self):
        _, bad_zip = create_fixture_package(self.root / "bad-src", corrupt_zip_sha=True)
        install_dir = self.root / "BadInstall"
        data_dir = self.root / "BadData"

        res = run_ps51([
            "-PackagePath", str(bad_zip),
            "-InstallDir", str(install_dir),
            "-DataDir", str(data_dir),
            "-NoStartMenuShortcut",
            "-SkipPrereqCheck",
        ])
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("SHA-256", res.stdout)
        self.assertFalse((install_dir / "Aura.exe").exists())
        self.assertFalse((data_dir / "plugins").exists())

    def test_portable_mode_and_uninstall_preserve_and_purge(self):
        pkg_dir, _ = self._package_sources()
        portable_dir = self.root / "PortableAura"

        res = run_ps51([
            "-PackagePath", str(pkg_dir),
            "-InstallDir", str(portable_dir),
            "-Portable",
            "-NoStartMenuShortcut",
            "-SkipPrereqCheck",
            "-ExportMcpConfig",
        ])
        self.assertEqual(res.returncode, 0, res.stdout)
        self.assertTrue((portable_dir / "portable.marker").is_file())
        self.assertTrue((portable_dir / "plugins/mcp/aura_mcp_server.py").is_file())
        mcp_cfg = json.loads((portable_dir / "aura-mcp-config.json").read_text(encoding="utf-8"))
        self.assertEqual(
            Path(mcp_cfg["mcpServers"]["aura"]["args"][0]).resolve(),
            (portable_dir / "plugins/mcp/aura_mcp_server.py").resolve(),
        )

        # Standard mode uninstall preserves DataDir unless -PurgeData is passed.
        std_install = self.root / "StdInstall"
        std_data = self.root / "StdData"
        self.assertEqual(
            run_ps51([
                "-PackagePath", str(pkg_dir),
                "-InstallDir", str(std_install),
                "-DataDir", str(std_data),
                "-NoStartMenuShortcut",
                "-SkipPrereqCheck",
            ]).returncode,
            0,
        )
        (std_data / "config.json").write_text('{"keep":true}', encoding="utf-8")

        uninst_keep = run_ps51([
            "-Uninstall",
            "-InstallDir", str(std_install),
            "-DataDir", str(std_data),
        ])
        self.assertEqual(uninst_keep.returncode, 0, uninst_keep.stdout)
        self.assertFalse(std_install.exists())
        self.assertTrue((std_data / "config.json").is_file())

        uninst_purge = run_ps51([
            "-Uninstall",
            "-PurgeData",
            "-InstallDir", str(std_install),
            "-DataDir", str(std_data),
        ])
        self.assertEqual(uninst_purge.returncode, 0, uninst_purge.stdout)
        self.assertFalse(std_data.exists())

    def test_deploy_bat_help(self):
        proc = subprocess.run(
            ["cmd.exe", "/c", str(DEPLOY_BAT), "-Help"],
            cwd=str(REPO_ROOT),
            env={**os.environ, "AURA_NO_PAUSE": "1"},
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        self.assertEqual(proc.returncode, 0, proc.stdout)
        self.assertIn("-Portable", proc.stdout)
        self.assertIn("-Uninstall", proc.stdout)


if __name__ == "__main__":
    unittest.main()
