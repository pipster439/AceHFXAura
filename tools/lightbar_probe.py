r"""
=============================================================================
ROG FALCHION ACE HFX 顶部 RGB Light Bar 硬件探针与诊断系统 (Light Bar Probe)
=============================================================================
基线与核心功能:
1. 诊断可靠性增强:
   - 绝不假设 LED_ID == slot_idx，显式维护 led_to_slot 映射与独立槽位寻址。
   - 严密捕获 hal.access() 返回值，若失败绝不虚假显示 Online。
   - 记录枚举到的所有设备指针与当前选定设备。
   - 实时追踪 Set_L_STD_SINGLE_XY 返回值与推流线程健康状态，连续异常时切入 STREAM ERROR。
   - 醒目区分 ONLINE / DRY RUN / STREAM ERROR / BLOCKED: Mutex 状态。
   - 检测底层互斥体 Global\ExclusiveExecution_ROGKB 与系统灯光服务 (LightingService, ArmouryCrateService)。
2. 最小物理验证实验 (A / B / C):
   - TEST A (对照组): 硬件表 [57]，RGB [255, 0, 0]，测试数字 7 键发送链路。
   - TEST B (Light Bar 候选): 硬件表 [56]，RGB [255, 0, 0]，观察第 8 格独立发光。
   - TEST C (Row 0 候选): 硬件表 [0,8,16,24,32,40,48,56,64,72,80,88,96,104,112] (15 槽 45 字节)，逐个测试 15 颗候选点。
3. 全引脚勘测与标定:
   - 支持 0~127 全引脚巡检与分类标注 (Light Bar / Keyboard Key / Indicator / No Response / Unknown)。
   - 自动导出 lightbar_mapping.json 与 lightbar_mapping.md。
4. CLI 自动化与脚本诊断模式:
   - 支持 --diagnostics, --test-a, --test-b, --test-c 参数进行非图形化实验与日志记录。
=============================================================================
"""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import json
import logging
import os
import subprocess
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox
from typing import Optional, Dict, Any, List, Tuple, Sequence

# 引入 tools/py 共享基础模块
_DIR = os.path.dirname(os.path.abspath(__file__))
_TOOLS_PY = os.path.join(_DIR, "py") if os.path.isdir(os.path.join(_DIR, "py")) else os.path.join(_DIR, "tools", "py")
if _TOOLS_PY not in sys.path:
    sys.path.insert(0, _TOOLS_PY)

import aura_hal
from aura_hal import (
    AuraHal,
    AuraHalDevice,
    TOTAL_LEDS,
    RGB_CHANNELS,
    CANONICAL_68_KEYS,
    find_calibrated_keymap_path,
    load_calibrated_keymap,
)

# 默认导出文件定位在工程根目录
REPO_ROOT = os.path.abspath(os.path.join(_DIR, ".."))
LIGHTBAR_MAPPING_FILE = os.path.join(REPO_ROOT, "lightbar_mapping.json")
PHYSICAL_TEST_LOG_FILE = os.path.join(REPO_ROOT, "docs", "reports", "PHYSICAL_TEST_LOG.md")
PHYSICAL_TEST_JSON_FILE = os.path.join(REPO_ROOT, "physical_test_log.json")

# 候选与对照组定义
TEST_A_CONTROL_PIN = 57  # 数字 7 键 (已验证物理对照组)
TEST_B_CANDIDATE_PIN = 56  # Light Bar 第 8 颗候选点
ROW0_CANDIDATES = [0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112]

# 分类定义与色彩映射
CLASSIFICATIONS = [
    "Light Bar",
    "Keyboard Key",
    "Indicator",
    "No Response",
    "Unknown"
]

CLASS_COLORS = {
    "Light Bar": "#00E676",      # 鲜亮绿
    "Keyboard Key": "#1E88E5",   # 湛蓝
    "Indicator": "#FB8C00",      # 暖橙
    "No Response": "#37474F",    # 深灰黑
    "Unknown": "#546E7A"         # 灰蓝
}

CLASS_TEXT_COLORS = {
    "Light Bar": "#000000",
    "Keyboard Key": "#FFFFFF",
    "Indicator": "#000000",
    "No Response": "#B0BEC5",
    "Unknown": "#ECEFF1"
}


# =============================================================================
# 互斥体与系统服务检测实用工具
# =============================================================================
def check_exclusive_mutex() -> Tuple[bool, Optional[int]]:
    r"""
    检测 Global\ExclusiveExecution_ROGKB 互斥体是否存在。
    返回值: (is_present, last_error)
    """
    k32 = ctypes.windll.kernel32
    SYNCHRONIZE = 0x00100000
    hMutex = k32.OpenMutexW(SYNCHRONIZE, False, "Global\\ExclusiveExecution_ROGKB")
    if hMutex:
        k32.CloseHandle(hMutex)
        return True, 0
    err = k32.GetLastError()
    return False, err


def get_service_status(svc_name: str) -> str:
    """查询 Windows 服务状态 (RUNNING / STOPPED / NOT_INSTALLED)"""
    try:
        out = subprocess.check_output(f'sc query "{svc_name}"', shell=True, text=True, stderr=subprocess.DEVNULL)
        if "RUNNING" in out:
            return "RUNNING"
        elif "STOPPED" in out:
            return "STOPPED"
        elif "PAUSED" in out:
            return "PAUSED"
        return "UNKNOWN"
    except Exception:
        return "NOT_INSTALLED"


def get_process_status(proc_name: str) -> str:
    """查询进程是否运行"""
    try:
        out = subprocess.check_output(f'tasklist /FI "IMAGENAME eq {proc_name}"', shell=True, text=True, stderr=subprocess.DEVNULL)
        if proc_name.lower() in out.lower():
            return "RUNNING"
        return "STOPPED"
    except Exception:
        return "UNKNOWN"


