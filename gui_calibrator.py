r"""
=============================================================================
ROG FALCHION ACE HFX 硬件级独立物理按键 RGB 图形化校准系统 (GUI Calibrator)
=============================================================================
- 纯鼠标可视化操作: 点击屏幕上的按键即可完成物理绑定
- 交互逻辑:
    1. 顶部控制条可切换当前硬件 LED ID (支持 上一个/下一个/滑动条/自动巡检播放)
    2. 观察物理键盘哪颗键亮起了冰蓝色
    3. 用鼠标直接点击屏幕上对应的按键 -> 立即完成绑定并自动步进下一个 ID！
    4. 若当前引脚未亮(空针脚)，直接点击 [未亮/跳过] 即可
    5. 点击 [导出详细档案] 可随时导出 JSON + Markdown 对照总表报告
=============================================================================
"""

import ctypes
from ctypes import wintypes
import time
import sys
import os
import json
import threading
import tkinter as tk
from tkinter import ttk, messagebox

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

# -------------------------------------------------------------------------
# ROG FALCHION ACE HFX 65% 标准物理键盘布局几何规格定义
# 每键: (key_name, display_label, width_units, physical_row, physical_col, category)
# -------------------------------------------------------------------------
GUI_LAYOUT = [
    # Row 1 (15 键, 总宽 16u)
    [
        ("ESC", "Esc", 1.0, 1, 1, "Function"),
        ("1", "1", 1.0, 1, 2, "Numeric"),
        ("2", "2", 1.0, 1, 3, "Numeric"),
        ("3", "3", 1.0, 1, 4, "Numeric"),
        ("4", "4", 1.0, 1, 5, "Numeric"),
        ("5", "5", 1.0, 1, 6, "Numeric"),
        ("6", "6", 1.0, 1, 7, "Numeric"),
        ("7", "7", 1.0, 1, 8, "Numeric"),
        ("8", "8", 1.0, 1, 9, "Numeric"),
        ("9", "9", 1.0, 1, 10, "Numeric"),
        ("0", "0", 1.0, 1, 11, "Numeric"),
        ("-", "-", 1.0, 1, 12, "Symbol"),
        ("=", "=", 1.0, 1, 13, "Symbol"),
        ("BACKSPACE", "Backspace", 2.0, 1, 14, "Function"),
        ("INS", "Ins", 1.0, 1, 15, "Navigation"),
    ],
    # Row 2 (15 键, 总宽 16u)
    [
        ("TAB", "Tab", 1.5, 2, 1, "Function"),
        ("Q", "Q", 1.0, 2, 2, "Alpha"),
        ("W", "W", 1.0, 2, 3, "Alpha"),
        ("E", "E", 1.0, 2, 4, "Alpha"),
        ("R", "R", 1.0, 2, 5, "Alpha"),
        ("T", "T", 1.0, 2, 6, "Alpha"),
        ("Y", "Y", 1.0, 2, 7, "Alpha"),
        ("U", "U", 1.0, 2, 8, "Alpha"),
        ("I", "I", 1.0, 2, 9, "Alpha"),
        ("O", "O", 1.0, 2, 10, "Alpha"),
        ("P", "P", 1.0, 2, 11, "Alpha"),
        ("[", "[", 1.0, 2, 12, "Symbol"),
        ("]", "]", 1.0, 2, 13, "Symbol"),
        ("\\", "\\", 1.5, 2, 14, "Symbol"),
        ("DEL", "Del", 1.0, 2, 15, "Navigation"),
    ],
    # Row 3 (14 键, 总宽 16u)
    [
        ("CAPS", "Caps", 1.75, 3, 1, "Function"),
        ("A", "A", 1.0, 3, 2, "Alpha"),
        ("S", "S", 1.0, 3, 3, "Alpha"),
        ("D", "D", 1.0, 3, 4, "Alpha"),
        ("F", "F", 1.0, 3, 5, "Alpha"),
        ("G", "G", 1.0, 3, 6, "Alpha"),
        ("H", "H", 1.0, 3, 7, "Alpha"),
        ("J", "J", 1.0, 3, 8, "Alpha"),
        ("K", "K", 1.0, 3, 9, "Alpha"),
        ("L", "L", 1.0, 3, 10, "Alpha"),
        (";", ";", 1.0, 3, 11, "Symbol"),
        ("'", "'", 1.0, 3, 12, "Symbol"),
        ("ENTER", "Enter", 2.25, 3, 13, "Function"),
        ("PGUP", "PgUp", 1.0, 3, 14, "Navigation"),
    ],
    # Row 4 (14 键, 总宽 16u)
    [
        ("L_SHIFT", "Shift", 2.25, 4, 1, "Modifier"),
        ("Z", "Z", 1.0, 4, 2, "Alpha"),
        ("X", "X", 1.0, 4, 3, "Alpha"),
        ("C", "C", 1.0, 4, 4, "Alpha"),
        ("V", "V", 1.0, 4, 5, "Alpha"),
        ("B", "B", 1.0, 4, 6, "Alpha"),
        ("N", "N", 1.0, 4, 7, "Alpha"),
        ("M", "M", 1.0, 4, 8, "Alpha"),
        (",", ",", 1.0, 4, 9, "Symbol"),
        (".", ".", 1.0, 4, 10, "Symbol"),
        ("/", "/", 1.0, 4, 11, "Symbol"),
        ("R_SHIFT", "Shift", 1.75, 4, 12, "Modifier"),
        ("UP", "↑", 1.0, 4, 13, "Arrow"),
        ("PGDN", "PgDn", 1.0, 4, 14, "Navigation"),
    ],
    # Row 5 (10 键, 总宽 16u)
    [
        ("L_CTRL", "Ctrl", 1.25, 5, 1, "Modifier"),
        ("L_WIN", "Win", 1.25, 5, 2, "Modifier"),
        ("L_ALT", "Alt", 1.25, 5, 3, "Modifier"),
        ("SPACE", "Space", 6.25, 5, 4, "Space"),
        ("R_ALT", "Alt", 1.0, 5, 5, "Modifier"),
        ("FN", "Fn", 1.0, 5, 6, "Modifier"),
        ("COPILOT", "Copilot", 1.0, 5, 7, "Modifier"),
        ("LEFT", "←", 1.0, 5, 8, "Arrow"),
        ("DOWN", "↓", 1.0, 5, 9, "Arrow"),
        ("RIGHT", "→", 1.0, 5, 10, "Arrow"),
    ]
]

