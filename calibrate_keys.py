r"""

# =============================================================================
# 提示: 本脚本为历史阶段性验证/测试工具，包含独立的 ctypes/COM 虚表直调实现。
# 生产与维护环境推荐统一使用官方封装模块 tools.py.aura_hal (tools/py/aura_hal.py)，
# 该模块提供完整的 COM 生命周期管理、边界防御与权威实测键位映射。
# =============================================================================
=============================================================================
ROG FALCHION ACE HFX 物理按键全域巡检、实时探针微调与详细导出系统
=============================================================================
- 覆盖范围: 整把键盘 5 排 68 个物理按键 + 全 128 个底层硬件引脚
- 解决未亮问题:
    1. 【实时探针微调】: 当前键没亮或亮错时，按 [+] / [-] 或 [↑] / [↓]，
       灯光会实时在键盘上逐个引脚跳动，直到看到目标键亮起，按 [Enter] 锁定！
    2. 【标记未亮快捷键】: 按 [N] 键直接将当前键标记为未亮 (UNLIT) 并跳过。
    3. 【硬件全引脚遍历扫描模式】: 支持 `python calibrate_keys.py --mode sweep`，
       从 ID 0 到 127 逐一点亮真机灯珠，遇到未亮空引脚按 [空格] 秒跳过，亮起按键直接录入。
- 导出规范: 详细结构化数据导出 (包含设备元数据、物理排/列、分类、电气矩阵坐标、双向检索表及 Markdown 总表)
=============================================================================
"""

import ctypes
from ctypes import wintypes
import time
import sys
import os
import json
import msvcrt
import threading
import argparse

ole32 = ctypes.oledll.ole32

class GUID(ctypes.Structure):
    _fields_ = [
        ('Data1', wintypes.DWORD),
        ('Data2', wintypes.WORD),
        ('Data3', wintypes.WORD),
        ('Data4', wintypes.BYTE * 8)
    ]

def to_guid(s):
    g = GUID()
    ole32.CLSIDFromString(ctypes.c_wchar_p(s), ctypes.byref(g))
    return g

CLSID_ClaymoreHal = '{AE9DB4C8-4F2A-4756-9B11-2F6D78C61F1A}'
IID_IAsusAacLedDeviceHal = '{F2C8D5B4-3854-4325-8A4F-FD7C5072E3BA}'

class StdVector(ctypes.Structure):
    _fields_ = [("first", ctypes.c_void_p), ("last", ctypes.c_void_p), ("end", ctypes.c_void_p)]

