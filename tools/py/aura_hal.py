#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
=============================================================================
Aura Hardware Abstraction Layer (HAL) 共享基础库
=============================================================================
适用设备: ASUS ROG FALCHION ACE HFX (PID: 0x1B7E, Model: 7038)
接口类型: COM In-Proc (AacKbHal_x64.dll) / USB HID Direct RAM Streaming
协议命令: Set_L_STD_SINGLE_XY (VTable[19], 0x81 0xC0)

本模块集中治理 Aura 系统的 Python 硬件交互逻辑，统一封装：
1. CLSID / IID 常量与 GUID 转换
2. 虚函数表 (VTable) 索引契约与设备对象内存偏移 (0x6C / 0x74)
3. 内存安全版 CreateLedDevice (FakeVector/StdVector 边界保护)
4. 64 字节 USB HID 报文边界隔离槽填充算法 (0xFF padding)
5. 权威标定键位表 (calibrated_keymap.json) 加载与冲突消除
6. 严格按键解析 (拒绝未实测/推导键，防止硬件错位)
7. AuraHal 与 AuraHalDevice 的 RAII 式生命周期管理
=============================================================================
"""

from __future__ import annotations

import ctypes
from ctypes import wintypes
import json
import logging
import os
import sys
from typing import Any, Dict, List, Optional, Sequence, Set, Tuple, Union

logger = logging.getLogger("aura_hal")

# -----------------------------------------------------------------------------
# 1. COM & Windows 底层类型与 GUID
# -----------------------------------------------------------------------------
ole32 = ctypes.oledll.ole32


class GUID(ctypes.Structure):
    """Windows 原生 GUID 结构体 (16 字节)"""
    _fields_ = [
        ('Data1', wintypes.DWORD),
        ('Data2', wintypes.WORD),
        ('Data3', wintypes.WORD),
        ('Data4', wintypes.BYTE * 8)
    ]

    def __repr__(self) -> str:
        d4_1 = "".join(f"{b:02X}" for b in self.Data4[:2])
        d4_2 = "".join(f"{b:02X}" for b in self.Data4[2:])
        return f"{{{self.Data1:08X}-{self.Data2:04X}-{self.Data3:04X}-{d4_1}-{d4_2}}}"


def to_guid(guid_str: str) -> GUID:
    """将标准 UUID/GUID 字符串转换为 Windows GUID 结构体"""
    g = GUID()
    hr = ole32.CLSIDFromString(ctypes.c_wchar_p(guid_str), ctypes.byref(g))
    if hr != 0:
        raise ValueError(f"无效的 GUID 字符串: {guid_str} (HRESULT: 0x{hr & 0xFFFFFFFF:08X})")
    return g


class StdVector(ctypes.Structure):
    """
    模拟 MSVC std::vector<void*> 内存布局 (first, last, end)。
    与 C++ FakeVector 内存结构完全一致，供 CreateLedDevice 填充返回的设备指针数组。
    """
    _fields_ = [
        ("first", ctypes.c_void_p),
        ("last", ctypes.c_void_p),
        ("end", ctypes.c_void_p)
    ]


# -----------------------------------------------------------------------------
# 2. 硬件常量与协议契约 (与 include/aura/aura_types.h 严格同步)
# -----------------------------------------------------------------------------
CLSID_CLAYMORE_HAL = "{AE9DB4C8-4F2A-4756-9B11-2F6D78C61F1A}"
IID_IASUS_AAC_LED_DEVICE_HAL = "{F2C8D5B4-3854-4325-8A4F-FD7C5072E3BA}"

# VTable 虚函数表索引 (经二进制逆向验证)
VTABLE_HAL_RELEASE = 2
VTABLE_HAL_ACCESS = 4
VTABLE_HAL_CREATE_LED_DEVICE = 5

VTABLE_DEV_RELEASE = 2
VTABLE_DEV_SET_SINGLE = 19       # Set_L_STD_SINGLE_XY (原生 USB HID 直控)
VTABLE_DEV_INIT_TABLE = 20

# 设备对象内存偏移
OFFSET_KEY_COUNT = 0x6C          # DWORD: 硬件寻址表条目数
OFFSET_LED_TABLE = 0x74          # BYTE[]: 硬件 LED ID 寻址表起始地址

# 硬件规格与缓冲区上界
TOTAL_LEDS = 128
RGB_CHANNELS = 3
FRAME_BUFFER_SIZE = TOTAL_LEDS * RGB_CHANNELS   # 384 字节
TOTAL_PHYSICAL_KEYS = 68
HARDWARE_STREAM_KEYS = 72         # 68 物理键 + 4 隔离槽
MAX_HARDWARE_STREAM_KEYS = 144    # 128 键最坏情况 137，安全上界 144
DUMMY_PADDING_LED_ID = 0xFF       # USB 64 字节 HID 报文边界隔离槽填充字节


# -----------------------------------------------------------------------------
# 3. 硬件寻址表填充算法 (USB 64 字节 HID 边界隔离)
# -----------------------------------------------------------------------------
def build_padded_hardware_table(led_ids: Sequence[int]) -> List[int]:
    """
    根据 ASUS ROG Falchion Ace HFX 的 USB HID 64 字节报文边界规则构建带填充的硬件寻址表。
    每 15 槽的第 14 槽 (% 15 == 14) 插入 0xFF 隔离槽，防止物理按键落在 HID 边界导致数据错位。

    推导依据：
      68 键  ->  72 项 (含 4 个 0xFF 隔离槽，恰满一个周期)
      128 键 -> 137 项 (含 9 个 0xFF 隔离槽) <= MAX_HARDWARE_STREAM_KEYS (144)
    """
    unique_sorted = sorted(set(int(i) for i in led_ids))
    padded: List[int] = []
    for kid in unique_sorted:
        if len(padded) % 15 == 14:
            padded.append(DUMMY_PADDING_LED_ID)
        padded.append(kid)

    if len(padded) > MAX_HARDWARE_STREAM_KEYS:
        raise ValueError(
            f"硬件寻址表条目数 ({len(padded)}) 超过安全上限 ({MAX_HARDWARE_STREAM_KEYS})"
        )
    return padded


# -----------------------------------------------------------------------------
# 4. 权威物理按键标定表与冲突消除 (ROG FALCHION ACE HFX 68 键)
# -----------------------------------------------------------------------------
# 权威 68 键硬编码基准（由 calibrated_keymap.json 实测物理标定并独立交叉验证）
# 排序规则：Row 1 -> Row 5，每排从左至右
CANONICAL_68_KEYS: Dict[str, int] = {
    # Row 1 (顶部数字与功能区, 15 键)
    "ESC": 1, "1": 9, "2": 17, "3": 25, "4": 33, "5": 41,
    "6": 49, "7": 57, "8": 65, "9": 73, "0": 81, "-": 89,
    "=": 97, "BACKSPACE": 105, "INS": 113,

    # Row 2 (QWERTY 字母区, 15 键)
    "TAB": 2, "Q": 10, "W": 18, "E": 26, "R": 34, "T": 42,
    "Y": 50, "U": 58, "I": 66, "O": 74, "P": 82, "[": 90,
    "]": 98, "\\": 106, "DEL": 114,

    # Row 3 (ASDF 字母主键区, 14 键)
    "CAPS": 3, "A": 11, "S": 19, "D": 27, "F": 35, "G": 43,
    "H": 51, "J": 59, "K": 67, "L": 75, ";": 83, "'": 91,
    "ENTER": 107, "PGUP": 115,

    # Row 4 (ZXCV 底部字母区, 14 键)
    "L_SHIFT": 4, "Z": 20, "X": 28, "C": 36, "V": 44, "B": 52,
    "N": 60, "M": 68, ",": 76, ".": 84, "/": 92, "R_SHIFT": 100,
    "UP": 108, "PGDN": 116,

    # Row 5 (空格、修饰与方向键, 10 键)
    "L_CTRL": 5, "L_WIN": 13, "L_ALT": 21, "SPACE": 53,
    "R_ALT": 77, "FN": 85, "COPILOT": 93, "LEFT": 101,
    "DOWN": 109, "RIGHT": 117
}

# 常用别名映射（统一指向 CANONICAL_68_KEYS 中的正规名称）
STANDARD_KEY_ALIASES: Dict[str, str] = {
    'ESCAPE': 'ESC',
    'SPACEBAR': 'SPACE',
    'RETURN': 'ENTER',
    'BS': 'BACKSPACE',
    'CAPSLOCK': 'CAPS',
    'SHIFT': 'L_SHIFT',
    'LSHIFT': 'L_SHIFT',
    'L_SHIFT': 'L_SHIFT',
    'RSHIFT': 'R_SHIFT',
    'R_SHIFT': 'R_SHIFT',
    'CTRL': 'L_CTRL',
    'LCTRL': 'L_CTRL',
    'L_CTRL': 'L_CTRL',
    'WIN': 'L_WIN',
    'LWIN': 'L_WIN',
    'L_WIN': 'L_WIN',
    'WINDOWS': 'L_WIN',
    'ALT': 'L_ALT',
    'LALT': 'L_ALT',
    'L_ALT': 'L_ALT',
    'RALT': 'R_ALT',
    'R_ALT': 'R_ALT',
    'DELETE': 'DEL',
    'INSERT': 'INS',
    'PAGEUP': 'PGUP',
    'PAGEDOWN': 'PGDN',
    'ARROWUP': 'UP',
    'ARROWDOWN': 'DOWN',
    'ARROWLEFT': 'LEFT',
    'ARROWRIGHT': 'RIGHT',
    'MINUS': '-',
    'EQUAL': '=',
    'EQUALS': '=',
    'COMMA': ',',
    'PERIOD': '.',
    'DOT': '.',
    'SLASH': '/',
    'SEMICOLON': ';',
    'QUOTE': "'",
    'APOSTROPHE': "'",
    'LBRACKET': '[',
    'LEFTBRACKET': '[',
    'RBRACKET': ']',
    'RIGHTBRACKET': ']',
    'BACKSLASH': '\\',
}

# 5 组历史 LED ID 冲突条目（AGENT.md §12.3 / REMEDIATION_PLAN.md §R21）
# 记录在历史 set_per_key.py:48-87 中的错误映射 vs 经实测标定的正确 LED ID
HISTORICAL_CONFLICT_GROUPS: Dict[str, Dict[str, Any]] = {
    'I_VS_ENTER': {
        'keys': {'I': 66, 'ENTER': 107, 'RETURN': 107},
        'historical_flawed_id': 66,
        'correct_mapping': {'I': 66, 'ENTER': 107},
        'note': '历史代码将 ENTER 与 I 均误设为 66，实测 ENTER 应为 107'
    },
    'K_VS_INS': {
        'keys': {'K': 67, 'INS': 113, 'INSERT': 113},
        'historical_flawed_id': 67,
        'correct_mapping': {'K': 67, 'INS': 113},
        'note': '历史代码将 INS 与 K 均误设为 67，实测 INS 应为 113'
    },
    'O_VS_PGUP_MINUS': {
        'keys': {'O': 74, 'PGUP': 115, 'PAGEUP': 115, 'MINUS': 89, '-': 89},
        'historical_flawed_id': 74,
        'correct_mapping': {'O': 74, 'PGUP': 115, '-': 89},
        'note': '历史代码将 PGUP、MINUS 与 O 均误设为 74，实测 MINUS=89, PGUP=115, O=74'
    },
    'L_VS_DEL': {
        'keys': {'L': 75, 'DEL': 114, 'DELETE': 114},
        'historical_flawed_id': 75,
        'correct_mapping': {'L': 75, 'DEL': 114},
        'note': '历史代码将 DEL 与 L 均误设为 75，实测 DEL 应为 114'
    },
    'P_VS_BACKSPACE_EQUAL': {
        'keys': {'P': 82, 'BACKSPACE': 105, 'BS': 105, 'EQUAL': 97, '=': 97},
        'historical_flawed_id': 82,
        'correct_mapping': {'P': 82, 'BACKSPACE': 105, '=': 97},
        'note': '历史代码将 BACKSPACE、EQUAL 与 P 均误设为 82，实测 BACKSPACE=105, EQUAL=97, P=82'
    }
}
CONFLICT_GROUPS = HISTORICAL_CONFLICT_GROUPS

# 明确属于标准键盘但本 68 键键盘不存在的功能键（防止在组合键拆解或模糊查找中被错误拆分或误匹配）
UNSUPPORTED_STANDARD_KEYS: Set[str] = {
    'F1', 'F2', 'F3', 'F4', 'F5', 'F6', 'F7', 'F8', 'F9', 'F10', 'F11', 'F12',
    'HOME', 'END', 'PRTSC', 'PRINTSCREEN', 'PRINT_SCREEN',
    'PAUSE', 'BREAK', 'SCROLLLOCK', 'SCROLL_LOCK', 'NUMLOCK', 'NUM_LOCK',
    'MENU', 'APPS'
}


def find_calibrated_keymap_path(explicit_path: Optional[str] = None) -> str:
    """寻找权威 calibrated_keymap.json 文件路径"""
    if explicit_path:
        return os.path.abspath(explicit_path)

    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(script_dir, "..", ".."))

    candidates = [
        os.path.join(os.getcwd(), "calibrated_keymap.json"),
        os.path.join(repo_root, "calibrated_keymap.json"),
        r"G:\Aura\calibrated_keymap.json",
        os.path.join(repo_root, "tests", "fixtures", "calibrated_keymap.json"),
        os.path.join(os.getcwd(), "tests", "fixtures", "calibrated_keymap.json"),
    ]
    for c in candidates:
        norm = os.path.abspath(c)
        if os.path.isfile(norm):
            return norm
    return os.path.abspath(r"G:\Aura\calibrated_keymap.json")


def load_calibrated_keymap(explicit_path: Optional[str] = None) -> Dict[str, Any]:
    """读取权威 calibrated_keymap.json 完整数据"""
    path = find_calibrated_keymap_path(explicit_path)
    if not os.path.isfile(path):
        logger.warning("未找到 calibrated_keymap.json (%s)，回退至内置权威 68 键映射", path)
        return {"keys": {k: {"led_id": v, "status": "VERIFIED"} for k, v in CANONICAL_68_KEYS.items()}}

    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def _build_authoritative_maps(explicit_path: Optional[str] = None) -> Tuple[Dict[str, int], Dict[int, str], Set[int]]:
    """构建权威键名映射表、反向映射表与实测 LED ID 集合"""
    data = load_calibrated_keymap(explicit_path)
    keys_obj = data.get("keys", {})

    led_map: Dict[str, int] = {}
    name_map: Dict[int, str] = {}
    verified_ids: Set[int] = set()

    # 1. 载入权威标定键 (仅收录 VERIFIED 状态)
    for k, v in keys_obj.items():
        if isinstance(v, dict):
            status = v.get("status", "VERIFIED")
            if status == "VERIFIED" and "led_id" in v:
                lid = int(v["led_id"])
                ku = k.upper()
                led_map[ku] = lid
                name_map[lid] = ku
                verified_ids.add(lid)
        elif isinstance(v, int):
            ku = k.upper()
            led_map[ku] = v
            name_map[v] = ku
            verified_ids.add(v)

    # 兜底补充未录入的权威基准键
    for k, v in CANONICAL_68_KEYS.items():
        ku = k.upper()
        if ku not in led_map:
            led_map[ku] = v
            if v not in name_map:
                name_map[v] = ku
            verified_ids.add(v)

    # 2. 补全标准别名（别名必须精确指向其在 led_map 中的权威目标）
    for alias, target in STANDARD_KEY_ALIASES.items():
        au = alias.upper()
        tu = target.upper()
        if tu in led_map:
            led_map[au] = led_map[tu]

    return led_map, name_map, verified_ids


# 模块级单例权威映射表
AUTHORITATIVE_KEY_MAP, ID_TO_KEY_NAME, VERIFIED_LED_IDS = _build_authoritative_maps()
HARDWARE_KEY_MAP = AUTHORITATIVE_KEY_MAP


def get_calibrated_led_map(explicit_path: Optional[str] = None) -> Dict[str, int]:
    """获取键名至权威 LED ID 的字典映射 (含标准别名)"""
    if explicit_path:
        m, _, _ = _build_authoritative_maps(explicit_path)
        return m
    return AUTHORITATIVE_KEY_MAP


def get_calibrated_name_map(explicit_path: Optional[str] = None) -> Dict[int, str]:
    """获取权威 LED ID 至正规键名的反向字典映射"""
    if explicit_path:
        _, m, _ = _build_authoritative_maps(explicit_path)
        return m
    return ID_TO_KEY_NAME


def get_verified_led_ids(explicit_path: Optional[str] = None) -> Set[int]:
    """获取所有经物理实测标定确认的 LED ID 集合"""
    if explicit_path:
        _, _, s = _build_authoritative_maps(explicit_path)
        return s
    return VERIFIED_LED_IDS


def resolve_key(key_name_or_id: Union[str, int], strict: bool = True) -> int:
    """
    将按键名称或硬件 ID 解析为权威实测的 LED ID。

    安全契约：
    1. 整数 ID：
       - 必须属于 [0, TOTAL_LEDS) 合法区间；
       - 若 strict=True，必须属于经实测标定的 68 键之一 (VERIFIED_LED_IDS)；
         未实测的空引脚抛出 ValueError，显式报错拒绝点灯，防止硬件错位。
    2. 字符串按键名：
       - 大小写无关，经由权威标定表及别名表解析；
       - 无法从权威源确定者，显式抛出 KeyError 并标注"仅推导、未实测"，拒绝静默点错灯。
    """
    if isinstance(key_name_or_id, int):
        lid = key_name_or_id
        if not (0 <= lid < TOTAL_LEDS):
            raise ValueError(f"LED ID {lid} 超出合法区间 [0, {TOTAL_LEDS})")
        if strict and lid not in VERIFIED_LED_IDS:
            raise ValueError(
                f"LED ID {lid} 未在权威标定表中实测确认 (仅推导、未实测)，拒绝点灯以防硬件错位"
            )
        return lid

    if isinstance(key_name_or_id, str):
        k = key_name_or_id.strip().upper()

        # 1. 优先从权威标定表及别名表中解析按键名称（如 "1", "ESC", "ENTER", "SPACE" 等）
        if k in AUTHORITATIVE_KEY_MAP:
            return AUTHORITATIVE_KEY_MAP[k]

        # 2. 属于标准键盘但本 68 键键盘不存在的功能键（如 F1~F12, HOME, END），明确拒绝
        if k in UNSUPPORTED_STANDARD_KEYS:
            raise KeyError(
                f"按键 [{key_name_or_id}] 无法从权威标定源确定 (仅推导、未实测)，拒绝点灯避免硬件错位"
            )

        # 3. 若不是已知键名，但为纯数字字符串（如 "107", "12"），则按硬件数字 LED ID 解析
        if k.isdigit():
            return resolve_key(int(k), strict=strict)

        # 4. 无法从权威源确定者：显式报错并标注"仅推导、未实测"，拒绝点灯
        raise KeyError(
            f"按键 [{key_name_or_id}] 无法从权威标定源确定 (仅推导、未实测)，拒绝点灯避免硬件错位"
        )

    raise TypeError(f"不支持的按键类型: {type(key_name_or_id).__name__}")


# -----------------------------------------------------------------------------
# 5. Aura 设备对象安全包装 (AuraHalDevice)
# -----------------------------------------------------------------------------
class AuraHalDevice:
    """
    ROG Falchion Ace HFX 硬件设备包装对象。
    封装 +0x6C 硬件寻址表条目数、+0x74 硬件寻址表写入、VTable[19] Set_L_STD_SINGLE_XY 直控与 VTable[2] Release。
    """

    def __init__(self, pDev: int):
        if not pDev:
            raise ValueError("设备指针 pDev 不能为空")
        self.pDev = pDev
        self._released = False

        # 解析设备对象虚函数表
        self.dev_vtable = ctypes.cast(
            ctypes.cast(self.pDev, ctypes.POINTER(ctypes.c_void_p))[0],
            ctypes.POINTER(ctypes.c_void_p)
        )

        # 绑定 VTable[19] Set_L_STD_SINGLE_XY
        self._fn_set_single = ctypes.WINFUNCTYPE(
            wintypes.LONG, ctypes.c_void_p, ctypes.c_void_p
        )(self.dev_vtable[VTABLE_DEV_SET_SINGLE])

        # 绑定 VTable[2] IUnknown::Release
        self._fn_release = ctypes.WINFUNCTYPE(
            wintypes.ULONG, ctypes.c_void_p
        )(self.dev_vtable[VTABLE_DEV_RELEASE])

    def configure_hardware_table(self, padded_table: Sequence[int]) -> int:
        """
        向设备对象写入硬件寻址表 (+0x6C 长度, +0x74 寻址表数组)。
        具备 MAX_HARDWARE_STREAM_KEYS (144) 上界防御，严禁越界写穿闭源对象内部堆栈。
        """
        if self._released or not self.pDev:
            raise RuntimeError("设备对象已释放，无法写入硬件寻址表")

        count = len(padded_table)
        if count > MAX_HARDWARE_STREAM_KEYS:
            raise ValueError(
                f"硬件寻址表长度 {count} 超过安全上界 {MAX_HARDWARE_STREAM_KEYS}，拒绝写入以防破坏内存"
            )

        # 写入 +0x6C (DWORD 长度)
        ctypes.cast(self.pDev + OFFSET_KEY_COUNT, ctypes.POINTER(wintypes.DWORD))[0] = count

        # 写入 +0x74 (BYTE 数组)
        led_id_table = ctypes.cast(self.pDev + OFFSET_LED_TABLE, ctypes.POINTER(ctypes.c_ubyte))
        for i, val in enumerate(padded_table):
            led_id_table[i] = val & 0xFF

        return count

    def set_led_direct(self, rgb_buf: Any) -> int:
        """
        通过 VTable[19] Set_L_STD_SINGLE_XY 直接向硬件推流一帧 RGB 色彩数据。
        支持 ctypes 数组、bytes、bytearray 或内存指针。
        """
        if self._released or not self.pDev:
            raise RuntimeError("设备对象已释放，无法执行推流")

        if isinstance(rgb_buf, (bytes, bytearray)):
            buf = (ctypes.c_ubyte * len(rgb_buf)).from_buffer_copy(rgb_buf)
            return self._fn_set_single(self.pDev, ctypes.byref(buf))
        elif hasattr(rgb_buf, '_type_'):
            return self._fn_set_single(self.pDev, ctypes.byref(rgb_buf))
        else:
            return self._fn_set_single(self.pDev, rgb_buf)

    def release(self) -> None:
        """安全释放设备 COM 引用，防止内存泄漏与句柄悬垂"""
        if not self._released and self.pDev:
            try:
                self._fn_release(self.pDev)
            except Exception as e:
                logger.debug("设备对象 Release 抛出异常: %s", e)
            finally:
                self.pDev = 0
                self._released = True

    def __enter__(self) -> AuraHalDevice:
        return self

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        self.release()

    def __del__(self) -> None:
        self.release()


# -----------------------------------------------------------------------------
# 6. Aura HAL COM 接口安全包装 (AuraHal)
# -----------------------------------------------------------------------------
class AuraHal:
    """
    ASUS Claymore HAL COM 接口安全包装。
    管理 COM STA 套间生命周期 (CoInitialize / CoUninitialize) 与 CreateLedDevice 枚举。
    """

    def __init__(self, init_com: bool = True):
        self._init_com = init_com
        self._com_initialized = False
        if self._init_com:
            ole32.CoInitialize(None)
            self._com_initialized = True

        self.pHal = ctypes.c_void_p()
        self._released = False

        clsid = to_guid(CLSID_CLAYMORE_HAL)
        iid = to_guid(IID_IASUS_AAC_LED_DEVICE_HAL)

        hr = ole32.CoCreateInstance(
            ctypes.byref(clsid),
            None,
            1,  # CLSCTX_INPROC_SERVER
            ctypes.byref(iid),
            ctypes.byref(self.pHal)
        )
        if hr != 0 or not self.pHal.value:
            if self._com_initialized:
                ole32.CoUninitialize()
                self._com_initialized = False
            raise RuntimeError(f"无法创建 CLSID_ClaymoreHal COM 实例 (0x{hr & 0xFFFFFFFF:08X})")

        self.hal_vtable = ctypes.cast(
            ctypes.cast(self.pHal.value, ctypes.POINTER(ctypes.c_void_p))[0],
            ctypes.POINTER(ctypes.c_void_p)
        )

        self._fn_create_device = ctypes.WINFUNCTYPE(
            wintypes.LONG, ctypes.c_void_p, ctypes.POINTER(StdVector)
        )(self.hal_vtable[VTABLE_HAL_CREATE_LED_DEVICE])

        self._fn_release = ctypes.WINFUNCTYPE(
            wintypes.ULONG, ctypes.c_void_p
        )(self.hal_vtable[VTABLE_HAL_RELEASE])

    def access(self) -> int:
        """调用 VTable[4] Access 获取底层硬件控制权"""
        if self._released or not self.pHal.value:
            raise RuntimeError("HAL 接口已释放")
        fn_access = ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p)(self.hal_vtable[VTABLE_HAL_ACCESS])
        return fn_access(self.pHal.value)

    def create_led_devices(self, capacity: int = 16) -> List[AuraHalDevice]:
        """
        调用 CreateLedDevice 枚举键盘设备。
        内置严格的 StdVector 预分配内存边界验证，杜绝溢出。
        """
        if self._released or not self.pHal.value:
            raise RuntimeError("HAL 接口已释放")

        storage = (ctypes.c_void_p * capacity)()
        p_first = ctypes.addressof(storage)
        elem_size = ctypes.sizeof(ctypes.c_void_p)
        p_end = p_first + capacity * elem_size

        vec = StdVector(p_first, p_first, p_end)
        hr = self._fn_create_device(self.pHal.value, ctypes.byref(vec))
        if hr != 0:
            logger.warning("CreateLedDevice 调用返回非零状态码: 0x%08X", hr & 0xFFFFFFFF)
            return []

        # 内存安全校验 (严格对齐 C++ FakeVector 防御逻辑)
        if vec.first != p_first or vec.last is None or vec.last < vec.first or vec.last > vec.end:
            raise RuntimeError("FATAL: CreateLedDevice 破坏了预分配向量边界！底层行为假设不成立。")

        count = (vec.last - vec.first) // elem_size
        devices: List[AuraHalDevice] = []
        for i in range(count):
            dev_ptr = storage[i]
            if dev_ptr:
                devices.append(AuraHalDevice(dev_ptr))

        return devices

    def release(self) -> None:
        """安全释放 HAL 驱动接口并注销 COM 套间"""
        if not self._released and self.pHal.value:
            try:
                self._fn_release(self.pHal.value)
            except Exception as e:
                logger.debug("HAL Release 异常: %s", e)
            finally:
                self.pHal = ctypes.c_void_p()
                self._released = True

        if self._com_initialized:
            ole32.CoUninitialize()
            self._com_initialized = False

    def __enter__(self) -> AuraHal:
        return self

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        self.release()

    def __del__(self) -> None:
        self.release()
