#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Unit tests for Light Bar Probe (tools/lightbar_probe.py)
"""

import json
import os
import sys
import tempfile
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)
TOOLS_DIR = os.path.join(ROOT, "tools")
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import lightbar_probe


class TestLightBarProbeLogic(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.export_json = os.path.join(self.temp_dir.name, "lightbar_mapping.json")

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_lighting_engine_dry_run(self):
        engine = lightbar_probe.LightingEngine(dry_run=True)
        self.assertEqual(engine.TOTAL_LEDS, 128)
        engine.set_single_key(0, 0, 220, 255)
        self.assertEqual(engine.rgb_buf[0], 0)
        self.assertEqual(engine.rgb_buf[1], 220)
        self.assertEqual(engine.rgb_buf[2], 255)

        engine.clear()
        self.assertEqual(engine.rgb_buf[1], 0)
        engine.close()

    def test_known_keys_loading(self):
        keys = lightbar_probe.load_existing_keymap_keys()
        self.assertIn(1, keys)  # ESC
        self.assertEqual(keys[1], "ESC")
        self.assertIn(107, keys)  # ENTER
        self.assertEqual(keys[107], "ENTER")

    def test_export_lightbar_mapping_schema(self):
        # 构造测试数据
        records = {}
        for lid in range(128):
            records[lid] = {
                "led_id": lid,
                "classification": "Unknown",
                "physical_position": None,
                "controllable": True,
                "notes": ""
            }

        # 标记两个 Light Bar 节点
        records[0]["classification"] = "Light Bar"
        records[0]["physical_position"] = 0
        records[0]["notes"] = "Leftmost"

        records[8]["classification"] = "Light Bar"
        records[8]["physical_position"] = 1
        records[8]["notes"] = "Second"

        lightbar_order = [0, 8]

        lightbar_probe.export_lightbar_mapping(records, lightbar_order, self.export_json)
        self.assertTrue(os.path.exists(self.export_json))

        # 验证 JSON 内容
        with open(self.export_json, "r", encoding="utf-8") as f:
            data = json.load(f)

        self.assertIn("lightbar", data)
        self.assertEqual(data["lightbar"], [0, 8])

        self.assertIn("summary", data)
        self.assertEqual(data["summary"]["total_lightbar_leds"], 2)

        self.assertIn("mapping", data)
        self.assertEqual(len(data["mapping"]), 128)

        # 检查 ID 0 的属性
        rec0 = next(item for item in data["mapping"] if item["led_id"] == 0)
        self.assertEqual(rec0["classification"], "Light Bar")
        self.assertEqual(rec0["physical_position"], 0)
        self.assertTrue(rec0["controllable"])
        self.assertEqual(rec0["notes"], "Leftmost")
        self.assertEqual(rec0["matrix_hardware_coords"]["matrix_col"], 0)
        self.assertEqual(rec0["matrix_hardware_coords"]["matrix_row"], 0)

        # 检查生成的 Markdown 报告
        md_file = self.export_json.replace(".json", ".md")
        self.assertTrue(os.path.exists(md_file))
        with open(md_file, "r", encoding="utf-8") as f:
            md_content = f.read()
        self.assertIn("Pos #0", md_content)
        self.assertIn("`[0, 8]`", md_content)

    def test_reorder_logic(self):
        # 模拟物理顺序重排
        order = [0, 8, 16]
        # 向右移
        idx = order.index(0)
        order[idx], order[idx + 1] = order[idx + 1], order[idx]
        self.assertEqual(order, [8, 0, 16])
        # 向左移
        idx = order.index(16)
        order[idx], order[idx - 1] = order[idx - 1], order[idx]
        self.assertEqual(order, [8, 16, 0])

    def test_explicit_led_to_slot_mapping(self):
        engine = lightbar_probe.LightingEngine(dry_run=True)

        # 1. 验证 128 槽直通表
        self.assertEqual(len(engine.hardware_table), 128)
        self.assertEqual(engine.led_to_slot[56], 56)
        self.assertEqual(engine.led_to_slot[57], 57)

        # 2. 验证 TEST A: table = [57] -> slot 0
        engine.activate_test_a()
        self.assertEqual(engine.configured_slots, 1)
        self.assertEqual(len(engine.rgb_buf), 3)
        self.assertEqual(engine.led_to_slot, {57: 0})
        self.assertEqual(engine.rgb_buf[0], 255)
        self.assertEqual(engine.rgb_buf[1], 0)
        self.assertEqual(engine.rgb_buf[2], 0)

        # 3. 验证 TEST B: table = [56] -> slot 0
        engine.activate_test_b()
        self.assertEqual(engine.configured_slots, 1)
        self.assertEqual(len(engine.rgb_buf), 3)
        self.assertEqual(engine.led_to_slot, {56: 0})
        self.assertEqual(engine.rgb_buf[0], 255)
        self.assertEqual(engine.rgb_buf[1], 0)
        self.assertEqual(engine.rgb_buf[2], 0)

        # 4. 验证 TEST C: table = ROW0_CANDIDATES -> 15 槽 45 字节
        engine.activate_test_c(56)
        self.assertEqual(engine.configured_slots, 15)
        self.assertEqual(len(engine.rgb_buf), 45)
        self.assertEqual(engine.led_to_slot[56], 7)  # 56 在第 7 槽
        self.assertEqual(engine.rgb_buf[7 * 3 + 0], 255)
        self.assertEqual(engine.rgb_buf[7 * 3 + 1], 0)
        self.assertEqual(engine.rgb_buf[7 * 3 + 2], 0)
        # 其它槽应为 0
        self.assertEqual(engine.rgb_buf[0], 0)
        self.assertEqual(engine.rgb_buf[1 * 3 + 0], 0)

        engine.close()

    def test_mutex_check_utility(self):
        is_pres, err = lightbar_probe.check_exclusive_mutex()
        self.assertIsInstance(is_pres, bool)
        self.assertIsInstance(err, int)


if __name__ == "__main__":
    unittest.main()