FULL_KEYBOARD_LAYOUT = [
    # === 第 1 排: 顶部数字与功能排 (共 15 键) ===
    (1, 1, "第 1 排 01", "ESC", 1, "Esc"),
    (1, 2, "第 1 排 02", "1", 6, "1 !"),
    (1, 3, "第 1 排 03", "2", 7, "2 @"),
    (1, 4, "第 1 排 04", "3", 9, "3 #"),
    (1, 5, "第 1 排 05", "4", 17, "4 $"),
    (1, 6, "第 1 排 06", "5", 25, "5 %"),
    (1, 7, "第 1 排 07", "6", 33, "6 ^"),
    (1, 8, "第 1 排 08", "7", 41, "7 &"),
    (1, 9, "第 1 排 09", "8", 49, "8 *"),
    (1, 10, "第 1 排 10", "9", 57, "9 ("),
    (1, 11, "第 1 排 11", "0", 65, "0 )"),
    (1, 12, "第 1 排 12", "-", 73, "- _"),
    (1, 13, "第 1 排 13", "=", 81, "= +"),
    (1, 14, "第 1 排 14", "BACKSPACE", 55, "Backspace ⌫"),
    (1, 15, "第 1 排 15", "INS", 70, "Insert"),

    # === 第 2 排: QWERTY 字母排 (共 15 键) ===
    (2, 1, "第 2 排 01", "TAB", 2, "Tab ⇥"),
    (2, 2, "第 2 排 02", "Q", 14, "Q"),
    (2, 3, "第 2 排 03", "W", 18, "W"),
    (2, 4, "第 2 排 04", "E", 10, "E"),
    (2, 5, "第 2 排 05", "R", 18, "R"),
    (2, 6, "第 2 排 06", "T", 26, "T"),
    (2, 7, "第 2 排 07", "Y", 34, "Y"),
    (2, 8, "第 2 排 08", "U", 42, "U"),
    (2, 9, "第 2 排 09", "I", 50, "I"),
    (2, 10, "第 2 排 10", "O", 58, "O"),
    (2, 11, "第 2 排 11", "P", 66, "P"),
    (2, 12, "第 2 排 12", "[", 74, "[ {"),
    (2, 13, "第 2 排 13", "]", 82, "] }"),
    (2, 14, "第 2 排 14", "\\", 62, "\\ |"),
    (2, 15, "第 2 排 15", "DEL", 71, "Delete"),

    # === 第 3 排: ASDF 主键排 (共 14 键) ===
    (3, 1, "第 3 排 01", "CAPS", 3, "Caps Lock ⇪"),
    (3, 2, "第 3 排 02", "A", 11, "A"),
    (3, 3, "第 3 排 03", "S", 19, "S"),
    (3, 4, "第 3 排 04", "D", 27, "D"),
    (3, 5, "第 3 排 05", "F", 35, "F"),
    (3, 6, "第 3 排 06", "G", 43, "G"),
    (3, 7, "第 3 排 07", "H", 51, "H"),
    (3, 8, "第 3 排 08", "J", 59, "J"),
    (3, 9, "第 3 排 09", "K", 67, "K"),
    (3, 10, "第 3 排 10", "L", 75, "L"),
    (3, 11, "第 3 排 11", ";", 67, "; :"),
    (3, 12, "第 3 排 12", "'", 75, "' \""),
    (3, 13, "第 3 排 13", "ENTER", 66, "Enter ↵"),
    (3, 14, "第 3 排 14", "PGUP", 94, "Page Up ⇞"),

    # === 第 4 排: ZXCV 底部字母排 (共 14 键) ===
    (4, 1, "第 4 排 01", "L_SHIFT", 4, "Left Shift ⇧"),
    (4, 2, "第 4 排 02", "Z", 31, "Z"),
    (4, 3, "第 4 排 03", "X", 12, "X"),
    (4, 4, "第 4 排 04", "C", 20, "C"),
    (4, 5, "第 4 排 05", "V", 28, "V"),
    (4, 6, "第 4 排 06", "B", 52, "B"),
    (4, 7, "第 4 排 07", "N", 60, "N"),
    (4, 8, "第 4 排 08", "M", 29, "M"),
    (4, 9, "第 4 排 09", ",", 76, ", <"),
    (4, 10, "第 4 排 10", ".", 84, ". >"),
    (4, 11, "第 4 排 11", "/", 69, "/ ?"),
    (4, 12, "第 4 排 12", "R_SHIFT", 102, "Right Shift ⇧"),
    (4, 13, "第 4 排 13", "UP", 108, "Up Arrow ↑"),
    (4, 14, "第 4 排 14", "PGDN", 95, "Page Down ⇟"),

    # === 第 5 排: 空格、修饰键与方向键排 (共 10 键) ===
    (5, 1, "第 5 排 01", "L_CTRL", 5, "Left Ctrl"),
    (5, 2, "第 5 排 02", "L_WIN", 13, "Left Win ⊞"),
    (5, 3, "第 5 排 03", "L_ALT", 21, "Left Alt"),
    (5, 4, "第 5 排 04", "SPACE", 53, "Spacebar"),
    (5, 5, "第 5 排 05", "R_ALT", 70, "Right Alt"),
    (5, 6, "第 5 排 06", "FN", 85, "Fn"),
    (5, 7, "第 5 排 07", "COPILOT", 93, "Copilot"),
    (5, 8, "第 5 排 08", "LEFT", 101, "Left Arrow ←"),
    (5, 9, "第 5 排 09", "DOWN", 109, "Down Arrow ↓"),
    (5, 10, "第 5 排 10", "RIGHT", 117, "Right Arrow →"),
]

