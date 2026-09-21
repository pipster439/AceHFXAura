"""Real daemon: packet burst before render, stack generation reload and cancellation."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest
from test_runtime_entrypoints import KEYMAP_FILE, check_daemon_prerequisites_or_skip, get_authoritative_binary, get_isolated_env, terminate_proc
from test_automation_reload_daemon import request


class TestAutomationRetriggerDaemon(unittest.TestCase):
    def test_burst_generation_reload_and_semantic_cancellation(self):
        check_daemon_prerequisites_or_skip(self)
        binary = Path(get_authoritative_binary("aura_daemon.exe"))
        with tempfile.TemporaryDirectory(prefix="aura_stage6_") as directory:
            root = Path(directory); dll = root / "live.dll"; trace = root / "trace.txt"
            shutil.copy2(binary.parent / "retrigger_old.dll", dll)
            config = {"fps": 10, "default_profile": "base", "profiles": {"base": {"type": "static", "fps": 10}}, "rules": [],
                "orchestration": {"rules": [{"id": "burst", "model": "automation_v2", "when": {"mode": "event", "condition": {"event": "event.kill"}},
                    "action": {"type": "trigger_effect", "lifetime": "one_shot", "retrigger": "stack", "effect": {"kind": "plugin", "name": str(dll)}}}]}}
            path = root / "config.json"; path.write_text(json.dumps(config), encoding="utf-8")
            env = get_isolated_env(directory); env["AURA_RETRIGGER_TRACE"] = str(trace)
            proc = subprocess.Popen([str(binary), "--dry-run", "--config", str(path), "--keymap", KEYMAP_FILE], cwd=root, env=env,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            def lines():
                return trace.read_text().splitlines() if trace.exists() else []
            def wait(predicate):
                deadline = time.monotonic() + 5
                while not predicate():
                    self.assertIsNone(proc.poll())
                    if time.monotonic() > deadline:
                        self.fail("daemon trace did not reach expected state")
                    time.sleep(.002)
            def packet(kills):
                self.assertEqual(request(19897, "/gsi", {"player": {"state": {"health": 100, "round_kills": kills}}})[0], 200)
            try:
                def ready():
                    try:
                        return request(19897, "/api/runtime/status")[0] == 200
                    except OSError:
                        return False
                wait(ready); packet(0); time.sleep(.15); packet(1)
                wait(lambda: any(x.startswith("render 17") for x in lines()))
                # Dispatch directly after a render boundary. Trace asserts the two
                # independent instances were both created before the next render.
                start = len(lines()); packet(2); packet(3)
                wait(lambda: sum(x.startswith("create 17") for x in lines()[start:]) >= 2)
                burst = lines()[start:]
                first_render = next((i for i, x in enumerate(burst) if x.startswith("render")), len(burst))
                self.assertEqual(sum(x.startswith("create 17") for x in burst[:first_render]), 2,
                    "both separate packets must be admitted before one render; no merged event")
                shutil.copy2(binary.parent / "retrigger_new.dll", dll)
                self.assertEqual(request(19897, "/api/plugin/reload", {"name": str(dll)})[0], 200)
                start = len(lines()); packet(4)
                wait(lambda: any(x.startswith("render 93") for x in lines()[start:]))
                self.assertTrue(any(x.startswith("render 17") for x in lines()[start:]), "old stack must still render on old DLL")
                config["orchestration"]["rules"][0]["action"]["priority"] = 99
                candidate = root / "next.json"; candidate.write_text(json.dumps(config), encoding="utf-8"); os.replace(candidate, path)
                time.sleep(.25); start = len(lines()); time.sleep(.2)
                self.assertFalse(any(x.startswith("render") for x in lines()[start:]), "semantic edit cancels every stacked instance")
            finally:
                terminate_proc(proc)


if __name__ == "__main__":
    unittest.main()
