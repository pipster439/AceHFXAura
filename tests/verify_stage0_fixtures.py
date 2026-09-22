"""Verify protected frozen plugin ABI evidence; never builds a DLL."""
import hashlib
import json
from pathlib import Path


def main():
    root = Path(__file__).parent / "fixtures" / "automation_v2_stage0"
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    assert manifest["baseline_commit"] == "7b2e1c7887cf9c6f7e5a9601530b19b00282ed24"
    assert manifest["captured_before_stage1_build"]
    for item in manifest["binaries"]:
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

    print("PASS: frozen plugin ABI evidence integrity")


if __name__ == "__main__":
    main()