CALIBRATION_FILE = r"g:\Aura\calibrated_keymap.json"

ROW_DESCRIPTIONS = {
    1: "第 1 排 (顶部数字与功能区)",
    2: "第 2 排 (QWERTY 字母区)",
    3: "第 3 排 (ASDF 字母主键区)",
    4: "第 4 排 (ZXCV 底部字母区)",
    5: "第 5 排 (空格、修饰与方向键)"
}

def categorize_key(name):
    u = name.upper()
    if u in ['UP', 'DOWN', 'LEFT', 'RIGHT', 'ARROWS']: return "Arrow"
    if u in ['L_CTRL', 'R_CTRL', 'CTRL', 'L_ALT', 'R_ALT', 'ALT', 'L_SHIFT', 'R_SHIFT', 'SHIFT', 'L_WIN', 'WIN', 'FN', 'COPILOT']: return "Modifier"
    if u in ['DEL', 'DELETE', 'INS', 'INSERT', 'PGUP', 'PAGEUP', 'PGDN', 'PAGEDOWN']: return "Navigation"
    if u in ['ESC', 'ESCAPE', 'ENTER', 'RETURN', 'BACKSPACE', 'BS', 'TAB', 'CAPS', 'CAPSLOCK']: return "Function"
    if u in ['SPACE', 'SPACEBAR']: return "Space"
    if u.isdigit(): return "Numeric"
    if len(u) == 1 and u.isalpha(): return "Alpha"
    return "Symbol"

class KeyboardLightingEngine:
    def __init__(self):
        ole32.CoInitialize(None)
        self.pHal = ctypes.c_void_p()
        hr = ole32.CoCreateInstance(
            ctypes.byref(to_guid(CLSID_ClaymoreHal)),
            None,
            1,
            ctypes.byref(to_guid(IID_IAsusAacLedDeviceHal)),
            ctypes.byref(self.pHal)
        )
        if hr != 0:
            raise RuntimeError(f"初始化 HAL 驱动失败 (0x{hr:X})")
            
        self.hal_vtable = ctypes.cast(
            ctypes.cast(self.pHal.value, ctypes.POINTER(ctypes.c_void_p))[0],
            ctypes.POINTER(ctypes.c_void_p)
        )
        ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p)(self.hal_vtable[4])(self.pHal.value)
        
        self.device_storage = (ctypes.c_void_p * 16)()
        vec = StdVector(
            ctypes.addressof(self.device_storage),
            ctypes.addressof(self.device_storage),
            ctypes.addressof(self.device_storage) + 16 * 8
        )
        ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p, ctypes.POINTER(StdVector))(self.hal_vtable[5])(
            self.pHal.value, ctypes.byref(vec)
        )
        
        self.pDev = self.device_storage[0]
        self.dev_vtable = ctypes.cast(
            ctypes.cast(self.pDev, ctypes.POINTER(ctypes.c_void_p))[0],
            ctypes.POINTER(ctypes.c_void_p)
        )
        
        self.TOTAL_LEDS = 128
        ctypes.cast(self.pDev + 0x6C, ctypes.POINTER(wintypes.DWORD))[0] = self.TOTAL_LEDS
        led_id_table = ctypes.cast(self.pDev + 0x74, ctypes.POINTER(ctypes.c_ubyte))
        for i in range(self.TOTAL_LEDS):
            led_id_table[i] = i
            
        self.rgb_buf = (ctypes.c_ubyte * (self.TOTAL_LEDS * 3))()
        self.fn_set_single = ctypes.WINFUNCTYPE(wintypes.LONG, ctypes.c_void_p, ctypes.c_void_p)(self.dev_vtable[19])
        
        self.running = True
        self.lock = threading.Lock()
        self.thread = threading.Thread(target=self._stream_worker, daemon=True)
        self.thread.start()

    def _stream_worker(self):
        while self.running:
            with self.lock:
                self.fn_set_single(self.pDev, ctypes.byref(self.rgb_buf))
            time.sleep(0.04)

    def set_single_key(self, led_id, r=0, g=220, b=255):
        with self.lock:
            for i in range(self.TOTAL_LEDS * 3):
                self.rgb_buf[i] = 0
            if 0 <= led_id < self.TOTAL_LEDS:
                self.rgb_buf[led_id * 3 + 0] = r
                self.rgb_buf[led_id * 3 + 1] = g
                self.rgb_buf[led_id * 3 + 2] = b

    def flash_feedback(self, led_id, r, g, b, duration=0.15):
        self.set_single_key(led_id, r, g, b)
        time.sleep(duration)

    def clear(self):
        with self.lock:
            for i in range(self.TOTAL_LEDS * 3):
                self.rgb_buf[i] = 0

    def close(self):
        self.running = False
        time.sleep(0.06)
        self.clear()
        self.fn_set_single(self.pDev, ctypes.byref(self.rgb_buf))
        ole32.CoUninitialize()

