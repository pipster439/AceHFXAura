import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import unittest

SCRIPT_PATH = Path(r"G:\Aura\plugins\mcp\aura_mcp_server.py")


class _MockState:
    def __init__(self) -> None:
        self.revision = 0
        self.entries: list[dict] = []
        self.etag = "deadbeef01"
        self.config = {
            "fps": 30,
            "blockly_effects": {
                "existing_fx": {"name": "existing_fx", "publication": {"mode": "continuous", "fade_out_ms": 0}, "json": {}}
            },
        }
        self.if_match_headers: list[str] = []
        self.rescan_calls = 0
        self.magnetic_calls: list[tuple[str, dict]] = []
        self.profile_calls: list[tuple[str, dict]] = []
        self.automation_rev = "rev-auto-1"
        self.automation_calls: list[tuple[str, str, dict]] = []


def _make_handler(state: _MockState, is_studio: bool):
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, format, *args):
            return

        def _send_json(self, code: int, body: dict, headers: dict[str, str] | None = None) -> None:
            raw = json.dumps(body, ensure_ascii=False).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(raw)))
            if headers:
                for k, v in headers.items():
                    self.send_header(k, v)
            self.end_headers()
            self.wfile.write(raw)

        def _read_json(self) -> dict:
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length).decode("utf-8") if length else "{}"
            return json.loads(raw)

        def do_GET(self) -> None:
            if not is_studio:
                if self.path == "/api/lighting/global":
                    self._send_json(200, {"status": "ok", "revision": "r1", "fps": 30})
                elif self.path == "/api/plugins/overview":
                    self._send_json(
                        200,
                        {
                            "status": "ok",
                            "native_plugins": [{"name": "hall_reactive", "has_hall_analog": True}],
                            "bridges": [
                                {"id": "wooting-analog-m605", "built": True},
                                {"id": "aura-mcp-server", "built": True},
                            ],
                        },
                    )
                elif self.path.startswith("/api/programs"):
                    self._send_json(200, {"schema_version": 1, "revision": state.revision, "entries": state.entries})
                elif self.path == "/api/magnetic/status":
                    self._send_json(200, {"status": "ok", "connected": True})
                elif self.path == "/api/magnetic/hall/snapshot":
                    self._send_json(200, {"status": "ok", "active": False, "keys": []})
                elif self.path == "/api/magnetic/hall/verify/status":
                    self._send_json(200, {"status": "ok", "state": "Idle", "results": []})
                elif self.path == "/api/device-profiles":
                    self._send_json(
                        200,
                        {
                            "status": "ok",
                            "document_revision": 4,
                            "selected_profile_id": "11111111-1111-1111-1111-111111111111",
                            "profiles": [{"id": "11111111-1111-1111-1111-111111111111", "name": "FPS"}],
                        },
                    )
                elif self.path == "/api/automation/v2/capabilities":
                    self._send_json(200, {"actions": ["activate_profile", "trigger_effect"]})
                elif self.path == "/api/automation/v2/records":
                    self._send_json(200, {"status": "ok", "revision": state.automation_rev, "records": []})
                else:
                    self._send_json(404, {"status": "error", "message": f"Unknown daemon GET {self.path}"})
            else:
                if self.path == "/api/config":
                    self._send_json(200, state.config, headers={"ETag": f'"{state.etag}"'})
                else:
                    self._send_json(404, {"status": "error", "message": f"Unknown studio GET {self.path}"})

        def do_POST(self) -> None:
            body = self._read_json()
            if not is_studio:
                if self.path == "/api/programs":
                    op = body["operation"]
                    prog_id = body["id"]
                    if op in ("install", "update"):
                        state.revision += 1
                        state.entries = [e for e in state.entries if e["id"] != prog_id]
                        state.entries.append(
                            {
                                "id": prog_id,
                                "name": body["name"],
                                "plugin_name": "ir_" + prog_id,
                                "enabled": False,
                                "versions": [{"version": body["version"], "hash": "a" * 64}],
                            }
                        )
                        self._send_json(
                            200,
                            {
                                "status": "ok",
                                "operation_id": body["operation_id"],
                                "id": prog_id,
                                "plugin_name": "ir_" + prog_id,
                                "revision": state.revision,
                                "enabled": False,
                            },
                        )
                    elif op == "enable":
                        state.revision += 1
                        for e in state.entries:
                            if e["id"] == prog_id:
                                e["enabled"] = True
                        self._send_json(
                            200,
                            {
                                "status": "ok",
                                "operation_id": body["operation_id"],
                                "id": prog_id,
                                "plugin_name": "ir_" + prog_id,
                                "revision": state.revision,
                                "enabled": True,
                            },
                        )
                    else:
                        state.revision += 1
                        self._send_json(200, {"status": "ok", "revision": state.revision})
                elif self.path == "/api/plugins/rescan":
                    state.rescan_calls += 1
                    self._send_json(
                        200,
                        {
                            "status": "ok",
                            "rescan_calls": state.rescan_calls,
                            "native_plugins": [{"name": "hall_reactive", "has_hall_analog": True}],
                            "bridges": [{"id": "aura-mcp-server", "built": True}],
                        },
                    )
                elif self.path == "/api/preview":
                    self._send_json(200, {"status": "ok"})
                elif self.path.startswith("/api/magnetic/"):
                    state.magnetic_calls.append((self.path, body))
                    self._send_json(200, {"status": "ok", "path": self.path, "echo": body})
                elif self.path.startswith("/api/device-profiles/"):
                    state.profile_calls.append((self.path, body))
                    self._send_json(200, {"status": "ok", "path": self.path, "outcome": "succeeded", "echo": body})
                elif self.path == "/api/automation/v2/rules":
                    state.automation_calls.append(("POST", self.path, body))
                    state.automation_rev = "rev-auto-2"
                    self._send_json(201, {"status": "ok", "revision": state.automation_rev, "rule": body.get("rule")})
                else:
                    self._send_json(404, {"status": "error", "message": f"Unknown daemon POST {self.path}"})
            else:
                if self.path == "/api/config":
                    if_match = self.headers.get("If-Match", "")
                    state.if_match_headers.append(if_match)
                    if if_match.strip('"') != state.etag:
                        self._send_json(412, {"status": "error", "message": "Precondition failed"})
                        return
                    state.config = body
                    state.etag = "deadbeef02"
                    self._send_json(200, {"status": "ok", "revision": state.etag})
                else:
                    self._send_json(404, {"status": "error", "message": f"Unknown studio POST {self.path}"})

        def do_DELETE(self) -> None:
            body = self._read_json()
            state.automation_calls.append(("DELETE", self.path, body))
            self._send_json(200, {"status": "ok", "revision": "rev-auto-3"})

    return Handler


class AuraMcpServerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.state = _MockState()
        self.daemon_httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), _make_handler(self.state, False))
        self.studio_httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), _make_handler(self.state, True))
        self.daemon_port = self.daemon_httpd.server_address[1]
        self.studio_port = self.studio_httpd.server_address[1]
        threading.Thread(target=self.daemon_httpd.serve_forever, daemon=True).start()
        threading.Thread(target=self.studio_httpd.serve_forever, daemon=True).start()

        env = os.environ.copy()
        env["AURA_DAEMON_URL"] = f"http://127.0.0.1:{self.daemon_port}"
        env["AURA_STUDIO_URL"] = f"http://127.0.0.1:{self.studio_port}"
        env["AURA_ROOT"] = r"G:\Aura"
        self.proc = subprocess.Popen(
            [sys.executable, str(SCRIPT_PATH)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
        )

    def tearDown(self) -> None:
        if self.proc.poll() is None:
            assert self.proc.stdin is not None
            self.proc.stdin.close()
            self.proc.wait(timeout=5)
        self.daemon_httpd.shutdown()
        self.daemon_httpd.server_close()
        self.studio_httpd.shutdown()
        self.studio_httpd.server_close()

    def _rpc(self, msg: dict) -> dict | None:
        assert self.proc.stdin is not None and self.proc.stdout is not None
        line = (json.dumps(msg, ensure_ascii=False) + "\n").encode("utf-8")
        self.proc.stdin.write(line)
        self.proc.stdin.flush()
        if "id" not in msg:
            return None
        resp_raw = self.proc.stdout.readline().decode("utf-8")
        return json.loads(resp_raw)

    def _call_tool(self, req_id: int, name: str, arguments: dict) -> tuple[bool, dict]:
        resp = self._rpc({"jsonrpc": "2.0", "id": req_id, "method": "tools/call", "params": {"name": name, "arguments": arguments}})
        assert resp is not None
        res = resp["result"]
        return bool(res.get("isError")), json.loads(res["content"][0]["text"])

    def test_mcp_stdio_roundtrip_tools_resources_prompts_and_magnetic_extensions(self) -> None:
        init = self._rpc({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2024-11-05"}})
        self.assertEqual(init["result"]["serverInfo"]["name"], "aura-mcp")
        self._rpc({"jsonrpc": "2.0", "method": "notifications/initialized"})

        ping = self._rpc({"jsonrpc": "2.0", "id": 2, "method": "ping"})
        self.assertEqual(ping["result"], {})

        tools = self._rpc({"jsonrpc": "2.0", "id": 3, "method": "tools/list"})["result"]["tools"]
        tool_names = {t["name"] for t in tools}
        self.assertEqual(len(tool_names), 21)
        for expected in (
            "aura_status",
            "studio_publish_ir_program",
            "aura_magnetic_set_actuation",
            "aura_magnetic_set_rapid_trigger",
            "aura_magnetic_set_deadzone",
            "aura_magnetic_set_dks",
            "aura_magnetic_set_speedtap",
            "aura_magnetic_maintenance",
            "aura_hall_verify",
            "aura_device_profiles",
            "aura_automation_rules",
        ):
            self.assertIn(expected, tool_names)

        res_list = self._rpc({"jsonrpc": "2.0", "id": 4, "method": "resources/list"})["result"]["resources"]
        self.assertEqual(len(res_list), 4)

        schema = self._rpc(
            {"jsonrpc": "2.0", "id": 5, "method": "resources/read", "params": {"uri": "aura://schemas/effect-ir-v1"}}
        )["result"]["contents"][0]["text"]
        self.assertIn("schema_version", schema)

        keymap = json.loads(
            self._rpc({"jsonrpc": "2.0", "id": 6, "method": "resources/read", "params": {"uri": "aura://keymap/m605-68"}})[
                "result"
            ]["contents"][0]["text"]
        )
        self.assertEqual(keymap["key_count"], 68)
        self.assertEqual(keymap["keys"]["W"]["logical_id"], 0x0701)
        self.assertEqual(keymap["keys"]["W"]["hid_usage"], 26)

        prompts = self._rpc({"jsonrpc": "2.0", "id": 7, "method": "prompts/list"})["result"]["prompts"]
        self.assertEqual(
            {p["name"] for p in prompts},
            {"create_ir_effect", "create_hall_reactive_cpp_plugin", "tune_magnetic_profile"},
        )
        prompt_msg = self._rpc(
            {
                "jsonrpc": "2.0",
                "id": 8,
                "method": "prompts/get",
                "params": {"name": "tune_magnetic_profile", "arguments": {"game_or_style": "Valorant"}},
            }
        )["result"]["messages"][0]["content"]["text"]
        self.assertIn("Valorant", prompt_msg)

        err, status_res = self._call_tool(9, "aura_status", {})
        self.assertFalse(err)
        self.assertTrue(status_res["daemon_reachable"])
        self.assertTrue(status_res["studio_reachable"])

        ir_doc = {
            "schema_version": 1,
            "entry": 0,
            "variables": 0,
            "slots": 0,
            "publication": {"mode": "continuous", "fade_out_ms": 0},
            "nodes": [
                {"kind": "action", "op": "fill", "color": {"op": "rgb", "args": [{"op": "number", "value": 0}, {"op": "number", "value": 120}, {"op": "number", "value": 255}]}, "next": 1},
                {"kind": "end"},
            ],
        }
        err, pub_res = self._call_tool(10, "studio_publish_ir_program", {"name": "mcp_blue", "program": ir_doc})
        self.assertFalse(err)
        self.assertTrue(pub_res["enable"]["enabled"])
        self.assertTrue(pub_res["plugin_name"].startswith("ir_"))

        err, save_res = self._call_tool(
            11,
            "studio_save_blockly_project",
            {"project_name": "new_fx", "project": {"name": "new_fx", "json": {}}},
        )
        self.assertFalse(err)
        self.assertEqual(save_res["result"]["status"], "ok")
        self.assertEqual(self.state.if_match_headers, ['"deadbeef01"'])

        err, plugins_res = self._call_tool(12, "aura_list_plugins", {"rescan": True})
        self.assertFalse(err)
        self.assertEqual(plugins_res["rescan_calls"], 1)

        # Magnetic switch tools: key-name resolution + batch + RT + Deadzone + DKS (incl. FN guard) + SpeedTap + Verify
        err, ap_res = self._call_tool(13, "aura_magnetic_set_actuation", {"scope": "batch", "keys": ["W", "A", "S", "D"], "mm": 0.3})
        self.assertFalse(err)
        self.assertEqual(ap_res["echo"]["logical_ids"], [0x0701, 0x0602, 0x0702, 0x0301])

        err, rt_res = self._call_tool(
            14,
            "aura_magnetic_set_rapid_trigger",
            {"scope": "batch", "action": "enable", "keys": ["A", "D"], "press_mm": 0.1, "release_mm": 0.1, "resolve_dks": True},
        )
        self.assertFalse(err)
        self.assertEqual(rt_res["echo"]["action"], "enable")

        err, dz_res = self._call_tool(15, "aura_magnetic_set_deadzone", {"scope": "global", "top_mm": 0.1, "bottom_mm": 0.1})
        self.assertFalse(err)
        self.assertEqual(dz_res["echo"], {"top_mm": 0.1, "bottom_mm": 0.1})

        # DKS rejects binding FN as an action target
        dks_slot = lambda kind, key=None: {
            "target": {"kind": kind, **({"key": key} if key else {})},
            "down_start": "Tap",
            "down_end": "Inactive",
            "up_start": "Inactive",
            "up_end": "Inactive",
        }
        err, fn_reject = self._call_tool(
            16,
            "aura_magnetic_set_dks",
            {"action": "set", "key": "CAPS", "start_mm": 0.4, "end_mm": 3.0, "slots": [dks_slot("LogicalKey", "FN"), dks_slot("DefaultSentinel"), dks_slot("DefaultSentinel"), dks_slot("DefaultSentinel")]},
        )
        self.assertTrue(err)
        self.assertIn("FN", fn_reject["error"])

        err, dks_ok = self._call_tool(
            17,
            "aura_magnetic_set_dks",
            {"action": "set", "key": "CAPS", "start_mm": 0.4, "end_mm": 3.0, "slots": [dks_slot("LogicalKey", "L_SHIFT"), dks_slot("DefaultSentinel"), dks_slot("DefaultSentinel"), dks_slot("DefaultSentinel")], "resolve_rt": True},
        )
        self.assertFalse(err)
        self.assertEqual(dks_ok["echo"]["slots"][0]["target"], {"kind": "LogicalKey", "logical_id": 0x0400})

        err, st_ok = self._call_tool(18, "aura_magnetic_set_speedtap", {"action": "pair_enable", "key1": "A", "key2": "D"})
        self.assertFalse(err)
        self.assertEqual(st_ok["echo"], {"key1": 0x0602, "key2": 0x0301})

        err, verify_start = self._call_tool(19, "aura_hall_verify", {"action": "start", "key": "W", "mode": "actuation", "actuation_points": [0.8, 2.0]})
        self.assertFalse(err)
        self.assertEqual(verify_start["echo"]["logical_id"], 0x0701)

        err, prof_act = self._call_tool(20, "aura_device_profiles", {"action": "activate", "profile_id": "11111111-1111-1111-1111-111111111111"})
        self.assertFalse(err)
        self.assertEqual(prof_act["echo"]["expected_revision"], 4)
        self.assertEqual(prof_act["echo"]["reason"], "Manual")

        err, auto_create = self._call_tool(21, "aura_automation_rules", {"action": "create", "rule": {"id": "rule_1", "model": "automation_v2"}})
        self.assertFalse(err)
        self.assertEqual(self.state.automation_calls[-1][2]["expected_revision"], "rev-auto-1")

        # Verify clean structured error when daemon shuts down mid-session
        self.daemon_httpd.shutdown()
        err, offline_res = self._call_tool(22, "studio_list_programs", {})
        self.assertTrue(err)
        self.assertIn("Connection failed", offline_res["error"])


if __name__ == "__main__":
    unittest.main()
