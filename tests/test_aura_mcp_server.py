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
                    self.do_GET_path_plugins()
                elif self.path == "/api/preview":
                    self._send_json(200, {"status": "ok"})
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

        def do_GET_path_plugins(self) -> None:
            self._send_json(
                200,
                {
                    "status": "ok",
                    "rescan_calls": state.rescan_calls,
                    "native_plugins": [{"name": "hall_reactive", "has_hall_analog": True}],
                    "bridges": [{"id": "aura-mcp-server", "built": True}],
                },
            )

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

    def test_mcp_stdio_roundtrip_tools_resources_prompts_and_offline(self) -> None:
        init = self._rpc({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2024-11-05"}})
        self.assertEqual(init["result"]["serverInfo"]["name"], "aura-mcp")
        self._rpc({"jsonrpc": "2.0", "method": "notifications/initialized"})

        ping = self._rpc({"jsonrpc": "2.0", "id": 2, "method": "ping"})
        self.assertEqual(ping["result"], {})

        tools = self._rpc({"jsonrpc": "2.0", "id": 3, "method": "tools/list"})["result"]["tools"]
        tool_names = {t["name"] for t in tools}
        self.assertIn("aura_status", tool_names)
        self.assertIn("studio_publish_ir_program", tool_names)
        self.assertIn("aura_hall_telemetry", tool_names)

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
        self.assertIn("ESC", keymap["keys"])

        prompts = self._rpc({"jsonrpc": "2.0", "id": 7, "method": "prompts/list"})["result"]["prompts"]
        self.assertEqual({p["name"] for p in prompts}, {"create_ir_effect", "create_hall_reactive_cpp_plugin"})
        prompt_msg = self._rpc(
            {
                "jsonrpc": "2.0",
                "id": 8,
                "method": "prompts/get",
                "params": {"name": "create_ir_effect", "arguments": {"description": "blue ripple"}},
            }
        )["result"]["messages"][0]["content"]["text"]
        self.assertIn("blue ripple", prompt_msg)

        status_res = json.loads(
            self._rpc({"jsonrpc": "2.0", "id": 9, "method": "tools/call", "params": {"name": "aura_status", "arguments": {}}})[
                "result"
            ]["content"][0]["text"]
        )
        self.assertTrue(status_res["daemon_reachable"])
        self.assertTrue(status_res["studio_reachable"])

        ir_doc = {
            "schema_version": 1,
            "entry": 0,
            "variables": 0,
            "slots": 0,
            "publication": {"mode": "continuous", "fade_out_ms": 0},
            "nodes": [{"kind": "action", "op": "fill", "color": {"op": "rgb", "args": [{"op": "number", "value": 0}, {"op": "number", "value": 120}, {"op": "number", "value": 255}]}, "next": 1}, {"kind": "end"}],
        }
        pub_res = json.loads(
            self._rpc(
                {
                    "jsonrpc": "2.0",
                    "id": 10,
                    "method": "tools/call",
                    "params": {"name": "studio_publish_ir_program", "arguments": {"name": "mcp_blue", "program": ir_doc}},
                }
            )["result"]["content"][0]["text"]
        )
        self.assertTrue(pub_res["enable"]["enabled"])
        self.assertTrue(pub_res["plugin_name"].startswith("ir_"))

        save_res = json.loads(
            self._rpc(
                {
                    "jsonrpc": "2.0",
                    "id": 11,
                    "method": "tools/call",
                    "params": {
                        "name": "studio_save_blockly_project",
                        "arguments": {"project_name": "new_fx", "project": {"name": "new_fx", "json": {}}},
                    },
                }
            )["result"]["content"][0]["text"]
        )
        self.assertEqual(save_res["result"]["status"], "ok")
        self.assertEqual(self.state.if_match_headers, ['"deadbeef01"'])
        self.assertIn("existing_fx", self.state.config["blockly_effects"])
        self.assertIn("new_fx", self.state.config["blockly_effects"])

        plugins_res = json.loads(
            self._rpc(
                {
                    "jsonrpc": "2.0",
                    "id": 12,
                    "method": "tools/call",
                    "params": {"name": "aura_list_plugins", "arguments": {"rescan": True}},
                }
            )["result"]["content"][0]["text"]
        )
        self.assertEqual(plugins_res["rescan_calls"], 1)

        # Verify clean structured error when daemon shuts down mid-session
        self.daemon_httpd.shutdown()
        offline_call = self._rpc(
            {"jsonrpc": "2.0", "id": 13, "method": "tools/call", "params": {"name": "studio_list_programs", "arguments": {}}}
        )["result"]
        self.assertTrue(offline_call["isError"])
        self.assertIn("Connection failed", offline_call["content"][0]["text"])


if __name__ == "__main__":
    unittest.main()
