#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_runtime_entrypoints.py - Entrypoints & Configuration Contract Regression Suite

Validates the runtime behavior of the three application entrypoints:
  1. aura_web_ui.exe
  2. aura_daemon.exe
  3. Aura.exe (Single-file self-extracting launcher)

Key Contracts Verified:
  - Hardware Protection & Dry-Run: Daemon and Launcher lifecycle tests strictly use --dry-run
    to guarantee zero physical hardware access or COM state disruption.
  - Single-Instance & Port Respect: Active daemon instances (Local\\RogFalchionAceHfxDaemonMutex)
    and port conflicts are detected and skipped; never bypasses or hacks the named mutex.
  - Runtime Cache Sandbox: Subprocesses run with isolated LOCALAPPDATA, ensuring tests
    never contaminate or overwrite the host system's real %LOCALAPPDATA%\\Aura\\runtime.
  - Strict Binary Resolution: Single authoritative build directory without heuristic fallback
    or mtime mixing; fails fast if binary is missing or stale relative to source.
  - Full Lifecycle Verification: Validates that processes actually load valid custom schema
    configurations (verified via HTTP response or daemon log matching) without false-passes.
  - Skip-Existing & Default Creation: Guarantees config.json is initialized when missing and
    strictly preserved when already present.
"""

import os
import sys
import json
import time
import shutil
import socket
import ctypes
import tempfile
import unittest
import subprocess
import urllib.request
import urllib.error
from typing import Optional, List, Dict, Any

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
FIXTURE_CONFIG = os.path.join(ROOT_DIR, "tests", "fixtures", "test_config.json")
EXAMPLE_CONFIG = os.path.join(ROOT_DIR, "config.example.json")
KEYMAP_FILE = os.path.join(ROOT_DIR, "calibrated_keymap.json")
REAL_LOCALAPPDATA = os.environ.get("LOCALAPPDATA", "")


def is_daemon_instance_running() -> bool:
    """Check whether an active daemon instance is currently holding the single-instance mutex."""
    SYNCHRONIZE = 0x00100000
    handle = ctypes.windll.kernel32.OpenMutexW(SYNCHRONIZE, False, "Local\\RogFalchionAceHfxDaemonMutex")
    if handle:
        ctypes.windll.kernel32.CloseHandle(handle)
        return True
    return False


def is_port_in_use(port: int) -> bool:
    """Check whether a specific localhost TCP port is currently bound/occupied."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        try:
            s.bind(('127.0.0.1', port))
            return False
        except OSError:
            return True


def check_daemon_prerequisites_or_skip(test_case: unittest.TestCase):
    """Skip lifecycle test if an active daemon instance or port conflict (GSI 19897 / WebUI 19898) is present."""
    if is_daemon_instance_running():
        test_case.skipTest(
            "Active daemon instance detected via Local\\RogFalchionAceHfxDaemonMutex; "
            "skipping live daemon/launcher lifecycle test to respect single-instance protection."
        )
    if is_port_in_use(19897):
        test_case.skipTest(
            "Port 19897 (CS2 GSI) is currently in use; skipping live daemon/launcher test to prevent port conflict."
        )
    if is_port_in_use(19898):
        test_case.skipTest(
            "Port 19898 (WebUI) is currently in use; skipping live daemon/launcher test to prevent port conflict."
        )


def make_valid_custom_config(custom_profile_name: str, custom_profile_title: str) -> Dict[str, Any]:
    """
    Construct a strictly valid configuration based on config.example.json.
    Sets default_profile to a custom profile with a valid schema so RuleEngine::LoadConfig
    validates and loads it successfully without warnings or fallbacks.
    """
    with open(EXAMPLE_CONFIG, "r", encoding="utf-8") as f:
        cfg = json.load(f)

    cfg["profiles"][custom_profile_name] = {
        "title": custom_profile_title,
        "effect": "static",
        "color": [42, 84, 126],
        "brightness": 222,
        "speed": 3
    }
    cfg["default_profile"] = custom_profile_name
    return cfg


def get_authoritative_binary(bin_name: str) -> str:
    """
    Locate binary from the single authoritative build directory.
    Strictly forbids candidate mixing and mtime-based selection heuristics.
    Fails immediately if the binary does not exist or is stale relative to its primary source.
    """
    if os.environ.get("AURA_BIN_DIR"):
        explicit_dir = os.path.abspath(os.environ["AURA_BIN_DIR"])
        target = os.path.join(explicit_dir, bin_name)
        if not os.path.isfile(target):
            raise FileNotFoundError(
                f"Binary '{bin_name}' not found in explicitly configured AURA_BIN_DIR: '{explicit_dir}'"
            )
        return target

    if bin_name == "Aura.exe":
        target = os.path.join(ROOT_DIR, "dist", bin_name)
    else:
        target = os.path.join(ROOT_DIR, "build", "Release", bin_name)

    if not os.path.isfile(target):
        raise FileNotFoundError(
            f"Authoritative binary '{bin_name}' not found at '{target}'. "
            f"Please compile the project before running entrypoint tests."
        )

    source_map = {
        "aura_daemon.exe": os.path.join(ROOT_DIR, "src", "main.cpp"),
        "aura_web_ui.exe": os.path.join(ROOT_DIR, "src", "web", "main_web.cpp"),
        "Aura.exe": os.path.join(ROOT_DIR, "src", "launcher", "launcher_main.cpp"),
    }
    src_file = source_map.get(bin_name)
    if src_file and os.path.isfile(src_file):
        bin_mtime = os.path.getmtime(target)
        src_mtime = os.path.getmtime(src_file)
        if bin_mtime < src_mtime:
            raise RuntimeError(
                f"Stale binary detected! '{target}' (mtime {bin_mtime}) is older than "
                f"source '{src_file}' (mtime {src_mtime}). Please recompile before testing."
            )

    return target


