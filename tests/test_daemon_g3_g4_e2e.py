import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.request
import urllib.error

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(ROOT_DIR, "tests"))

from test_runtime_entrypoints import (
    KEYMAP_FILE, EXAMPLE_CONFIG,
    check_daemon_prerequisites_or_skip,
    get_authoritative_binary,
    get_isolated_env,
    terminate_proc
)

def make_ir_program(r=180, g=20, b=50):
    return {
        "schema_version": 1,
        "entry": 0,
        "variables": 0,
        "slots": 0,
        "publication": {"mode": "continuous", "fade_out_ms": 0},
        "nodes": [
            {
                "kind": "action",
                "op": "fill",
                "color": {
                    "op": "rgb",
                    "args": [
                        {"op": "number", "value": r},
                        {"op": "number", "value": g},
                        {"op": "number", "value": b}
                    ]
                },
                "next": 1
            },
            {"kind": "end"}
        ]
    }

def http_json(url, method="GET", body=None, timeout=3.0):
    headers = {"Content-Type": "application/json"} if body is not None else {}
    data = json.dumps(body).encode("utf-8") if body is not None else None
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        content = resp.read().decode("utf-8")
        return resp.status, json.loads(content) if content else {}

class TestDaemonG3G4E2E(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        os.environ.setdefault("AURA_BIN_DIR", os.path.join(ROOT_DIR, "audit_repros", "studio_platform_build", "native", "Release"))

    def setUp(self):
        check_daemon_prerequisites_or_skip(self)
        self.tmp_dir = tempfile.mkdtemp(prefix="aura_g3_g4_e2e_")
        self.config_path = os.path.join(self.tmp_dir, "config.json")
        with open(EXAMPLE_CONFIG, "r", encoding="utf-8") as f:
            cfg = json.load(f)
        with open(self.config_path, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=2)

        self.binary = get_authoritative_binary("aura_daemon.exe")
        self.env = get_isolated_env(self.tmp_dir)
        self.proc = subprocess.Popen(
            [self.binary, "--dry-run", "--config", self.config_path, "--keymap", KEYMAP_FILE],
            cwd=self.tmp_dir,
            env=self.env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )

        # Wait for daemon to be healthy and responsive on port 19897
        deadline = time.monotonic() + 8.0
        ready = False
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                self.fail(f"daemon exited prematurely with code {self.proc.returncode}")
            try:
                status, data = http_json("http://127.0.0.1:19897/api/runtime/status", timeout=0.5)
                if status == 200 and data.get("status") == "ok":
                    ready = True
                    break
            except Exception:
                time.sleep(0.08)
        if not ready:
            terminate_proc(self.proc)
            self.fail("Daemon did not become healthy within 8 seconds")

    def tearDown(self):
        if hasattr(self, "proc") and self.proc:
            terminate_proc(self.proc)
        if hasattr(self, "tmp_dir") and os.path.isdir(self.tmp_dir):
            shutil.rmtree(self.tmp_dir, ignore_errors=True)

    def test_g4_input_capture_and_compositor_e2e(self):
        # 1. Verify initial status via /api/inputs
        status, inputs = http_json("http://127.0.0.1:19897/api/inputs")
        self.assertEqual(status, 200)
        self.assertEqual(inputs.get("schema_version"), 1)
        self.assertEqual(inputs.get("consent_scope"), "daemon_session")
        self.assertFalse(inputs["audio"]["enabled"])
        self.assertFalse(inputs["screen"]["enabled"])
        self.assertEqual(inputs["audio"]["capture"]["availability"], "disabled")
        self.assertEqual(inputs["screen"]["capture"]["availability"], "disabled")

        # 2. Verify runtime status snapshot includes inputs and compositor render diagnostics
        status, snap = http_json("http://127.0.0.1:19897/api/runtime/status")
        self.assertEqual(status, 200)
        self.assertIn("inputs", snap)
        self.assertIn("render", snap)
        initial_compositor_calls = snap["render"]["compositor_calls"]
        self.assertGreaterEqual(initial_compositor_calls, 0)

        # 3. Enable Audio Capture via POST /api/inputs
        status, res = http_json("http://127.0.0.1:19897/api/inputs", method="POST", body={"audio": True})
        self.assertEqual(status, 200)
        self.assertEqual(res.get("status"), "ok")
        self.assertTrue(res["audio"]["enabled"])
        self.assertFalse(res["screen"]["enabled"])

        # 4. Enable Screen Capture via POST /api/inputs
        status, res = http_json("http://127.0.0.1:19897/api/inputs", method="POST", body={"screen": True})
        self.assertEqual(status, 200)
        self.assertEqual(res.get("status"), "ok")
        self.assertTrue(res["audio"]["enabled"])
        self.assertTrue(res["screen"]["enabled"])

        # Verify runtime status snapshot reflects enabled inputs
        time.sleep(0.1)
        status, snap = http_json("http://127.0.0.1:19897/api/runtime/status")
        self.assertEqual(status, 200)
        self.assertTrue(snap["inputs"]["audio"]["enabled"])
        self.assertTrue(snap["inputs"]["screen"]["enabled"])

        # 5. Disable all inputs via POST /api/inputs
        status, res = http_json("http://127.0.0.1:19897/api/inputs", method="POST", body={"audio": False, "screen": False})
        self.assertEqual(status, 200)
        self.assertEqual(res.get("status"), "ok")
        self.assertFalse(res["audio"]["enabled"])
        self.assertFalse(res["screen"]["enabled"])

        # 6. Verify LayerCompositor execution in the live 25 FPS render loop
        # Wait ~200ms (5 ticks at 25 FPS) and check that compositor_calls increases
        time.sleep(0.2)
        status, snap = http_json("http://127.0.0.1:19897/api/runtime/status")
        self.assertEqual(status, 200)
        later_compositor_calls = snap["render"]["compositor_calls"]
        self.assertGreater(later_compositor_calls, initial_compositor_calls,
                           "Compositor calls must advance with the live daemon render loop")
        self.assertGreater(snap["render"]["rendered_frames"], 0)

    def test_g3_restricted_ir_catalog_full_lifecycle_e2e(self):
        # 1. Initial catalog listing
        status, cat = http_json("http://127.0.0.1:19897/api/programs")
        self.assertEqual(status, 200)
        self.assertEqual(cat.get("schema_version"), 1)
        self.assertEqual(cat.get("entries"), [])
        rev = cat.get("revision", 0)

        prog_id = "0123456789abcdef0123456789abcdef"
        op_install = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        ir_v1 = make_ir_program(200, 30, 40)

        # 2. Install Restricted IR Program
        install_req = {
            "operation": "install",
            "operation_id": op_install,
            "expected_revision": rev,
            "id": prog_id,
            "name": "E2E_Breathing",
            "version": "1.0.0",
            "program": ir_v1
        }
        status, res = http_json("http://127.0.0.1:19897/api/programs", method="POST", body=install_req)
        self.assertEqual(status, 200)
        self.assertEqual(res["status"], "ok")
        self.assertEqual(res["id"], prog_id)
        self.assertFalse(res["enabled"], "Installed programs must default to disabled")
        self.assertFalse(res["removed"])
        hash_v1 = res["active"]
        rev = res["revision"]

        # Verify durable receipt lookup
        status, receipt = http_json(f"http://127.0.0.1:19897/api/programs?operation_id={op_install}")
        self.assertEqual(status, 200)
        self.assertEqual(receipt["active"], hash_v1)

        # 3. Enable the Program
        op_enable = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
        enable_req = {
            "operation": "enable",
            "operation_id": op_enable,
            "expected_revision": rev,
            "id": prog_id
        }
        status, res = http_json("http://127.0.0.1:19897/api/programs", method="POST", body=enable_req)
        self.assertEqual(status, 200)
        self.assertEqual(res["status"], "ok")
        self.assertTrue(res["enabled"], "Program must now be enabled")
        rev = res["revision"]

        # 4. Link to active profile in config and verify daemon renders with the IR program
        with open(self.config_path, "r", encoding="utf-8") as f:
            cfg = json.load(f)
        profile_name = f"ir_{prog_id}"
        cfg["profiles"][profile_name] = {
            "title": "IR Profile",
            "type": "plugin",
            "plugin_name": profile_name
        }
        cfg["default_profile"] = profile_name
        with open(self.config_path, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=2)

        # Wait for daemon config hot-reload and frame boundary application
        deadline = time.monotonic() + 3.0
        program_rendered = False
        while time.monotonic() < deadline:
            status, snap = http_json("http://127.0.0.1:19897/api/runtime/status")
            if status == 200 and snap.get("runtime", {}).get("active_profile") == profile_name:
                if snap.get("render", {}).get("active_program"):
                    program_rendered = True
                    break
            time.sleep(0.05)
        self.assertTrue(program_rendered, "Daemon must render the enabled IR program in the main loop")

        # 5. Update Program to v2.0.0 with different color
        ir_v2 = make_ir_program(10, 220, 30)
        op_update = "cccccccccccccccccccccccccccccccc"
        update_req = {
            "operation": "update",
            "operation_id": op_update,
            "expected_revision": rev,
            "id": prog_id,
            "name": "E2E_Breathing",
            "version": "2.0.0",
            "program": ir_v2
        }
        status, res = http_json("http://127.0.0.1:19897/api/programs", method="POST", body=update_req)
        self.assertEqual(status, 200)
        self.assertEqual(res["status"], "ok")
        self.assertTrue(res["enabled"], "Update preserves enabled state")
        hash_v2 = res["active"]
        self.assertNotEqual(hash_v1, hash_v2)
        rev = res["revision"]

        # 6. Rollback to Version 1.0.0
        op_rollback = "dddddddddddddddddddddddddddddddd"
        rollback_req = {
            "operation": "rollback",
            "operation_id": op_rollback,
            "expected_revision": rev,
            "id": prog_id,
            "hash": hash_v1
        }
        status, res = http_json("http://127.0.0.1:19897/api/programs", method="POST", body=rollback_req)
        self.assertEqual(status, 200)
        self.assertEqual(res["status"], "ok")
        self.assertEqual(res["active"], hash_v1)
        self.assertTrue(res["enabled"], "Rollback preserves enabled state and automatically hot-reloads version 1")
        rev = res["revision"]

        # 7. Uninstall Program
        op_uninstall = "ffffffffffffffffffffffffffffffff"
        uninstall_req = {
            "operation": "uninstall",
            "operation_id": op_uninstall,
            "expected_revision": rev,
            "id": prog_id
        }
        status, res = http_json("http://127.0.0.1:19897/api/programs", method="POST", body=uninstall_req)
        self.assertEqual(status, 200)
        self.assertEqual(res["status"], "ok")
        self.assertTrue(res["removed"])
        self.assertFalse(res["enabled"])

        # Verify entry is marked removed in catalog, but versions remain on disk
        status, cat = http_json("http://127.0.0.1:19897/api/programs")
        self.assertEqual(status, 200)
        self.assertEqual(len(cat["entries"]), 1)
        self.assertTrue(cat["entries"][0]["removed"])
        self.assertFalse(cat["entries"][0]["enabled"])

    def test_web_ui_proxy_inputs_and_programs(self):
        # Start web UI alongside daemon
        web_bin = get_authoritative_binary("aura_web_ui.exe")
        web_proc = subprocess.Popen(
            [web_bin, "--config", self.config_path, "--port", "19898", "--web-root", os.path.join(ROOT_DIR, "web")],
            cwd=self.tmp_dir,
            env=self.env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        try:
            deadline = time.monotonic() + 5.0
            ready = False
            while time.monotonic() < deadline:
                if web_proc.poll() is not None:
                    self.fail("Web UI exited prematurely")
                try:
                    status, data = http_json("http://127.0.0.1:19898/api/status", timeout=0.5)
                    if status == 200:
                        ready = True
                        break
                except Exception:
                    time.sleep(0.08)
            self.assertTrue(ready, "Web UI did not start within 5s")

            # 1. Test GET /api/inputs via port 19898 (proxied to 19897)
            status, inputs = http_json("http://127.0.0.1:19898/api/inputs")
            self.assertEqual(status, 200)
            self.assertEqual(inputs.get("schema_version"), 1)
            self.assertIn("audio", inputs)
            self.assertIn("screen", inputs)

            # 2. Test POST /api/inputs via port 19898 (proxied to 19897)
            status, res = http_json("http://127.0.0.1:19898/api/inputs", method="POST", body={"audio": True})
            self.assertEqual(status, 200)
            self.assertEqual(res.get("status"), "ok")
            self.assertTrue(res["audio"]["enabled"])

            # 3. Test GET /api/programs via port 19898 (proxied to 19897)
            status, cat = http_json("http://127.0.0.1:19898/api/programs")
            self.assertEqual(status, 200)
            self.assertEqual(cat.get("schema_version"), 1)
            self.assertIn("entries", cat)
        finally:
            terminate_proc(web_proc)

if __name__ == "__main__":
    unittest.main()