def load_calibrated_map():
    if os.path.exists(CALIBRATION_FILE):
        try:
            with open(CALIBRATION_FILE, 'r', encoding='utf-8') as f:
                data = json.load(f)
                if 'lookup_table' in data:
                    return data['lookup_table']
                elif 'keys' in data:
                    res = {}
                    for k, v in data['keys'].items():
                        res[k] = v.get('led_id') if isinstance(v, dict) else v
                    return res
                elif isinstance(data, dict):
                    return data
        except Exception:
            pass
    return {}

def save_detailed_calibrated_map(records_dict, device_name="ROG FALCHION ACE HFX"):
    now_str = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime())
    
    keys_detail = {}
    lookup_table = {}
    reverse_id_table = {}
    row_counts = {1: 0, 2: 0, 3: 0, 4: 0, 5: 0}
    
    for k_name, rec in records_dict.items():
        led_id = rec['led_id']
        row_num = rec.get('row', 0)
        col_idx = rec.get('col', 0)
        status = rec.get('status', 'VERIFIED')
        display_label = rec.get('label', k_name)
        
        if row_num in row_counts:
            row_counts[row_num] = row_counts.get(row_num, 0) + 1
        category = categorize_key(k_name)
        
        matrix_col = led_id // 8 if isinstance(led_id, int) else None
        matrix_row = led_id % 8 if isinstance(led_id, int) else None
        
        keys_detail[k_name] = {
            "key_name": k_name,
            "display_label": display_label,
            "physical_row": row_num,
            "physical_col": col_idx,
            "row_description": ROW_DESCRIPTIONS.get(row_num, f"第 {row_num} 排"),
            "category": category,
            "led_id": led_id,
            "matrix_hardware_coords": {
                "matrix_col": matrix_col,
                "matrix_row": matrix_row,
                "formula": f"col * 8 + row = {matrix_col} * 8 + {matrix_row} = {led_id}" if matrix_col is not None else "N/A"
            },
            "status": status,
            "calibrated_time": rec.get('time', now_str)
        }
        
        if isinstance(led_id, int):
            lookup_table[k_name] = led_id
            reverse_id_table[str(led_id)] = k_name

    export_data = {
        "device_profile": {
            "device_name": device_name,
            "device_name_zh": "ROG 魔导士 ACE HFX 竞技版磁轴键盘",
            "model_id": 7038,
            "usb_pid": "0x1B7E",
            "usb_vid": "0x0B05",
            "total_physical_keys": len(keys_detail),
            "layout_form_factor": "65% Compact (68-Key ANSI)",
            "lighting_interface": "USB HID Interface 3 (UsagePage 0xFF00, Usage 0x01)",
            "protocol_command": "Set_L_STD_SINGLE_XY (0x81 0xC0, 15-keys/packet)",
            "operating_mode": "Volatile High-Speed Direct RAM Stream (Zero EEPROM Wear)",
            "export_version": "2.0.0",
            "last_exported_at": now_str
        },
        "summary": {
            "total_calibrated_keys": len(keys_detail),
            "rows_breakdown": {
                "row_1_top_numeric": row_counts.get(1, 0),
                "row_2_qwerty": row_counts.get(2, 0),
                "row_3_home_asdf": row_counts.get(3, 0),
                "row_4_bottom_zxcv": row_counts.get(4, 0),
                "row_5_modifier_space_arrows": row_counts.get(5, 0)
            }
        },
        "keys": keys_detail,
        "lookup_table": lookup_table,
        "reverse_id_table": reverse_id_table
    }

    with open(CALIBRATION_FILE, 'w', encoding='utf-8') as f:
        json.dump(export_data, f, indent=2, ensure_ascii=False)

    md_file = CALIBRATION_FILE.replace('.json', '.md')
    with open(md_file, 'w', encoding='utf-8') as f:
        f.write(f"# {device_name} 物理按键硬件校准全貌详细报告\n\n")
        f.write(f"- **设备型号**: ROG FALCHION ACE HFX (PID: `0x1B7E`, Model: 7038)\n")
        f.write(f"- **总校准按键数**: {len(keys_detail)} 键\n")
        f.write(f"- **导出时间**: {now_str}\n")
        f.write(f"- **通讯协议**: 原生底层 USB HID 接口 3 帧直控 (`0x81 0xC0`)\n")
        f.write(f"- **安全等级**: 易失性 RAM 高速推流 (零 Flash 磨损)\n\n")
        f.write("---\n\n")
        f.write("## 物理按键与硬件 LED ID 详细对照总表\n\n")
        f.write("| 排号 | 排内序号 | 按键标识 | 键帽字符 | 分类 | 硬件 LED ID | 电气矩阵 (Col, Row) | 校准状态 |\n")
        f.write("| :---: | :---: | :--- | :---: | :---: | :---: | :---: | :---: |\n")
        for k, v in keys_detail.items():
            m = v['matrix_hardware_coords']
            col_str = f"{m['matrix_col']}" if m['matrix_col'] is not None else "-"
            row_str = f"{m['matrix_row']}" if m['matrix_row'] is not None else "-"
            f.write(f"| {v['physical_row']} | {v['physical_col']:02d} | **{k}** | {v['display_label']} | {v['category']} | `{v['led_id']}` | `({col_str}, {row_str})` | {v['status']} |\n")
        f.write("\n---\n*由 ROG Falchion Ace HFX Per-Key Calibrator 自动生成导出*\n")

