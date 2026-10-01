"""Permanent source/ownership/fixture guards; no daemon startup or hardware access."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]

def require(condition, message):
    if not condition:
        raise SystemExit(message)

tracked = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT).decode().split("\0")
require("config.json" not in tracked, "User config must not be tracked")
require(not any(re.match(r"plugins/src/effect_(studio|test|adv)_.*\.cpp$", p) for p in tracked),
        "Generated plugin sources must not be tracked")

runtime_tests = (ROOT / "tests/test_device_profile_runtime.cpp").read_text(encoding="utf-8")
cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
for include in ("test_legacy_rt_migration.inc", "test_phase4b_runtime.inc"):
    require(f'#include "{include}"' in runtime_tests, f"Permanent fixture not included: {include}")
    fixture = (ROOT / "tests" / include).read_text(encoding="utf-8")
    for function in re.findall(r"bool (Test\w+)\(", fixture):
        require(function in runtime_tests, f"Fixture function not registered: {function}")
require("add_test(NAME device_profile_runtime COMMAND test_device_profile_runtime)" in cmake,
        "Daemon Profile permanent suite must remain in CTest")
runtime = (ROOT / "src/config/device_profile_runtime.cpp").read_text(encoding="utf-8")
require("Legacy global RT settings cannot be safely applied" in runtime, "Legacy planner safety guard missing")
require("RejectNewLegacyRt(next, document_)" in runtime, "Legacy writer safety guard missing")
require("NeutralV1AllAuditedKeys" in runtime, "Migration contract missing")
client = (ROOT / "winui/Services/ProfileControlClient.cs").read_text(encoding="utf-8")
require("HttpClient" in client and "/api/device-profiles" in client, "Profile API client boundary missing")
for file in (ROOT / "winui").rglob("*.cs"):
    if any(part in {"bin", "obj"} for part in file.parts):
        continue
    code = file.read_text(encoding="utf-8-sig")
    require("FileProfileStore" not in code and "new ProfileManager(" not in code,
            f"Second live Profile writer/coordinator in {file.relative_to(ROOT)}")
print("Repository/source/migration ownership guards passed (software only).")