def get_isolated_env(tmp_dir: str) -> Dict[str, str]:
    """
    Construct an environment where LOCALAPPDATA, TEMP, and TMP are redirected
    to isolated subdirectories inside the test's temporary directory.
    Guarantees zero leakage into the user's real %LOCALAPPDATA%\\Aura\\runtime.
    """
    env = dict(os.environ)
    isolated_appdata = os.path.join(tmp_dir, "isolated_localappdata")
    isolated_temp = os.path.join(tmp_dir, "isolated_temp")
    os.makedirs(isolated_appdata, exist_ok=True)
    os.makedirs(isolated_temp, exist_ok=True)
    env["LOCALAPPDATA"] = isolated_appdata
    env["TEMP"] = isolated_temp
    env["TMP"] = isolated_temp
    return env


def run_proc(args: List[str], cwd: str, timeout: float = 6.0, env: Optional[Dict[str, str]] = None) -> subprocess.CompletedProcess:
    """Execute command with isolated LOCALAPPDATA, UTF-8 encoding, and timeout protection."""
    if env is None:
        env = get_isolated_env(cwd)
    try:
        return subprocess.run(
            args,
            cwd=cwd,
            env=env,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout
        )
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout.decode("utf-8", errors="replace") if isinstance(exc.stdout, bytes) else (exc.stdout or "")
        stderr = exc.stderr.decode("utf-8", errors="replace") if isinstance(exc.stderr, bytes) else (exc.stderr or "")
        return subprocess.CompletedProcess(args, returncode=-999, stdout=stdout, stderr=stderr)


def _owned_child_handles(parent_pid):
    """Retain handles solely to wait for this test process's descendants after job teardown."""
    if os.name != "nt":
        return []
    import ctypes
    from ctypes import wintypes
    class Entry(ctypes.Structure):
        _fields_ = [("size",wintypes.DWORD),("usage",wintypes.DWORD),("pid",wintypes.DWORD),
            ("heap",ctypes.c_size_t),("module",wintypes.DWORD),("threads",wintypes.DWORD),
            ("parent",wintypes.DWORD),("priority",wintypes.LONG),("flags",wintypes.DWORD),("exe",wintypes.WCHAR*260)]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.Process32FirstW.argtypes = kernel.Process32NextW.argtypes = [wintypes.HANDLE,ctypes.POINTER(Entry)]
    snapshot = kernel.CreateToolhelp32Snapshot(2,0)
    if snapshot == wintypes.HANDLE(-1).value: return []
    pairs=[]; entry=Entry();entry.size=ctypes.sizeof(entry)
    try:
        valid=kernel.Process32FirstW(snapshot,ctypes.byref(entry))
        while valid:
            pairs.append((entry.pid,entry.parent))
            valid=kernel.Process32NextW(snapshot,ctypes.byref(entry))
    finally: kernel.CloseHandle(snapshot)
    owned={parent_pid}; changed=True
    while changed:
        before=len(owned);owned.update(pid for pid,parent in pairs if parent in owned);changed=len(owned)!=before
    return [handle for pid in owned if pid!=parent_pid if (handle:=kernel.OpenProcess(0x100000,False,pid))]


def terminate_proc(proc: subprocess.Popen, timeout: float = 3.0):
    """Terminate only the test-created process and wait for its job-owned descendants to drain."""
    if proc.poll() is not None:
        return
    children = _owned_child_handles(proc.pid)
    try:
        proc.terminate()
        proc.wait(timeout=timeout)
    except (subprocess.TimeoutExpired, Exception):
        try:
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                           capture_output=True, timeout=3.0)
        except Exception:
            pass
        try:
            proc.kill()
        except Exception:
            pass
    finally:
        if children:
            import ctypes
            from ctypes import wintypes
            kernel=ctypes.WinDLL("kernel32",use_last_error=True)
            kernel.WaitForSingleObject.argtypes=[wintypes.HANDLE,wintypes.DWORD]
            kernel.CloseHandle.argtypes=[wintypes.HANDLE]
            for handle in children:
                try: kernel.WaitForSingleObject(handle,int(timeout*1000))
                finally: kernel.CloseHandle(handle)


