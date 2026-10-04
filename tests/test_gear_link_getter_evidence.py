import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/research"))
from gear_link_getter_evidence import classify_getter, safety_findings, validate_device_response


class GetterEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.fixture = json.loads((ROOT / "tests/fixtures/gear_link/dks_timeout_slot5.json")
                                  .read_text(encoding="utf8"))
        self.rows = self.fixture["timeline"]
        self.case = self.fixture["sdk_case"]

    def test_captured_dks_timeout_is_not_readback(self):
        result = classify_getter(self.rows, self.case, (0x25, 2))
        self.assertTrue(result["authority_passed"])
        self.assertEqual(result["outcome"], "NO_MATCHING_RESPONSE")
        self.assertEqual(result["request_frames"], [97, 100, 105, 110])
        self.assertEqual(result["response_frames"], [])
        self.assertFalse(result["field_meaning_verified"])

    def test_source_only_sdk_result_cannot_prove_standard(self):
        self.assertEqual(self.case["result"], {"source": 1026})
        result = classify_getter(self.rows, self.case, (0x25, 2))
        self.assertNotEqual(result["outcome"], "RESPONSE_CAPTURED")

    def test_wrong_pre_bank_invalidates_case(self):
        self.case["pre"][10] = 1
        self.assertEqual(classify_getter(self.rows, self.case, (0x25, 2))["outcome"],
                         "INVALID_AUTHORITY")

    def test_wrong_post_bank_invalidates_case(self):
        self.case["post"][10] = 1
        self.assertEqual(classify_getter(self.rows, self.case, (0x25, 2))["outcome"],
                         "INVALID_AUTHORITY")

    def test_missing_authority_is_not_assumed(self):
        del self.case["post"]
        self.assertFalse(classify_getter(self.rows, self.case, (0x25, 2))["authority_passed"])

    def test_other_query_response_is_not_dks_response(self):
        self.assertEqual(classify_getter(self.rows, self.case, (0x25, 2))["response_frames"], [])
        self.assertTrue(any(r["direction"] == "IN" and r["payload_hex"].startswith("12 12")
                            for r in self.rows))

    def test_authorized_profile_selection_does_not_authorize_setter(self):
        findings = safety_findings(self.rows, (49,))
        self.assertEqual(findings["unauthorized_selection_frames"], [])
        self.assertEqual(findings["setter_frames"], [57, 65, 71, 78])
        self.assertTrue(findings["stop_required"])

    def test_unapproved_selector_still_stops(self):
        self.assertEqual(safety_findings(self.rows)["unauthorized_selection_frames"], [49])

    def test_getter_interval_alone_has_no_mutation(self):
        rows = [r for r in self.rows if r["frame"] >= 85]
        self.assertFalse(safety_findings(rows)["stop_required"])

    def test_recovery_companion_is_outside_getter_window(self):
        findings = safety_findings(self.rows, case_start=self.case["start"], case_end=self.case["end"])
        self.assertFalse(findings["stop_required"])
        self.assertEqual(classify_getter(self.rows, self.case, (0x25, 2))["outcome"],
                         "NO_MATCHING_RESPONSE")

    def test_same_polling_setter_inside_getter_window_stops(self):
        import copy
        setter = copy.deepcopy(next(r for r in self.rows if r["frame"] == 57))
        setter["timestamp_utc"] = self.case["query_start"]
        self.rows.append(setter)
        self.assertEqual(classify_getter(self.rows, self.case, (0x25, 2))["outcome"],
                         "READ_ONLY_GATE_FAIL")

    def test_timeout_sentinel_never_authorizes_decoder(self):
        self.assertFalse(validate_device_response([255, 170, 0, 3, 3, 3], (0x25, 2)))
        self.assertFalse(classify_getter(self.rows, self.case, (0x25, 2))["readback_valid"])

    def test_write_error_sentinel_never_authorizes_decoder(self):
        self.assertFalse(validate_device_response([255, 170, 0, 255, 255, 255], (0x25, 2)))

    def test_truncated_response_never_authorizes_decoder(self):
        self.assertFalse(validate_device_response([0x25, 2], (0x25, 2)))

    def test_unrelated_basicinfo_is_not_parameter_response(self):
        self.assertFalse(validate_device_response(self.case["pre"], (0x25, 2)))

    def test_captured_ap_dz_fallbacks_are_not_readback(self):
        fixture = json.loads((ROOT / "tests/fixtures/gear_link/ap_dz_no_response_slot5.json")
                             .read_text(encoding="utf8"))
        for case in fixture["cases"]:
            with self.subTest(case=case["case"]):
                result = classify_getter(case["timeline"], case["sdk_case"], case["opcode"])
                self.assertTrue(result["authority_passed"])
                self.assertEqual(len(result["request_frames"]), 4)
                self.assertEqual(result["outcome"], "NO_MATCHING_RESPONSE")
                self.assertFalse(result["readback_valid"])
                self.assertFalse(result["read_only_gate"]["stop_required"])

    def test_plausible_sdk_values_without_in_still_rejected(self):
        self.case["result"] = {"enabled": True, "press": 5, "release": 15}
        self.assertFalse(classify_getter(self.rows, self.case, (0x25, 2))["readback_valid"])

    def test_captured_runtime_drift_requires_stop_despite_ui_slot_five(self):
        fixture = json.loads((ROOT / "tests/fixtures/gear_link/rt_baseline_bank_drift.json")
                             .read_text(encoding="utf8"))
        findings = safety_findings(fixture["timeline"])
        self.assertEqual(findings["unauthorized_selection_frames"], [29, 31, 49, 51])
        self.assertTrue(findings["stop_required"])
        self.assertEqual(fixture["final_ui_slot"], 5)
        self.assertEqual(fixture["final_basicinfo_slot"], 1)

    def test_captured_rt01_submission_does_not_prove_test_bank_baseline(self):
        fixture = json.loads((ROOT / "tests/fixtures/gear_link/rt_baseline_bank_drift.json")
                             .read_text(encoding="utf8"))
        rows = fixture["timeline"]
        rt01 = next(r for r in rows if r["frame"] == 39)
        prior_status = next(r for r in rows if r["frame"] == 38)
        self.assertEqual(bytes.fromhex(rt01["payload_hex"])[6], 1)
        self.assertEqual(bytes.fromhex(prior_status["payload_hex"])[10], 1)
        self.assertLess(prior_status["frame"], rt01["frame"])
        self.assertFalse(fixture["slot5_baseline_valid"])


if __name__ == "__main__":
    unittest.main()
