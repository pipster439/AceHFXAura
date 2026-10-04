"""Public package metadata must be exact, including referenced assemblies."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("package_winui", Path(__file__).resolve().parents[1] / "tools/package_winui.py")
pack = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(pack)
VERSION = "0.1.0-alpha.7"


class PublicPackageMetadata(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="aura-package-metadata-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        required = ("Aura.exe", "Aura.dll", "AsusPlatform.dll", "Aura.runtimeconfig.json", "Aura.pri",
                    "App.xbf", "MainWindow.xbf", "Pages/GameIntegrationPage.xbf", "Pages/SettingsPage.xbf",
                    "Pages/StudioPage.xbf", "Assets/AppIcon.ico", "coreclr.dll", "Microsoft.UI.Xaml.dll",
                    "CommunityToolkit.WinUI.Controls.SettingsControls.dll")
        for name in required:
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(b"fixture")
        (self.root / "Aura.deps.json").write_text(json.dumps({"libraries": {
            "CommunityToolkit.WinUI.Controls.SettingsControls/fixture": {}}}))
        payload = self.root / "runtime-payload"
        files = []
        for role, relative in pack.ASSETS.items():
            target = payload / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            data = bytearray(128)
            data[0x3c:0x40] = (64).to_bytes(4, "little")
            data[64:70] = b"PE\0\0\x64\x86"
            target.write_bytes(data)
            files.append({"role": role, "path": relative, "sha256": pack.digest(target)})
        (payload / "runtime-manifest.json").write_text(json.dumps({
            "schema_version": 1, "version": VERSION, "build_id": "fixture", "files": files}))

    def version_reader(self, path, *, exact=False):
        # Exercise the public verifier's exact flag: normalization would hide this regression.
        value = self.versions.get(path.name, VERSION)
        return value if exact else value.split("+")[0]

    def verify(self):
        with patch.object(pack, "product_version", side_effect=self.version_reader):
            return pack.verify_package(self.root)

    def test_exact_product_versions_accepted(self):
        self.versions = {}
        self.assertEqual(self.verify()["version"], VERSION)

    def test_commit_suffix_rejected_on_each_public_binary(self):
        for name in ("Aura.exe", "Aura.dll", "AsusPlatform.dll", "aura_daemon.exe", "aura_web_ui.exe"):
            with self.subTest(binary=name):
                self.versions = {name: VERSION + "+0123456789abcdef"}
                with self.assertRaisesRegex(RuntimeError, "Stale or mixed-version"):
                    self.verify()

    def test_missing_platform_dependency_rejected(self):
        self.versions = {}
        (self.root / "AsusPlatform.dll").unlink()
        with self.assertRaisesRegex(RuntimeError, "AsusPlatform.dll"):
            self.verify()

    def test_debug_symbols_rejected_case_insensitively(self):
        self.versions = {}
        symbols = self.root / "runtime-payload/debug/Private.PDB"
        symbols.parent.mkdir()
        symbols.write_bytes(b"private symbols")
        with self.assertRaisesRegex(RuntimeError, "Forbidden public package"):
            self.verify()

    def test_managed_metadata_change_identifies_new_candidate(self):
        before = pack.candidate_build_id(self.root, [])
        self.assertEqual(before, pack.candidate_build_id(self.root, []))
        (self.root / "Aura.dll").write_bytes(b"same runtime payload; new managed metadata")
        self.assertNotEqual(before, pack.candidate_build_id(self.root, []))


if __name__ == "__main__":
    unittest.main()
