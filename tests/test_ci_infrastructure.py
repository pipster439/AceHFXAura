"""Permanent orchestration/permission tests. No real HTTP, foreground or HID."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

def powershell(code):
    return subprocess.run(["pwsh", "-NoProfile", "-Command", code], cwd=ROOT, text=True,
                          encoding="utf-8", errors="replace", capture_output=True)

class CiInfrastructure(unittest.TestCase):
    def test_failure_retains_summary_log_and_dirty_source(self):
        watched = [ROOT / ".github/workflows/ci.yml", ROOT / "CMakeLists.txt", ROOT / "frontend/package-lock.json"]
        before = [hashlib.sha256(p.read_bytes()).hexdigest() for p in watched]
        with tempfile.TemporaryDirectory(prefix="aura-ci-test-") as temp:
            output = str(Path(temp) / "evidence").replace("'", "''")
            # Mock external guard command, exercise the real stage and failure path.
            result = powershell("function global:python { 'intentional mocked guard failure'; $global:LASTEXITCODE = 23 }; "
                                f"& ./tools/ci/run-ci.ps1 -OutputDirectory '{output}'")
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            summary = json.loads((Path(temp) / "evidence/ci-summary.json").read_text(encoding="utf-8-sig"))
            self.assertEqual(summary["schema_version"], 1)
            self.assertEqual(summary["overall"], "failed")
            self.assertEqual(summary["stages"][0]["exit_code"], 23)
            self.assertIn("intentional mocked", (Path(temp) / "evidence/guards.log").read_text(encoding="utf-8-sig"))
            self.assertTrue((Path(temp) / "evidence/step-summary.md").is_file())
        self.assertEqual(before, [hashlib.sha256(p.read_bytes()).hexdigest() for p in watched])

    def test_ci_rejects_hardware_before_network_or_output(self):
        with tempfile.TemporaryDirectory(prefix="aura-hardware-guard-") as temp:
            for marker in ("CI", "GITHUB_ACTIONS"):
                destination = str(Path(temp) / marker).replace("'", "''")
                result = powershell(f"$env:CI='false'; $env:GITHUB_ACTIONS='false'; $env:{marker}='true'; "
                    f"& ./tools/hardware/run-profile-automation-smoke.ps1 -AllowHardwareWrites -OutputDirectory '{destination}'")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("forbidden", result.stderr)
                self.assertFalse((Path(temp) / marker).exists())

    def test_sanitization_restore_comparison_and_target_contract(self):
        result = powershell(r'''
. ./tools/hardware/smoke-common.ps1
$d = @{runtime=@{selected_profile_id='A'; active_profile_id='A'; dirty=$false;document_revision=3;runtime_revision=2;mutation_revision=2};
 daemon=@{process_instance_id='instance';product_version='fixture';pid=999;path='SECRET'};
 automation=@{foreground_process='notepad.exe';debounce_pending=$false;debounce_ms=500;decision_sequence=1;
 resolved_profile_id='A';manual_hold=$false;manual_hold_foreground=$null;activation_attempt=1;activation_outcome='succeeded';activation_target='A';activation_state='succeeded';paths='SECRET'};
 m605=@{persistent_safety_quarantine=$false;health='Clean';session_generation=7;raw_hid='SECRET'} }
$e = Get-SmokeEvidence $d 'notepad.exe'
if (($e | ConvertTo-Json -Depth 20) -match 'SECRET|pid|raw_hid') { throw 'privacy failure' }
if (-not (Assert-SmokeTarget $e 'notepad.exe' 'A' 'instance' 7)) { throw 'target failure' }
$e.actual_foreground='powershell.exe'
if (Assert-SmokeTarget $e 'notepad.exe' 'A' 'instance' 7) { throw 'fake foreground accepted' }
if (-not (Test-SmokeConfigEqual @{enabled=$true;bindings=@()} @{bindings=@();enabled=$true})) { throw 'order sensitivity' }
if (Test-SmokeConfigEqual @{enabled=$true} @{enabled=$false}) { throw 'concurrent edit overwritten' }
try { Assert-SmokeTarget $e 'notepad.exe' 'A' 'other' 7; throw 'restart accepted' } catch {
 if ($_.Exception.Message -eq 'restart accepted') { throw }
}
Write-Output 'smoke helper contracts passed'
''')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_default_smoke_is_read_only_with_mocked_api(self):
        with tempfile.TemporaryDirectory(prefix="aura-readonly-") as temp:
            code = r'''
function global:Invoke-RestMethod {
 param($Uri,$TimeoutSec,$Method,$ContentType,$Body)
 if ($Method -or $Body) { throw 'Unexpected write' }
 return @{api_version=1; diagnostics=@{diagnostic_schema_version=1;
  daemon=@{process_instance_id='fixture';product_version='fixture'};
  runtime=@{selected_profile_id=$null;active_profile_id=$null;dirty=$true;document_revision=1;runtime_revision=1;mutation_revision=1};
  automation=@{foreground_process=$null;debounce_pending=$false;debounce_ms=500;decision_sequence=0;resolved_profile_id=$null;manual_hold=$false;manual_hold_foreground=$null;activation_attempt=0;activation_outcome=$null;activation_target=$null;activation_state='Idle'};
  m605=@{persistent_safety_quarantine=$false;health='Clean';session_generation=0}}}
}
function global:Start-Process { throw 'Unexpected foreground launch' }
'''
            code += f"& ./tools/hardware/run-profile-automation-smoke.ps1 -OutputDirectory '{temp.replace(chr(39), chr(39)*2)}'"
            result = powershell(code)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            summary = json.loads((Path(temp)/"hardware-smoke-summary.json").read_text(encoding="utf-8-sig"))
            self.assertEqual(summary["mode"], "ReadOnly")
            self.assertEqual(summary["restoration"], "not-required")
            self.assertEqual(summary["overall"], "passed")
            Path(temp + ".zip").unlink()

    def test_workflow_policy(self):
        yaml = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        for text in ("contents: read", "windows-latest", "tools/ci/run-ci.ps1"):
            self.assertIn(text, yaml)
        for text in ("pull_request_target", "package_release", "AllowHardwareWrites", "Invoke-Expression"):
            self.assertNotIn(text, yaml)

    def test_mocked_write_mode_restores_on_failure_and_retains_concurrent_edits(self):
        # All HTTP/configuration and foreground operations are in-memory fakes.
        # Deliberate launch failure exercises the real finally restoration path.
        for scenario in ("launch-failure", "timeout-after-commit", "concurrent-edit"):
            with self.subTest(scenario=scenario), tempfile.TemporaryDirectory(prefix="aura-restore-") as temp:
                code = r'''
$env:CI='false'; $env:GITHUB_ACTIONS='false'
$global:smokeConfig=@{schema_version=1;enabled=$false;fallback_profile_id=$null;bindings=@();extension=@{retained=42}}
$global:smokeRevision=10; $global:posts=0
$global:scenario='SCENARIO'
Add-Type 'public static class AuraSmokeForeground { public static string Basename() { return "notepad.exe"; } }'
function global:Add-Type { param($TypeDefinition) }
function global:Invoke-RestMethod {
 param($Uri,$TimeoutSec,$Method,$ContentType,$Body)
 if ($Uri.EndsWith('/diagnostics')) {
  return @{api_version=1;diagnostics=@{diagnostic_schema_version=1;
   daemon=@{process_instance_id='fixture';product_version='fixture'};
   runtime=@{selected_profile_id=$null;active_profile_id=$null;dirty=$true;document_revision=$global:smokeRevision;runtime_revision=1;mutation_revision=1};
   automation=@{foreground_process='notepad.exe';debounce_pending=$false;debounce_ms=500;decision_sequence=0;resolved_profile_id=$null;manual_hold=$false;manual_hold_foreground=$null;activation_attempt=0;activation_outcome=$null;activation_target=$null;activation_state='Idle'};
   m605=@{persistent_safety_quarantine=$false;health='Clean';session_generation=1}}}
 }
 if ($Method) {
  $request=$Body | ConvertFrom-Json -AsHashtable
  if ($request.expected_revision -ne $global:smokeRevision) { throw 'revision conflict' }
  $global:posts++; $global:smokeRevision++; $global:smokeConfig=$request.device_profile_automation
  if ($global:scenario -eq 'timeout-after-commit' -and $global:posts -eq 1) { throw 'mock transport timeout after commit' }
 }
 return @{automation_configuration_available=$true; document_revision=$global:smokeRevision;
  device_profile_automation=$global:smokeConfig;profiles=@(@{id='11111111-1111-4111-8111-111111111111'},@{id='22222222-2222-4222-8222-222222222222'})}
}
function global:Start-Process {
 if ($global:scenario -eq 'concurrent-edit') { $global:smokeConfig.extension.retained=99; $global:smokeRevision++ }
 throw 'controlled fake foreground launch failure'
}
& ./tools/hardware/run-profile-automation-smoke.ps1 -AllowHardwareWrites -ProfileA '11111111-1111-4111-8111-111111111111' -ProfileB '22222222-2222-4222-8222-222222222222' -OutputDirectory 'OUTPUT'
@{posts=$global:posts;config=$global:smokeConfig} | ConvertTo-Json -Depth 30 | Set-Content 'OUTPUT/fake-final.json'
'''
                code = code.replace("SCENARIO", scenario).replace("OUTPUT", temp.replace("'", "''").replace("\\", "/"))
                result = powershell(code)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                final = json.loads((Path(temp) / "fake-final.json").read_text(encoding="utf-8-sig"))
                summary = json.loads((Path(temp) / "hardware-smoke-summary.json").read_text(encoding="utf-8-sig"))
                self.assertEqual(summary["overall"], "failed")
                if scenario == "concurrent-edit":
                    self.assertEqual(summary["restoration"], "failed")
                    self.assertEqual(final["posts"], 1)
                    self.assertEqual(final["config"]["extension"]["retained"], 99)
                else:
                    self.assertEqual(summary["restoration"], "restored", result.stdout + result.stderr)
                    self.assertEqual(final["posts"], 2)
                    self.assertFalse(final["config"]["enabled"])
                    self.assertEqual(final["config"]["bindings"], [])
                    self.assertEqual(final["config"]["extension"]["retained"], 42)
                Path(temp + ".zip").unlink()

if __name__ == "__main__":
    unittest.main(verbosity=2)
