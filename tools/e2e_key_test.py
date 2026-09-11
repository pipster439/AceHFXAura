#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
端到端验证：真实按键 -> 灯效响应。

区别于 smoke_test_effects.py（只验证各灯效能渲染、空闲不误触发），本脚本通过 Win32
SendInput 注入**真实系统按键事件**，验证完整链路：

    SendInput -> WH_KEYBOARD_LL 钩子 -> KeyInputHub -> Reactive/Ripple 渲染 -> FrameBuffer

判定依据：--dry-run 模式下 AuraAdapter::PushFrame 每秒打印「活跃通道数 N/128」以及
前 4 个非零通道的 LED ID。配置 bg=[0,0,0] 时：
  - 空闲阶段 N 必须为 0        （无自动误触发）
  - 按键阶段 N 必须 >0，且点亮 ID 必须与被按下的键在 calibrated_keymap.json 中的
    led_id 对应               （真实按键确实驱动了灯效）

为此特意挑选：
  - 'A' / 'SPACE'  : 旧前端 demoList 里的键（曾经会自己乱闪）
  - 'J' / 'Z'      : 旧前端 demoList 里**没有**的键（曾经"其余按键均无响应"）
"""
import ctypes
import json
import os
import re
import signal
import subprocess
import sys
import time
from ctypes import wintypes

# 引入 tools/py 共享模块
_TOOLS_PY = os.path.abspath(os.path.join(os.path.dirname(__file__), "tools", "py"))
if _TOOLS_PY not in sys.path:
    sys.path.insert(0, _TOOLS_PY)

import aura_hal
from aura_hal import (
    find_calibrated_keymap_path,
    load_calibrated_keymap,
    get_calibrated_led_map,
    get_calibrated_name_map,
)

ROOT = os.path.dirname(os.path.abspath(__file__))
_candidate_daemons = [
    os.environ.get("AURA_DAEMON_PATH", ""),
    os.path.join("G:/Aura-build-verify", "Release", "aura_daemon.exe"),
    os.path.join(ROOT, "build", "Release", "aura_daemon.exe"),
    os.path.join(ROOT, "aura_daemon.exe"),
]
DAEMON = next((d for d in _candidate_daemons if d and os.path.exists(d)), os.path.join(ROOT, "build", "Release", "aura_daemon.exe"))
KEYMAP = find_calibrated_keymap_path()
TMP_CFG = os.path.join(ROOT, "e2e_test_config.json")

# ---------------- Win32 SendInput ----------------
KEYEVENTF_KEYUP = 0x0002
INPUT_KEYBOARD = 1
ULONG_PTR = ctypes.c_ulonglong if ctypes.sizeof(ctypes.c_void_p) == 8 else ctypes.c_ulong


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [
        ("wVk", wintypes.WORD),
        ("wScan", wintypes.WORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ULONG_PTR),
    ]


class INPUTUNION(ctypes.Union):
    # x64 下 union 的真实尺寸由最大的成员 MOUSEINPUT(32 字节) 决定，
    # 因此必须显式补齐到 32 字节，否则 sizeof(INPUT) 会算成 32 而非正确的 40，
    # SendInput 将以 ERROR_INVALID_PARAMETER(87) 失败。
    _fields_ = [
        ("ki", KEYBDINPUT),
        ("pad", ctypes.c_ubyte * 32),
    ]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wintypes.DWORD), ("u", INPUTUNION)]


assert ctypes.sizeof(INPUT) == 40, f"sizeof(INPUT)={ctypes.sizeof(INPUT)}，x64 下应为 40"


_user32 = ctypes.windll.user32
_user32.SendInput.argtypes = (wintypes.UINT, ctypes.POINTER(INPUT), ctypes.c_int)
_user32.SendInput.restype = wintypes.UINT


def send_key(vk: int):
    """发送一次完整的按下+抬起。"""
    down = INPUT()
    down.type = INPUT_KEYBOARD
    down.u.ki.wVk = vk
    down.u.ki.dwFlags = 0
    up = INPUT()
    up.type = INPUT_KEYBOARD
    up.u.ki.wVk = vk
    up.u.ki.dwFlags = KEYEVENTF_KEYUP
    arr = (INPUT * 2)(down, up)
    return _user32.SendInput(2, arr, ctypes.sizeof(INPUT))


def park_focus_on_desktop():
    """把前台窗口切到桌面，避免测试按键被输入到当前终端。返回原控制台句柄。"""
    console = ctypes.windll.kernel32.GetConsoleWindow()
    if console:
        _user32.ShowWindow(console, 6)  # SW_MINIMIZE
    hwnd = _user32.FindWindowW("Progman", None)
    if hwnd:
        _user32.SetForegroundWindow(hwnd)
    return console


def restore_focus(console):
    if console:
        _user32.ShowWindow(console, 9)  # SW_RESTORE


# ---------------- 键位映射 (基于 tools.py.aura_hal 权威标定) ----------------
_km = load_calibrated_keymap(KEYMAP)
KEYS = _km.get("keys", {})
LED_TO_NAME = get_calibrated_name_map(KEYMAP)
NAME_TO_LED = get_calibrated_led_map(KEYMAP)

VK = {
    "A": 0x41, "J": 0x4A, "Z": 0x5A, "SPACE": 0x20,
}

FRAME_RE = re.compile(r"\[Dry-Run 效果帧 #(\d+)\] 活跃通道数: (\d+)/128 \|(.*)")
LED_RE = re.compile(r"ID (\d+)->RGB\((\d+),(\d+),(\d+)\)")


def write_config(effect: dict):
    conf = {"default_profile": "t", "profiles": {"t": effect}, "rules": []}
    with open(TMP_CFG, "w", encoding="utf-8") as f:
        json.dump(conf, f, ensure_ascii=False, indent=2)


def start_daemon():
    p = subprocess.Popen(
        [DAEMON, "--dry-run", "--config", TMP_CFG, "--keymap", KEYMAP],
        cwd=ROOT,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace",
        creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
    )
    return p


def stop_daemon(p):
    try:
        p.send_signal(signal.CTRL_BREAK_EVENT)
    except Exception:
        pass
    try:
        out, _ = p.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        p.kill()
        out, _ = p.communicate()
    if p.stdout:
        p.stdout.close()
    return out or ""


def parse_frames(text):
    frames = []
    for line in text.splitlines():
        m = FRAME_RE.search(line)
        if m:
            count = int(m.group(2))
            ids = [int(i) for i, r, g, b in LED_RE.findall(m.group(3))]
            frames.append((int(m.group(1)), count, ids))
    return frames


def probe(effect: dict, key_name: str):
    """对某个灯效做一次「空闲 -> 按键 -> 衰减」探针。

    时间轴（25 FPS，日志在帧序 %25==1 时打印，即 1/26/51/76/101/126/151/176）：
        t=0.0~3.5s  空闲    -> 帧 #1(0s) #26(1s) #51(2s) #76(3s)   期望 0 通道
        t=3.5~6.0s  连续按键 -> 帧 #101(4s) #126(5s) #151(6s)       期望 >0 通道
        t=6.0~8.0s  停止    -> 帧 #176(7s)                          观察衰减
    """
    write_config(effect)
    p = start_daemon()
    console = None
    try:
        time.sleep(1.0)
        console = park_focus_on_desktop()

        # 阶段 1：空闲至 3.5s
        time.sleep(2.5)

        # 阶段 2：连续按键脉冲 2.5 秒（保证每秒采样都能命中）
        vk = VK[key_name]
        deadline = time.time() + 2.5
        sent = 0
        failed = 0
        while time.time() < deadline:
            if send_key(vk) == 2:
                sent += 1
            else:
                failed += 1
            time.sleep(0.12)
        inject_ok = sent > 0 and failed == 0

        # 阶段 3：停止按键，观察衰减
        time.sleep(2.0)
    finally:
        out = stop_daemon(p)
        if console:
            restore_focus(console)

    frames = parse_frames(out)
    hook_ok = "WH_KEYBOARD_LL) 注册成功" in out
    return frames, hook_ok, out, inject_ok, sent, failed


def main():
    print("=" * 80)
    print("Aura 端到端按键链路验证 (真实 SendInput -> 钩子 -> 灯效)")
    print("=" * 80)
    print(f"键位映射：{len(KEYS)} 键，LED ID 范围 {min(NAME_TO_LED.values())}~{max(NAME_TO_LED.values())}")
    print()

    reactive_cfg = {
        "type": "reactive", "bg": [0, 0, 0],
        "color": [255, 25, 41], "period_ms": 4000,
    }
    ripple_cfg = {
        "type": "ripple", "bg": [0, 0, 0],
        "color": [0, 240, 255], "period_ms": 4000,
    }

    results = []

    # --- 测试 1：reactive，旧 demoList 内的键 A ---
    # --- 测试 2：reactive，旧 demoList 外的键 J（曾经"其余按键均无响应"）---
    for key_name, note in [("A", "旧 demoList 内"), ("J", "旧 demoList 外 —— 验证'其余按键无响应'已修复"),
                           ("Z", "旧 demoList 外"), ("SPACE", "旧 demoList 内")]:
        frames, hook_ok, raw, inject_ok, sent, failed = probe(reactive_cfg, key_name)
        expect_led = NAME_TO_LED[key_name]

        if not frames:
            results.append((f"reactive/{key_name}", False, "无帧输出"))
            print(f"[FAIL] reactive 按键 {key_name:<6} ({note}) —— 无帧输出")
            continue

        counts = [c for _, c, _ in frames]
        # 前 3 秒空闲：帧序号 1~75，取前 2 个采样点（#1、#26）
        idle_counts = [c for fno, c, _ in frames if fno <= 76]
        # 按键阶段：帧序号 > 76
        press_frames = [(fno, c, ids) for fno, c, ids in frames if 76 < fno <= 151]
        press_counts = [c for _, c, _ in press_frames]
        lit_ids = sorted({i for _, _, ids in press_frames for i in ids})

        ok = True
        msgs = []
        if any(c != 0 for c in idle_counts):
            ok = False
            msgs.append(f"空闲误触发! 帧计数={idle_counts}")
        else:
            msgs.append(f"空闲静默 {idle_counts}")

        if not press_counts or max(press_counts) == 0:
            ok = False
            msgs.append("按键无响应! 按压阶段 0 通道点亮")
        else:
            hit = expect_led in lit_ids
            if not hit:
                ok = False
            names = [LED_TO_NAME.get(i, f"?{i}") for i in lit_ids]
            msgs.append(f"按压点亮 {max(press_counts)} 通道, LED{lit_ids}={names}")
            msgs.append(f"{key_name}(LED{expect_led}) " + ("命中" if hit else "未命中"))

        if not hook_ok:
            ok = False
            msgs.append("钩子未注册")

        if not inject_ok:
            ok = False
            msgs.append(f"按键注入失败! 成功{sent}次/失败{failed}次 (测试环境问题，非产品缺陷)")
        else:
            msgs.append(f"SendInput 注入 {sent} 次全部成功")

        status = "PASS" if ok else "FAIL"
        print(f"[{status}] reactive 按键 {key_name:<6} ({note})")
        print(f"        " + " | ".join(msgs))
        results.append((f"reactive/{key_name}", ok, "; ".join(msgs)))

    print()

    # --- 测试 3：ripple 涟漪扩散（应点亮一圈，不止一个通道）---
    frames, hook_ok, raw, inject_ok, sent, failed = probe(ripple_cfg, "SPACE")
    if not frames:
        results.append(("ripple/SPACE", False, "无帧输出"))
        print("[FAIL] ripple 按键 SPACE —— 无帧输出")
    else:
        idle_counts = [c for fno, c, _ in frames if fno <= 76]
        press_frames = [(fno, c, ids) for fno, c, ids in frames if 76 < fno <= 151]
        press_counts = [c for _, c, _ in press_frames]
        space_led = NAME_TO_LED["SPACE"]
        lit_ids = sorted({i for _, _, ids in press_frames for i in ids})

        ok = True
        msgs = []
        if any(c != 0 for c in idle_counts):
            ok = False
            msgs.append(f"空闲误触发! {idle_counts}")
        else:
            msgs.append(f"空闲静默 {idle_counts}")

        if not press_counts or max(press_counts) == 0:
            ok = False
            msgs.append("按键无响应!")
        else:
            msgs.append(f"按压峰值 {max(press_counts)} 通道点亮")
            if max(press_counts) >= 2:
                msgs.append("呈涟漪扩散形态 (>1 通道)")
            else:
                msgs.append(f"仅 {max(press_counts)} 通道，扩散不明显")
            msgs.append(f"涉及 LED: {lit_ids}")

        if not hook_ok:
            ok = False
            msgs.append("钩子未注册")

        if not inject_ok:
            ok = False
            msgs.append(f"按键注入失败! 成功{sent}次/失败{failed}次 (测试环境问题，非产品缺陷)")
        else:
            msgs.append(f"SendInput 注入 {sent} 次全部成功")

        status = "PASS" if ok else "FAIL"
        print(f"[{status}] ripple  按键 SPACE  (涟漪扩散)")
        print(f"        " + " | ".join(msgs))
        results.append(("ripple/SPACE", ok, "; ".join(msgs)))

    if os.path.exists(TMP_CFG):
        os.remove(TMP_CFG)

    print()
    print("=" * 80)
    failed = [r for r in results if not r[1]]
    print(f"合计 {len(results)} 项，通过 {len(results) - len(failed)} 项，失败 {len(failed)} 项")
    for n, _, m in failed:
        print(f"  - {n}: {m}")
    print("=" * 80)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
