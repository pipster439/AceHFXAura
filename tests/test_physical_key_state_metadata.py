"""Offline tests of metadata ABI/sanitization, not physical-key evidence."""

import importlib.util
from pathlib import Path
import struct
import unittest

SPEC = importlib.util.spec_from_file_location(
    "physical_key_state_metadata", Path(__file__).resolve().parents[1]
    / "tools/research/physical_key_state_metadata.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class TargetIdentityTests(unittest.TestCase):
    def test_target_discards_serial_and_path(self):
        path = r"\\?\HID#VID_0B05&PID_1B7E&MI_02&COL03#PRIVATE_SERIAL#{GUID}"
        self.assertEqual(MODULE.target_identity(path), {"interface": "MI_02", "collection": "Col03"})

    def test_keyboard_without_collection_suffix(self):
        self.assertEqual(MODULE.target_identity(r"\\?\hid#vid_0b05&pid_1b7e&mi_03#private"),
                         {"interface": "MI_03", "collection": None})

    def test_near_matches_rejected(self):
        for path in (
            "hid#vid_0b05&pid_1b7f&mi_03#x", "hid#vid_0b05&pid_1b7e0&mi_03#x",
            "hid#vid_0b050&pid_1b7e&mi_03#x", "hid#vid_0b05&pid_1b7e&mi_030#x",
            "hid#vid_0b05&pid_1b7e#x", "textvid_0b05&pid_1b7e&mi_03#x",
        ):
            with self.subTest(path=path):
                self.assertIsNone(MODULE.target_identity(path))


class CapabilityAbiTests(unittest.TestCase):
    def test_nkro_usage_range_is_metadata_not_wire_identity(self):
        data = bytearray(72)
        struct.pack_into("<H", data, 0, 7)
        struct.pack_into("<H", data, 4, 2)
        data[12] = data[15] = 1
        struct.pack_into("<HH", data, 56, 0, 151)
        row = MODULE.decode_capability(bytes(data), "button")
        self.assertEqual((row["usage_page"], row["usage_min"], row["usage_max"]), (7, 0, 151))
        self.assertEqual(row["report_id"], 0)
        self.assertNotIn("physical_key_id", row)
        self.assertNotIn("report_count", row)  # button API-version-dependent field

    def test_nonrange_ignores_reserved_usage_max(self):
        data = bytearray(72)
        struct.pack_into("<H", data, 0, 0xFFC0)
        data[2] = 3
        struct.pack_into("<HH", data, 56, 2, 0xFFFF)
        row = MODULE.decode_capability(bytes(data), "button")
        self.assertEqual((row["report_id"], row["usage_min"], row["usage_max"]), (3, 2, 2))

    def test_vendor_value_abi(self):
        data = bytearray(72)
        struct.pack_into("<H", data, 0, 0xFF00)
        struct.pack_into("<HH", data, 18, 8, 64)
        struct.pack_into("<H", data, 56, 2)
        row = MODULE.decode_capability(bytes(data), "value")
        self.assertEqual((row["bit_size"], row["report_count"], row["usage_page"]), (8, 64, 0xFF00))

    def test_truncated_or_oversized_caps_rejected(self):
        for length in (0, 71, 73, 65536):
            with self.subTest(length=length), self.assertRaises(ValueError):
                MODULE.decode_capability(bytes(length), "button")

    def test_unknown_kind_rejected(self):
        with self.assertRaises(ValueError):
            MODULE.decode_capability(bytes(72), "input_report")


if __name__ == "__main__":
    unittest.main()