def get_free_port() -> int:
    """Acquire an unused TCP port on localhost."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def fetch_http_json(url: str, timeout: float = 2.0) -> Any:
    """Make HTTP GET request and parse JSON response."""
    req = urllib.request.Request(url, headers={"User-Agent": "AuraTest/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        if resp.status != 200:
            raise ValueError(f"HTTP status {resp.status} for {url}")
        return json.loads(resp.read().decode("utf-8"))


def fetch_http_text(url: str, timeout: float = 2.0) -> str:
    """Make HTTP GET request and read UTF-8 text response."""
    req = urllib.request.Request(url, headers={"User-Agent": "AuraTest/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        if resp.status != 200:
            raise ValueError(f"HTTP status {resp.status} for {url}")
        return resp.read().decode("utf-8")


def wait_for_http_ready(port: int, max_wait: float = 5.0, path: str = "/api/status") -> bool:
    """Poll the endpoint until server is healthy and responding."""
    start_time = time.time()
    url = f"http://127.0.0.1:{port}{path}"
    while time.time() - start_time < max_wait:
        try:
            data = fetch_http_json(url, timeout=0.5)
            if data.get("status") == "ok":
                return True
        except Exception:
            pass
        time.sleep(0.1)
    return False


def wait_for_daemon_log_match(log_path: str, expected_snippet: str, proc: subprocess.Popen, timeout: float = 5.0) -> bool:
    """
    Poll the daemon log file until expected_snippet is found,
    while continually verifying that the process has NOT died or exited prematurely.
    Prevents false-pass assertions when process fails to start.
    """
    start_time = time.time()
    while time.time() - start_time < timeout:
        if proc.poll() is not None:
            return False
        if os.path.isfile(log_path):
            try:
                with open(log_path, "r", encoding="utf-8", errors="replace") as f:
                    content = f.read()
                if expected_snippet in content:
                    return True
            except Exception:
                pass
        time.sleep(0.1)
    return False


# =============================================================================
# WebUI Entrypoint Test Suite
# =============================================================================
class TestWebUiEntrypoint(unittest.TestCase):
    """Verifies aura_web_ui CLI parsing, configuration lifecycle, and HTTP responses."""

    @classmethod
    def setUpClass(cls):
        cls.web_exe = get_authoritative_binary("aura_web_ui.exe")

    def setUp(self):
        self.tmp_dir = tempfile.mkdtemp(prefix="aura_test_web_")
        self.env = get_isolated_env(self.tmp_dir)

    def tearDown(self):
        shutil.rmtree(self.tmp_dir, ignore_errors=True)

    def test_help_exits_zero_and_keeps_cwd_clean(self):
        res = run_proc([self.web_exe, "--help"], self.tmp_dir, env=self.env)
        self.assertEqual(res.returncode, 0)
        self.assertTrue("--port" in res.stdout and "--config" in res.stdout)
        self.assertFalse(os.path.exists(os.path.join(self.tmp_dir, "config.json")),
                         "Running --help must not create config.json in CWD")

    def test_missing_port_value_exits_one(self):
        res = run_proc([self.web_exe, "--port"], self.tmp_dir, env=self.env)
        self.assertEqual(res.returncode, 1)
        self.assertIn("--port", res.stderr)

    def test_invalid_port_non_numeric_exits_one(self):
        res = run_proc([self.web_exe, "--port", "abc"], self.tmp_dir, env=self.env)
        self.assertEqual(res.returncode, 1)
        self.assertIn("--port", res.stderr)

    def test_invalid_port_out_of_range_exits_one(self):
        res = run_proc([self.web_exe, "--port", "99999"], self.tmp_dir, env=self.env)
        self.assertEqual(res.returncode, 1)
        self.assertIn("--port", res.stderr)

    def test_missing_config_value_exits_one(self):
        res = run_proc([self.web_exe, "--config"], self.tmp_dir, env=self.env)
        self.assertEqual(res.returncode, 1)
        self.assertIn("--config", res.stderr)

    def test_missing_shutdown_event_value_exits_one(self):
        res = run_proc([self.web_exe, "--shutdown-event"], self.tmp_dir, env=self.env)
        self.assertEqual(res.returncode, 1)
        self.assertIn("--shutdown-event", res.stderr)

    def test_unknown_option_exits_one(self):
        res = run_proc([self.web_exe, "--unknown-test-flag"], self.tmp_dir, env=self.env)
        self.assertEqual(res.returncode, 1)
        self.assertIn("--unknown-test-flag", res.stderr)

    def test_explicit_nonexistent_config_exits_one_and_no_cwd_pollution(self):
        fake_config = os.path.join(self.tmp_dir, "does_not_exist.json")
        res = run_proc([self.web_exe, "--config", fake_config], self.tmp_dir, env=self.env)
        self.assertEqual(res.returncode, 1)
        self.assertFalse(os.path.exists(os.path.join(self.tmp_dir, "config.json")),
                         "Failed explicit config must not create config.json in CWD")

    def test_explicit_existing_config_loaded_without_cwd_pollution(self):
        custom_dir = os.path.join(self.tmp_dir, "subdir")
        os.makedirs(custom_dir, exist_ok=True)
        custom_cfg_path = os.path.join(custom_dir, "explicit.json")
        custom_cfg = make_valid_custom_config("custom_explicit_web_prof", "WebUI Explicit Title 8848")
        with open(custom_cfg_path, "w", encoding="utf-8") as f:
            json.dump(custom_cfg, f)

        port = get_free_port()
        proc = subprocess.Popen([self.web_exe, "--port", str(port), "--config", custom_cfg_path],
                                cwd=self.tmp_dir, env=self.env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, encoding="utf-8", errors="replace")
        try:
            ready = wait_for_http_ready(port)
            self.assertTrue(ready, f"WebUI server failed to start on port {port}")
            cfg_data = fetch_http_json(f"http://127.0.0.1:{port}/api/config")
            # 确认子进程实际加载了显式配置
            self.assertEqual(cfg_data.get("default_profile"), "custom_explicit_web_prof")
            self.assertEqual(
                cfg_data.get("profiles", {}).get("custom_explicit_web_prof", {}).get("title"),
                "WebUI Explicit Title 8848"
            )
        finally:
            terminate_proc(proc)

        self.assertFalse(os.path.exists(os.path.join(self.tmp_dir, "config.json")),
                         "Explicit config must not generate config.json in CWD")

    def test_default_config_creation_and_http_serving(self):
        shutil.copyfile(EXAMPLE_CONFIG, os.path.join(self.tmp_dir, "config.example.json"))
        port = get_free_port()
        proc = subprocess.Popen([self.web_exe, "--port", str(port)], cwd=self.tmp_dir, env=self.env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, encoding="utf-8", errors="replace")
        try:
            ready = wait_for_http_ready(port)
            self.assertTrue(ready, f"WebUI server failed to respond on port {port}")

            # 1. 验证 /api/status 返回合法 JSON 与服务标识
            status_data = fetch_http_json(f"http://127.0.0.1:{port}/api/status")
            self.assertEqual(status_data.get("status"), "ok")
            self.assertEqual(status_data.get("service"), "aura_web_ui")

            # 2. 验证 /api/config 成功读取初始化的配置
            cfg_data = fetch_http_json(f"http://127.0.0.1:{port}/api/config")
            self.assertIn("profiles", cfg_data)

            # 3. 验证 / 根路径分发 HTML 文档
            html_text = fetch_http_text(f"http://127.0.0.1:{port}/")
            self.assertTrue("<!DOCTYPE html>" in html_text or "<html" in html_text,
                            "WebUI root path must serve HTML content")
        finally:
            terminate_proc(proc)

        created_config = os.path.join(self.tmp_dir, "config.json")
        self.assertTrue(os.path.isfile(created_config), "WebUI should have initialized config.json in CWD")

    def test_default_config_skip_existing_preserves_custom_file(self):
        custom_config_path = os.path.join(self.tmp_dir, "config.json")
        custom_cfg = make_valid_custom_config("custom_web_skip_prof", "WebUI Custom Title 12345")
        with open(custom_config_path, "w", encoding="utf-8") as f:
            json.dump(custom_cfg, f)

        shutil.copyfile(EXAMPLE_CONFIG, os.path.join(self.tmp_dir, "config.example.json"))
        port = get_free_port()
        proc = subprocess.Popen([self.web_exe, "--port", str(port)], cwd=self.tmp_dir, env=self.env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, encoding="utf-8", errors="replace")
        try:
            ready = wait_for_http_ready(port)
            self.assertTrue(ready, f"WebUI server failed to respond on port {port}")

            # 确认子进程实际加载并返回既有配置内容
            cfg_data = fetch_http_json(f"http://127.0.0.1:{port}/api/config")
            self.assertEqual(cfg_data.get("default_profile"), "custom_web_skip_prof")
            self.assertEqual(
                cfg_data.get("profiles", {}).get("custom_web_skip_prof", {}).get("title"),
                "WebUI Custom Title 12345"
            )
        finally:
            terminate_proc(proc)

        # 断言磁盘文件未被覆盖
        with open(custom_config_path, "r", encoding="utf-8") as f:
            loaded = json.load(f)
        self.assertEqual(loaded.get("default_profile"), "custom_web_skip_prof")
        self.assertEqual(
            loaded.get("profiles", {}).get("custom_web_skip_prof", {}).get("title"),
            "WebUI Custom Title 12345",
            "Existing config.json must not be overwritten (skip_existing invariant)"
        )


# =============================================================================
# Daemon Entrypoint Test Suite
# =============================================================================
class TestDaemonEntrypoint(unittest.TestCase):
    """Verifies aura_daemon CLI argument parsing, isolation, and configuration behaviors."""

    @classmethod
    def setUpClass(cls):
        cls.daemon_exe = get_authoritative_binary("aura_daemon.exe")

    def setUp(self):
        self.tmp_dir = tempfile.mkdtemp(prefix="aura_test_daemon_")
        self.env = get_isolated_env(self.tmp_dir)

    def tearDown(self):
        shutil.rmtree(self.tmp_dir, ignore_errors=True)

    def test_help_exits_zero_and_keeps_cwd_clean(self):
        res = run_proc([self.daemon_exe, "--help"], self.tmp_dir, env=self.env)
        self.assertEqual(res.returncode, 0)
        self.assertTrue("--test-init" in res.stdout and "--config" in res.stdout)
        self.assertFalse(os.path.exists(os.path.join(self.tmp_dir, "config.json")),
                         "aura_daemon --help must not create config.json in CWD")

    def test_missing_config_value_exits_one(self):
        res = run_proc([self.daemon_exe, "--config"], self.tmp_dir, env=self.env)
        self.assertEqual(res.returncode, 1)
        self.assertIn("--config", res.stderr)

    def test_missing_keymap_value_exits_one(self):
        res = run_proc([self.daemon_exe, "--keymap"], self.tmp_dir, env=self.env)
        self.assertEqual(res.returncode, 1)
        self.assertIn("--keymap", res.stderr)

    def test_missing_test_init_value_exits_one(self):
        res = run_proc([self.daemon_exe, "--test-init"], self.tmp_dir, env=self.env)
        self.assertEqual(res.returncode, 1)
        self.assertIn("--test-init", res.stderr)

    def test_invalid_test_init_value_exits_one(self):
        res = run_proc([self.daemon_exe, "--test-init", "-5"], self.tmp_dir, env=self.env)
        self.assertEqual(res.returncode, 1)
        self.assertIn("--test-init", res.stderr)

    def test_missing_test_stability_value_exits_one(self):
        res = run_proc([self.daemon_exe, "--test-stability"], self.tmp_dir, env=self.env)
        self.assertEqual(res.returncode, 1)
        self.assertIn("--test-stability", res.stderr)

    def test_missing_log_level_value_exits_one(self):
        res = run_proc([self.daemon_exe, "--log-level"], self.tmp_dir, env=self.env)
        self.assertEqual(res.returncode, 1)
        self.assertIn("--log-level", res.stderr)

    def test_invalid_log_level_value_exits_one(self):
        res = run_proc([self.daemon_exe, "--log-level", "super_verbose"], self.tmp_dir, env=self.env)
        self.assertEqual(res.returncode, 1)
        self.assertIn("super_verbose", res.stderr)

    def test_unknown_option_exits_one(self):
        res = run_proc([self.daemon_exe, "--completely-unknown-option"], self.tmp_dir, env=self.env)
        self.assertEqual(res.returncode, 1)
        self.assertIn("--completely-unknown-option", res.stderr)

    def test_explicit_nonexistent_config_exits_one_and_no_cwd_pollution(self):
        fake_config = os.path.join(self.tmp_dir, "missing_config.json")
        res = run_proc([self.daemon_exe, "--dry-run", "--config", fake_config], self.tmp_dir, env=self.env)
        self.assertEqual(res.returncode, 1)
        self.assertFalse(os.path.exists(os.path.join(self.tmp_dir, "config.json")),
                         "aura_daemon failed explicit config must not create config.json in CWD")

    def test_explicit_existing_config_loaded_without_cwd_pollution(self):
        check_daemon_prerequisites_or_skip(self)

        custom_dir = os.path.join(self.tmp_dir, "custom_sub")
        os.makedirs(custom_dir, exist_ok=True)
        custom_cfg_path = os.path.join(custom_dir, "daemon_cfg.json")
        custom_cfg = make_valid_custom_config("daemon_explicit_prof", "Daemon Explicit Title 10086")
        with open(custom_cfg_path, "w", encoding="utf-8") as f:
            json.dump(custom_cfg, f)

        shutil.copyfile(KEYMAP_FILE, os.path.join(self.tmp_dir, "calibrated_keymap.json"))

        # 严格使用 --dry-run 保护物理硬件
        proc = subprocess.Popen([self.daemon_exe, "--dry-run", "--config", custom_cfg_path],
                                cwd=self.tmp_dir, env=self.env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, encoding="utf-8", errors="replace")
        log_path = os.path.join(self.tmp_dir, "aura_daemon.log")
        try:
            # 确认子进程成功启动并实际加载了显式配置中的目标默认方案
            loaded_ok = wait_for_daemon_log_match(log_path, "默认方案: daemon_explicit_prof", proc, timeout=5.0)
            self.assertTrue(loaded_ok, "aura_daemon failed to load explicit configuration (check log)")
        finally:
            terminate_proc(proc)

        self.assertFalse(os.path.exists(os.path.join(self.tmp_dir, "config.json")),
                         "Explicit daemon config must not create config.json in CWD")

    def test_daemon_default_config_creation_when_template_available(self):
        check_daemon_prerequisites_or_skip(self)

        shutil.copyfile(EXAMPLE_CONFIG, os.path.join(self.tmp_dir, "config.example.json"))
        shutil.copyfile(KEYMAP_FILE, os.path.join(self.tmp_dir, "calibrated_keymap.json"))

        self.assertFalse(os.path.exists(os.path.join(self.tmp_dir, "config.json")))

        # 严格使用 --dry-run 保护物理硬件
        proc = subprocess.Popen([self.daemon_exe, "--dry-run"],
                                cwd=self.tmp_dir, env=self.env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, encoding="utf-8", errors="replace")
        log_path = os.path.join(self.tmp_dir, "aura_daemon.log")
        try:
            # 确认子进程实际执行了从模板初始化并在日志中记录成功
            init_ok = wait_for_daemon_log_match(log_path, "已从模板成功初始化默认配置文件", proc, timeout=5.0)
            self.assertTrue(init_ok, "aura_daemon failed to initialize default config from template")
        finally:
            terminate_proc(proc)

        created_config = os.path.join(self.tmp_dir, "config.json")
        self.assertTrue(os.path.isfile(created_config),
                        "aura_daemon must initialize config.json in CWD when template is available")
        with open(created_config, "r", encoding="utf-8") as f:
            data = json.load(f)
        self.assertIn("profiles", data)

    def test_daemon_default_config_skip_existing_preserves_custom_file(self):
        check_daemon_prerequisites_or_skip(self)

        custom_config_path = os.path.join(self.tmp_dir, "config.json")
        custom_cfg = make_valid_custom_config("daemon_skip_prof", "Daemon Preserved Title 776655")
        with open(custom_config_path, "w", encoding="utf-8") as f:
            json.dump(custom_cfg, f)

        shutil.copyfile(EXAMPLE_CONFIG, os.path.join(self.tmp_dir, "config.example.json"))
        shutil.copyfile(KEYMAP_FILE, os.path.join(self.tmp_dir, "calibrated_keymap.json"))

        # 严格使用 --dry-run 保护物理硬件
        proc = subprocess.Popen([self.daemon_exe, "--dry-run"],
                                cwd=self.tmp_dir, env=self.env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, encoding="utf-8", errors="replace")
        log_path = os.path.join(self.tmp_dir, "aura_daemon.log")
        try:
            # 确认子进程实际加载并激活了既有配置中的自定义默认方案
            loaded_ok = wait_for_daemon_log_match(log_path, "默认方案: daemon_skip_prof", proc, timeout=5.0)
            self.assertTrue(loaded_ok, "aura_daemon failed to load custom existing config (check log)")
        finally:
            terminate_proc(proc)

        with open(custom_config_path, "r", encoding="utf-8") as f:
            loaded = json.load(f)
        self.assertEqual(loaded.get("default_profile"), "daemon_skip_prof")
        self.assertEqual(
            loaded.get("profiles", {}).get("daemon_skip_prof", {}).get("title"),
            "Daemon Preserved Title 776655",
            "aura_daemon must not overwrite existing config.json in CWD"
        )

    def test_daemon_runtime_status_endpoint_returns_ok_under_dry_run(self):
        check_daemon_prerequisites_or_skip(self)

        shutil.copyfile(EXAMPLE_CONFIG, os.path.join(self.tmp_dir, "config.example.json"))
        shutil.copyfile(KEYMAP_FILE, os.path.join(self.tmp_dir, "calibrated_keymap.json"))

        # 严格使用 --dry-run 保护物理硬件
        proc = subprocess.Popen([self.daemon_exe, "--dry-run"],
                                cwd=self.tmp_dir, env=self.env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, encoding="utf-8", errors="replace")
        try:
            ready = wait_for_http_ready(19897, max_wait=5.0, path="/api/runtime/status")
            self.assertTrue(ready, "aura_daemon GSI server failed to respond on 19897 /api/runtime/status")

            data = fetch_http_json("http://127.0.0.1:19897/api/runtime/status")
            self.assertEqual(data.get("status"), "ok")
            self.assertEqual(data.get("api_version"), 1)

            # Constraint 3:
            # hardware.connected 表示真实物理硬件连接，dry-run 时必须为 false
            hw = data.get("hardware", {})
            self.assertFalse(hw.get("connected"), "Dry-run mode must report hardware.connected as False")
            self.assertEqual(hw.get("state"), "connected")
            self.assertEqual(hw.get("active_backend"), "dry_run")

            rt = data.get("runtime", {})
            self.assertTrue(rt.get("dry_run"), "Dry-run mode must report runtime.dry_run as True")
            self.assertEqual(rt.get("fps"), 25)
            self.assertTrue(len(rt.get("active_profile", "")) > 0)

            gsi = data.get("gsi", {})
            self.assertIn("active", gsi)
        finally:
            terminate_proc(proc)

    def test_daemon_lighting_endpoints_return_profiles_and_detail_under_dry_run(self):
        check_daemon_prerequisites_or_skip(self)

        shutil.copyfile(EXAMPLE_CONFIG, os.path.join(self.tmp_dir, "config.example.json"))
        shutil.copyfile(KEYMAP_FILE, os.path.join(self.tmp_dir, "calibrated_keymap.json"))

        # 严格使用 --dry-run 保护物理硬件
        proc = subprocess.Popen([self.daemon_exe, "--dry-run"],
                                cwd=self.tmp_dir, env=self.env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, encoding="utf-8", errors="replace")
        try:
            ready = wait_for_http_ready(19897, max_wait=5.0, path="/api/runtime/status")
            self.assertTrue(ready, "aura_daemon GSI server failed to respond on 19897 /api/runtime/status")

            # 1. GET /api/lighting/profiles
            profiles_data = fetch_http_json("http://127.0.0.1:19897/api/lighting/profiles")
            self.assertEqual(profiles_data.get("status"), "ok")
            self.assertEqual(profiles_data.get("api_version"), 1)
            self.assertTrue(len(profiles_data.get("revision", "")) > 0)
            profiles = profiles_data.get("profiles", [])
            self.assertTrue(len(profiles) >= 2)
            names = [p.get("name") for p in profiles]
            self.assertIn("desktop", names)

            # 2. GET /api/lighting/profiles/desktop
            detail_data = fetch_http_json("http://127.0.0.1:19897/api/lighting/profiles/desktop")
            self.assertEqual(detail_data.get("status"), "ok")
            self.assertEqual(detail_data.get("api_version"), 1)
            prof = detail_data.get("profile", {})
            self.assertEqual(prof.get("name"), "desktop")
            self.assertEqual(prof.get("type"), "breathing")
            self.assertTrue(prof.get("supports_period"))
            self.assertEqual(prof.get("period_ms"), 3500)
            self.assertEqual(prof.get("brightness"), 1.0)
            self.assertEqual(prof.get("fps"), 25)
        finally:
            terminate_proc(proc)


# =============================================================================
# Launcher Entrypoint Test Suite (Aura.exe)
# =============================================================================
class TestLauncherEntrypoint(unittest.TestCase):
    """Verifies Aura.exe launcher behaviors with strict runtime cache sandbox isolation."""

    @classmethod
    def setUpClass(cls):
        cls.launcher_exe = get_authoritative_binary("Aura.exe")
        cls.real_runtime_dir = os.path.join(REAL_LOCALAPPDATA, "Aura", "runtime") if REAL_LOCALAPPDATA else ""
        cls.initial_real_runtime_mtime = (
            os.path.getmtime(cls.real_runtime_dir)
            if (cls.real_runtime_dir and os.path.exists(cls.real_runtime_dir))
            else None
        )

    def setUp(self):
        self.tmp_dir = tempfile.mkdtemp(prefix="aura_test_launcher_")
        self.env = get_isolated_env(self.tmp_dir)

    def tearDown(self):
        # 严格验证：真实系统的 %LOCALAPPDATA%\Aura\runtime 绝对不能被测试修改或写入
        if self.real_runtime_dir and os.path.exists(self.real_runtime_dir):
            current_mtime = os.path.getmtime(self.real_runtime_dir)
            self.assertEqual(
                current_mtime,
                self.initial_real_runtime_mtime,
                "CRITICAL: Real %LOCALAPPDATA%\\Aura\\runtime was contaminated during test execution!"
            )
        shutil.rmtree(self.tmp_dir, ignore_errors=True)

    def test_launcher_help_exits_zero_and_keeps_cwd_clean(self):
        res = run_proc([self.launcher_exe, "--help"], self.tmp_dir, env=self.env)
        self.assertEqual(res.returncode, 0)
        self.assertFalse(os.path.exists(os.path.join(self.tmp_dir, "config.json")),
                         "Aura.exe --help must not create config.json in CWD")

        # 验证资源解包严格限制在隔离沙箱目录中
        isolated_runtime = os.path.join(self.env["LOCALAPPDATA"], "Aura", "runtime")
        self.assertTrue(os.path.exists(isolated_runtime),
                        "Launcher should have extracted runtime into the isolated LOCALAPPDATA directory")
        self.assertTrue(os.path.isfile(os.path.join(isolated_runtime, "aura_daemon.exe")),
                        "Unpacked aura_daemon.exe must reside inside isolated sandbox")
        for h in [
            os.path.join("include", "engine", "effect.h"),
            os.path.join("include", "engine", "plugin_interface.h"),
            os.path.join("include", "aura", "aura_types.h"),
            os.path.join("include", "aura", "keymap.h")
        ]:
            self.assertTrue(os.path.isfile(os.path.join(isolated_runtime, h)),
                            f"Unpacked SDK header '{h}' must reside inside isolated sandbox")

    def test_launcher_explicit_nonexistent_config_exits_one(self):
        fake_config = os.path.join(self.tmp_dir, "nonexistent.json")
        res = run_proc([self.launcher_exe, "--dry-run", "--config", fake_config], self.tmp_dir, env=self.env)
        self.assertEqual(res.returncode, 1)
        self.assertFalse(os.path.exists(os.path.join(self.tmp_dir, "config.json")),
                         "Aura.exe --config nonexistent must not create config.json in CWD")

    def test_launcher_explicit_existing_config_loaded_without_cwd_pollution(self):
        check_daemon_prerequisites_or_skip(self)

        custom_dir = os.path.join(self.tmp_dir, "launcher_sub")
        os.makedirs(custom_dir, exist_ok=True)
        custom_cfg_path = os.path.join(custom_dir, "custom_launcher.json")
        custom_cfg = make_valid_custom_config("launcher_explicit_prof", "Launcher Explicit Title 99911")
        with open(custom_cfg_path, "w", encoding="utf-8") as f:
            json.dump(custom_cfg, f)

        # 严格使用 --dry-run 保护物理硬件
        proc = subprocess.Popen([self.launcher_exe, "--dry-run", "--config", custom_cfg_path],
                                cwd=self.tmp_dir, env=self.env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, encoding="utf-8", errors="replace")
        log_path = os.path.join(self.env["LOCALAPPDATA"], "Aura", "aura_daemon.log")
        try:
            # 确认启动器透明转发显式配置给守护进程，并在日志中确认加载成功
            loaded_ok = wait_for_daemon_log_match(log_path, "默认方案: launcher_explicit_prof", proc, timeout=6.0)
            self.assertTrue(loaded_ok, "Launcher failed to pass or load explicit config (check log)")
        finally:
            terminate_proc(proc)

        self.assertFalse(os.path.exists(os.path.join(self.tmp_dir, "config.json")),
                         "Aura.exe with explicit --config must not create config.json in CWD")

    def test_launcher_default_config_creation_when_template_available(self):
        check_daemon_prerequisites_or_skip(self)

        self.assertFalse(os.path.exists(os.path.join(self.tmp_dir, "config.json")))

        # 严格使用 --dry-run 保护物理硬件
        proc = subprocess.Popen([self.launcher_exe, "--dry-run"],
                                cwd=self.tmp_dir, env=self.env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, encoding="utf-8", errors="replace")
        log_path = os.path.join(self.env["LOCALAPPDATA"], "Aura", "aura_daemon.log")
        try:
            # 等待启动器在隔离 LOCALAPPDATA 初始化默认 config.json
            created_config = os.path.join(self.env["LOCALAPPDATA"], "Aura", "config.json")
            for _ in range(40):
                if os.path.isfile(created_config):
                    break
                time.sleep(0.1)

            # 确认守护进程成功启动并加载了生成的默认配置
            loaded_ok = wait_for_daemon_log_match(log_path, "默认方案: desktop", proc, timeout=6.0)
            self.assertTrue(loaded_ok, "Launcher failed to start daemon or daemon failed to load initialized config")
        finally:
            terminate_proc(proc)

        created_config = os.path.join(self.env["LOCALAPPDATA"], "Aura", "config.json")
        self.assertTrue(os.path.isfile(created_config),
                        "Single-file Aura.exe must create canonical config.json in isolated LOCALAPPDATA")
        with open(created_config, "r", encoding="utf-8") as f:
            data = json.load(f)
        self.assertIn("profiles", data)

        # 再次确认解包目录位于临时沙箱内
        isolated_runtime = os.path.join(self.env["LOCALAPPDATA"], "Aura", "runtime")
        self.assertTrue(os.path.isfile(os.path.join(isolated_runtime, "aura_daemon.exe")),
                        "Unpacked aura_daemon.exe must exist in isolated runtime sandbox")

    def test_launcher_default_config_skip_existing_preserves_custom_file(self):
        check_daemon_prerequisites_or_skip(self)

        custom_config_path = os.path.join(self.tmp_dir, "config.json")
        custom_cfg = make_valid_custom_config("launcher_skip_prof", "Launcher Preserved Title 332211")
        with open(custom_config_path, "w", encoding="utf-8") as f:
            json.dump(custom_cfg, f)

        # 严格使用 --dry-run 保护物理硬件
        proc = subprocess.Popen([self.launcher_exe, "--dry-run"],
                                cwd=self.tmp_dir, env=self.env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, encoding="utf-8", errors="replace")
        log_path = os.path.join(self.env["LOCALAPPDATA"], "Aura", "aura_daemon.log")
        try:
            # 确认子进程实际加载并激活了既有配置中的自定义默认方案
            loaded_ok = wait_for_daemon_log_match(log_path, "默认方案: launcher_skip_prof", proc, timeout=6.0)
            self.assertTrue(loaded_ok, "Launcher failed to preserve or load existing config (check log)")
        finally:
            terminate_proc(proc)

        with open(custom_config_path, "r", encoding="utf-8") as f:
            loaded = json.load(f)
        self.assertEqual(loaded.get("default_profile"), "launcher_skip_prof")
        self.assertEqual(
            loaded.get("profiles", {}).get("launcher_skip_prof", {}).get("title"),
            "Launcher Preserved Title 332211",
            "Aura.exe must not overwrite existing config.json in CWD"
        )


def run_suite():
    suite = unittest.TestSuite()
    loader = unittest.TestLoader()
    suite.addTests(loader.loadTestsFromTestCase(TestWebUiEntrypoint))
    suite.addTests(loader.loadTestsFromTestCase(TestDaemonEntrypoint))
    suite.addTests(loader.loadTestsFromTestCase(TestLauncherEntrypoint))

    runner = unittest.TextTestRunner(verbosity=2)
    print("=" * 80)
    print("  ROG Falchion Ace HFX - Runtime Entrypoints & Config Contract Test Suite")
    print("=" * 80)
    result = runner.run(suite)
    return result.wasSuccessful()


if __name__ == "__main__":
    success = run_suite()
    sys.exit(0 if success else 1)
