"""Opt-in packaged WebView2/React/Blockly smoke against an isolated dry-run core."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import shutil
import time
import unittest

from test_runtime_entrypoints import (
    EXAMPLE_CONFIG, check_daemon_prerequisites_or_skip,
    get_isolated_env, terminate_proc, wait_for_http_ready,
)
from test_automation_authoring_daemon import request


class TestEmbeddedStudio(unittest.TestCase):
    def test_packaged_studio_hosts_one_retained_editor(self):
        exe = os.environ.get("AURA_STUDIO_EXE")
        output = os.environ.get("AURA_STUDIO_VALIDATION_DIR")
        if not exe or not output:
            self.skipTest("Set AURA_STUDIO_EXE and AURA_STUDIO_VALIDATION_DIR for desktop WebView2 validation")
        check_daemon_prerequisites_or_skip(self)
        payload = Path(exe).resolve().parent / "runtime-payload"
        self.assertTrue((payload / "runtime-manifest.json").is_file(), "Use an extracted alpha.4 package")
        self.assertTrue((payload / "web" / "index.html").is_file(), "Package must carry the built frontend")
        Path(output).mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="aura-embedded-", ignore_cleanup_errors=True) as directory:
            root = Path(directory)
            config = json.loads(Path(EXAMPLE_CONFIG).read_text(encoding="utf-8"))
            config["blockly_effects"] = {
                "validation_effect": {"name": "validation_effect", "version": 2,
                    "blockly_json": {"blocks": {"languageVersion": 0, "blocks": []}}}
            }
            path = root / "config.json"
            path.write_text(json.dumps(config), encoding="utf-8")
            env = get_isolated_env(directory)
            env["AURA_DATA_ROOT"] = directory
            env["AURA_STUDIO_VALIDATION_DIR"] = str(Path(output).resolve())
            env.pop("AURA_DEV_ROOT", None)
            env.pop("AURA_DEV_BIN", None)
            core = subprocess.Popen(
                [str(payload / "aura_daemon.exe"), "--dry-run", "--config", str(path), "--keymap", str(payload / "calibrated_keymap.json")],
                cwd=root, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
            gui = None
            try:
                self.assertTrue(wait_for_http_ready(19897, path="/api/runtime/status"))
                self.assertTrue(wait_for_http_ready(19898, path="/api/status"))
                self.assertEqual(request(19897, "/api/runtime/status")[1]["identity"]["process_id"], core.pid)
                gui = subprocess.Popen([str(Path(exe).resolve()), "--validate-studio"], cwd=root, env=env)
                gui.wait(timeout=240)
                results = json.loads((Path(output) / "studio-results.json").read_text(encoding="utf-8"))
                self.assertIsNone(results["error"], results["error"])
                self.assertGreaterEqual(len(results["results"]), 13)
                self.assertIsNone(core.poll(), "GUI must not stop an externally started core")
            finally:
                if gui is not None:
                    terminate_proc(gui)
                terminate_proc(core)
                # WebView2 browser children can release their profile lock after the GUI process exits.
                if root.resolve().is_relative_to(Path(tempfile.gettempdir()).resolve()):
                    for _ in range(30):
                        try:
                            shutil.rmtree(root)
                            break
                        except OSError:
                            time.sleep(.2)


if __name__ == "__main__":
    unittest.main()
