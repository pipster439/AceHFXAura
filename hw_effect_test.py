#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# =============================================================================
# 提示: 本脚本为历史阶段性验证/测试工具，包含独立的 ctypes/COM 虚表直调实现。
# 生产与维护环境推荐统一使用官方封装模块 tools.py.aura_hal (tools/py/aura_hal.py)，
# 该模块提供完整的 COM 生命周期管理、边界防御与权威实测键位映射。
# =============================================================================
"""
真机灯效推流验证（非 dry-run）。

针对问题 4（星空/流沙/电流/雨滴 在键盘上完全无效）：
在**真实硬件链路**下逐个运行这些灯效，确认 AuraAdapter::PushFrame 未报错。

判定依据（来自 src/aura/aura_adapter.cpp:233）：
  Set_L_STD_SINGLE_XY 连续失败 3 次会打印「连续推流异常 ... 判定硬件连接断开」。
  因此一次运行只要不出现该警告、且不出现 ERROR 级日志，即说明帧被硬件正常接受。

注意：CreateLedDevice/Release 存在已知内存泄漏（AGENT.md §13.5），
故每次进程生命周期内只跑一种灯效，且用例数刻意保持精简。
"""
import json
import os
import re
import signal
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
DAEMON = os.path.join(ROOT, "aura_daemon.exe")
KEYMAP = os.path.join(ROOT, "calibrated_keymap.json")
TMP_CFG = os.path.join(ROOT, "hw_test_config.json")

RUN_SECONDS = 8
COOLDOWN = 2

EFFECTS = {
    "static(对照)": {"type": "static", "color": [0, 80, 200]},
    "starry_night": {"type": "starry_night", "color": [255, 255, 255],
                     "random_colors": True, "period_ms": 2000},
    "quicksand":    {"type": "quicksand", "color1": [255, 25, 41],
                     "color2": [20, 138, 196], "period_ms": 2000, "direction": "right"},
    "current":      {"type": "current", "color": [0, 240, 255], "period_ms": 1500},
    "raindrop":     {"type": "raindrop", "color": [0, 240, 255], "period_ms": 1500},
}

BAD_PATTERNS = [
    "连续推流异常",
    "判定硬件连接断开",
    "[ERROR]",
    "注册失败",
    "未找到",
]


def run_one(name, cfg):
    conf = {"default_profile": "t", "profiles": {"t": cfg}, "rules": []}
    with open(TMP_CFG, "w", encoding="utf-8") as f:
        json.dump(conf, f, ensure_ascii=False, indent=2)

    p = subprocess.Popen(
        [DAEMON, "--config", TMP_CFG, "--keymap", KEYMAP],
        cwd=ROOT,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace",
        creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
    )
    try:
        time.sleep(RUN_SECONDS)
    finally:
        try:
            p.send_signal(signal.CTRL_BREAK_EVENT)
        except Exception:
            pass
        try:
            out, _ = p.communicate(timeout=8)
        except subprocess.TimeoutExpired:
            p.kill()
            out, _ = p.communicate()
        if p.stdout:
            p.stdout.close()
    return out or ""


def main():
    if not os.path.exists(DAEMON):
        print(f"[跳过] 未找到 {DAEMON}")
        return 0

    print("=" * 74)
    print(f"Aura 真机灯效推流验证（每例 {RUN_SECONDS}s，非 dry-run）")
    print("=" * 74)

    results = []
    for name, cfg in EFFECTS.items():
        out = run_one(name, cfg)

        hw_ok = "成功获取硬件控制权" in out
        hook_ok = "WH_KEYBOARD_LL) 注册成功" in out
        m = re.search(r'检测到设备数量:\s*(\d+)', out)
        dev_count = int(m.group(1)) if m else 0
        bad = [pat for pat in BAD_PATTERNS if pat in out]

        ok = hw_ok and hook_ok and (dev_count > 0) and not bad
        msgs = []
        msgs.append("硬件就绪" if hw_ok else "硬件未就绪")
        msgs.append("钩子OK" if hook_ok else "钩子未注册")
        if bad:
            msgs.append("异常: " + ", ".join(bad))
        else:
            msgs.append("推流无异常")

        # 提取设备数量行
        for line in out.splitlines():
            if "检测到设备数量" in line:
                msgs.append(line.split("]")[-1].strip())

        status = "PASS" if ok else "FAIL"
        print(f"[{status}] {name:<16} " + " | ".join(msgs))
        results.append((name, ok, "; ".join(msgs)))

        time.sleep(COOLDOWN)

    if os.path.exists(TMP_CFG):
        os.remove(TMP_CFG)
    # 清理可能残留的运行标记
    marker = os.path.join(ROOT, ".daemon_running")
    if os.path.exists(marker):
        os.remove(marker)

    print("=" * 74)
    failed = [r for r in results if not r[1]]
    print(f"合计 {len(results)} 项，通过 {len(results) - len(failed)} 项，失败 {len(failed)} 项")
    for n, _, m in failed:
        print(f"  - {n}: {m}")
    print("=" * 74)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
