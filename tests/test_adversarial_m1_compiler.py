#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_adversarial_m1_compiler.py
Adversarial Stress Test Suite for WebServer Compiler Endpoint:
  - POST /api/compile_effect (Path Traversal, Invalid C++, Infinite Loops, Payload Bounds)
  - HTTP Header Hardening (Host whitelist, Content-Type, Origin/Referer anti-CSRF)
  - Process Lifecycles & Crash Prevention
"""

import sys
import os
import time
import json
import socket
import http.client
import subprocess
import shutil

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
TEST_PORT = 29898
BASE_URL = f"http://127.0.0.1:{TEST_PORT}"

# Ensure UTF-8 console output
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

g_passes = 0
g_failures = 0

def check(condition: bool, msg: str):
    global g_passes, g_failures
    if condition:
        print(f"  [PASS] {msg}")
        g_passes += 1
    else:
        print(f"  [FAIL] {msg}")
        g_failures += 1

def send_http_request(method: str, path: str, body: bytes = b"", headers: dict = None) -> tuple:
    if headers is None:
        headers = {
            "Host": f"127.0.0.1:{TEST_PORT}",
            "Content-Type": "application/json"
        }
    conn = http.client.HTTPConnection("127.0.0.1", TEST_PORT, timeout=35)
    try:
        conn.request(method, path, body=body, headers=headers)
        res = conn.getresponse()
        resp_body = res.read()
        return res.status, res.headers, resp_body
    finally:
        conn.close()

def wait_for_server(timeout=10.0) -> bool:
    start_t = time.time()
    while time.time() - start_t < timeout:
        try:
            status, _, body = send_http_request("GET", "/api/status", headers={"Host": f"127.0.0.1:{TEST_PORT}"})
            if status == 200:
                data = json.loads(body.decode("utf-8"))
                if data.get("status") == "ok":
                    return True
        except Exception:
            pass
        time.sleep(0.2)
    return False

def main():
    global g_passes, g_failures
    print("=========================================================")
    print("  Adversarial Stress Harness: WebServer Compiler Endpoint")
    print("=========================================================\n")

    exe_path = os.path.join(ROOT_DIR, "aura_web_ui.exe")
    if not os.path.exists(exe_path):
        exe_path = os.path.join(ROOT_DIR, "build", "Release", "aura_web_ui.exe")
    if not os.path.exists(exe_path):
        print(f"[FATAL] Cannot locate aura_web_ui.exe at {exe_path}")
        return 1

    config_path = os.path.join(ROOT_DIR, "tests", "fixtures", "test_config.json")
    if not os.path.exists(config_path):
        config_path = os.path.join(ROOT_DIR, "config.example.json")

    print(f"Launching aura_web_ui.exe on port {TEST_PORT}...")
    proc = subprocess.Popen(
        [exe_path, "--port", str(TEST_PORT), "--config", config_path],
        cwd=ROOT_DIR,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    try:
        ready = wait_for_server(10.0)
        check(ready, f"Server started and responded on port {TEST_PORT}")
        if not ready:
            print("[FATAL] Server failed to start within timeout.")
            return 1

        # ---------------------------------------------------------------------
        # Category 1: Path Traversal & Name Injection Attacks
        # ---------------------------------------------------------------------
        print("\n[Category 1] Path Traversal & Name Injection Attacks...")
        traversal_names = [
            ("../escape", "Directory traversal with ../"),
            ("..\\escape", "Directory traversal with ..\\"),
            ("../../windows/system32/calc", "Deep directory traversal"),
            ("foo/bar", "Subdirectory slash"),
            ("foo\\bar", "Subdirectory backslash"),
            ("effect_../../escape", "Prefix bypass attempt with ../"),
            ("my effect", "Whitespace in name"),
            ("test;calc", "Command separator semicolon"),
            ("test&dir", "Command separator ampersand"),
            ("test|whoami", "Command separator pipe"),
            ("test`calc`", "Backtick command injection"),
            ("test$calc", "Dollar sign variable injection"),
            ("test\x00extra", "Null byte injection"),
            ("测试光效", "Non-ASCII Unicode characters"),
            ("a" * 65, "Length exceeding 64 characters"),
            ("", "Empty plugin name")
        ]

        dummy_cpp = R"""
        #include "engine/effect.h"
        class DummyEffect : public aura::Effect {
        public:
            DummyEffect() : aura::Effect("dummy") {}
            void Render(uint64_t, aura::FrameBuffer&, const aura::Keymap&) override {}
        };
        """

        for name, desc in traversal_names:
            payload = json.dumps({"name": name, "code": dummy_cpp}).encode("utf-8")
            status, _, body = send_http_request("POST", "/api/compile_effect", body=payload)
            check(status == 400, f"Rejected invalid/malicious name: {desc} (HTTP {status})")
            if status == 400:
                try:
                    res_json = json.loads(body.decode("utf-8"))
                    check(res_json.get("success") is False, f"Response contains success: false for {desc}")
                except Exception as e:
                    check(False, f"Response body is valid JSON for {desc}: {e}")

        # Verify no rogue files created outside plugins/
        check(not os.path.exists(os.path.join(ROOT_DIR, "escape.dll")), "No escape.dll created in root")
        check(not os.path.exists(os.path.join(ROOT_DIR, "escape.cpp")), "No escape.cpp created in root")

        # ---------------------------------------------------------------------
        # Category 2: Payload Bounds & Malformed Request Bodies
        # ---------------------------------------------------------------------
        print("\n[Category 2] Payload Bounds & Malformed Bodies...")
        # 2.1: Empty body
        status, _, body = send_http_request("POST", "/api/compile_effect", body=b"")
        check(status == 400, f"Empty body rejected with 400 (got {status})")

        # 2.2: Malformed JSON syntax
        status, _, body = send_http_request("POST", "/api/compile_effect", body=b'{"name": "test", code:')
        check(status == 400, f"Malformed JSON syntax rejected with 400 (got {status})")

        # 2.3: Missing name field
        status, _, body = send_http_request("POST", "/api/compile_effect", body=json.dumps({"code": dummy_cpp}).encode("utf-8"))
        check(status == 400, f"Missing name field rejected with 400 (got {status})")

        # 2.4: Missing code field
        status, _, body = send_http_request("POST", "/api/compile_effect", body=json.dumps({"name": "test_missing_code"}).encode("utf-8"))
        check(status == 400, f"Missing code field rejected with 400 (got {status})")

        # 2.5: Empty code string
        status, _, body = send_http_request("POST", "/api/compile_effect", body=json.dumps({"name": "test_empty_code", "code": ""}).encode("utf-8"))
        check(status == 400, f"Empty code string rejected with 400 (got {status})")

        # 2.6: Payload exceeding 1MB (1024 * 1024 bytes) -> HTTP 413
        oversized_code = "// " + ("A" * (1024 * 1024 + 10000))
        oversized_payload = json.dumps({"name": "oversized_effect", "code": oversized_code}).encode("utf-8")
        status, _, body = send_http_request("POST", "/api/compile_effect", body=oversized_payload)
        check(status == 413, f"Oversized payload (>1MB) rejected with HTTP 413 (got {status})")

        # ---------------------------------------------------------------------
        # Category 3: Invalid C++ Code (Syntax, Semantic, Linker Errors)
        # ---------------------------------------------------------------------
        print("\n[Category 3] Invalid C++ Code Compilation...")
        # 3.1: Syntax Error
        syntax_err_cpp = "int main( { return 0; }"
        payload = json.dumps({"name": "test_syntax_err", "code": syntax_err_cpp}).encode("utf-8")
        status, _, body = send_http_request("POST", "/api/compile_effect", body=payload)
        check(status == 400, f"C++ syntax error returns HTTP 400 (got {status})")
        if status == 400:
            res_json = json.loads(body.decode("utf-8"))
            check(res_json.get("success") is False, "success flag is false")
            check("compiler_output" in res_json or "log" in res_json, "compiler_output / log contains compiler message")
            log_str = res_json.get("compiler_output", res_json.get("log", ""))
            check("error" in log_str.lower() or "c2" in log_str.lower(), "compiler log reports syntax error")

        # 3.2: Linker Error (Unresolved external symbol)
        linker_err_cpp = R"""
        extern void non_existent_external_function_xyz();
        void test_fn() { non_existent_external_function_xyz(); }
        """
        payload = json.dumps({"name": "test_linker_err", "code": linker_err_cpp}).encode("utf-8")
        status, _, body = send_http_request("POST", "/api/compile_effect", body=payload)
        check(status == 400, f"Unresolved external symbol returns HTTP 400 (got {status})")
        if status == 400:
            res_json = json.loads(body.decode("utf-8"))
            log_str = res_json.get("compiler_output", res_json.get("log", ""))
            check("lnk" in log_str.lower() or "error" in log_str.lower(), "compiler log reports linker/compile error")

        # 3.3: Missing Includes / Unknown Types
        missing_inc_cpp = R"""
        void test_fn() {
            NonExistentTypeVariable var;
        }
        """
        payload = json.dumps({"name": "test_missing_type", "code": missing_inc_cpp}).encode("utf-8")
        status, _, body = send_http_request("POST", "/api/compile_effect", body=payload)
        check(status == 400, f"Missing include / unknown type returns HTTP 400 (got {status})")

        # ---------------------------------------------------------------------
        # Category 4: Infinite Compilation Loop / Heavy Recursion Handling
        # ---------------------------------------------------------------------
        print("\n[Category 4] Heavy Metaprogramming & Watchdog Safety...")
        # Recursive template exceeding MSVC default limits
        heavy_template_cpp = R"""
        template<int N> struct RecurseLoop {
            enum { val = RecurseLoop<N-1>::val + RecurseLoop<N-2>::val };
        };
        template struct RecurseLoop<10000>;
        """
        payload = json.dumps({"name": "test_heavy_recurse", "code": heavy_template_cpp}).encode("utf-8")
        t0 = time.time()
        status, _, body = send_http_request("POST", "/api/compile_effect", body=payload)
        t_elapsed = time.time() - t0
        check(status == 400, f"Heavy recursive template failed safely without crashing server (got HTTP {status} in {t_elapsed:.2f}s)")
        # Verify server is still alive and responsive after heavy compilation
        status_check, _, body_check = send_http_request("GET", "/api/status", headers={"Host": f"127.0.0.1:{TEST_PORT}"})
        check(status_check == 200, "Server remains responsive on /api/status after heavy compilation")

        # ---------------------------------------------------------------------
        # Category 5: Valid C++ Plugin Compilation & DLL Verification
        # ---------------------------------------------------------------------
        print("\n[Category 5] Valid Plugin Compilation & DLL Generation...")
        valid_cpp = R"""
        #include "engine/effect.h"
        #include "engine/plugin_interface.h"
        #include <algorithm>

        class TestValidAdversarialEffect : public aura::Effect {
        public:
            TestValidAdversarialEffect() : aura::Effect("adv_valid_effect") {}
            void Render(uint64_t elapsed_ms, aura::FrameBuffer& out_frame, const aura::Keymap& keymap) override {
                double safe_bound = std::max(0.0, std::min(255.0, static_cast<double>(elapsed_ms % 255)));
                out_frame.Fill(aura::ColorRGB(static_cast<uint8_t>(safe_bound), 128, 64));
            }
        };

        extern "C" {
            __declspec(dllexport) uint32_t AuraGetPluginApiVersion() { return 1; }
            __declspec(dllexport) const char* AuraGetEffectName() { return "adv_valid_effect"; }
            __declspec(dllexport) aura::Effect* AuraCreateEffect() { return new TestValidAdversarialEffect(); }
            __declspec(dllexport) void AuraDestroyEffect(aura::Effect* effect) { delete effect; }
        }
        """
        payload = json.dumps({"name": "adv_valid_effect", "code": valid_cpp}).encode("utf-8")
        status, _, body = send_http_request("POST", "/api/compile_effect", body=payload)
        check(status == 200, f"Valid C++ plugin compiles successfully with HTTP 200 (got {status})")
        if status == 200:
            res_json = json.loads(body.decode("utf-8"))
            check(res_json.get("success") is True, "Response indicates success: true")
            dll_path = res_json.get("dll_path", res_json.get("plugin_path", ""))
            check(bool(dll_path), f"Response returned dll_path: {dll_path}")
            full_dll = os.path.join(ROOT_DIR, dll_path)
            check(os.path.exists(full_dll), f"Target DLL exists on disk: {full_dll}")

        # ---------------------------------------------------------------------
        # Category 6: HTTP Security Headers Hardening (Anti-CSRF / DNS Rebinding)
        # ---------------------------------------------------------------------
        print("\n[Category 6] HTTP Security Headers Hardening...")
        # 6.1: Forged Host header (DNS rebinding)
        status, _, _ = send_http_request(
            "POST", "/api/compile_effect",
            body=json.dumps({"name": "evil", "code": "int x = 1;"}).encode("utf-8"),
            headers={"Host": "attacker-dns-rebind.com", "Content-Type": "application/json"}
        )
        check(status == 403, f"Forged Host header blocked with HTTP 403 (got {status})")

        # 6.2: Invalid Content-Type (Form submission CSRF)
        status, _, _ = send_http_request(
            "POST", "/api/compile_effect",
            body=b"name=evil&code=int+x=1;",
            headers={"Host": f"127.0.0.1:{TEST_PORT}", "Content-Type": "application/x-www-form-urlencoded"}
        )
        check(status == 415, f"Non-JSON Content-Type blocked with HTTP 415 (got {status})")

        # 6.3: Untrusted Origin header
        status, _, _ = send_http_request(
            "POST", "/api/compile_effect",
            body=json.dumps({"name": "evil", "code": "int x = 1;"}).encode("utf-8"),
            headers={"Host": f"127.0.0.1:{TEST_PORT}", "Content-Type": "application/json", "Origin": "http://evil-attacker.com"}
        )
        check(status == 403, f"Untrusted Origin header blocked with HTTP 403 (got {status})")

        # 6.4: Literal 'null' Origin (Sandboxed iframe)
        status, _, _ = send_http_request(
            "POST", "/api/compile_effect",
            body=json.dumps({"name": "evil", "code": "int x = 1;"}).encode("utf-8"),
            headers={"Host": f"127.0.0.1:{TEST_PORT}", "Content-Type": "application/json", "Origin": "null"}
        )
        check(status == 403, f"Literal 'null' Origin blocked with HTTP 403 (got {status})")

        # 6.5: Legitimate Origin (127.0.0.1:19898 or localhost)
        status, _, _ = send_http_request(
            "POST", "/api/compile_effect",
            body=json.dumps({"name": "test_legit_origin", "code": valid_cpp}).encode("utf-8"),
            headers={"Host": f"127.0.0.1:{TEST_PORT}", "Content-Type": "application/json", "Origin": f"http://127.0.0.1:{TEST_PORT}"}
        )
        check(status == 200, f"Legitimate Origin allowed with HTTP 200 (got {status})")

    finally:
        print("\nShutting down test aura_web_ui server...")
        proc.terminate()
        try:
            proc.wait(timeout=3.0)
        except Exception:
            proc.kill()
        print("Server shutdown complete.")

    print("\n=========================================================")
    print(f"  Total Checks: {g_passes + g_failures} | Passed: {g_passes} | Failed: {g_failures}")
    if g_failures == 0:
        print("  [SUCCESS] All WebServer Compiler Adversarial Checks Passed!")
    else:
        print(f"  [FAILURE] {g_failures} checks failed!")
    print("=========================================================\n")

    return 0 if g_failures == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
