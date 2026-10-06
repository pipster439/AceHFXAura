"""Real daemon: packet burst before render, stack generation reload and cancellation."""
import json
import contextlib
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest
import uuid
from test_runtime_entrypoints import KEYMAP_FILE, check_daemon_prerequisites_or_skip, get_authoritative_binary, get_isolated_env, terminate_proc
from test_automation_reload_daemon import request


@contextlib.contextmanager
def first_render_gate(env):
    """Pause only the synthetic DLL, so both HTTP packets precede the next owner render."""
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateEventW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
    kernel.CreateEventW.restype = wintypes.HANDLE
    kernel.SetEvent.argtypes = [wintypes.HANDLE]; kernel.SetEvent.restype = wintypes.BOOL
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]; kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]; kernel.CloseHandle.restype = wintypes.BOOL
    handles = []
    def release():
        if not kernel.SetEvent(handles[1]): raise ctypes.WinError(ctypes.get_last_error())
    def ready():
        result = kernel.WaitForSingleObject(handles[0], 0)
        if result == 0xFFFFFFFF: raise ctypes.WinError(ctypes.get_last_error())
        return result == 0
    try:
        token = uuid.uuid4().hex
        for field, name in [('READY', 'Ready'), ('RELEASE', 'Release')]:
            event_name = 'Local\\AuraRetrigger' + name + '-' + token
            handle = kernel.CreateEventW(None, True, False, event_name)
            if not handle: raise ctypes.WinError(ctypes.get_last_error())
            handles.append(handle); env['AURA_RETRIGGER_GATE_' + field] = event_name
        yield ready, release
    finally:
        if len(handles) == 2: kernel.SetEvent(handles[1])
        for handle in reversed(handles): kernel.CloseHandle(handle)


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
            gate = first_render_gate(env)
            gate_ready, gate_release = gate.__enter__()
            try:
                proc = subprocess.Popen([str(binary), "--dry-run", "--config", str(path), "--keymap", KEYMAP_FILE], cwd=root, env=env,
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except BaseException:
                gate.__exit__(None, None, None)
                raise
            def lines():
                return trace.read_text().splitlines() if trace.exists() else []
            def wait(predicate):
                deadline = time.monotonic() + 5
                while not predicate():
                    self.assertIsNone(proc.poll())
                    if time.monotonic() > deadline:
                        self.fail("daemon trace did not reach expected state; trace=" + repr(lines()[-128:]))
                    time.sleep(.002)
            def packet(kills):
                self.assertEqual(request(19897, "/gsi", {"player": {"state": {"health": 100, "round_kills": kills}}})[0], 200)
            try:
                def ready():
                    try:
                        status, snapshot, _ = request(19897, "/api/runtime/status")
                        owner = snapshot.get('identity', {}).get('process_id')
                        if owner not in (None, 0, proc.pid): self.fail('Unexpected daemon owner')
                        return status == 200 and owner == proc.pid and snapshot.get('runtime', {}).get('dry_run') is True
                    except OSError:
                        return False
                wait(ready); packet(0)
                # Wait for the owner's initial freshness/baseline evaluation,
                # rather than hoping a fixed sleep precedes the first event.
                wait(lambda: request(19897, '/api/gsi/current')[1]['freshness'].get('fresh') is True)
                self.assertEqual(request(19897, '/api/gsi/current')[1]['packet_count'], 1,
                                 'Unexpected external GSI input in the isolated burst fixture')
                packet(1)
                wait(gate_ready)
                self.assertTrue(any(x.startswith('gate_entered 17') for x in lines()))
                # The fixture owner is held after its first render trace; network
                # receipt of both independent packets is guaranteed before release.
                start = len(lines()); packet(2); packet(3)
                self.assertEqual(request(19897, '/api/gsi/current')[1]['packet_count'], 4,
                                 'Unexpected external GSI input during the controlled burst')
                gate_release()
                wait(lambda: sum(x.startswith("create 17") for x in lines()[start:]) >= 2 and
                     any(x.startswith('render') for x in lines()[start:]))
                burst = lines()[start:]
                self.assertTrue(any(x.startswith('gate_released 17') for x in burst))
                self.assertFalse(any(x.startswith(('gate_error', 'gate_timeout')) for x in lines()))
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
                try:
                    gate_release()  # Never leave the owned daemon blocked during graceful cleanup.
                finally:
                    try:
                        terminate_proc(proc)
                    finally:
                        gate.__exit__(None, None, None)


if __name__ == "__main__":
    unittest.main()
