"""Candidate packaging must never deploy over the owner's checkout/runtime."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("package_release", Path(__file__).resolve().parents[1] / "tools/package_release.py")
packaging = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(packaging)


class PackagingLocalState(unittest.TestCase):
    def test_candidate_build_preserves_local_binaries_config_and_runtime(self):
        with tempfile.TemporaryDirectory(prefix="aura-package-state-") as directory:
            root = Path(directory)
            release = root / "build/Release"
            release.mkdir(parents=True)
            (root / "build/CMakeCache.txt").write_text("CMAKE_GENERATOR:INTERNAL=" + packaging.VS_GENERATOR)
            protected = [root / name for name in ("Aura.exe", "aura_daemon.exe", "aura_web_ui.exe", "config.json")]
            runtime = root / "localappdata/Aura/runtime/aura_daemon.exe"
            runtime.parent.mkdir(parents=True)
            protected.append(runtime)
            for path in protected:
                path.write_bytes(b"previous-local-state")
            for name in ("aura_daemon.exe", "aura_web_ui.exe"):
                (release / name).write_bytes(b"new-candidate-runtime")
            for name in ("calibrated_keymap.json", "config.example.json", "web/index.html", "include/engine/effect.h", "include/engine/plugin_interface.h", "include/aura/aura_types.h", "include/aura/keymap.h"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"candidate-asset")
            def compiler(command, **kwargs):
                if command.startswith("cl.exe"):
                    (root / "dist/Aura.exe").write_bytes(b"new-candidate-launcher")
                return ""
            with patch.multiple(packaging, REPO_ROOT=str(root), BUILD_RELEASE_DIR=str(release), DIST_DIR=str(root / "dist")), patch.object(packaging, "run_cmd", side_effect=compiler), patch.dict(os.environ, {"LOCALAPPDATA": str(root / "localappdata")}):
                packaging.ensure_binaries()
                packaging.build_single_exe("0.1.0-alpha.3")
            self.assertEqual((root / "dist/Aura.exe").read_bytes(), b"new-candidate-launcher")
            for path in protected:
                self.assertEqual(path.read_bytes(), b"previous-local-state", str(path))


if __name__ == "__main__":
    unittest.main()
