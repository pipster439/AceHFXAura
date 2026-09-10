#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Aura HAL (tools.py.aura_hal) 单元与集成测试套件
验证项目:
1. 基础常量、内存偏移与数据结构 ABI 兼容性
2. 64 字节 USB HID 边界隔离槽填充算法 (0xFF padding)
3. 权威标定表 (calibrated_keymap.json) 完整性与 68 物理键无重叠
4. 5 组历史 LED ID 冲突消除与权威覆盖
5. 无法从权威源确定按键时的显式报错与防错点灯机制
6. set_per_key.py 与 gui_calibrator.py 迁移后的 dry-run 行为自洽性
"""

import ctypes
import os
import sys
import unittest

# 确保加载 Aura 根目录及 tools/py 模块
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)
TOOLS_PY = os.path.join(ROOT, "tools", "py")
if TOOLS_PY not in sys.path:
    sys.path.insert(0, TOOLS_PY)

import aura_hal
import set_per_key
import gui_calibrator
import e2e_key_test


class TestAuraHalConstantsAndABI(unittest.TestCase):
    """测试常量定义、内存偏移与虚表索引"""

    def test_constants_alignment(self):
        self.assertEqual(aura_hal.TOTAL_LEDS, 128)
        self.assertEqual(aura_hal.RGB_CHANNELS, 3)
        self.assertEqual(aura_hal.FRAME_BUFFER_SIZE, 384)
        self.assertEqual(aura_hal.TOTAL_PHYSICAL_KEYS, 68)
        self.assertEqual(aura_hal.HARDWARE_STREAM_KEYS, 72)
        self.assertEqual(aura_hal.MAX_HARDWARE_STREAM_KEYS, 144)
        self.assertEqual(aura_hal.DUMMY_PADDING_LED_ID, 0xFF)

    def test_memory_offsets_and_vtables(self):
        self.assertEqual(aura_hal.OFFSET_KEY_COUNT, 0x6C)
        self.assertEqual(aura_hal.OFFSET_LED_TABLE, 0x74)
        self.assertEqual(aura_hal.VTABLE_HAL_RELEASE, 2)
        self.assertEqual(aura_hal.VTABLE_HAL_ACCESS, 4)
        self.assertEqual(aura_hal.VTABLE_HAL_CREATE_LED_DEVICE, 5)
        self.assertEqual(aura_hal.VTABLE_DEV_RELEASE, 2)
        self.assertEqual(aura_hal.VTABLE_DEV_SET_SINGLE, 19)

    def test_guid_conversion(self):
        g = aura_hal.to_guid(aura_hal.CLSID_CLAYMORE_HAL)
        self.assertEqual(ctypes.sizeof(g), 16)
        # GUID Data1 for {AE9DB4C8-...}
        self.assertEqual(g.Data1, 0xAE9DB4C8)

    def test_std_vector_abi(self):
        vec = aura_hal.StdVector()
        # 64 位下 3 个指针各 8 字节，总共 24 字节
        self.assertEqual(ctypes.sizeof(vec), 24)


class TestPaddedHardwareTable(unittest.TestCase):
    """测试 USB 64 字节 HID 报文边界隔离填充算法"""

    def test_68_keys_padding(self):
        ids = sorted(list(aura_hal.VERIFIED_LED_IDS))
        self.assertEqual(len(ids), 68)
        table = aura_hal.build_padded_hardware_table(ids)
        # 68 键插入 4 个 0xFF，总长 72
        self.assertEqual(len(table), 72)
        # 隔离槽位置验证：第 14、29、44、59 槽为 0xFF
        self.assertEqual(table[14], 0xFF)
        self.assertEqual(table[29], 0xFF)
        self.assertEqual(table[44], 0xFF)
        self.assertEqual(table[59], 0xFF)
        # 非隔离槽不应包含 0xFF
        non_padding_slots = [i for i in range(72) if i not in (14, 29, 44, 59)]
        for idx in non_padding_slots:
            self.assertNotEqual(table[idx], 0xFF)

    def test_128_keys_padding_upper_bound(self):
        # 极端最坏情况：128 个唯一键
        all_ids = list(range(128))
        table = aura_hal.build_padded_hardware_table(all_ids)
        self.assertEqual(len(table), 137)
        self.assertLessEqual(len(table), aura_hal.MAX_HARDWARE_STREAM_KEYS)

    def test_exceeding_upper_bound_raises(self):
        # 超出 144 槽上限必须抛出 ValueError
        excessive_ids = list(range(140))  # 140 键 + 9 隔离槽 = 149 > 144
        with self.assertRaises(ValueError):
            aura_hal.build_padded_hardware_table(excessive_ids)


class TestAuthoritativeKeymapAndConflicts(unittest.TestCase):
    """测试权威键位标定及 5 组历史冲突消除"""

    def test_authoritative_key_count(self):
        # 实测确认的物理键必须恰好 68 个唯一 ID
        self.assertEqual(len(aura_hal.VERIFIED_LED_IDS), 68)
        self.assertEqual(min(aura_hal.VERIFIED_LED_IDS), 1)
        self.assertEqual(max(aura_hal.VERIFIED_LED_IDS), 117)

    def test_conflict_group_1_i_vs_enter(self):
        # 历史冲突: I=66, ENTER=66
        self.assertEqual(aura_hal.resolve_key('I'), 66)
        self.assertEqual(aura_hal.resolve_key('ENTER'), 107)
        self.assertEqual(aura_hal.resolve_key('RETURN'), 107)
        self.assertNotEqual(aura_hal.resolve_key('I'), aura_hal.resolve_key('ENTER'))

    def test_conflict_group_2_k_vs_ins(self):
        # 历史冲突: K=67, INS=67
        self.assertEqual(aura_hal.resolve_key('K'), 67)
        self.assertEqual(aura_hal.resolve_key('INS'), 113)
        self.assertEqual(aura_hal.resolve_key('INSERT'), 113)
        self.assertNotEqual(aura_hal.resolve_key('K'), aura_hal.resolve_key('INS'))

    def test_conflict_group_3_o_vs_pgup_minus(self):
        # 历史冲突: O=74, PGUP=74, MINUS=74
        self.assertEqual(aura_hal.resolve_key('O'), 74)
        self.assertEqual(aura_hal.resolve_key('PGUP'), 115)
        self.assertEqual(aura_hal.resolve_key('PAGEUP'), 115)
        self.assertEqual(aura_hal.resolve_key('-'), 89)
        self.assertEqual(aura_hal.resolve_key('MINUS'), 89)
        self.assertEqual(len({aura_hal.resolve_key('O'), aura_hal.resolve_key('PGUP'), aura_hal.resolve_key('MINUS')}), 3)

    def test_conflict_group_4_l_vs_del(self):
        # 历史冲突: L=75, DEL=75
        self.assertEqual(aura_hal.resolve_key('L'), 75)
        self.assertEqual(aura_hal.resolve_key('DEL'), 114)
        self.assertEqual(aura_hal.resolve_key('DELETE'), 114)
        self.assertNotEqual(aura_hal.resolve_key('L'), aura_hal.resolve_key('DEL'))

    def test_conflict_group_5_p_vs_backspace_equal(self):
        # 历史冲突: P=82, BACKSPACE=82, EQUAL=82
        self.assertEqual(aura_hal.resolve_key('P'), 82)
        self.assertEqual(aura_hal.resolve_key('BACKSPACE'), 105)
        self.assertEqual(aura_hal.resolve_key('BS'), 105)
        self.assertEqual(aura_hal.resolve_key('='), 97)
        self.assertEqual(aura_hal.resolve_key('EQUAL'), 97)
        self.assertEqual(len({aura_hal.resolve_key('P'), aura_hal.resolve_key('BACKSPACE'), aura_hal.resolve_key('EQUAL')}), 3)

    def test_uncertified_key_explicit_rejection(self):
        # 无法从权威源确定者必须显式抛异常拒绝点灯
        with self.assertRaises(KeyError) as ctx:
            aura_hal.resolve_key('F1')
        self.assertIn("仅推导、未实测", str(ctx.exception))

        with self.assertRaises(KeyError) as ctx:
            aura_hal.resolve_key('UNKNOWN_KEY_NAME')
        self.assertIn("无法从权威标定源确定", str(ctx.exception))

    def test_unverified_led_id_rejection_in_strict_mode(self):
        # 12 曾被旧代码误标为 Z（实测 Z 为 20），12 是未连接的空引脚
        with self.assertRaises(ValueError) as ctx:
            aura_hal.resolve_key(12, strict=True)
        self.assertIn("仅推导、未实测", str(ctx.exception))

        # 超界 ID 抛出 ValueError
        with self.assertRaises(ValueError):
            aura_hal.resolve_key(128, strict=True)
        with self.assertRaises(ValueError):
            aura_hal.resolve_key(-1, strict=True)

    def test_all_canonical_68_keys_resolution(self):
        # 验证全部 68 颗权威物理按键均能正确无误解析为对应的 LED ID
        for k, expected_id in aura_hal.CANONICAL_68_KEYS.items():
            actual_id = aura_hal.resolve_key(k)
            self.assertEqual(
                actual_id, expected_id,
                f"按键 [{k}] 解析错误: 实际 ID={actual_id}, 期望 ID={expected_id}"
            )

    def test_digit_keys_resolution(self):
        # 验证数字按键 '0'~'9' 必须解析为顶部数字排对应的真实 LED ID，而不是将数字字符串误判为 LED ID
        expected_digits = {
            '1': 9, '2': 17, '3': 25, '4': 33, '5': 41,
            '6': 49, '7': 57, '8': 65, '9': 73, '0': 81
        }
        for d, expected_id in expected_digits.items():
            self.assertEqual(aura_hal.resolve_key(d), expected_id)

    def test_numeric_string_vs_integer_id(self):
        # "1" 作为按键名，应解析为按键 '1' 的 LED ID (9)
        self.assertEqual(aura_hal.resolve_key("1"), 9)
        # 1 作为整数，代表 LED ID 1 (ESC 物理引脚)
        self.assertEqual(aura_hal.resolve_key(1), 1)

        # "107" 不是按键名，但为纯数字字符串，应解析为 LED ID 107 (ENTER)
        self.assertEqual(aura_hal.resolve_key("107"), 107)

        # "12" 不是按键名，LED ID 12 为未实测引脚，strict 模式必须拒绝
        with self.assertRaises(ValueError):
            aura_hal.resolve_key("12", strict=True)
        # strict=False 模式允许透传合法区间 [0, 128) 内的引脚
        self.assertEqual(aura_hal.resolve_key("12", strict=False), 12)

    def test_unsupported_standard_keys_rejection(self):
        # 标准键盘功能键在本 68 键物理键盘不存在，必须明确抛出 KeyError
        for k in ['F1', 'F5', 'F12', 'HOME', 'END', 'PRTSC', 'PAUSE']:
            with self.assertRaises(KeyError) as ctx:
                aura_hal.resolve_key(k)
            self.assertIn("仅推导、未实测", str(ctx.exception))


class TestMigratedScriptsBehavior(unittest.TestCase):
    """测试迁移后三个常用脚本的逻辑自洽性"""

    def test_set_per_key_controller_dry_run(self):
        ctrl = set_per_key.RogFalchionController(dry_run=True)
        self.assertEqual(ctrl.TOTAL_LEDS, 128)
        self.assertEqual(len(ctrl.padded_table), 72)

        # 测试正常设置
        lid = ctrl.set_key_color('ENTER', 255, 0, 0)
        self.assertEqual(lid, 107)
        self.assertEqual(ctrl.rgb_buf[107 * 3 + 0], 255)

        # 测试别名设置
        lid_bs = ctrl.set_key_color('BS', 0, 255, 0)
        self.assertEqual(lid_bs, 105)
        self.assertEqual(ctrl.rgb_buf[105 * 3 + 1], 255)

        # 测试数字键名设置
        lid_1 = ctrl.set_key_color('1', 255, 0, 0)
        self.assertEqual(lid_1, 9)

        # 测试未知按键报错
        with self.assertRaises(KeyError):
            ctrl.set_key_color('INVALID_KEY', 10, 20, 30)

        # 单帧推流不崩溃
        res = ctrl.stream_once()
        self.assertEqual(res, 0)
        ctrl.close()

    def test_set_per_key_cli_invalid_keys_abort_immediately(self):
        import subprocess
        # 即使未指定 --duration，出现非法或未标定键时必须立即打印错误并退出 (returncode=1)，严禁挂起
        test_args = [
            ['UNKNOWN_KEY'],
            ['F1'],
            ['12'],
        ]
        for bad_key in test_args:
            cmd = [sys.executable, os.path.join(ROOT, "set_per_key.py"), "--dry-run", "--key", bad_key[0], "255", "0", "0"]
            proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=5)
            self.assertEqual(proc.returncode, 1, f"命令 {cmd} 应该返回 1，实际返回: {proc.returncode}")
            self.assertIn("[-] 错误:", proc.stdout)
            self.assertIn("中止推流以防硬件错位", proc.stdout)

    def test_set_per_key_cli_key_resolution(self):
        import subprocess
        # 验证 CLI 参数解析正确识别单键、数字键、数字 ID 与连击组合键
        cmd = [
            sys.executable, os.path.join(ROOT, "set_per_key.py"), "--dry-run", "--duration", "0.1",
            "--key", "1", "255", "0", "0",
            "--key", "107", "0", "255", "0",
            "--key", "1234", "0", "0", "255",
            "--key", "QWER", "255", "255", "0"
        ]
        proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=5)
        self.assertEqual(proc.returncode, 0, f"STDOUT: {proc.stdout}\nSTDERR: {proc.stderr}")
        self.assertIn("1(ID 9)", proc.stdout)
        self.assertIn("107(ID 107)", proc.stdout)
        self.assertIn("1(ID 9), 2(ID 17), 3(ID 25), 4(ID 33)", proc.stdout)
        self.assertIn("Q(ID 10), W(ID 18), E(ID 26), R(ID 34)", proc.stdout)

    def test_gui_calibrator_engine_dry_run(self):
        engine = gui_calibrator.KeyboardLightingEngine(dry_run=True)
        engine.set_single_key(1, 0, 255, 255)
        self.assertEqual(engine.rgb_buf[1 * 3 + 1], 255)
        self.assertEqual(engine.rgb_buf[1 * 3 + 2], 255)
        engine.clear()
        self.assertEqual(engine.rgb_buf[1 * 3 + 1], 0)
        engine.close()

    def test_e2e_key_test_imports_aura_hal(self):
        self.assertTrue(hasattr(e2e_key_test, "KEYS"))
        self.assertTrue(hasattr(e2e_key_test, "NAME_TO_LED"))
        self.assertTrue(hasattr(e2e_key_test, "LED_TO_NAME"))
        self.assertEqual(e2e_key_test.NAME_TO_LED.get("ENTER"), 107)
        self.assertEqual(e2e_key_test.NAME_TO_LED.get("SPACE"), 53)


if __name__ == "__main__":
    unittest.main(verbosity=2)