def get_input_action(expected_key):
    last_enter_time = 0

    while True:
        if msvcrt.kbhit():
            ch = msvcrt.getwch()
            now = time.time()

            # 扩展键 (方向键)
            if ch in ('\x00', '\xe0'):
                ext = msvcrt.getwch()
                if ext == 'H':  # Up 键 -> 探针自增
                    return ('PROBE_INC', None)
                elif ext == 'P':  # Down 键 -> 探针自减
                    return ('PROBE_DEC', None)
                continue

            # 探针微调热键: + / -
            if ch in ('+', '='):
                return ('PROBE_INC', None)
            if ch in ('-', '_'):
                return ('PROBE_DEC', None)

            # 标记未亮 (No light)
            if ch.lower() == 'n':
                return ('UNLIT', None)

            # 跳过当前
            if ch.lower() == 's':
                return ('SKIP', None)

            # 退出
            if ch.lower() == 'q':
                return ('QUIT', None)

            # 返回上一步
            if ch.lower() == 'b':
                return ('BACK', None)

            # 回车键检测
            if ch == '\r' or ch == '\n':
                if now - last_enter_time < 0.65:
                    return ('RECORD', None)
                else:
                    last_enter_time = now
                    time.sleep(0.05)
                    continue

            # 空格键确认
            if ch == ' ':
                return ('NEXT', None)

            # 用户直接按下物理键
            if expected_key and len(expected_key) == 1 and ch.upper() == expected_key.upper():
                return ('NEXT', None)

            if last_enter_time > 0 and (now - last_enter_time >= 0.65):
                return ('NEXT', None)
        else:
            now = time.time()
            if last_enter_time > 0 and (now - last_enter_time >= 0.65):
                return ('NEXT', None)
            time.sleep(0.02)

