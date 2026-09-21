"""Verify frozen evidence and future conversion expectations; never builds a DLL."""
import hashlib
import json
from pathlib import Path


def main():
    root = Path(__file__).parent / "fixtures" / "automation_v2_stage0"
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    assert manifest["baseline_commit"] == "7b2e1c7887cf9c6f7e5a9601530b19b00282ed24"
    assert manifest["captured_before_stage1_build"]
    for item in manifest["binaries"] + manifest["configs"]:
        data = (root / item["file"]).read_bytes()
        assert hashlib.sha256(data).hexdigest() == item["sha256"], item["file"]
        if item["file"].endswith(".json"):
            json.loads(data)
        if item["classification"] == "synthetic_byte_mutation_not_historical":
            source = (root / item["source"]).read_bytes()
            assert hashlib.sha256(source).hexdigest() == item["source_sha256"]
            expected = bytearray(source)
            offset = item["patch_offset"]
            before = bytes.fromhex(item["patch_before_hex"])
            after = bytes.fromhex(item["patch_after_hex"])
            assert source[offset:offset + len(before)] == before
            expected[offset:offset + len(before)] = after
            assert bytes(expected) == data, item["file"]
        print("Verified frozen SHA-256:", item["file"])

    # These are schema/fixture checks, not a claim that future Promote is implemented.
    expectations = json.loads((root / "promotion_expectations.json").read_text(encoding="utf-8"))
    for case in expectations["cases"]:
        if "expected_rule_fields" in case:
            old, new = case["input"], case["expected_rule_fields"]
            assert new["dnd"] is old["suppress_web_ui"]
            assert "dnd" not in new["action"]
            assert new["action"]["profile"] == old["profile"]
            assert new["when"]["condition"]["value"] == old["process"]
    assert expectations["cases"][-1]["expected"] == "blocked_unknown_legacy_field"
    print("PASS: frozen evidence integrity and future DND fixture consistency")


if __name__ == "__main__":
    main()