def set_service_state(svc_name: str, start: bool) -> bool:
    """
    临时停止或恢复服务 (绝不修改启动类型)
    """
    action = "start" if start else "stop"
    flag = "" if start else " /y"
    try:
        ret = subprocess.call(
            f'net {action} "{svc_name}"{flag}',
            shell=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        return ret == 0
    except Exception:
        return False


# =============================================================================
# 硬件直控推流引擎 (显式 led_to_slot 映射与安全守护)
# =============================================================================
class LightingEngine:
    def __init__(self, dry_run: bool = False):
        self.dry_run = dry_run
        self.TOTAL_LEDS = TOTAL_LEDS  # 128 (保持向后兼容)
        self.running = True
        self.lock = threading.Lock()

        # 显式寻址映射表 (严禁假设 slot_idx == led_id)
        self.hardware_table: List[int] = list(range(self.TOTAL_LEDS))
        self.configured_slots: int = len(self.hardware_table)
        self.led_to_slot: Dict[int, int] = {lid: idx for idx, lid in enumerate(self.hardware_table)}
        self.slot_to_led: List[int] = list(self.hardware_table)
        self.rgb_buf: bytearray = bytearray(self.configured_slots * RGB_CHANNELS)

        # 硬件与驱动句柄
        self.hal: Optional[AuraHal] = None
        self.device: Optional[AuraHalDevice] = None
        self.pDev: int = 0
        self.devices_count: int = 0
        self.devices_list: List[int] = []
        self.selected_device_idx: int = -1

        # 诊断与监控指标
        self.last_access_return: Optional[int] = None
        self.last_stream_return: Optional[int] = None
        self.consecutive_stream_errors: int = 0
        self.stream_thread_alive: bool = False
        self.stream_error: bool = False
        self.last_exception: Optional[str] = None
        self.test_mode_name: str = "GENERAL_PROBE"

        # 互斥体检测
        self.mutex_present, self.mutex_error = check_exclusive_mutex()

        if not self.dry_run and not self.mutex_present:
            try:
                self.hal = AuraHal()
                self.last_access_return = self.hal.access()
                devices = self.hal.create_led_devices()
                self.devices_count = len(devices)
                self.devices_list = [d.pDev for d in devices]

                if not devices:
                    self.hal.release()
                    self.hal = None
                    raise RuntimeError("未检测到已连接的 ROG FALCHION ACE HFX 硬件设备！")

                self.selected_device_idx = 0
                self.device = devices[0]
                self.pDev = self.device.pDev

                # 配置初始寻址表 (128 槽全量)
                self.configure_table(self.hardware_table)
            except Exception as e:
                self.last_exception = str(e)
                if not self.dry_run:
                    raise

        # 启动后台守护推流线程 (25Hz)
        self.thread = threading.Thread(target=self._stream_worker, daemon=True)
        self.thread.start()

    def configure_table(self, table: Sequence[int]):
        """更新硬件寻址表并重构 RGB 缓冲区与反向映射"""
        with self.lock:
            self.hardware_table = [int(x) for x in table]
            self.configured_slots = len(self.hardware_table)
            self.led_to_slot = {lid: idx for idx, lid in enumerate(self.hardware_table)}
            self.slot_to_led = list(self.hardware_table)
            self.rgb_buf = bytearray(self.configured_slots * RGB_CHANNELS)

            if self.device:
                self.device.configure_hardware_table(self.hardware_table)

    def _stream_worker(self):
        """后台高频推流工作者线程 (25Hz, 带全局异常保护与错误追踪)"""
        self.stream_thread_alive = True
        while self.running:
            try:
                with self.lock:
                    if self.device and self.configured_slots > 0:
                        ret = self.device.set_led_direct(self.rgb_buf)
                        self.last_stream_return = ret
                        # AacKbHal_x64.dll 中正返回值代表成功
                        if ret is not None and ret > 0:
                            self.consecutive_stream_errors = 0
                            self.stream_error = False
                        else:
                            self.consecutive_stream_errors += 1
                            if self.consecutive_stream_errors >= 5:
                                self.stream_error = True
            except Exception as e:
                self.stream_error = True
                self.last_exception = str(e)
            time.sleep(0.04)
        self.stream_thread_alive = False

    def set_single_key(self, led_id: int, r: int = 0, g: int = 220, b: int = 255):
        """单 LED 点亮，清空其他所有 LED (显式维护 led_to_slot)"""
        with self.lock:
            for i in range(len(self.rgb_buf)):
                self.rgb_buf[i] = 0
            slot_idx = self.led_to_slot.get(led_id)
            if slot_idx is not None and slot_idx * RGB_CHANNELS + 2 < len(self.rgb_buf):
                self.rgb_buf[slot_idx * RGB_CHANNELS + 0] = r & 0xFF
                self.rgb_buf[slot_idx * RGB_CHANNELS + 1] = g & 0xFF
                self.rgb_buf[slot_idx * RGB_CHANNELS + 2] = b & 0xFF

    def set_slot_color(self, slot_idx: int, r: int, g: int, b: int):
        """按槽位索引更新颜色"""
        with self.lock:
            if 0 <= slot_idx < self.configured_slots:
                base = slot_idx * RGB_CHANNELS
                self.rgb_buf[base + 0] = r & 0xFF
                self.rgb_buf[base + 1] = g & 0xFF
                self.rgb_buf[base + 2] = b & 0xFF

    def flash_feedback(self, led_id: int, r: int = 0, g: int = 255, b: int = 0, duration: float = 0.15):
        """操作成功高亮反馈闪烁"""
        def _flash():
            self.set_single_key(led_id, r, g, b)
            time.sleep(duration)
            self.set_single_key(led_id, 0, 220, 255)
        threading.Thread(target=_flash, daemon=True).start()

    def fill_all(self, r: int = 0, g: int = 220, b: int = 255):
        """全亮已配置槽位"""
        with self.lock:
            for slot in range(self.configured_slots):
                self.rgb_buf[slot * RGB_CHANNELS + 0] = r & 0xFF
                self.rgb_buf[slot * RGB_CHANNELS + 1] = g & 0xFF
                self.rgb_buf[slot * RGB_CHANNELS + 2] = b & 0xFF

    def clear(self):
        """熄灭所有槽位"""
        with self.lock:
            for i in range(len(self.rgb_buf)):
                self.rgb_buf[i] = 0

    def activate_test_a(self):
        """TEST A: 控制对照组 (Pin 57 / 键 7), table = [57], rgb = [255, 0, 0]"""
        self.test_mode_name = "TEST_A_CONTROL_PIN57"
        self.configure_table([TEST_A_CONTROL_PIN])
        self.set_single_key(TEST_A_CONTROL_PIN, 255, 0, 0)

    def activate_test_b(self):
        """TEST B: Light Bar 候选点 (Pin 56), table = [56], rgb = [255, 0, 0]"""
        self.test_mode_name = "TEST_B_CANDIDATE_PIN56"
        self.configure_table([TEST_B_CANDIDATE_PIN])
        self.set_single_key(TEST_B_CANDIDATE_PIN, 255, 0, 0)

    def activate_test_c(self, candidate_led_id: int):
        """TEST C: 15 个 Row 0 候选矩阵, table = ROW0_CANDIDATES (15 槽), 单灯亮红"""
        self.test_mode_name = f"TEST_C_ROW0_{candidate_led_id}"
        self.configure_table(ROW0_CANDIDATES)
        self.set_single_key(candidate_led_id, 255, 0, 0)

    def restore_probe_mode(self, current_id: int = 0):
        """恢复 128 引脚全局探针模式"""
        self.test_mode_name = "GENERAL_PROBE"
        self.configure_table(list(range(self.TOTAL_LEDS)))
        self.set_single_key(current_id, 0, 220, 255)

    def close(self):
        """安全关闭推流并释放硬件资源"""
        self.running = False
        time.sleep(0.06)
        self.clear()
        if self.device:
            try:
                self.device.set_led_direct(self.rgb_buf)
                self.device.release()
            except Exception:
                pass
            self.device = None
        if self.hal:
            try:
                self.hal.release()
            except Exception:
                pass
            self.hal = None


# =============================================================================
# 数据存档与加载逻辑
# =============================================================================
def load_existing_keymap_keys() -> Dict[int, str]:
    """读取已知的 68 颗键盘按键 ID 与键名对应字典"""
    res = {}
    try:
        data = load_calibrated_keymap()
        if "reverse_id_table" in data:
            for lid_str, kn in data["reverse_id_table"].items():
                res[int(lid_str)] = kn
        elif "keys" in data:
            for kn, v in data["keys"].items():
                lid = v.get("led_id") if isinstance(v, dict) else v
                if lid is not None:
                    res[int(lid)] = kn
    except Exception:
        for kn, lid in CANONICAL_68_KEYS.items():
            res[lid] = kn
    return res


def load_lightbar_mapping() -> Optional[Dict[str, Any]]:
    """加载已保存的 lightbar_mapping.json"""
    if os.path.exists(LIGHTBAR_MAPPING_FILE):
        try:
            with open(LIGHTBAR_MAPPING_FILE, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            pass
    return None


def export_lightbar_mapping(records: Dict[int, Dict[str, Any]], lightbar_order: List[int], file_path: str = LIGHTBAR_MAPPING_FILE):
    """导出标准 lightbar_mapping.json 及对应的 Markdown 勘测报告"""
    now_str = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime())
    counts = {c: 0 for c in CLASSIFICATIONS}
    mapping_list = []

    for lid in range(TOTAL_LEDS):
        rec = records.get(lid, {})
        cls_name = rec.get("classification", "Unknown")
        if cls_name in counts:
            counts[cls_name] += 1

        col = lid // 8
        row = lid % 8
        mapping_list.append({
            "led_id": lid,
            "classification": cls_name,
            "physical_position": rec.get("physical_position"),
            "controllable": rec.get("controllable", True),
            "matrix_hardware_coords": {
                "matrix_col": col,
                "matrix_row": row,
                "formula": f"col * 8 + row = {col} * 8 + {row} = {lid}"
            },
            "notes": rec.get("notes", "")
        })

    export_data = {
        "device_profile": {
            "device_name": "ROG FALCHION ACE HFX",
            "device_name_zh": "ROG 魔导士 ACE HFX 竞技版磁轴键盘",
            "model_id": 7038,
            "usb_pid": "0x1B7E",
            "usb_vid": "0x0B05",
            "total_hardware_leds": TOTAL_LEDS,
            "lighting_interface": "USB HID Interface 3 (Set_L_STD_SINGLE_XY)",
            "operating_mode": "Volatile High-Speed Direct RAM Stream (Zero EEPROM Wear)",
            "export_version": "1.1.0",
            "last_exported_at": now_str
        },
        "summary": {
            "total_lightbar_leds": len(lightbar_order),
            "total_keyboard_keys": counts["Keyboard Key"],
            "total_indicators": counts["Indicator"],
            "total_no_response": counts["No Response"],
            "total_unknown": counts["Unknown"]
        },
        "lightbar": list(lightbar_order),
        "mapping": mapping_list
    }

    try:
        os.makedirs(os.path.dirname(file_path), exist_ok=True)
        with open(file_path, "w", encoding="utf-8") as f:
            json.dump(export_data, f, indent=2, ensure_ascii=False)

        md_path = file_path.replace(".json", ".md")
        with open(md_path, "w", encoding="utf-8") as f:
            f.write("# ROG FALCHION ACE HFX 顶部 RGB Light Bar 引脚勘测报告\n\n")
            f.write(f"- **设备型号**: ROG FALCHION ACE HFX (PID: `0x1B7E`)\n")
            f.write(f"- **勘测时间**: {now_str}\n")
            f.write(f"- **总硬件寻址上限**: {TOTAL_LEDS} (ID 0~127)\n")
            f.write(f"- **已捕获 Light Bar 灯珠数**: **{len(lightbar_order)}** 颗\n")
            f.write(f"- **键盘按键**: {counts['Keyboard Key']} | **指示灯**: {counts['Indicator']} | **空引脚/未亮**: {counts['No Response']} | **待测**: {counts['Unknown']}\n\n")
            f.write("---\n\n")
            f.write("## 1. 顶部 Light Bar 物理顺序映射表 (从左到右)\n\n")
            if lightbar_order:
                f.write("| 物理位置 | 硬件 LED ID | 电气矩阵 (Col, Row) | 控制状态 | 备注 |\n")
                f.write("| :---: | :---: | :---: | :---: | :--- |\n")
                for idx, lid in enumerate(lightbar_order):
                    col = lid // 8
                    row = lid % 8
                    rec = records.get(lid, {})
                    ctrl = "可控 (Controllable)" if rec.get("controllable", True) else "不可控"
                    notes = rec.get("notes", f"LightBar[{idx}]")
                    f.write(f"| **Pos #{idx}** (左起第 {idx+1} 颗) | `{lid}` | `({col}, {row})` | {ctrl} | {notes} |\n")
                f.write(f"\n**RGB Engine 直接切片数组**: `{json.dumps(lightbar_order)}`\n\n")
            else:
                f.write("*暂未标记任何 Light Bar 灯珠。请在 GUI 界面巡检并标记。*\n\n")
            f.write("---\n\n")
            f.write("## 2. 0~127 全引脚详细勘测记录\n\n")
            f.write("| LED ID | 分类 (Classification) | 物理位置 | 电气矩阵 | 备注 |\n")
            f.write("| :---: | :--- | :---: | :---: | :--- |\n")
            for item in mapping_list:
                pos_str = f"Pos #{item['physical_position']}" if item['physical_position'] is not None else "-"
                m = item["matrix_hardware_coords"]
                f.write(f"| `{item['led_id']:3d}` | **{item['classification']}** | {pos_str} | `({m['matrix_col']}, {m['matrix_row']})` | {item['notes']} |\n")
            f.write("\n---\n*由 ROG Falchion Ace HFX Light Bar Probe 自动生成*\n")
    except Exception as e:
        print(f"导出映射文件异常: {e}")


# =============================================================================
# Light Bar Probe GUI 界面实现
# =============================================================================
class LightBarProbeGUI:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("ROG FALCHION ACE HFX - 顶部 Light Bar 独立控制探针与硬件诊断系统")
        self.root.geometry("1220x860")
        self.root.minsize(1100, 780)
        self.root.configure(bg="#1E1E24")

        # 检查互斥体与系统服务
        self.mutex_present, _ = check_exclusive_mutex()
        self.svc_lighting = get_service_status("LightingService")
        self.svc_armoury = get_service_status("ArmouryCrateService")

        # 初始化推流引擎
        self.engine = None
        if self.mutex_present:
            messagebox.showwarning(
                "互斥体阻塞警告",
                "检测到底层互斥体 Global\\ExclusiveExecution_ROGKB 存在！\n\n"
                "华硕官方软件当前独占访问，DLL 推流可能会被跳过。\n"
                "程序将切入受限诊断模式，请勿强行删除外部互斥体。"
            )
            self.engine = LightingEngine(dry_run=True)
        else:
            try:
                self.engine = LightingEngine(dry_run=False)
            except Exception as e:
                messagebox.showwarning(
                    "硬件连接提示",
                    f"物理设备连接或 HAL 初始化异常:\n{e}\n\n已切入离线模拟 (dry-run) 模式供离线查看与编辑。"
                )
                self.engine = LightingEngine(dry_run=True)

        # 状态数据
        self.current_id = 0
        self.is_scanning = False
        self.scan_timer = None
        self.sweep_testing = False
        self.test_c_current_candidate = 56

        # 预载入已知 68 键
        self.known_keys = load_existing_keymap_keys()

        # 建立全量 0~127 记录
        self.records: Dict[int, Dict[str, Any]] = {}
        self.lightbar_order: List[int] = []

        saved = load_lightbar_mapping()
        if saved and "mapping" in saved:
            self.lightbar_order = saved.get("lightbar", [])
            for item in saved["mapping"]:
                lid = item["led_id"]
                self.records[lid] = {
                    "led_id": lid,
                    "classification": item.get("classification", "Unknown"),
                    "physical_position": item.get("physical_position"),
                    "controllable": item.get("controllable", True),
                    "notes": item.get("notes", "")
                }
        else:
            for lid in range(TOTAL_LEDS):
                if lid in self.known_keys:
                    self.records[lid] = {
                        "led_id": lid,
                        "classification": "Keyboard Key",
                        "physical_position": None,
                        "controllable": True,
                        "notes": f"Key: {self.known_keys[lid]}"
                    }
                else:
                    self.records[lid] = {
                        "led_id": lid,
                        "classification": "Unknown",
                        "physical_position": None,
                        "controllable": True,
                        "notes": ""
                    }

        # 构建主界面
        self.grid_buttons: Dict[int, tk.Button] = {}
        self._build_ui()
        self._set_hardware_led(self.current_id)

        # 启动周期性诊断数据更新
        self._schedule_diagnostics_refresh()

    # -------------------------------------------------------------------------
    # UI 构建逻辑
    # -------------------------------------------------------------------------
    def _build_ui(self):
        # 1. 顶部状态横幅 (Status Banner)
        self.banner_frame = tk.Frame(self.root, pady=6)
        self.banner_frame.pack(fill=tk.X)
        self.banner_lbl = tk.Label(
            self.banner_frame,
            text="",
            font=("Segoe UI", 11, "bold"),
            padx=15,
            pady=2
        )
        self.banner_lbl.pack(fill=tk.X)
        self._update_banner()

        # 2. 核心主视窗 Tab 切换 (Notebook)
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

        # Tab 1: 最小物理验证实验 (A / B / C)
        self.tab_physical_tests = tk.Frame(self.notebook, bg="#1E1E24")
        self.notebook.add(self.tab_physical_tests, text=" 🧪 最小物理验证实验 (A / B / C) ")
        self._build_physical_tests_tab(self.tab_physical_tests)

        # Tab 2: 引脚探针与灯带映射 (0~127 全局巡检)
        self.tab_probe = tk.Frame(self.notebook, bg="#1E1E24")
        self.notebook.add(self.tab_probe, text=" 🔍 引脚探针与映射 (0~127) ")
        self._build_probe_tab(self.tab_probe)

        # Tab 3: 硬件与驱动实时诊断面板
        self.tab_diag = tk.Frame(self.notebook, bg="#1E1E24")
        self.notebook.add(self.tab_diag, text=" 📊 实时硬件诊断面板 (Diagnostics) ")
        self._build_diagnostics_tab(self.tab_diag)

        # 底部退出钩子
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

    def _update_banner(self):
        """刷新顶部横幅显式状态"""
        if self.mutex_present:
            self.banner_frame.config(bg="#4A148C")
            self.banner_lbl.config(
                bg="#4A148C",
                fg="#FFEB3B",
                text="⚠️ BLOCKED: ExclusiveExecution_ROGKB 互斥体存在 — 华硕官方独占中，推流被阻止"
            )
        elif self.engine.dry_run:
            self.banner_frame.config(bg="#E65100")
            self.banner_lbl.config(
                bg="#E65100",
                fg="#FFFFFF",
                text="⚠️ DRY RUN - NO HARDWARE OUTPUT (离线模拟模式，未向硬件发送真实报文)"
            )
        elif self.engine.stream_error:
            self.banner_frame.config(bg="#B71C1C")
            self.banner_lbl.config(
                bg="#B71C1C",
                fg="#FFFFFF",
                text="❌ STREAM ERROR - 持续推流异常或底层返回值错误，请检查连接"
            )
        else:
            self.banner_frame.config(bg="#1B5E20")
            self.banner_lbl.config(
                bg="#1B5E20",
                fg="#FFFFFF",
                text="🟢 ONLINE - 硬件已连接并持续稳定推流 (20~25Hz VTable[19] Direct Stream)"
            )

    # -------------------------------------------------------------------------
    # Tab 1: 最小物理验证实验 (A / B / C)
    # -------------------------------------------------------------------------
    def _build_physical_tests_tab(self, parent: tk.Frame):
        container = tk.Frame(parent, bg="#1E1E24", padx=15, pady=15)
        container.pack(fill=tk.BOTH, expand=True)

        intro = tk.Label(
            container,
            text="最小物理验证实验严格遵循因果基线，持续 20~25Hz 稳定推流。请按顺序执行并肉眼观察键盘反馈：",
            font=("Segoe UI", 10),
            fg="#B0BEC5",
            bg="#1E1E24",
            anchor="w"
        )
        intro.pack(fill=tk.X, pady=(0, 10))

        # 实验卡片容器
        cards_frame = tk.Frame(container, bg="#1E1E24")
        cards_frame.pack(fill=tk.X, pady=5)

        # Card A
        card_a = tk.LabelFrame(
            cards_frame,
            text=" TEST A: 已确认普通按键 (对照组基准) ",
            font=("Segoe UI", 10, "bold"),
            fg="#81C784",
            bg="#263238",
            padx=10,
            pady=8
        )
        card_a.pack(fill=tk.X, pady=5)

        tk.Label(
            card_a,
            text="• 配置: table = [57] (1 槽 3 字节), slot 0 映射 LED ID 57 (按键 '7')，持续亮红。\n"
                 "• 验证目标: 证明驱动发送链路与 Set_L_STD_SINGLE_XY 完全有效。\n"
                 "• 判定规则: 若按键 7 不亮，立即停止研究 Pin 56，先排查链路！",
            font=("Segoe UI", 9),
            fg="#ECEFF1",
            bg="#263238",
            justify=tk.LEFT
        ).pack(side=tk.LEFT, padx=5)

        btn_run_a = tk.Button(
            card_a,
            text="▶ 执行 TEST A (Pin 57)",
            font=("Segoe UI", 9, "bold"),
            bg="#2E7D32",
            fg="white",
            padx=12,
            pady=6,
            command=self.run_test_a
        )
        btn_run_a.pack(side=tk.RIGHT, padx=10)

        # Card B
        card_b = tk.LabelFrame(
            cards_frame,
            text=" TEST B: Light Bar 候选引脚 (Pin 56) ",
            font=("Segoe UI", 10, "bold"),
            fg="#4DD0E1",
            bg="#263238",
            padx=10,
            pady=8
        )
        card_b.pack(fill=tk.X, pady=5)

        tk.Label(
            card_b,
            text="• 配置: table = [56] (1 槽 3 字节), slot 0 映射 LED ID 56，持续亮红。\n"
                 "• 验证目标: 观察顶部 Light Bar 第 8 格发光状态与下方数字 7 键的关联。\n"
                 "• 肉眼观察: 第 8 格是否单独亮？按键 7 是否也亮？两者是否同亮或均灭？",
            font=("Segoe UI", 9),
            fg="#ECEFF1",
            bg="#263238",
            justify=tk.LEFT
        ).pack(side=tk.LEFT, padx=5)

        btn_run_b = tk.Button(
            card_b,
            text="▶ 执行 TEST B (Pin 56)",
            font=("Segoe UI", 9, "bold"),
            bg="#00838F",
            fg="white",
            padx=12,
            pady=6,
            command=self.run_test_b
        )
        btn_run_b.pack(side=tk.RIGHT, padx=10)

        # Card C
        card_c = tk.LabelFrame(
            cards_frame,
            text=" TEST C: 15 个 Row 0 候选矩阵引脚 ",
            font=("Segoe UI", 10, "bold"),
            fg="#FFB74D",
            bg="#263238",
            padx=10,
            pady=8
        )
        card_c.pack(fill=tk.X, pady=5)

        c_info = tk.Frame(card_c, bg="#263238")
        c_info.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)

        tk.Label(
            c_info,
            text="• 配置: table = [0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112] (15 槽 45 字节)。\n"
                 "• 选定候选点单独亮红，其余 14 槽全黑。led_to_slot 显式寻址。",
            font=("Segoe UI", 9),
            fg="#ECEFF1",
            bg="#263238",
            justify=tk.LEFT
        ).pack(anchor="w")

        cand_bar = tk.Frame(c_info, bg="#263238")
        cand_bar.pack(fill=tk.X, pady=5)
        tk.Label(cand_bar, text="选定候选: ", font=("Segoe UI", 9, "bold"), fg="#FFD54F", bg="#263238").pack(side=tk.LEFT)
        for cand in ROW0_CANDIDATES:
            b = tk.Button(
                cand_bar,
                text=str(cand),
                font=("Segoe UI", 8),
                bg="#37474F",
                fg="white",
                width=3,
                command=lambda c=cand: self.run_test_c(c)
            )
            b.pack(side=tk.LEFT, padx=1)

        btn_run_c = tk.Button(
            card_c,
            text="▶ 执行 TEST C (当前引脚)",
            font=("Segoe UI", 9, "bold"),
            bg="#EF6C00",
            fg="white",
            padx=12,
            pady=6,
            command=lambda: self.run_test_c(self.test_c_current_candidate)
        )
        btn_run_c.pack(side=tk.RIGHT, padx=10)

        # 恢复巡检模式按钮
        restore_frame = tk.Frame(container, bg="#1E1E24")
        restore_frame.pack(fill=tk.X, pady=10)

        self.current_test_status_lbl = tk.Label(
            restore_frame,
            text="当前运行模式: 0~127 全局探针模式 (128-slot Identity Table)",
            font=("Segoe UI", 10, "bold"),
            fg="#80D8FF",
            bg="#1E1E24"
        )
        self.current_test_status_lbl.pack(side=tk.LEFT)

        btn_restore = tk.Button(
            restore_frame,
            text="🔄 恢复 128 引脚全局巡检模式",
            font=("Segoe UI", 9),
            bg="#455A64",
            fg="white",
            padx=10,
            pady=4,
            command=self.restore_probe
        )
        btn_restore.pack(side=tk.RIGHT)

        # 肉眼观察记录区
        record_box = tk.LabelFrame(
            container,
            text=" 📝 人工肉眼观察结果记录 (严禁程序自动代填“成功”) ",
            font=("Segoe UI", 10, "bold"),
            fg="#FFD54F",
            bg="#1E1E24",
            padx=10,
            pady=8
        )
        record_box.pack(fill=tk.BOTH, expand=True, pady=10)

        rec_row = tk.Frame(record_box, bg="#1E1E24")
        rec_row.pack(fill=tk.X, pady=5)

        tk.Label(rec_row, text="观察现象:", font=("Segoe UI", 9, "bold"), fg="#ECEFF1", bg="#1E1E24").pack(side=tk.LEFT)

        self.obs_var = tk.StringVar(value="数字 7 键稳定亮红")
        obs_options = [
            "数字 7 键稳定亮红",
            "Light Bar 第 8 格单独亮红",
            "数字 7 键与灯带均亮",
            "完全无发光反应",
            "闪烁一下后熄灭",
            "其他异常现象"
        ]
        self.obs_combo = ttk.Combobox(rec_row, textvariable=self.obs_var, values=obs_options, width=28, state="readonly")
        self.obs_combo.pack(side=tk.LEFT, padx=10)

        tk.Label(rec_row, text="详细备注:", font=("Segoe UI", 9), fg="#ECEFF1", bg="#1E1E24").pack(side=tk.LEFT, padx=(10, 2))
        self.obs_notes_entry = tk.Entry(rec_row, font=("Segoe UI", 9), width=35, bg="#263238", fg="white", insertbackground="white")
        self.obs_notes_entry.pack(side=tk.LEFT, padx=5)

        btn_save_obs = tk.Button(
            rec_row,
            text="💾 记录本组实测结果",
            font=("Segoe UI", 9, "bold"),
            bg="#0288D1",
            fg="white",
            padx=10,
            pady=3,
            command=self.save_observation
        )
        btn_save_obs.pack(side=tk.LEFT, padx=10)

        self.obs_log_text = tk.Text(record_box, height=7, bg="#121214", fg="#80D8FF", font=("Consolas", 9), relief=tk.FLAT)
        self.obs_log_text.pack(fill=tk.BOTH, expand=True, pady=5)
        self._load_existing_test_log()

    def run_test_a(self):
        self.engine.activate_test_a()
        self.current_test_status_lbl.config(text=f"当前模式: TEST A (对照组 Pin 57 / 键 7) [已推流: return={self.engine.last_stream_return}]", fg="#81C784")
        self._append_obs_log(f"[{time.strftime('%H:%M:%S')}] 启动 TEST A: Table=[57], RGB=[255,0,0], 推流返回值: {self.engine.last_stream_return}")
        self._update_banner()

    def run_test_b(self):
        self.engine.activate_test_b()
        self.current_test_status_lbl.config(text=f"当前模式: TEST B (Light Bar 候选 Pin 56) [已推流: return={self.engine.last_stream_return}]", fg="#4DD0E1")
        self._append_obs_log(f"[{time.strftime('%H:%M:%S')}] 启动 TEST B: Table=[56], RGB=[255,0,0], 推流返回值: {self.engine.last_stream_return}")
        self._update_banner()

    def run_test_c(self, cand_id: int):
        self.test_c_current_candidate = cand_id
        self.engine.activate_test_c(cand_id)
        slot_idx = self.engine.led_to_slot.get(cand_id, -1)
        self.current_test_status_lbl.config(
            text=f"当前模式: TEST C (Row 0 候选 Pin {cand_id}, Slot {slot_idx}/15) [推流 return={self.engine.last_stream_return}]",
            fg="#FFB74D"
        )
        self._append_obs_log(f"[{time.strftime('%H:%M:%S')}] 启动 TEST C: Table=Row0(15 slots), LED {cand_id}->Slot {slot_idx}, 推流返回值: {self.engine.last_stream_return}")
        self._update_banner()

    def restore_probe(self):
        self.engine.restore_probe_mode(self.current_id)
        self.current_test_status_lbl.config(text="当前运行模式: 0~127 全局探针模式 (128-slot Identity Table)", fg="#80D8FF")
        self._append_obs_log(f"[{time.strftime('%H:%M:%S')}] 已恢复 128 槽全局探针模式。")
        self._update_banner()

    def _append_obs_log(self, text: str):
        self.obs_log_text.insert(tk.END, text + "\n")
        self.obs_log_text.see(tk.END)

    def _load_existing_test_log(self):
        if os.path.exists(PHYSICAL_TEST_LOG_FILE):
            try:
                with open(PHYSICAL_TEST_LOG_FILE, "r", encoding="utf-8") as f:
                    self.obs_log_text.insert(tk.END, f"--- 历史实测记录文件: {PHYSICAL_TEST_LOG_FILE} ---\n")
                    self.obs_log_text.insert(tk.END, f.read() + "\n")
            except Exception:
                pass

    def save_observation(self):
        mode = self.engine.test_mode_name
        phenomenon = self.obs_var.get()
        notes = self.obs_notes_entry.get().strip()
        now_str = time.strftime("%Y-%m-%d %H:%M:%S")

        record = {
            "timestamp": now_str,
            "test_mode": mode,
            "configured_slots": self.engine.configured_slots,
            "hardware_table": list(self.engine.hardware_table),
            "last_stream_return": self.engine.last_stream_return,
            "stream_thread_alive": self.engine.stream_thread_alive,
            "visual_phenomenon": phenomenon,
            "user_notes": notes
        }

        log_data = []
        if os.path.exists(PHYSICAL_TEST_JSON_FILE):
            try:
                with open(PHYSICAL_TEST_JSON_FILE, "r", encoding="utf-8") as f:
                    log_data = json.load(f)
            except Exception:
                log_data = []
        log_data.append(record)
        os.makedirs(os.path.dirname(PHYSICAL_TEST_JSON_FILE), exist_ok=True)
        with open(PHYSICAL_TEST_JSON_FILE, "w", encoding="utf-8") as f:
            json.dump(log_data, f, indent=2, ensure_ascii=False)

        os.makedirs(os.path.dirname(PHYSICAL_TEST_LOG_FILE), exist_ok=True)
        with open(PHYSICAL_TEST_LOG_FILE, "a", encoding="utf-8") as f:
            f.write(f"\n### 实测记录 [{now_str}]\n")
            f.write(f"- **测试模式**: `{mode}`\n")
            f.write(f"- **硬件表**: `{self.engine.hardware_table}` (槽位数: {self.engine.configured_slots})\n")
            f.write(f"- **底层推流返回**: `{self.engine.last_stream_return}` (线程存活: {self.engine.stream_thread_alive})\n")
            f.write(f"- **肉眼观察现象**: **{phenomenon}**\n")
            if notes:
                f.write(f"- **用户备注**: {notes}\n")

        self._append_obs_log(f"[{time.strftime('%H:%M:%S')}] ✓ 实测记录已保存: {mode} -> {phenomenon}")
        messagebox.showinfo("保存成功", f"实测结果已写入:\n{PHYSICAL_TEST_LOG_FILE}")

    # -------------------------------------------------------------------------
    # Tab 2: 引脚探针与映射 (0~127 全局巡检)
    # -------------------------------------------------------------------------
    def _build_probe_tab(self, parent: tk.Frame):
        self._build_lightbar_strip(parent)
        self._build_control_panel(parent)
        self._build_tagging_panel(parent)
        self._build_matrix_grid(parent)
        self._build_footer(parent)

    def _build_lightbar_strip(self, parent: tk.Frame):
        lb_container = tk.LabelFrame(
            parent,
            text="✨ 顶部 Light Bar 物理顺序示意条 (从左到右: Pos #0 -> Pos #N-1)",
            font=("Segoe UI", 10, "bold"),
            fg="#00E676",
            bg="#1E1E24",
            padx=10,
            pady=4
        )
        lb_container.pack(fill=tk.X, padx=10, pady=4)

        top_bar = tk.Frame(lb_container, bg="#1E1E24")
        top_bar.pack(fill=tk.X, pady=(0, 2))

        self.lb_count_lbl = tk.Label(
            top_bar,
            text=f"已发现灯珠: {len(self.lightbar_order)} 颗",
            font=("Segoe UI", 10, "bold"),
            fg="#80D8FF",
            bg="#1E1E24"
        )
        self.lb_count_lbl.pack(side=tk.LEFT)

        self.sweep_test_btn = tk.Button(
            top_bar,
            text="▶ 从左到右走灯测试 (Sweep Test)",
            font=("Segoe UI", 9, "bold"),
            bg="#7B1FA2",
            fg="white",
            relief=tk.FLAT,
            padx=10,
            pady=2,
            cursor="hand2",
            command=self.run_sweep_test
        )
        self.sweep_test_btn.pack(side=tk.RIGHT, padx=5)

        self.strip_frame = tk.Frame(lb_container, bg="#131316", pady=4)
        self.strip_frame.pack(fill=tk.X, pady=2)
        self._refresh_lightbar_strip()

    def _build_control_panel(self, parent: tk.Frame):
        ctrl_frame = tk.Frame(parent, bg="#1E1E24", padx=5, pady=2)
        ctrl_frame.pack(fill=tk.X, padx=10, pady=2)

        nav_box = tk.LabelFrame(ctrl_frame, text=" 🕹 单步导航 (Single Step) ", font=("Segoe UI", 9, "bold"), fg="#80D8FF", bg="#1E1E24", padx=8, pady=4)
        nav_box.pack(side=tk.LEFT, fill=tk.Y, padx=(0, 8))

        tk.Button(nav_box, text="◀ 上一个 (Prev)", font=("Segoe UI", 9, "bold"), bg="#37474F", fg="white", padx=8, pady=2, command=self.prev_id).pack(side=tk.LEFT, padx=3)
        self.id_display_lbl = tk.Label(nav_box, text="ID [   0 ]", font=("Consolas", 11, "bold"), fg="#00E5FF", bg="#121214", width=22, relief=tk.SUNKEN, bd=1)
        self.id_display_lbl.pack(side=tk.LEFT, padx=4)
        tk.Button(nav_box, text="下一个 (Next) ▶", font=("Segoe UI", 9, "bold"), bg="#37474F", fg="white", padx=8, pady=2, command=self.next_id).pack(side=tk.LEFT, padx=3)

        self.id_slider = tk.Scale(nav_box, from_=0, to=127, orient=tk.HORIZONTAL, showvalue=0, bg="#1E1E24", fg="#80D8FF", highlightthickness=0, length=120, command=self.on_slider_move)
        self.id_slider.pack(side=tk.LEFT, padx=6)

        scan_box = tk.LabelFrame(ctrl_frame, text=" ⚡ 自动巡检 (Auto Patrol) ", font=("Segoe UI", 9, "bold"), fg="#B388FF", bg="#1E1E24", padx=8, pady=4)
        scan_box.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        tk.Label(scan_box, text="范围:", font=("Segoe UI", 9), fg="#B0BEC5", bg="#1E1E24").pack(side=tk.LEFT, padx=(0, 2))
        self.start_id_entry = tk.Entry(scan_box, width=4, font=("Consolas", 9), justify="center")
        self.start_id_entry.insert(0, "0")
        self.start_id_entry.pack(side=tk.LEFT)
        tk.Label(scan_box, text="~", font=("Segoe UI", 9), fg="#B0BEC5", bg="#1E1E24").pack(side=tk.LEFT)
        self.end_id_entry = tk.Entry(scan_box, width=4, font=("Consolas", 9), justify="center")
        self.end_id_entry.insert(0, "127")
        self.end_id_entry.pack(side=tk.LEFT)

        tk.Label(scan_box, text="间隔(ms):", font=("Segoe UI", 9), fg="#B0BEC5", bg="#1E1E24").pack(side=tk.LEFT, padx=(8, 2))
        self.interval_entry = tk.Entry(scan_box, width=5, font=("Consolas", 9), justify="center")
        self.interval_entry.insert(0, "600")
        self.interval_entry.pack(side=tk.LEFT)

        self.skip_known_var = tk.BooleanVar(value=True)
        tk.Checkbutton(scan_box, text="跳过 68 键盘键", variable=self.skip_known_var, font=("Segoe UI", 9), fg="#80CBC4", bg="#1E1E24", selectcolor="#263238").pack(side=tk.LEFT, padx=8)

        self.scan_btn = tk.Button(scan_box, text="▷ 开始自动巡检", font=("Segoe UI", 9, "bold"), bg="#5E35B1", fg="white", padx=10, pady=2, command=self.toggle_scan)
        self.scan_btn.pack(side=tk.LEFT, padx=4)

        tk.Button(ctrl_frame, text="🌑 一键全灭", font=("Segoe UI", 9), bg="#263238", fg="#ECEFF1", padx=8, pady=2, command=self.turn_all_off).pack(side=tk.RIGHT, padx=5)

    def _build_tagging_panel(self, parent: tk.Frame):
        tag_frame = tk.LabelFrame(parent, text=" 🏷 当前引脚分类标记与物理顺序配置 ", font=("Segoe UI", 9, "bold"), fg="#FFD54F", bg="#1E1E24", padx=10, pady=4)
        tag_frame.pack(fill=tk.X, padx=10, pady=2)

        row1 = tk.Frame(tag_frame, bg="#1E1E24")
        row1.pack(fill=tk.X, pady=2)

        self.current_status_lbl = tk.Label(row1, text="当前分类: [ Unknown ]", font=("Segoe UI", 10, "bold"), fg="#ECEFF1", bg="#1E1E24", width=24, anchor="w")
        self.current_status_lbl.pack(side=tk.LEFT)

        for cname in CLASSIFICATIONS:
            btn = tk.Button(
                row1,
                text=f"{'🌟 ' if cname=='Light Bar' else ''}{cname}",
                font=("Segoe UI", 9, "bold" if cname=="Light Bar" else "normal"),
                bg=CLASS_COLORS[cname],
                fg=CLASS_TEXT_COLORS[cname],
                padx=8,
                pady=2,
                cursor="hand2",
                command=lambda c=cname: self.set_current_classification(c)
            )
            btn.pack(side=tk.LEFT, padx=3)

        self.btn_pos_left = tk.Button(row1, text="◀ 向左调", font=("Segoe UI", 8), bg="#37474F", fg="white", command=self.move_current_lb_left)
        self.btn_pos_left.pack(side=tk.LEFT, padx=(15, 2))
        self.pos_display_lbl = tk.Label(row1, text="Pos: -", font=("Consolas", 9, "bold"), fg="#00E676", bg="#1E1E24", width=8)
        self.pos_display_lbl.pack(side=tk.LEFT)
        self.btn_pos_right = tk.Button(row1, text="向右调 ▶", font=("Segoe UI", 8), bg="#37474F", fg="white", command=self.move_current_lb_right)
        self.btn_pos_right.pack(side=tk.LEFT, padx=2)

        tk.Button(row1, text="🗑 清空灯带记录", font=("Segoe UI", 8), bg="#455A64", fg="#FF8A80", command=self.clear_lightbar_list).pack(side=tk.RIGHT, padx=5)

    def _build_matrix_grid(self, parent: tk.Frame):
        grid_container = tk.LabelFrame(
            parent,
            text=" 🎛 0~127 全硬件引脚状态矩阵 (16 列 × 8 行: Row 0 顶部灯带候选 / Row 1~5 按键 / Row 6~7 侧边与保留) ",
            font=("Segoe UI", 9, "bold"),
            fg="#90A4AE",
            bg="#1E1E24",
            padx=6,
            pady=4
        )
        grid_container.pack(fill=tk.BOTH, expand=True, padx=10, pady=2)

        grid_frame = tk.Frame(grid_container, bg="#18181C")
        grid_frame.pack(fill=tk.BOTH, expand=True)

        for col in range(16):
            grid_frame.columnconfigure(col, weight=1)
            tk.Label(grid_frame, text=f"C{col}", font=("Segoe UI", 7), fg="#546E7A", bg="#18181C").grid(row=0, column=col, pady=1)

        for row in range(8):
            grid_frame.rowconfigure(row + 1, weight=1)
            for col in range(16):
                lid = col * 8 + row
                btn = tk.Button(
                    grid_frame,
                    text=f"{lid:2d}",
                    font=("Consolas", 8),
                    bg=CLASS_COLORS["Unknown"],
                    fg="#FFFFFF",
                    relief=tk.FLAT,
                    bd=1,
                    cursor="hand2",
                    command=lambda i=lid: self._set_hardware_led(i)
                )
                btn.grid(row=row + 1, column=col, padx=1, pady=1, sticky="nsew")
                self.grid_buttons[lid] = btn

    def _build_footer(self, parent: tk.Frame):
        footer = tk.Frame(parent, bg="#18181C", padx=10, pady=4)
        footer.pack(fill=tk.X, side=tk.BOTTOM)

        self.status_lbl = tk.Label(footer, text="就绪。点击上方按钮或键盘引脚即可点亮测试。", font=("Segoe UI", 9), fg="#80D8FF", bg="#18181C")
        self.status_lbl.pack(side=tk.LEFT)

        tk.Button(footer, text="💾 导出映射表至项目根目录", font=("Segoe UI", 9, "bold"), bg="#2E7D32", fg="white", padx=12, pady=2, command=self.save_and_export).pack(side=tk.RIGHT, padx=5)

    # -------------------------------------------------------------------------
    # Tab 3: 硬件与驱动实时诊断面板
    # -------------------------------------------------------------------------
    def _build_diagnostics_tab(self, parent: tk.Frame):
        container = tk.Frame(parent, bg="#1E1E24", padx=15, pady=15)
        container.pack(fill=tk.BOTH, expand=True)

        header = tk.Label(
            container,
            text="AURA HAL 与 AAC 键盘驱动底层运行时状态实时监控 (Real-Time Diagnostics)",
            font=("Segoe UI", 11, "bold"),
            fg="#80D8FF",
            bg="#1E1E24",
            anchor="w"
        )
        header.pack(fill=tk.X, pady=(0, 10))

        diag_box = tk.LabelFrame(container, text=" 运行时参数表 (Runtime Variables) ", font=("Segoe UI", 10, "bold"), fg="#B0BEC5", bg="#1E1E24", padx=15, pady=10)
        diag_box.pack(fill=tk.BOTH, expand=True)

        self.diag_labels: Dict[str, tk.Label] = {}
        items = [
            ("MODE", "运行工作模式"),
            ("Script", "探针执行脚本路径"),
            ("aura_hal.py", "HAL 模块定位路径"),
            ("AacKbHal_x64.dll", "驱动 DLL 实际加载路径"),
            ("DLL SHA256", "DLL 二进制 SHA256 哈希"),
            ("HAL Access return", "VTable[4] Access 返回码"),
            ("Devices found", "CreateLedDevice 枚举设备总数"),
            ("Selected device", "当前选定的键盘对象指针"),
            ("Configured slots", "当前已配置硬件寻址槽数"),
            ("RGB buffer bytes", "当前推流缓冲区实际字节数"),
            ("Required buffer bytes", "按槽数要求的最少字节数 (slots * 3)"),
            ("Stream thread", "后台高频推流守护线程状态"),
            ("Last Set_L_STD_SINGLE_XY return", "最后一次推流帧底层返回值"),
            ("Last stream exception", "推流异常记录"),
            ("Global\\ExclusiveExecution_ROGKB", "官方独占互斥体状态"),
            ("LightingService", "系统 LightingService 服务状态"),
            ("ArmouryCrateService", "系统 ArmouryCrateService 服务状态")
        ]

        for i, (key, desc) in enumerate(items):
            row_f = tk.Frame(diag_box, bg="#1E1E24")
            row_f.pack(fill=tk.X, pady=2)

            tk.Label(row_f, text=f"{key}:", width=32, anchor="w", font=("Consolas", 9, "bold"), fg="#80D8FF", bg="#1E1E24").pack(side=tk.LEFT)
            val_lbl = tk.Label(row_f, text="--", anchor="w", font=("Consolas", 9), fg="#ECEFF1", bg="#1E1E24")
            val_lbl.pack(side=tk.LEFT, fill=tk.X, expand=True)
            self.diag_labels[key] = val_lbl

        # 华硕服务临时管理按钮
        svc_frame = tk.Frame(container, bg="#1E1E24", pady=10)
        svc_frame.pack(fill=tk.X)

        tk.Label(svc_frame, text="华硕灯光服务临时控制 (仅临时暂停/恢复，绝不永久修改启动类型):", font=("Segoe UI", 9), fg="#B0BEC5", bg="#1E1E24").pack(side=tk.LEFT)

        btn_stop_svc = tk.Button(
            svc_frame,
            text="⏸ 临时停止 LightingService (测试专用)",
            font=("Segoe UI", 8),
            bg="#D32F2F",
            fg="white",
            command=self._temp_stop_services
        )
        btn_stop_svc.pack(side=tk.LEFT, padx=10)

        btn_start_svc = tk.Button(
            svc_frame,
            text="▶ 恢复启动 LightingService",
            font=("Segoe UI", 8),
            bg="#388E3C",
            fg="white",
            command=self._temp_start_services
        )
        btn_start_svc.pack(side=tk.LEFT, padx=5)

    def _temp_stop_services(self):
        if messagebox.askyesno("服务控制确认", "确认临时停止 LightingService 服务以排除推流争抢吗？\n\n测试完毕后可一键恢复。"):
            res = set_service_state("LightingService", False)
            if res:
                messagebox.showinfo("服务提示", "LightingService 已临时暂停。")
            else:
                messagebox.showwarning("权限不足", "停止服务失败，请确认是否以管理员权限运行。")
            self._update_diagnostics()

    def _temp_start_services(self):
        res = set_service_state("LightingService", True)
        if res:
            messagebox.showinfo("服务提示", "LightingService 已恢复启动。")
        else:
            messagebox.showwarning("权限不足", "启动服务失败，请确认是否以管理员权限运行。")
        self._update_diagnostics()

    def _schedule_diagnostics_refresh(self):
        self._update_diagnostics()
        self.root.after(500, self._schedule_diagnostics_refresh)

    def _update_diagnostics(self):
        mode_str = "ONLINE"
        if self.mutex_present:
            mode_str = "BLOCKED (Mutex Present)"
        elif self.engine.dry_run:
            mode_str = "DRY-RUN (Offline Simulation)"
        elif self.engine.stream_error:
            mode_str = "STREAM-ERROR"
        self.diag_labels["MODE"].config(
            text=mode_str,
            fg="#00E676" if mode_str=="ONLINE" else ("#FF1744" if "STREAM-ERROR" in mode_str else "#FFD54F")
        )

        self.diag_labels["Script"].config(text=os.path.abspath(__file__))
        self.diag_labels["aura_hal.py"].config(text=os.path.abspath(aura_hal.__file__))

        dll_path = "--"
        dll_hash = "--"
        acc_ret = "--"
        if self.engine.hal:
            dll_path = str(self.engine.hal.loaded_dll_path)
            dll_hash = str(self.engine.hal.loaded_dll_sha256)
            if self.engine.last_access_return is not None:
                acc_ret = f"0x{self.engine.last_access_return & 0xFFFFFFFF:08X} ({self.engine.last_access_return})"
        self.diag_labels["AacKbHal_x64.dll"].config(text=dll_path)
        self.diag_labels["DLL SHA256"].config(text=dll_hash)
        self.diag_labels["HAL Access return"].config(text=acc_ret)

        dev_cnt = f"{self.engine.devices_count} 个设备"
        dev_sel = f"Index: {self.engine.selected_device_idx} (0x{self.engine.pDev:016X})" if self.engine.device else "None"
        self.diag_labels["Devices found"].config(text=dev_cnt)
        self.diag_labels["Selected device"].config(text=dev_sel)

        slots = self.engine.configured_slots
        req_bytes = slots * RGB_CHANNELS
        act_bytes = len(self.engine.rgb_buf)
        self.diag_labels["Configured slots"].config(text=f"{slots} 槽位")
        self.diag_labels["RGB buffer bytes"].config(text=f"{act_bytes} 字节")
        self.diag_labels["Required buffer bytes"].config(text=f"{req_bytes} 字节 (槽数 * 3)")

        thread_str = "ALIVE (正在稳定推流)" if self.engine.stream_thread_alive else "DEAD (已退出或未启动)"
        self.diag_labels["Stream thread"].config(
            text=thread_str,
            fg="#00E676" if self.engine.stream_thread_alive else "#FF1744"
        )

        last_ret = f"0x{self.engine.last_stream_return & 0xFFFFFFFF:08X} ({self.engine.last_stream_return})" if self.engine.last_stream_return is not None else "--"
        self.diag_labels["Last Set_L_STD_SINGLE_XY return"].config(text=last_ret)
        self.diag_labels["Last stream exception"].config(text=str(self.engine.last_exception or "None"))

        mutex_str = "PRESENT (被占用)" if self.mutex_present else "NOT PRESENT (未被占用)"
        self.diag_labels["Global\\ExclusiveExecution_ROGKB"].config(
            text=mutex_str,
            fg="#FF1744" if self.mutex_present else "#00E676"
        )

        svc_light = get_service_status("LightingService")
        svc_ac = get_service_status("ArmouryCrateService")
        self.diag_labels["LightingService"].config(
            text=svc_light,
            fg="#FFB74D" if svc_light=="RUNNING" else "#ECEFF1"
        )
        self.diag_labels["ArmouryCrateService"].config(
            text=svc_ac,
            fg="#FFB74D" if svc_ac=="RUNNING" else "#ECEFF1"
        )

    # -------------------------------------------------------------------------
    # 状态刷新与事件
    # -------------------------------------------------------------------------
    def _refresh_lightbar_strip(self):
        for widget in self.strip_frame.winfo_children():
            widget.destroy()

        self.lb_count_lbl.config(text=f"已发现 Light Bar 灯珠: {len(self.lightbar_order)} 颗")

        if not self.lightbar_order:
            tk.Label(
                self.strip_frame,
                text="（暂未捕获灯带灯珠，请在网格巡检引脚并点击 [🌟 Light Bar] 记录）",
                font=("Segoe UI", 9),
                fg="#78909C",
                bg="#131316",
                pady=10
            ).pack(side=tk.LEFT, padx=10)
            return

        for pos, lid in enumerate(self.lightbar_order):
            is_active = (lid == self.current_id)
            bg_color = "#00E676" if not is_active else "#00E5FF"
            fg_color = "#000000"
            border_col = "#FFFFFF" if is_active else "#2E7D32"

            seg = tk.Frame(self.strip_frame, bg=border_col, padx=1, pady=1)
            seg.pack(side=tk.LEFT, padx=3, pady=4)

            btn = tk.Button(
                seg,
                text=f"Pos #{pos}\nID: {lid}",
                font=("Segoe UI", 8, "bold"),
                bg=bg_color,
                fg=fg_color,
                relief=tk.FLAT,
                padx=6,
                pady=2,
                cursor="hand2",
                command=lambda i=lid: self._set_hardware_led(i)
            )
            btn.pack()

    def _refresh_matrix_colors(self):
        for lid, btn in self.grid_buttons.items():
            rec = self.records.get(lid, {})
            cname = rec.get("classification", "Unknown")
            bg = CLASS_COLORS.get(cname, CLASS_COLORS["Unknown"])
            fg = CLASS_TEXT_COLORS.get(cname, "#FFFFFF")

            if lid == self.current_id:
                btn.config(bg="#00E5FF", fg="#000000", relief=tk.RAISED, bd=2)
            else:
                btn.config(bg=bg, fg=fg, relief=tk.FLAT, bd=1)

    def _set_hardware_led(self, lid: int):
        self.current_id = max(0, min(127, lid))
        self.engine.set_single_key(self.current_id, 0, 220, 255)

        col = self.current_id // 8
        row = self.current_id % 8
        self.id_display_lbl.config(text=f"当前引脚: ID [ {self.current_id:3d} ]  (Col:{col:2d}, Row:{row})")
        self.id_slider.set(self.current_id)

        self._refresh_matrix_colors()
        self._refresh_lightbar_strip()
        rec = self.records.get(self.current_id, {})
        cname = rec.get("classification", "Unknown")
        pos = rec.get("physical_position")
        self.current_status_lbl.config(text=f"当前分类: [ {cname} ]", fg=CLASS_COLORS.get(cname, "#FFD54F"))
        if pos is not None:
            self.pos_display_lbl.config(text=f"Pos: #{pos}", fg="#00E676")
            self.btn_pos_left.config(state=tk.NORMAL)
            self.btn_pos_right.config(state=tk.NORMAL)
        else:
            self.pos_display_lbl.config(text="Pos: -", fg="#78909C")
            self.btn_pos_left.config(state=tk.DISABLED)
            self.btn_pos_right.config(state=tk.DISABLED)

    def next_id(self):
        next_lid = (self.current_id + 1) % TOTAL_LEDS
        if self.skip_known_var.get():
            attempts = 0
            while self.records.get(next_lid, {}).get("classification") == "Keyboard Key" and attempts < TOTAL_LEDS:
                next_lid = (next_lid + 1) % TOTAL_LEDS
                attempts += 1
        self._set_hardware_led(next_lid)

    def prev_id(self):
        prev_lid = (self.current_id - 1) % TOTAL_LEDS
        if self.skip_known_var.get():
            attempts = 0
            while self.records.get(prev_lid, {}).get("classification") == "Keyboard Key" and attempts < TOTAL_LEDS:
                prev_lid = (prev_lid - 1) % TOTAL_LEDS
                attempts += 1
        self._set_hardware_led(prev_lid)

    def turn_all_off(self):
        self.engine.clear()
        self.status_lbl.config(text="已熄灭所有硬件引脚。", fg="#B0BEC5")

    def on_slider_move(self, val):
        new_id = int(float(val))
        if new_id != self.current_id:
            self._set_hardware_led(new_id)

    def set_current_classification(self, cls_name: str):
        rec = self.records.setdefault(self.current_id, {
            "led_id": self.current_id,
            "classification": "Unknown",
            "physical_position": None,
            "controllable": True,
            "notes": ""
        })
        old_cls = rec.get("classification")
        if cls_name == "Light Bar":
            rec["classification"] = "Light Bar"
            if self.current_id not in self.lightbar_order:
                self.lightbar_order.append(self.current_id)
            for idx, lid in enumerate(self.lightbar_order):
                if lid in self.records:
                    self.records[lid]["physical_position"] = idx
            self.engine.flash_feedback(self.current_id, 0, 255, 0, duration=0.15)
        else:
            if old_cls == "Light Bar" and self.current_id in self.lightbar_order:
                self.lightbar_order.remove(self.current_id)
                for idx, lid in enumerate(self.lightbar_order):
                    if lid in self.records:
                        self.records[lid]["physical_position"] = idx
            rec["classification"] = cls_name
            rec["physical_position"] = None

        export_lightbar_mapping(self.records, self.lightbar_order)
        self._refresh_matrix_colors()
        self._refresh_lightbar_strip()
        if not self.is_scanning:
            self.root.after(200, self.next_id)

    def move_current_lb_left(self):
        if self.current_id not in self.lightbar_order:
            return
        idx = self.lightbar_order.index(self.current_id)
        if idx > 0:
            self.lightbar_order[idx], self.lightbar_order[idx - 1] = self.lightbar_order[idx - 1], self.lightbar_order[idx]
            for i, lid in enumerate(self.lightbar_order):
                if lid in self.records:
                    self.records[lid]["physical_position"] = i
            export_lightbar_mapping(self.records, self.lightbar_order)
            self._refresh_lightbar_strip()

    def move_current_lb_right(self):
        if self.current_id not in self.lightbar_order:
            return
        idx = self.lightbar_order.index(self.current_id)
        if idx < len(self.lightbar_order) - 1:
            self.lightbar_order[idx], self.lightbar_order[idx + 1] = self.lightbar_order[idx + 1], self.lightbar_order[idx]
            for i, lid in enumerate(self.lightbar_order):
                if lid in self.records:
                    self.records[lid]["physical_position"] = i
            export_lightbar_mapping(self.records, self.lightbar_order)
            self._refresh_lightbar_strip()

    def clear_lightbar_list(self):
        if not self.lightbar_order:
            return
        if messagebox.askyesno("清空确认", "确定要清空已记录的顶部 Light Bar 序列吗？"):
            for lid in list(self.lightbar_order):
                if lid in self.records:
                    self.records[lid]["classification"] = "Unknown"
                    self.records[lid]["physical_position"] = None
            self.lightbar_order.clear()
            export_lightbar_mapping(self.records, self.lightbar_order)
            self._refresh_lightbar_strip()
            self._refresh_matrix_colors()

    def run_sweep_test(self):
        if not self.lightbar_order or self.sweep_testing:
            return
        self.sweep_testing = True
        self.sweep_test_btn.config(text="测试走灯中...", bg="#D81B60", state=tk.DISABLED)

        def _sweep():
            for idx, lid in enumerate(self.lightbar_order):
                self.engine.set_single_key(lid, 0, 255, 128)
                time.sleep(0.22)
            time.sleep(0.1)
            self.engine.set_single_key(self.current_id, 0, 220, 255)
            self.sweep_testing = False
            self.root.after(0, lambda: self.sweep_test_btn.config(text="▶ 从左到右走灯测试 (Sweep Test)", bg="#7B1FA2", state=tk.NORMAL))

        threading.Thread(target=_sweep, daemon=True).start()

    def toggle_scan(self):
        if not self.is_scanning:
            self.is_scanning = True
            self.scan_btn.config(text="⏸ 暂停巡检", bg="#D81B60")
            self._auto_step()
        else:
            self.is_scanning = False
            self.scan_btn.config(text="▷ 开始自动巡检", bg="#5E35B1")
            if self.scan_timer:
                self.root.after_cancel(self.scan_timer)

    def _auto_step(self):
        if not self.is_scanning:
            return
        try:
            start_id = max(0, min(127, int(self.start_id_entry.get())))
            end_id = max(0, min(127, int(self.end_id_entry.get())))
            interval = max(100, int(self.interval_entry.get()))
        except Exception:
            start_id, end_id, interval = 0, 127, 800

        cur = self.current_id
        next_lid = start_id if cur < start_id or cur >= end_id else cur + 1
        if self.skip_known_var.get():
            attempts = 0
            while self.records.get(next_lid, {}).get("classification") == "Keyboard Key" and attempts < 128:
                next_lid = next_lid + 1
                if next_lid > end_id:
                    next_lid = start_id
                attempts += 1

        self._set_hardware_led(next_lid)
        self.scan_timer = self.root.after(interval, self._auto_step)

    def save_and_export(self):
        export_lightbar_mapping(self.records, self.lightbar_order)
        messagebox.showinfo("导出成功", f"映射档案已导出至:\n{LIGHTBAR_MAPPING_FILE}")

    def on_close(self):
        if self.is_scanning and self.scan_timer:
            self.root.after_cancel(self.scan_timer)
        export_lightbar_mapping(self.records, self.lightbar_order)
        if self.engine:
            self.engine.close()
        self.root.destroy()


