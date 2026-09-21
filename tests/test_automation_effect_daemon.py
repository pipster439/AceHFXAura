"""Real Stage 3 daemon admission/render/completion, dry-run and synthetic DLL only."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest
import urllib.request

from test_runtime_entrypoints import (
    KEYMAP_FILE, check_daemon_prerequisites_or_skip, get_authoritative_binary,
    get_isolated_env, terminate_proc,
)


class TestAutomationEffectDaemon(unittest.TestCase):
    def test_persistent_completion_does_not_respawn(self):
        check_daemon_prerequisites_or_skip(self)
        binary = get_authoritative_binary("aura_daemon.exe")
        fixture = Path(binary).parent / "automation_lifecycle_old.dll"
        self.assertTrue(fixture.is_file(), "build Stage 3 synthetic fixture")
        with tempfile.TemporaryDirectory(prefix="aura_stage3_daemon_") as directory:
            config = {
                "default_profile": "base", "profiles": {"base": {"type": "static"}},
                "orchestration": {"rules": [{
                    "id": "persistent", "model": "automation_v2",
                    "when": {"mode": "state", "condition": {
                        "field": "player.state.health", "op": "<", "value": 15}},
                    "action": {"type": "trigger_effect", "lifetime": "while_true",
                               "effect": {"kind": "plugin", "name": str(fixture)}},
                }]},
            }
            path = Path(directory) / "config.json"
            path.write_text(json.dumps(config), encoding="utf-8")
            trace = Path(directory) / "lifecycle.txt"
            env = get_isolated_env(directory)
            env["AURA_LIFECYCLE_FIXTURE_TRACE"] = str(trace)
            proc = subprocess.Popen(
                [binary, "--dry-run", "--config", str(path), "--keymap", KEYMAP_FILE],
                cwd=directory, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
            try:
                deadline = time.monotonic() + 5
                while True:
                    self.assertIsNone(proc.poll(), "daemon exited before packet")
                    try:
                        request = urllib.request.Request("http://127.0.0.1:19897/gsi",
                            data=json.dumps({"player": {"state": {"health": 10}}}).encode(),
                            headers={"Content-Type": "application/json"})
                        with urllib.request.urlopen(request, timeout=0.3) as response:
                            self.assertEqual(response.status, 200)
                        break
                    except OSError:
                        if time.monotonic() >= deadline:
                            self.fail("GSI listener unavailable")
                        time.sleep(0.05)
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline:
                    text = trace.read_text() if trace.exists() else ""
                    if "finished\n" in text:
                        self.assertIn("render\n", text)
                        time.sleep(0.25)
                        self.assertIsNone(proc.poll(), "daemon exited at completion")
                        self.assertEqual(trace.read_text(), text, "completed True interval respawned")
                        return
                    time.sleep(0.02)
                self.fail("real daemon did not render and complete V2 effect")
            finally:
                terminate_proc(proc)


if __name__ == "__main__":
    unittest.main()