def run_sweep_mode(engine):
    """硬件引脚逐一扫描模式: 从 ID 0 扫到 127"""
    print("\n" + "=" * 76)
    print(" 📡 硬件引脚全域逐一扫描模式 (ID 0 ~ 127)")
    print("=" * 76)
    print(" [说明]: 系统将逐一点亮每一个硬件引脚，遇到没亮的空引脚，按 [空格] 瞬间跳过！")
    print(" [操作]: 看到键亮起 -> 输入按键名回车录入 | 未亮空针脚 -> 按 [空格] 跳过 | [Q] 退出")
    print("=" * 76)
    
    records = {}
    saved_lookup = load_calibrated_map()
    rev_map = {v: k for k, v in saved_lookup.items() if isinstance(v, int)}
    
    NAME_TO_LAYOUT = {item[3]: item for item in FULL_KEYBOARD_LAYOUT}
    
    for lid in range(128):
        engine.set_single_key(lid, 0, 220, 255)
        known = rev_map.get(lid, "未知/待录入")
        print(f"\n[引脚 {lid:3d}/127] 当前通电 ID: [{lid}] | 历史已知: [{known}]")
        print("  ▶ 观察键盘: [按空格=空引脚跳过] | [直接敲键盘输入键名后按 Enter] | [Q=退出]")
        print("  请输入按键名: ", end="", flush=True)
        
        buf = []
        while True:
            if msvcrt.kbhit():
                ch = msvcrt.getwch()
                
                # 1. 忽略功能键扫描码
                if ch in ('\x00', '\xe0'):
                    msvcrt.getwch()
                    continue
                    
                # 2. 回车提交
                if ch == '\r' or ch == '\n':
                    name = "".join(buf).strip().upper()
                    print()
                    if name:
                        meta = NAME_TO_LAYOUT.get(name)
                        row = meta[0] if meta else 0
                        col = meta[1] if meta else 0
                        lbl = meta[5] if meta else name
                        records[name] = {
                            "name": name, "led_id": lid, "row": row, "col": col,
                            "label": lbl, "status": "SWEEP_IDENTIFIED", "verified": True
                        }
                        rev_map[lid] = name
                        save_detailed_calibrated_map(records)
                        print(f"  [✓ 绑定成功] 按键 [{name}] -> ID [{lid}]")
                        engine.flash_feedback(lid, 0, 255, 0, duration=0.2)
                    else:
                        print("  [>] 未输入内容，已跳过")
                    break
                    
                # 3. 空格键快速跳过 (仅当输入框为空时作为跳过指令)
                elif ch == ' ':
                    if not buf:
                        print("  [>] 空引脚/未亮，已跳过")
                        break
                    else:
                        buf.append(' ')
                        sys.stdout.write(' ')
                        sys.stdout.flush()
                        
                # 4. 退格键删除字符
                elif ch == '\b':
                    if buf:
                        buf.pop()
                        sys.stdout.write('\b \b')
                        sys.stdout.flush()
                        
                # 5. Q 键退出 (仅当未开始输入字符时触发)
                elif ch.lower() == 'q' and not buf:
                    print("\n[*] 正在保存扫描成果并退出...")
                    save_detailed_calibrated_map(records)
                    return
                    
                # 6. 普通字符输入
                else:
                    buf.append(ch)
                    sys.stdout.write(ch)
                    sys.stdout.flush()
                    
            time.sleep(0.01)
            
    save_detailed_calibrated_map(records)
    print("\n[✓] 128 引脚扫描已全部完成！")

