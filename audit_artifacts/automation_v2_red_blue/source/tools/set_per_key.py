#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
=============================================================================
ROG FALCHION ACE HFX 硬件级独立单键 RGB 专属控制引擎 (Zero-Wear Per-Key RGB)
=============================================================================
- 控制方式: 华硕原生底层驱动 USB HID 直控 (Set_L_STD_SINGLE_XY)
- 寿命安全: 100% 运行于高速易失性 RAM 推流模式，对键盘 EEPROM/Flash 擦写磨损为零
- 系统兼容: 永久绕过且不依赖 Windows 动态照明 (WDL)
- 硬件映射: 100% 经物理点对点标定实测完成 (基于 tools.py.aura_hal 共享模块)
=============================================================================
"""

import argparse
import ctypes
import os
import sys
import time
from typing import List, Optional, Union

# 引入 tools/py 共享基础模块
_DIR = os.path.dirname(os.path.abspath(__file__))
_TOOLS_PY = os.path.join(_DIR, "py") if os.path.isdir(os.path.join(_DIR, "py")) else os.path.join(_DIR, "tools", "py")
if _TOOLS_PY not in sys.path:
    sys.path.insert(0, _TOOLS_PY)

import aura_hal
from aura_hal import (
    AuraHal,
    AuraHalDevice,
    HARDWARE_KEY_MAP,
    VERIFIED_LED_IDS,
    TOTAL_LEDS,
    RGB_CHANNELS,
    build_padded_hardware_table,
    resolve_key,
    CONFLICT_GROUPS,
    UNSUPPORTED_STANDARD_KEYS,
)


class RogFalchionController:
    """
    ROG FALCHION ACE HFX 键盘灯效控制器。
    封装硬件连接、帧缓冲维护与推流控制。
    """

    def __init__(self, dry_run: bool = False):
        self.dry_run = dry_run
        self.TOTAL_LEDS = TOTAL_LEDS
        self.rgb_buf = (ctypes.c_ubyte * (self.TOTAL_LEDS * RGB_CHANNELS))()

        # 构建安全隔离硬件寻址表 (68 物理键 + 64 字节 HID 边界隔离槽)
        calibrated_ids = sorted(list(VERIFIED_LED_IDS))
        self.padded_table = build_padded_hardware_table(calibrated_ids)
        self.hardware_count = len(self.padded_table)
        self.stream_buf = (ctypes.c_ubyte * (self.hardware_count * RGB_CHANNELS))()

        self.hal: Optional[AuraHal] = None
        self.device: Optional[AuraHalDevice] = None
        self.pDev = 0

        if not self.dry_run:
            self.hal = AuraHal()
            devices = self.hal.create_led_devices()
            if not devices:
                self.hal.release()
                raise RuntimeError("未检测到已连接的 ROG FALCHION ACE HFX 硬件！")
            self.device = devices[0]
            self.pDev = self.device.pDev
            self.device.configure_hardware_table(self.padded_table)

    def clear(self) -> None:
        """将所有按键熄灭"""
        for i in range(self.TOTAL_LEDS * RGB_CHANNELS):
            self.rgb_buf[i] = 0

    def set_key_color(self, key_name_or_id: Union[str, int], r: int, g: int, b: int, strict: bool = True) -> int:
        """
        设置单个按键的 RGB 颜色 (0~255)。
        以 calibrated_keymap.json 权威标定为准；无法确定的键显式报错拒绝点灯，杜绝错乱点灯。
        """
        lid = resolve_key(key_name_or_id, strict=strict)
        self.rgb_buf[lid * RGB_CHANNELS + 0] = max(0, min(255, int(r)))
        self.rgb_buf[lid * RGB_CHANNELS + 1] = max(0, min(255, int(g)))
        self.rgb_buf[lid * RGB_CHANNELS + 2] = max(0, min(255, int(b)))
        return lid

    def fill(self, r: int, g: int, b: int) -> None:
        """将所有按键设置为指定底色"""
        for lid in range(self.TOTAL_LEDS):
            self.rgb_buf[lid * RGB_CHANNELS + 0] = max(0, min(255, int(r)))
            self.rgb_buf[lid * RGB_CHANNELS + 1] = max(0, min(255, int(g)))
            self.rgb_buf[lid * RGB_CHANNELS + 2] = max(0, min(255, int(b)))

    def apply_preset(self, preset_name: str) -> None:
        """加载预设灯效布局"""
        self.clear()
        name = preset_name.lower()
        if name in ['wasd', 'gamer']:
            # 竞技游戏布局: WASD=绿色, ESC=红色, SPACE=冰蓝, 方向键=黄色
            self.set_key_color('ESC', 255, 0, 0)
            for k in ['W', 'A', 'S', 'D']:
                self.set_key_color(k, 0, 255, 0)
            self.set_key_color('SPACE', 0, 200, 255)
            for k in ['UP', 'DOWN', 'LEFT', 'RIGHT']:
                self.set_key_color(k, 255, 255, 0)
        elif name == 'cyberpunk':
            # 赛博朋克风格: WASD=霓虹青, 方向键=荧光粉, 空格=黄色
            for k in ['W', 'A', 'S', 'D']:
                self.set_key_color(k, 0, 255, 255)
            for k in ['UP', 'DOWN', 'LEFT', 'RIGHT']:
                self.set_key_color(k, 255, 0, 128)
            self.set_key_color('SPACE', 255, 255, 0)
            self.set_key_color('ESC', 0, 255, 255)
        elif name == 'off':
            self.clear()
        else:
            print(f"[-] 未知预设: {preset_name}")

    def stream_once(self) -> int:
        """单次推流一帧 (硬件保持当前色彩)"""
        for i, kid in enumerate(self.padded_table):
            if kid != 0xFF and kid < self.TOTAL_LEDS:
                self.stream_buf[i * RGB_CHANNELS + 0] = self.rgb_buf[kid * RGB_CHANNELS + 0]
                self.stream_buf[i * RGB_CHANNELS + 1] = self.rgb_buf[kid * RGB_CHANNELS + 1]
                self.stream_buf[i * RGB_CHANNELS + 2] = self.rgb_buf[kid * RGB_CHANNELS + 2]
            else:
                self.stream_buf[i * RGB_CHANNELS + 0] = 0
                self.stream_buf[i * RGB_CHANNELS + 1] = 0
                self.stream_buf[i * RGB_CHANNELS + 2] = 0

        if self.device:
            return self.device.set_led_direct(self.stream_buf)
        return 0

    def stream_loop(self, duration: Optional[float] = None, fps: int = 25) -> None:
        """持续推流维持静态单键光效 (按 Ctrl+C 可退出)"""
        delay = 1.0 / max(1, fps)
        start_time = time.time()
        try:
            while True:
                self.stream_once()
                if duration and (time.time() - start_time >= duration):
                    break
                time.sleep(delay)
        except KeyboardInterrupt:
            pass

    def close(self) -> None:
        """释放底层设备与 COM 套间资源"""
        if self.device:
            self.device.release()
            self.device = None
        if self.hal:
            self.hal.release()
            self.hal = None


def main() -> None:
    parser = argparse.ArgumentParser(description="ROG FALCHION ACE HFX 硬件级单键独立 RGB 控制器")
    parser.add_argument("--preset", type=str, choices=['wasd', 'gamer', 'cyberpunk', 'off'], default=None,
                        help="预设方案: wasd, cyberpunk, off (未指定且无 --key 时默认 wasd)")
    parser.add_argument("--bg", nargs=3, type=int, metavar=('R', 'G', 'B'), default=[0, 0, 0],
                        help="未指定按键的背景底色 (默认全黑 0 0 0)")
    parser.add_argument("--key", action="append", nargs=4, metavar=('KEY', 'R', 'G', 'B'),
                        help="自定义单键颜色，例: --key WASD 0 255 0 --key SPACE 0 200 255")
    parser.add_argument("--duration", type=float, default=None,
                        help="持续维持灯效秒数 (默认无限循环直到按 Ctrl+C)")
    parser.add_argument("--fps", type=int, default=25, help="推流帧率 (默认 25 FPS)")
    parser.add_argument("--dry-run", action="store_true", help="模拟运行模式 (虚拟硬件推流，不挂载实际 DLL)")

    args = parser.parse_args()

    print("[+] 正在初始化 ROG FALCHION ACE HFX 硬件单键直控通道 (RAM模式，零寿命磨损)...")
    ctrl = RogFalchionController(dry_run=args.dry_run)
    if args.dry_run:
        print("[+] 虚拟硬件模式就绪 (Dry-Run)")
    else:
        print("[+] 硬件驱动挂载成功！")

    # 1. 底色处理
    if args.bg != [0, 0, 0]:
        ctrl.fill(args.bg[0], args.bg[1], args.bg[2])
    else:
        ctrl.clear()

    # 2. 预设处理
    if args.preset:
        ctrl.apply_preset(args.preset)
    elif not args.key:
        ctrl.apply_preset('wasd')

    # 3. 自定义单键覆盖
    has_error = False
    if args.key:
        for k, r, g, b in args.key:
            ku = k.upper()
            target_keys: List[str] = []
            if ku == 'WASD':
                target_keys = ['W', 'A', 'S', 'D']
            elif ku == 'ARROWS':
                target_keys = ['UP', 'DOWN', 'LEFT', 'RIGHT']
            elif ku in HARDWARE_KEY_MAP:
                target_keys = [ku]
            elif ku in UNSUPPORTED_STANDARD_KEYS:
                # 明确属于非 68 键物理布局的标准按键，保持整词传给 resolve_key 触发明确报错
                target_keys = [ku]
            elif ku.isdigit() and int(ku) < TOTAL_LEDS:
                # 直接传合法范围内的纯数字 LED ID（例如 "107"）
                target_keys = [ku]
            elif len(ku) > 1 and all(c in HARDWARE_KEY_MAP for c in ku):
                # 支持类似 'TYGF', 'QWER', 'ZXCV', '1234' 等多个单字符按键组成的连续字符串
                target_keys = list(ku)
            else:
                target_keys = [ku]

            configured_lids: List[str] = []
            for single in target_keys:
                try:
                    lid = ctrl.set_key_color(single, int(r), int(g), int(b), strict=True)
                    configured_lids.append(f"{single}(ID {lid})")
                except (KeyError, ValueError) as err:
                    has_error = True
                    print(f"[-] 错误: {err}")

            if configured_lids:
                print(f"[+] 已配置按键: {', '.join(configured_lids)} -> RGB({r}, {g}, {b})")

    # 遇到未识别或非法按键强力拦截：立即报错并退出，拒绝静默点错灯与无效推流
    if has_error:
        print("[-] 存在未识别或非法按键，中止推流以防硬件错位。")
        ctrl.close()
        sys.exit(1)

    dur_str = f"{args.duration} 秒" if args.duration else "持续运行 (按 Ctrl+C 退出)"
    print(f"[*] 正在推流渲染中... [{dur_str}]")

    ctrl.stream_loop(duration=args.duration, fps=args.fps)
    ctrl.close()
    print("[+] 推流已安全结束。")


if __name__ == '__main__':
    main()