CALIBRATION_FILE = r"g:\Aura\calibrated_keymap.json"

ROW_DESCRIPTIONS = {
    1: "第 1 排 (顶部数字与功能区)",
    2: "第 2 排 (QWERTY 字母区)",
    3: "第 3 排 (ASDF 字母主键区)",
    4: "第 4 排 (ZXCV 底部字母区)",
    5: "第 5 排 (空格、修饰与方向键)"
}

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
        def _flash():
            self.set_single_key(led_id, r, g, b)
            time.sleep(duration)
        threading.Thread(target=_flash, daemon=True).start()

    def fill_all(self, r, g, b):
        with self.lock:
            for i in range(self.TOTAL_LEDS):
                self.rgb_buf[i * 3 + 0] = r
                self.rgb_buf[i * 3 + 1] = g
                self.rgb_buf[i * 3 + 2] = b

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
        category = rec.get('category', 'General')
        
        if row_num in row_counts:
            row_counts[row_num] = row_counts.get(row_num, 0) + 1
            
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
        f.write("\n---\n*由 ROG Falchion Ace HFX Per-Key Calibrator GUI 自动生成导出*\n")

class KeyboardCalibratorGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("ROG FALCHION ACE HFX 硬件级独立单键 RGB 可视化校准系统")
        self.root.geometry("1100x680")
        self.root.minsize(1000, 620)
        self.root.configure(bg="#1E1E24")

        # 初始化驱动
        self.engine = KeyboardLightingEngine()

        # 状态数据
        self.current_id = 1
        self.is_playing = False
        self.play_timer = None
        self.calibrated_data = {}
        self.key_widgets = {}

        # 载入已有校准记录
        saved_lookup = load_calibrated_map()

        # 初始化所有 68 键元数据字典
        for row in GUI_LAYOUT:
            for k_name, label, w, r, c, cat in row:
                lid = saved_lookup.get(k_name, None)
                self.calibrated_data[k_name] = {
                    "name": k_name,
                    "label": label,
                    "row": r,
                    "col": c,
                    "category": cat,
                    "led_id": lid,
                    "status": "VERIFIED" if lid is not None else "UNMAPPED",
                    "time": time.strftime('%Y-%m-%d %H:%M:%S', time.localtime())
                }

        self._build_ui()
        self._set_hardware_led(self.current_id)

    def _build_ui(self):
        # 1. 顶部标题与说明面板
        header_frame = tk.Frame(self.root, bg="#18181C", padx=15, pady=10)
        header_frame.pack(fill=tk.X)

        title_lbl = tk.Label(
            header_frame,
            text="🎮 ROG FALCHION ACE HFX 纯鼠标可视化单键校准工具",
            font=("Segoe UI", 14, "bold"),
            fg="#00E676",
            bg="#18181C"
        )
        title_lbl.pack(anchor="w")

        hint_lbl = tk.Label(
            header_frame,
            text="【使用说明】: 观察真实键盘上哪颗键亮起了冰蓝光，直接用鼠标点击下方对应的虚拟按键即可完成绑定并自动下一步！未亮空引脚点击 [跳过]。",
            font=("Segoe UI", 9),
            fg="#B0BEC5",
            bg="#18181C"
        )
        hint_lbl.pack(anchor="w", pady=(2, 0))

        # 2. 核心控制栏 (引脚步进控制)
        control_frame = tk.Frame(self.root, bg="#25262E", padx=15, pady=12)
        control_frame.pack(fill=tk.X, padx=15, pady=(12, 0))

        # 上一个按钮
        self.prev_btn = tk.Button(
            control_frame, text="◀ 上一个 (ID - 1)", font=("Segoe UI", 10, "bold"),
            bg="#37474F", fg="white", activebackground="#455A64", activeforeground="white",
            relief=tk.FLAT, padx=12, pady=6, cursor="hand2", command=self.prev_id
        )
        self.prev_btn.pack(side=tk.LEFT)

        # 当前通电 ID 大标签显示
        self.id_display_lbl = tk.Label(
            control_frame, text=f"当前通电引脚: ID [ {self.current_id:3d} ]",
            font=("Consolas", 15, "bold"), fg="#00E5FF", bg="#1A1B20",
            padx=20, pady=5, relief=tk.RIDGE
        )
        self.id_display_lbl.pack(side=tk.LEFT, padx=15)

        # 下一个按钮
        self.next_btn = tk.Button(
            control_frame, text="下一个 (ID + 1) ▶", font=("Segoe UI", 10, "bold"),
            bg="#0288D1", fg="white", activebackground="#039BE5", activeforeground="white",
            relief=tk.FLAT, padx=12, pady=6, cursor="hand2", command=self.next_id
        )
        self.next_btn.pack(side=tk.LEFT)

        # 跳过空引脚按钮
        self.skip_btn = tk.Button(
            control_frame, text="○ 此引脚未亮 (跳过)", font=("Segoe UI", 10),
            bg="#455A64", fg="#CFD8DC", activebackground="#546E7A", activeforeground="white",
            relief=tk.FLAT, padx=14, pady=6, cursor="hand2", command=self.skip_id
        )
        self.skip_btn.pack(side=tk.LEFT, padx=12)

        # 自动巡检播放/暂停按钮
        self.play_btn = tk.Button(
            control_frame, text="▷ 自动巡检播放", font=("Segoe UI", 10, "bold"),
            bg="#5E35B1", fg="white", activebackground="#673AB7", activeforeground="white",
            relief=tk.FLAT, padx=12, pady=6, cursor="hand2", command=self.toggle_play
        )
        self.play_btn.pack(side=tk.LEFT, padx=(0, 15))

        # 滑动条快速定位
        tk.Label(control_frame, text="滑动直达:", font=("Segoe UI", 9), fg="#90A4AE", bg="#25262E").pack(side=tk.LEFT)
        self.id_slider = ttk.Scale(control_frame, from_=0, to=127, orient=tk.HORIZONTAL, command=self.on_slider_move)
        self.id_slider.set(self.current_id)
        self.id_slider.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=8)

        # 3. 虚拟键盘可视化面板 (65% 配列)
        keyboard_container = tk.Frame(self.root, bg="#1E1E24", padx=15, pady=10)
        keyboard_container.pack(fill=tk.BOTH, expand=True)

        keyboard_frame = tk.Frame(keyboard_container, bg="#131316", padx=16, pady=16, relief=tk.GROOVE, bd=1)
        keyboard_frame.pack(expand=True)

        UNIT_WIDTH = 54  # 1u 像素宽度
        KEY_HEIGHT = 46  # 按键像素高度

        for r_idx, row_keys in enumerate(GUI_LAYOUT):
            row_frame = tk.Frame(keyboard_frame, bg="#131316")
            row_frame.pack(fill=tk.X, pady=2)

            for key_name, label, w_units, p_row, p_col, cat in row_keys:
                kw = int(w_units * UNIT_WIDTH)
                
                # 使用固定像素 Frame 确保各排按键比例与物理键盘 100% 精确对齐
                btn_container = tk.Frame(row_frame, width=kw, height=KEY_HEIGHT, bg="#131316")
                btn_container.pack(side=tk.LEFT, padx=2)
                btn_container.pack_propagate(False)
                
                k_btn = tk.Button(
                    btn_container,
                    text=f"{label}\n--",
                    font=("Segoe UI", 8, "bold"),
                    fg="#CFD8DC",
                    bg="#2A2B36",
                    activebackground="#00E5FF",
                    activeforeground="black",
                    relief=tk.RAISED,
                    bd=1,
                    cursor="hand2",
                    command=lambda kn=key_name: self.on_key_clicked(kn)
                )
                k_btn.pack(fill=tk.BOTH, expand=True)
                # 绑定鼠标右键测试发光
                k_btn.bind("<Button-3>", lambda e, kn=key_name: self.on_key_right_clicked(kn))
                self.key_widgets[key_name] = k_btn

        self._refresh_keyboard_colors()

        # 4. 底部状态与操作栏
        bottom_frame = tk.Frame(self.root, bg="#18181C", padx=15, pady=10)
        bottom_frame.pack(fill=tk.X, side=tk.BOTTOM)

        self.status_lbl = tk.Label(
            bottom_frame,
            text=f"就绪: 当前引脚 ID {self.current_id} 已在键盘上发光，请在屏幕键盘上点击对应的物理键。",
            font=("Segoe UI", 10),
            fg="#81C784",
            bg="#18181C"
        )
        self.status_lbl.pack(side=tk.LEFT, fill=tk.X, expand=True)

        # 全部点亮测试按钮
        self.test_all_btn = tk.Button(
            bottom_frame, text="💡 全盘常亮测试", font=("Segoe UI", 9),
            bg="#37474F", fg="white", relief=tk.FLAT, padx=10, pady=4, cursor="hand2",
            command=self.test_all_keys
        )
        self.test_all_btn.pack(side=tk.LEFT, padx=6)

        # 导出详细档案按钮
        self.save_btn = tk.Button(
            bottom_frame, text="💾 导出详细档案 (JSON+MD)", font=("Segoe UI", 9, "bold"),
            bg="#2E7D32", fg="white", activebackground="#388E3C", relief=tk.FLAT, padx=12, pady=4, cursor="hand2",
            command=self.save_and_export
        )
        self.save_btn.pack(side=tk.RIGHT, padx=6)

        # 退出按钮
        self.exit_btn = tk.Button(
            bottom_frame, text="✕ 退出", font=("Segoe UI", 9),
            bg="#C62828", fg="white", activebackground="#D32F2F", relief=tk.FLAT, padx=10, pady=4, cursor="hand2",
            command=self.on_close
        )
        self.exit_btn.pack(side=tk.RIGHT)

        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

    def _refresh_keyboard_colors(self):
        """刷新屏幕上按键的显示文字与颜色"""
        for k_name, rec in self.calibrated_data.items():
            btn = self.key_widgets.get(k_name)
            if not btn:
                continue
            lid = rec.get('led_id')
            label = rec.get('label', k_name)

            if lid is not None:
                btn.config(
                    text=f"{label}\nID:{lid}",
                    bg="#1B5E20",  # 深绿底色表示已校准
                    fg="#A5D6A7"
                )
            else:
                btn.config(
                    text=f"{label}\n--",
                    bg="#2A2B36",  # 灰黑底色表示未绑定
                    fg="#B0BEC5"
                )

    def _set_hardware_led(self, lid):
        """下发硬件单键发光，并同步更新界面状态"""
        self.current_id = max(0, min(127, lid))
        self.engine.set_single_key(self.current_id, 0, 220, 255)
        self.id_display_lbl.config(text=f"当前通电引脚: ID [ {self.current_id:3d} ]")
        self.id_slider.set(self.current_id)

    def next_id(self):
        """步进至下一个 ID"""
        self._set_hardware_led((self.current_id + 1) % 128)
        self.status_lbl.config(
            text=f"已切换至 ID {self.current_id}，请观察键盘哪颗按键亮起并点击屏幕按键。",
            fg="#80D8FF"
        )

    def prev_id(self):
        """返回上一个 ID"""
        self._set_hardware_led((self.current_id - 1) % 128)
        self.status_lbl.config(
            text=f"已切换至 ID {self.current_id}，请观察键盘哪颗按键亮起并点击屏幕按键。",
            fg="#80D8FF"
        )

    def skip_id(self):
        """跳过当前未亮/空引脚"""
        self.status_lbl.config(text=f"已跳过未亮引脚 ID {self.current_id}。", fg="#B0BEC5")
        self.next_id()

    def on_slider_move(self, val):
        """滑动条快速跳转"""
        new_id = int(float(val))
        if new_id != self.current_id:
            self._set_hardware_led(new_id)

    def on_key_clicked(self, key_name):
        """鼠标点击屏幕虚拟按键核心事件"""
        # 将当前通电 ID 绑定给点击的按键
        self.calibrated_data[key_name]['led_id'] = self.current_id
        self.calibrated_data[key_name]['status'] = "MOUSE_CALIBRATED"
        self.calibrated_data[key_name]['time'] = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime())

        # 绿色闪烁反馈
        self.engine.flash_feedback(self.current_id, 0, 255, 0, duration=0.15)

        # 实时自动导出详尽数据
        save_detailed_calibrated_map(self.calibrated_data)
        self._refresh_keyboard_colors()

        # 状态更新提示
        self.status_lbl.config(
            text=f"✓ 成功将物理按键 [{key_name}] 绑定至硬件 ID [{self.current_id}]！数据已实时保存。",
            fg="#00E676"
        )

        # 自动步进到下一个硬件 ID
        self.next_id()

    def on_key_right_clicked(self, key_name):
        """鼠标右键点击屏幕按键: 测试并单独点亮此键已绑定的物理灯珠"""
        rec = self.calibrated_data.get(key_name)
        if rec and rec.get('led_id') is not None:
            lid = rec['led_id']
            self._set_hardware_led(lid)
            self.status_lbl.config(
                text=f"正在测试按键 [{key_name}] (已绑定 ID: {lid})，已在键盘上点亮冰蓝色发光。",
                fg="#FFD54F"
            )
        else:
            self.status_lbl.config(
                text=f"按键 [{key_name}] 尚未绑定任何硬件 ID。请先在键盘点亮时左键点击它绑定。",
                fg="#FF8A80"
            )

    def toggle_play(self):
        """自动巡检播放/暂停切换"""
        if not self.is_playing:
            self.is_playing = True
            self.play_btn.config(text="⏸ 暂停巡检", bg="#D81B60")
            self._auto_step()
        else:
            self.is_playing = False
            self.play_btn.config(text="▷ 自动巡检播放", bg="#5E35B1")
            if self.play_timer:
                self.root.after_cancel(self.play_timer)

    def _auto_step(self):
        if self.is_playing:
            self.next_id()
            self.play_timer = self.root.after(1600, self._auto_step)

    def test_all_keys(self):
        """一键全盘常亮测试 (5秒)"""
        self.status_lbl.config(text="正在进行全键盘常亮测试 (5秒)...", fg="#FFD54F")
        self.engine.fill_all(0, 220, 255)
        self.root.after(5000, lambda: self._set_hardware_led(self.current_id))

    def save_and_export(self):
        """手动点击保存与导出"""
        save_detailed_calibrated_map(self.calibrated_data)
        messagebox.showinfo(
            "导出成功",
            f"物理按键详细校准档案已成功导出至：\n\n1. {CALIBRATION_FILE}\n2. {CALIBRATION_FILE.replace('.json', '.md')}"
        )

    def on_close(self):
        """关闭退出清理"""
        if self.is_playing and self.play_timer:
            self.root.after_cancel(self.play_timer)
        save_detailed_calibrated_map(self.calibrated_data)
        self.engine.close()
        self.root.destroy()

def main():
    root = tk.Tk()
    app = KeyboardCalibratorGUI(root)
    root.mainloop()

if __name__ == '__main__':
    main()
