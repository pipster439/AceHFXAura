"""Real daemon decision integration, dry-run only; no lighting hardware access."""
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


class TestAutomationV2Daemon(unittest.TestCase):
    def test_state_profile_expires_without_another_packet(self):
        check_daemon_prerequisites_or_skip(self)
        binary = get_authoritative_binary("aura_daemon.exe")
        with tempfile.TemporaryDirectory(prefix="aura_v2_daemon_") as directory:
            config = {
                "default_profile": "v2_desktop",
                "profiles": {name: {"type": "static"} for name in ("v2_desktop", "v2_low")},
                "orchestration": {"automation_freshness_ms": 3000, "rules": [{
                    "id": "low-health", "model": "automation_v2",
                    "when": {"mode": "state", "condition": {
                        "field": "player.state.health", "op": "<", "value": 15}},
                    "action": {"type": "activate_profile", "profile": "v2_low"},
                }]},
            }
            config_path = Path(directory) / "v2.json"
            config_path.write_text(json.dumps(config), encoding="utf-8")
            proc = subprocess.Popen(
                [binary, "--dry-run", "--config", str(config_path), "--keymap", KEYMAP_FILE],
                cwd=directory, env=get_isolated_env(directory),
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
            try:
                deadline = time.monotonic() + 5
                while True:
                    self.assertIsNone(proc.poll(), "daemon exited before GSI admission")
                    try:
                        request = urllib.request.Request("http://127.0.0.1:19897/gsi",
                            data=json.dumps({"player": {"state": {"health": 10}}}).encode(),
                            headers={"Content-Type": "application/json"})
                        with urllib.request.urlopen(request, timeout=0.3) as response:
                            self.assertEqual(response.status, 200)
                        break
                    except OSError:
                        if time.monotonic() >= deadline:
                            self.fail("daemon GSI listener did not become ready")
                        time.sleep(0.05)
                log = Path(directory) / "aura_daemon.log"
                deadline = time.monotonic() + 6
                while time.monotonic() < deadline:
                    self.assertIsNone(proc.poll(), "daemon exited during reconciliation")
                    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
                    low = text.find("[v2_low]")
                    if low >= 0 and "[v2_desktop]" in text[low:]:
                        return  # one POST, actual profile selection and expiry in main loop
                    time.sleep(0.05)
                self.fail("did not observe low-health profile followed by freshness fallback")
            finally:
                terminate_proc(proc)


if __name__ == "__main__":
    unittest.main()