def main():
    parser = argparse.ArgumentParser(description="ROG FALCHION ACE HFX 硬件级全键盘巡检与校准系统")
    parser.add_argument("--mode", choices=['layout', 'sweep'], default='layout',
                        help="模式选择 (layout: 按键盘5排物理顺序校准带探针微调; sweep: 按0~127硬件引脚逐一扫描)")
    parser.add_argument("--row", type=int, choices=[1, 2, 3, 4, 5], default=None,
                        help="仅校准指定排 (1:顶部排, 2:QWERTY排, 3:ASDF排, 4:ZXCV排, 5:空格底排)")
    parser.add_argument("--start", type=int, default=1, help="从第几个键开始 (1 ~ 68)")
    args = parser.parse_args()

    engine = KeyboardLightingEngine()

    if args.mode == 'sweep':
        try:
            run_sweep_mode(engine)
        finally:
            engine.close()
        return

    # Layout 模式 (带实时探针微调与未亮标记)
    saved_lookup = load_calibrated_map()

    runtime_records = {}
    for row, col, pos, name, def_id, label in FULL_KEYBOARD_LAYOUT:
        lid = saved_lookup.get(name, def_id)
        runtime_records[name] = {
            "name": name,
            "led_id": lid,
            "row": row,
            "col": col,
            "label": label,
            "status": "CONFIRMED" if name in saved_lookup else "PENDING",
            "verified": name in saved_lookup
        }

    scan_queue = list(FULL_KEYBOARD_LAYOUT)
    if args.row is not None:
        scan_queue = [item for item in scan_queue if item[0] == args.row]

    start_idx = max(0, min(len(scan_queue) - 1, args.start - 1))

    print("=" * 76)
    print(" 🎮 ROG FALCHION ACE HFX 物理按键巡检校准系统 (支持【实时探针微调】)")
    print("=" * 76)
    print(" [核心解决未亮特性]:")
    print("   🔎 没亮或亮错？直接按 [+] / [-] 或 [↑] / [↓] 实时微调切换 ID，直到键亮起按 Enter！")
    print("   ○ 遇到无灯按键？按 [N] 键立即标记为未亮 (UNLIT) 并自动跳到下一个！")
    print("   ✓ 亮起符合预期？按物理按键 或 按 [空格] / [Enter 1次] 立即下一步！")
    print("   ✎ 需修正键名？快速按下两次 [Enter] (双击回车) 输入实际键名！")
    print("   ⏮ [B] 上一个  |  [S] 跳过当前  |  [Q] 保存详细档案并退出")
    print("=" * 76)

    idx = start_idx
    total = len(scan_queue)
    current_row = -1

    try:
        while idx < total:
            row, col, pos, expected_name, def_id, label = scan_queue[idx]
            current_led_id = runtime_records[expected_name]['led_id']

            if row != current_row:
                current_row = row
                print("\n" + "=" * 76)
                print(f"  ▼▼▼ 开始巡检: {ROW_DESCRIPTIONS.get(row, f'第 {row} 排')} ▼▼▼")
                print("=" * 76)

            engine.set_single_key(current_led_id, 0, 220, 255)

            print(f"\n[{idx + 1}/{total}] {pos} | 预期按键: [{expected_name}] ({label}) | 硬件尝试 ID: [{current_led_id}]")
            print("  ▶ 键盘观察: 按键亮了吗？")
            print("    [按物理键 / 空格 / Enter] = 确认无误  |  [+ / -] = 探针微调寻找  |  [N] = 标记未亮跳过  |  [双击Enter] = 纠错")

            while True:
                action, _ = get_input_action(expected_name)

                if action == 'PROBE_INC':
                    current_led_id = (current_led_id + 1) % 128
                    engine.set_single_key(current_led_id, 0, 220, 255)
                    print(f"\r  [🔎 探针微调] 切换至 ID: [{current_led_id:3d}]  (按 + / - 继续寻找, 按 Enter 锁定绑定, 按 N 跳过)  ", end="", flush=True)
                    continue

                elif action == 'PROBE_DEC':
                    current_led_id = (current_led_id - 1) % 128
                    engine.set_single_key(current_led_id, 0, 220, 255)
                    print(f"\r  [🔎 探针微调] 切换至 ID: [{current_led_id:3d}]  (按 + / - 继续寻找, 按 Enter 锁定绑定, 按 N 跳过)  ", end="", flush=True)
                    continue

                elif action == 'UNLIT':
                    print(f"\n  [○ 标记未亮] 按键 [{expected_name}] 已标记为未亮 (UNLIT)")
                    runtime_records[expected_name]['status'] = "UNLIT"
                    runtime_records[expected_name]['verified'] = False
                    save_detailed_calibrated_map(runtime_records)
                    idx += 1
                    break

                elif action == 'SKIP':
                    print("  [>] 已跳过当前按键")
                    idx += 1
                    break

                elif action == 'BACK':
                    if idx > 0:
                        idx -= 1
                    break

                elif action == 'QUIT':
                    print("\n[*] 正在导出详细校准档案并退出...")
                    save_detailed_calibrated_map(runtime_records)
                    print(f"[✓] 详细档案已保存: {CALIBRATION_FILE}")
                    idx = total + 1
                    break

                elif action == 'NEXT':
                    runtime_records[expected_name]['led_id'] = current_led_id
                    runtime_records[expected_name]['status'] = "VERIFIED_WITH_PROBE" if current_led_id != def_id else "VERIFIED"
                    runtime_records[expected_name]['verified'] = True
                    save_detailed_calibrated_map(runtime_records)
                    engine.flash_feedback(current_led_id, 0, 255, 0, duration=0.15)
                    if current_led_id != def_id:
                        print(f"\n  [✓ 探针锁定成功] [{expected_name}] 已校准并绑定至物理 ID [{current_led_id}]！")
                    idx += 1
                    break

                elif action == 'RECORD':
                    engine.set_single_key(current_led_id, 255, 255, 0)
                    print(f"\n  [✎ 录入模式] 当前发光硬件 ID 为: [{current_led_id}]")
                    actual_name = input("  请输入此灯珠实际对应的按键名 (例如 W, Space, Up, Del...): ").strip().upper()
                    if actual_name:
                        runtime_records[actual_name] = {
                            "name": actual_name, "led_id": current_led_id,
                            "row": row, "col": col, "label": actual_name,
                            "status": "MANUALLY_CALIBRATED", "verified": True,
                            "time": time.strftime('%Y-%m-%d %H:%M:%S', time.localtime())
                        }
                        save_detailed_calibrated_map(runtime_records)
                        print(f"  [✓ 录入成功] [{actual_name}] -> ID [{current_led_id}]")
                        engine.flash_feedback(current_led_id, 255, 0, 255, duration=0.2)
                    idx += 1
                    break

        if idx == total:
            save_detailed_calibrated_map(runtime_records)
            print("\n" + "=" * 76)
            print(" 🎉 本轮巡检全部完成！详细档案已保存！")
            print("=" * 76)

    except KeyboardInterrupt:
        print("\n[*] 用户中断运行，已保存最新进度。")
        save_detailed_calibrated_map(runtime_records)
    finally:
        engine.close()
        print("[+] 驱动已断开，灯效已复位。")

if __name__ == '__main__':
    main()
