"""Opt-in flag with CI markers: refusal must precede test/HID initialization."""
import os
import subprocess
import sys
for marker in ("CI", "GITHUB_ACTIONS"):
    env = dict(os.environ, CI="false", GITHUB_ACTIONS="false")
    env[marker] = "true"
    result = subprocess.run([sys.argv[1], "--live-hardware"], env=env, capture_output=True, text=True)
    assert result.returncode == 2, (marker, result.returncode, result.stdout, result.stderr)
    assert "Hardware acceptance is forbidden in CI." in result.stderr
    assert "Live Physical Hardware Test" not in result.stdout
print("CI and GITHUB_ACTIONS both reject physical acceptance before initialization.")