# =============================================================================
# CLI 自动化与脚本验证运行入口
# =============================================================================
def run_cli(args_list: List[str]):
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

    parser = argparse.ArgumentParser(description="ROG Falchion Ace HFX Light Bar 探针与硬件实验 CLI")
    parser.add_argument("--diagnostics", "-d", action="store_true", help="打印实时硬件与驱动诊断信息并退出")
    parser.add_argument("--test-a", action="store_true", help="执行 TEST A (对照组 Pin 57 键7, 持续发光并打印日志)")
    parser.add_argument("--test-b", action="store_true", help="执行 TEST B (候选点 Pin 56, 持续发光并打印日志)")
    parser.add_argument("--test-c", type=int, nargs="?", const=56, help="执行 TEST C (Row 0 15 候选点，可选指定引脚，默认 56)")
    parser.add_argument("--duration", type=float, default=2.0, help="推流持续时间 (秒，默认 2.0)")
    args = parser.parse_args(args_list)

    print("=============================================================================")
    print("ROG FALCHION ACE HFX - Light Bar 硬件诊断与物理测试 CLI")
    print("=============================================================================")

    # 1. 检测环境互斥体与服务
    mutex_present, mutex_err = check_exclusive_mutex()
    svc_light = get_service_status("LightingService")
    svc_ac = get_service_status("ArmouryCrateService")

    print(f"* Global\\ExclusiveExecution_ROGKB 互斥体: {'PRESENT (被占用)' if mutex_present else 'NOT PRESENT'}")
    print(f"* LightingService 状态: {svc_light}")
    print(f"* ArmouryCrateService 状态: {svc_ac}")

    engine = LightingEngine(dry_run=mutex_present)
    print(f"* 推流引擎模式: {'DRY RUN (受限模拟)' if engine.dry_run else 'ONLINE (直连硬件)'}")
    if engine.hal:
        print(f"* 实际加载 DLL: {engine.hal.loaded_dll_path}")
        print(f"* DLL SHA256: {engine.hal.loaded_dll_sha256}")
        print(f"* HAL Access 返回值: 0x{engine.last_access_return & 0xFFFFFFFF:08X} ({engine.last_access_return})")
        print(f"* 枚举设备总数: {engine.devices_count}")
        print(f"* 选定设备指针: 0x{engine.pDev:016X}")

    if args.diagnostics:
        print("\n--- 诊断项检查完毕 ---")
        engine.close()
        return

    if args.test_a:
        print(f"\n[TEST A 启动] 配置 table = [{TEST_A_CONTROL_PIN}] (Pin 57 / 键 7)，亮红持续 {args.duration} 秒...")
        engine.activate_test_a()
        time.sleep(0.1)
        print(f"* 已配置槽数: {engine.configured_slots}, RGB 缓冲字节: {len(engine.rgb_buf)}")
        print(f"* 推流线程存活: {engine.stream_thread_alive}, Set_L_STD_SINGLE_XY 返回值: {engine.last_stream_return}")
        time.sleep(args.duration)
        print(f"[TEST A 完成] 最后推流返回值: {engine.last_stream_return}, 异常: {engine.last_exception}")
        engine.close()
        return

    if args.test_b:
        print(f"\n[TEST B 启动] 配置 table = [{TEST_B_CANDIDATE_PIN}] (Light Bar 候选 Pin 56)，亮红持续 {args.duration} 秒...")
        engine.activate_test_b()
        time.sleep(0.1)
        print(f"* 已配置槽数: {engine.configured_slots}, RGB 缓冲字节: {len(engine.rgb_buf)}")
        print(f"* 推流线程存活: {engine.stream_thread_alive}, Set_L_STD_SINGLE_XY 返回值: {engine.last_stream_return}")
        time.sleep(args.duration)
        print(f"[TEST B 完成] 最后推流返回值: {engine.last_stream_return}, 异常: {engine.last_exception}")
        engine.close()
        return

    if args.test_c is not None:
        cand_id = args.test_c
        print(f"\n[TEST C 启动] 配置 table = {ROW0_CANDIDATES} (15 槽 45 字节)，引脚 {cand_id} 亮红持续 {args.duration} 秒...")
        engine.activate_test_c(cand_id)
        time.sleep(0.1)
        slot_idx = engine.led_to_slot.get(cand_id, -1)
        print(f"* 已配置槽数: {engine.configured_slots}, RGB 缓冲字节: {len(engine.rgb_buf)}, 目标槽位: {slot_idx}")
        print(f"* 推流线程存活: {engine.stream_thread_alive}, Set_L_STD_SINGLE_XY 返回值: {engine.last_stream_return}")
        time.sleep(args.duration)
        print(f"[TEST C 完成] 最后推流返回值: {engine.last_stream_return}, 异常: {engine.last_exception}")
        engine.close()
        return

    engine.close()


def main():
    if len(sys.argv) > 1 and sys.argv[1].startswith("-"):
        run_cli(sys.argv[1:])
    else:
        root = tk.Tk()
        app = LightBarProbeGUI(root)
        root.mainloop()


if __name__ == "__main__":
    main()
