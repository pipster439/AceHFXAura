"""Frame-boundary reload and actual Studio compile/load/config transaction tests."""
import contextlib
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest
import urllib.error
import urllib.request
from test_runtime_entrypoints import KEYMAP_FILE, check_daemon_prerequisites_or_skip, get_authoritative_binary, get_isolated_env, terminate_proc


def request(port, path, body=None, headers=None):
    req = urllib.request.Request(f"http://127.0.0.1:{port}{path}",
        data=None if body is None else json.dumps(body).encode(),
        headers={"Content-Type": "application/json", **(headers or {})})
    try:
        response = urllib.request.urlopen(req, timeout=30)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        return response.status, json.loads(response.read()), response.headers


class TestAutomationReloadDaemon(unittest.TestCase):
    @contextlib.contextmanager
    def daemon(self, config, prepare=None):
        check_daemon_prerequisites_or_skip(self)
        binary = Path(get_authoritative_binary("aura_daemon.exe"))
        with tempfile.TemporaryDirectory(prefix="aura_stage4_") as directory:
            root = Path(directory)
            if prepare:
                prepare(root, binary.parent, config)
            path = root / "config.json"
            path.write_text(json.dumps(config), encoding="utf-8")
            env = get_isolated_env(directory)
            env["AURA_LIFECYCLE_FIXTURE_TRACE"] = str(root / "trace.txt")
            env["AURA_LIFECYCLE_FIXTURE_EVENTS"] = str(root / "fixture-events.jsonl")
            output = (root / "daemon.log").open("w", encoding="utf-8")
            proc = subprocess.Popen([str(binary), "--dry-run", "--config", str(path), "--keymap", KEYMAP_FILE],
                cwd=root, env=env, stdout=output, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 8
                while True:
                    self.assertIsNone(proc.poll(), "daemon exited at startup")
                    try:
                        status, data, _ = request(19897, "/api/runtime/status")
                        if (status == 200 and data.get("identity", {}).get("process_id") == proc.pid
                                and data.get("runtime", {}).get("dry_run") is True):
                            break
                    except OSError:
                        pass
                    if time.monotonic() > deadline:
                        self.fail("daemon not ready")
                    time.sleep(.03)
                yield root, binary.parent, path
            finally:
                terminate_proc(proc)
                output.close()

    def test_cosmetic_reload_and_plugin_swap_preserve_old_transient(self):
        def rule(id, mode):
            return {"id": id, "model": "automation_v2", "when": {"mode": mode, "condition":
                {"event": "event.kill"} if mode == "event" else {"field": "player.state.health", "op": "<", "value": 15}},
                "action": {"type": "trigger_effect", "lifetime": "one_shot" if mode == "event" else "while_true",
                           "effect": {"kind": "plugin", "name": "placeholder"}}}
        config = {"default_profile": "base", "profiles": {"base": {"type": "static"}}, "rules": [],
                  "orchestration": {"rules": [rule("shot", "event"), rule("persistent", "state")]}}
        def prepare(root, binaries, cfg):
            dll = root / "live.dll"
            shutil.copy2(binaries / "automation_reload_old.dll", dll)
            for r in cfg["orchestration"]["rules"]:
                r["action"]["effect"]["name"] = str(dll)
        with self.daemon(config, prepare) as (root, binaries, path):
            def wait_for(predicate, description):
                deadline = time.monotonic() + 4
                while True:
                    value = predicate()
                    if value:
                        return value
                    if time.monotonic() >= deadline:
                        self.fail(f"Timed out waiting for {description}; events={events()}")
                    time.sleep(.01)  # Poll a specific owner/lifecycle event, not an observation window.
            def events():
                journal = root / "fixture-events.jsonl"
                if not journal.exists():
                    return []
                # The DLL appends one complete line per event. Ignore an unfinished final write.
                return [json.loads(line) for line in journal.read_text().splitlines(keepends=True)
                        if line.endswith("\n")]
            def rendered(marker):
                return {e["instance"] for e in events() if e["event"] == "render" and e["marker"] == marker}
            def destroyed(marker, instance):
                return any(e["event"] == "destroyed" and e["marker"] == marker
                           and e["instance"] == instance for e in events())
            def renders_after(index, marker, instance):
                return sum(e["event"] == "render" and e["marker"] == marker and e["instance"] == instance
                           for e in events()[index:])
            def packet(kills):
                # --dry-run protects hardware, not the live GSI listener. Use the existing
                # owner-thread simulation queue so a running CS2 cannot overwrite this fixture.
                status, queued, _ = request(19897, "/api/gsi/simulation", {
                    "enabled": True, "heartbeat": True, "foreground_process": "cs2.exe",
                    "health": 10, "round_kills": kills})
                self.assertEqual(status, 202)
                wait_for(lambda: request(19897, "/api/gsi/simulation")[1]["applied_sequence"] >= queued["sequence"],
                         "simulation command applied by render owner")
            def trace():
                return (root / "trace.txt").read_text() if (root / "trace.txt").exists() else ""
            def load_count():
                return (root / "daemon.log").read_text(encoding="utf-8", errors="replace").count("成功加载配置文件:")
            packet(0)
            old_persistent = next(iter(wait_for(lambda: rendered(200), "old persistent render")))
            packet(1)
            old_shot = next(iter(wait_for(lambda: rendered(200) - {old_persistent}, "old one-shot render")))
            old_count = trace().count("old_render")
            self.assertGreater(old_count, 0)
            loaded = load_count()
            config["orchestration"]["rules"][0]["description"] = "cosmetic"
            temporary = root / "next.json"
            temporary.write_text(json.dumps(config), encoding="utf-8"); os.replace(temporary, path)
            wait_for(lambda: load_count() > loaded, "cosmetic config accepted")
            index = len(events())
            wait_for(lambda: renders_after(index, 200, old_shot), "same old one-shot after cosmetic reload")
            self.assertFalse(destroyed(200, old_shot), "cosmetic edit cancelled old shot")
            self.assertFalse(destroyed(200, old_persistent), "cosmetic edit replaced persistent instance")
            self.assertGreater(trace().count("old_render"), old_count)
            # Permanent regression for the previously uncontrolled input: a valid external
            # packet would make health < 15 false without exclusive fixture input ownership.
            self.assertEqual(request(19897, "/gsi", {"player": {"state": {"health": 100, "round_kills": 1}}})[0], 200)
            self.assertEqual(request(19897, "/api/gsi/simulation")[1]["payload"]["player"]["state"]["health"], 10)
            shutil.copy2(binaries / "automation_reload_new.dll", root / "live.dll")
            self.assertEqual(request(19897, "/api/plugin/reload", {"name": str(root / "live.dll")})[0], 200)
            before = trace()
            new_persistent = next(iter(wait_for(lambda: rendered(100), "new generation persistent render")))
            index = len(events())
            wait_for(lambda: renders_after(index, 200, old_shot) and renders_after(index, 100, new_persistent),
                     "new persistent and original old one-shot rendering together")
            wait_for(lambda: destroyed(200, old_persistent), "old persistent retirement")
            self.assertFalse(destroyed(200, old_shot), "DLL reload cancelled old shot")
            self.assertEqual(rendered(100), {new_persistent}, "duplicate new persistent instances")
            after = trace()
            self.assertGreater(after.count("old_render"), before.count("old_render"), "old transient cancelled by DLL reload")
            self.assertGreater(after.count("new_render"), before.count("new_render"), "persistent did not swap to new DLL")
            config["orchestration"]["rules"][0]["action"]["priority"] = 99
            temporary.write_text(json.dumps(config), encoding="utf-8"); os.replace(temporary, path)
            wait_for(lambda: destroyed(200, old_shot), "semantic edit cancellation of original shot")
            retirement = next(e for e in events() if e["event"] == "destroyed" and e["marker"] == 200
                              and e["instance"] == old_shot)
            self.assertLess(retirement["elapsed"], 10000, "old shot finished naturally instead of being cancelled")
            before = trace(); index = len(events())
            wait_for(lambda: renders_after(index, 100, new_persistent) >= 2, "render ticks after semantic cancellation")
            after = trace()
            self.assertEqual(after.count("old_render"), before.count("old_render"), "semantic edit did not cancel old shot")
            self.assertGreater(after.count("new_render"), before.count("new_render"))
            self.assertEqual(rendered(100), {new_persistent}, "semantic edit duplicated persistent instance")

    def test_studio_publication_failures_retain_applied_pair(self):
        config = {"default_profile": "demo", "profiles": {"demo": {"type": "plugin", "plugin_name": "placeholder"}}, "rules": [],
                  "blockly_effects": {"demo": {"applied_plugin_name": "placeholder"}}}
        def prepare(root, binaries, cfg):
            dll = root / "old.dll"; shutil.copy2(binaries / "studio_old.dll", dll)
            cfg["profiles"]["demo"]["plugin_name"] = str(dll)
            cfg["blockly_effects"]["demo"]["applied_plugin_name"] = str(dll)
        with self.daemon(config, prepare) as (root, binaries, path):
            deadline = time.monotonic() + 8
            while True:
                try:
                    status, data, _ = request(19898, "/api/status")
                    if status == 200:
                        break
                except OSError:
                    pass
                if time.monotonic() > deadline:
                    self.fail("web supervisor did not start publication server")
                time.sleep(.05)
            self.assertTrue(data.get("studio_publish_ready"), "actual MSVC/SDK required for publication acceptance")
            original = path.read_bytes()
            _, _, headers = request(19898, "/api/config")
            revision = headers["ETag"]
            source = (binaries.parent / "studio_stage4" / "studio_one_shot.cpp").read_text(encoding="utf-8")
            def compile(name, code):
                return request(19898, "/api/compile_effect", {"name": name, "code": code})
            self.assertEqual(compile("compile_failure", "this is not valid C++")[0], 400)
            self.assertEqual(path.read_bytes(), original)
            bad = source.replace("studio_one_shot", "bad_abi").replace("return 0x00010000;", "return 2;")
            self.assertEqual(compile("bad_abi", bad)[0], 200)
            self.assertNotEqual(request(19898, "/api/reload_plugin", {"name": "bad_abi", "require_lifecycle": True})[0], 200)
            self.assertEqual(path.read_bytes(), original)
            good = source.replace("studio_one_shot", "accepted_publication")
            self.assertEqual(compile("accepted_publication", good)[0], 200)
            status, loaded, _ = request(19898, "/api/reload_plugin", {"name": "accepted_publication", "require_lifecycle": True})
            self.assertEqual(status, 200); self.assertTrue(loaded["lifecycle_verified"])
            dll = root / "plugins" / "effect_accepted_publication.dll"
            digest = hashlib.sha256(dll.read_bytes()).digest()
            self.assertEqual(compile("accepted_publication", good)[0], 400)
            self.assertEqual(hashlib.sha256(dll.read_bytes()).digest(), digest, "immutable DLL overwritten")
            candidate = json.loads(original)
            candidate["profiles"]["demo"]["plugin_name"] = "accepted_publication"
            candidate["blockly_effects"]["demo"]["applied_plugin_name"] = "accepted_publication"
            self.assertEqual(request(19898, "/api/config", candidate, {"If-Match": '"stale"'})[0], 409)
            self.assertEqual(path.read_bytes(), original, "failed config commit changed applied pair")
            self.assertEqual(request(19898, "/api/config", candidate, {"If-Match": revision})[0], 200)
            self.assertEqual(json.loads(path.read_text())["profiles"]["demo"]["plugin_name"], "accepted_publication")


if __name__ == "__main__":
    unittest.main()
