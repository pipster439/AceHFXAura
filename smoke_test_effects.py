#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
全灯效冒烟测试：逐个以 default_profile 指向某一种灯效，启动 --dry-run 守护进程若干秒，
采集渲染帧输出，确认：
  1) 该灯效被正确解析并渲染（帧数据存在且色彩随时间变化或稳定）；
  2) 无按键输入时，键盘响应型灯效 (reactive/ripple/static+analog) 不产生任何点亮
     —— 即无自动误触发。
"""
import json, os, subprocess, sys, time, signal, re

ROOT = os.path.dirname(os.path.abspath(__file__))
DAEMON = os.path.join(ROOT, "build", "Release", "aura_daemon.exe")
KEYMAP = os.path.join(ROOT, "calibrated_keymap.json")
TMP = os.path.join(ROOT, "smoke_test_config.json")

EFFECTS = {
    "static":        {"type": "static", "color": [0, 80, 200]},
    "breathing":     {"type": "breathing", "color1": [0, 100, 255], "color2": [0, 10, 50], "period_ms": 2000},
    "color_cycle":   {"type": "color_cycle", "period_ms": 2000},
    "wave":          {"type": "wave", "period_ms": 2000, "direction": "right"},
    "custom_keymap": {"type": "custom_keymap", "bg": [10, 10, 10]},
    "reactive":      {"type": "reactive", "bg": [0, 0, 0], "color": [255, 25, 41], "period_ms": 800},
    "ripple":        {"type": "ripple", "bg": [0, 0, 0], "color": [0, 240, 255], "period_ms": 800},
    "starry_night":  {"type": "starry_night", "color": [255, 255, 255], "random_colors": True, "period_ms": 2000},
    "quicksand":     {"type": "quicksand", "color1": [255, 25, 41], "color2": [20, 138, 196], "period_ms": 2000, "direction": "right"},
    "current":       {"type": "current", "color": [0, 240, 255], "period_ms": 1500},
    "raindrop":      {"type": "raindrop", "color": [0, 240, 255], "period_ms": 1500},
}

# 需要严格验证「无操作时全灭」的灯效（键盘响应型 / 常亮+模拟）
IDLE_MUST_BE_DARK = {"reactive", "ripple"}

RUN_SECONDS = 6

FRAME_RE = re.compile(r"\[Dry-Run 效果帧 #(\d+)\] 活跃通道数: (\d+)/128 \| (.*)")


def parse_lit_channels(sample: str):
    """从帧采样串中统计非黑通道数量。"""
    cols = re.findall(r"ID (\d+)->RGB\((\d+),(\d+),(\d+)\)", sample)
    lit = [(int(i), int(r), int(g), int(b)) for i, r, g, b in cols
           if (int(r) + int(g) + int(b)) > 12]
    return lit


def run_one(name: str, cfg: dict):
    conf = {
        "default_profile": "t",
        "profiles": {"t": cfg},
        "rules": []
    }
    with open(TMP, "w", encoding="utf-8") as f:
        json.dump(conf, f, ensure_ascii=False, indent=2)

    p = subprocess.Popen(
        [DAEMON, "--dry-run", "--config", TMP, "--keymap", KEYMAP],
        cwd=os.path.join(ROOT, "build", "Release"),
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace",
        creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0,
    )
    frames = []
    hook_ok = False
    try:
        time.sleep(RUN_SECONDS)
    finally:
        if os.name == "nt":
            p.send_signal(signal.CTRL_BREAK_EVENT)
        else:
            p.terminate()
        try:
            out, _ = p.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            p.kill()
            out, _ = p.communicate()
        if p.stdout:
            p.stdout.close()

    for line in (out or "").splitlines():
        if "WH_KEYBOARD_LL) 注册成功" in line:
            hook_ok = True
        m = FRAME_RE.search(line)
        if m:
            frames.append((int(m.group(1)), int(m.group(2)), m.group(3)))

    return frames, hook_ok, (out or "")


def main():
    print("=" * 78)
    print("Aura 全灯效冒烟测试 (--dry-run)")
    print("=" * 78)
    results = []
    for name, cfg in EFFECTS.items():
        frames, hook_ok, raw = run_one(name, cfg)
        if not frames:
            print(f"[FAIL] {name:<14} 未采集到任何渲染帧")
            results.append((name, False, "无帧输出"))
            continue

        first = frames[0]
        last = frames[-1]
        lit_first = parse_lit_channels(first[2])
        lit_last = parse_lit_channels(last[2])

        ok = True
        notes = []

        if name in IDLE_MUST_BE_DARK:
            # 键盘响应型：无按键时应当全灭（仅背景色 bg=[0,0,0]）
            if len(lit_first) > 0 or len(lit_last) > 0:
                ok = False
                notes.append(f"空闲误触发! 点亮通道 {len(lit_first)}/{len(lit_last)}")
            else:
                notes.append("空闲静默 (0 通道点亮) —— 无自动误触发")
        else:
            if first[1] == 0:
                ok = False
                notes.append("活跃通道数为 0，灯效未渲染")
            else:
                notes.append(f"活跃通道 {first[1]}/128 -> {last[1]}/128")

        if not hook_ok:
            notes.append("(本轮未捕获钩子日志行)")

        status = "PASS" if ok else "FAIL"
        print(f"[{status}] {name:<14} 帧数={len(frames):<4} 首帧#{first[0]} 末帧#{last[0]} | " + "; ".join(notes))
        results.append((name, ok, "; ".join(notes)))

    if os.path.exists(TMP):
        os.remove(TMP)

    print("=" * 78)
    failed = [r for r in results if not r[1]]
    print(f"合计 {len(results)} 项，通过 {len(results)-len(failed)} 项，失败 {len(failed)} 项")
    if failed:
        for n, _, msg in failed:
            print(f"  - {n}: {msg}")
    print("=" * 78)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
