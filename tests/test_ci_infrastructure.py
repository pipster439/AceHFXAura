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
    def test_selector_dedup_uses_decision_identity_not_noop_attempts(self):
        result = powershell(r'''
. ./tools/hardware/smoke-common.ps1
$first=@{decision_sequence=7;configuration_revision=3;manual_action_sequence=0;
 foreground_observation_sequence=8;resolved_profile_id='A';activation_attempt=1;
 hardware_slot=@{selector_count=10}}
$next=$first.Clone();$next.activation_attempt=2
Assert-SmokeStableDecision $first $next -HardwareSlot
$next.hardware_slot=@{selector_count=11}
try { Assert-SmokeStableDecision $first $next -HardwareSlot; throw 'duplicate allowed' }
catch { if($_.Exception.Message -eq 'duplicate allowed'){throw} }
$next.hardware_slot=@{selector_count=10};$next.decision_sequence=8
try { Assert-SmokeStableDecision $first $next -HardwareSlot; throw 'new decision allowed' }
catch { if($_.Exception.Message -eq 'new decision allowed'){throw} }
Write-Output 'selector decision identity and duplicate rejection passed'
''')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
    def test_hardware_slot_smoke_refuses_ci_before_any_io(self):
        for variable in ('CI', 'GITHUB_ACTIONS'):
            result = powershell("$env:CI='false'; $env:GITHUB_ACTIONS='false'; "
                + f"$env:{variable}='true'; "
                + "function global:Invoke-RestMethod { throw 'unexpected HTTP' }; "
                + "& ./tools/hardware/run-hardware-slot-smoke.ps1 -AllowHardwareWrites")
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn('unexpected HTTP', result.stdout + result.stderr)


    def test_hardware_slot_wrapper_forwards_only_existing_slot_profiles(self):
        with tempfile.TemporaryDirectory(prefix="slot-wrapper-") as temp:
            root = Path(temp)
            hardware = root / "tools/hardware"; hardware.mkdir(parents=True)
            ci = root / "tools/ci"; ci.mkdir()
            for name in ("run-hardware-slot-smoke.ps1", "smoke-common.ps1"):
                (hardware / name).write_bytes((ROOT / "tools/hardware" / name).read_bytes())
            (ci / "ci-common.ps1").write_bytes((ROOT / "tools/ci/ci-common.ps1").read_bytes())
            (hardware / "run-profile-automation-smoke.ps1").write_text(r'''
param($DaemonUrl,$StageTimeoutSeconds,$ProfileA,$ProfileB,[switch]$AllowHardwareWrites,
      $ExpectedHardwareSlotA,$ExpectedHardwareSlotB,$ProgramA,$ProgramB,$OutputDirectory)
@{a=$ProfileA;b=$ProfileB;writes=[bool]$AllowHardwareWrites;slot_a=$ExpectedHardwareSlotA;
 slot_b=$ExpectedHardwareSlotB;program_a=$ProgramA;program_b=$ProgramB} | ConvertTo-Json -Compress
''', encoding="utf-8")
            script = str(hardware / "run-hardware-slot-smoke.ps1").replace("'", "''")
            result = powershell(r'''
$env:CI='false';$env:GITHUB_ACTIONS='false'
function global:Invoke-RestMethod {
 return [pscustomobject]@{profiles=@(
  [pscustomobject]@{id='old';name='old host profile'},
  [pscustomobject]@{id='aaaa';name='slot5';activation_backend='hardware_slot';hardware_slot=5},
  [pscustomobject]@{id='bbbb';name='slot1';activation_backend='hardware_slot';hardware_slot=1})}
}
''' + f"& '{script}' -AllowHardwareWrites")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            data = json.loads(result.stdout)
            self.assertEqual(data, dict(a="aaaa", b="bbbb", writes=True, slot_a=5, slot_b=1,
                                       program_a="notepad.exe", program_b="charmap.exe"))

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
Add-Type 'public static class AuraSmokeForeground { public static string Current = "notepad.exe"; public static string Basename() { return Current; } }'
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
  device_profile_automation=$global:smokeConfig;profiles=@(@{id='11111111-1111-4111-8111-111111111111';name='Desktop'},@{id='22222222-2222-4222-8222-222222222222';name='Gaming'})}
}
function global:Start-Process {
 param($FilePath)
 if ($FilePath -match 'charmap') { [AuraSmokeForeground]::Current = 'charmap.exe' } else { [AuraSmokeForeground]::Current = 'notepad.exe' }
 if ($global:posts -gt 0) {
  if ($global:scenario -eq 'concurrent-edit') { $global:smokeConfig.extension.retained=99; $global:smokeRevision++ }
  throw 'controlled fake foreground stage failure'
 }
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

    def test_smoke_profile_and_program_resolution_unit_contracts(self):
        result = powershell(r'''
. ./tools/hardware/smoke-common.ps1
$profiles = @(
    @{ id = '11111111-1111-4111-8111-111111111111'; name = 'Desktop' },
    @{ id = '22222222-2222-4222-8222-222222222222'; name = 'Gaming' },
    @{ id = '33333333-3333-4333-8333-333333333333'; name = 'Typing' },
    @{ id = '44444444-4444-4444-8444-444444444444'; name = 'dup-name' },
    @{ id = '55555555-5555-4555-8555-555555555555'; name = 'DUP-NAME' }
)

# 1. GUID input -> correct resolution
$r1 = Resolve-SmokeProfile '11111111-1111-4111-8111-111111111111' 'ProfileA' $profiles
if ($r1.resolved_id -ne '11111111-1111-4111-8111-111111111111' -or $r1.resolved_name -ne 'Desktop') { throw 'Case 1 failed' }

# 2. exact Profile name -> GUID
$r2 = Resolve-SmokeProfile 'Desktop' 'ProfileA' $profiles
if ($r2.resolved_id -ne '11111111-1111-4111-8111-111111111111' -or $r2.resolved_name -ne 'Desktop') { throw 'Case 2 failed' }

# 3. case-insensitive name -> GUID
$r3 = Resolve-SmokeProfile 'desktop' 'ProfileA' $profiles
if ($r3.resolved_id -ne '11111111-1111-4111-8111-111111111111' -or $r3.resolved_name -ne 'Desktop') { throw 'Case 3 failed' }
$r3b = Resolve-SmokeProfile 'GAMING' 'ProfileB' $profiles
if ($r3b.resolved_id -ne '22222222-2222-4222-8222-222222222222' -or $r3b.resolved_name -ne 'Gaming') { throw 'Case 3b failed' }

# 4. unknown name -> fail with available profiles list
try { Resolve-SmokeProfile 'NonExistent' 'ProfileA' $profiles; throw 'Case 4 expected failure' } catch {
    if ($_.Exception.Message -notmatch "Profile 'NonExistent' was not found" -or
        $_.Exception.Message -notmatch "Available profiles:" -or
        $_.Exception.Message -notmatch "Desktop") { throw "Case 4 message mismatch: $($_.Exception.Message)" }
}

# 5. duplicate case-insensitive names -> fail
try { Resolve-SmokeProfile 'dup-name' 'ProfileA' $profiles; throw 'Case 5 expected failure' } catch {
    if ($_.Exception.Message -notmatch "Profile name 'dup-name' is ambiguous" -or
        $_.Exception.Message -notmatch "44444444" -or
        $_.Exception.Message -notmatch "55555555") { throw "Case 5 message mismatch: $($_.Exception.Message)" }
}

# 6. nonexistent GUID -> fail
try { Resolve-SmokeProfile '00000000-0000-0000-0000-000000000000' 'ProfileA' $profiles; throw 'Case 6 expected failure' } catch {
    if ($_.Exception.Message -notmatch "Profile GUID '00000000-0000-0000-0000-000000000000' was not found") { throw "Case 6 message mismatch: $($_.Exception.Message)" }
}

# 9. missing Program B resolution
try { Resolve-SmokeProgram 'non_existent_program_xyz.exe' 'ProgramB'; throw 'Case 9 expected failure' } catch {
    if ($_.Exception.Message -notmatch "Test program 'non_existent_program_xyz.exe' could not be found") { throw "Case 9 message mismatch: $($_.Exception.Message)" }
}

# 12. Program resolution for defaults
$progA = Resolve-SmokeProgram 'notepad.exe' 'ProgramA'
$progB = Resolve-SmokeProgram 'charmap.exe' 'ProgramB'
if (-not $progA.EndsWith('notepad.exe', [StringComparison]::OrdinalIgnoreCase) -or
    -not $progB.EndsWith('charmap.exe', [StringComparison]::OrdinalIgnoreCase)) { throw 'Case 12 default program resolution failed' }

Write-Output 'resolution unit contracts passed'
''')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_smoke_profile_automation_execution_contracts(self):
        # 12. Verify default parameters on script
        param_check = powershell(r'''
$ast = [System.Management.Automation.Language.Parser]::ParseFile('tools/hardware/run-profile-automation-smoke.ps1', [ref]$null, [ref]$null)
$params = @{}
foreach ($p in $ast.ParamBlock.Parameters) {
    if ($p.DefaultValue) { $params[$p.Name.VariablePath.UserPath] = $p.DefaultValue.Value }
}
if ($params['ProgramA'] -ne 'notepad.exe' -or $params['ProgramB'] -ne 'charmap.exe') {
    throw "Default programs mismatch: $($params['ProgramA']), $($params['ProgramB'])"
}
Write-Output 'default parameters verified'
''')
        self.assertEqual(param_check.returncode, 0, param_check.stdout + param_check.stderr)

        # Failure before mutation test harness:
        # Ensures no mutation POST occurs and configuration remains untouched.
        fail_template = r'''
$env:CI='false'; $env:GITHUB_ACTIONS='false'
$global:smokeConfig=@{schema_version=1;enabled=$false;fallback_profile_id=$null;bindings=@()}
$global:smokeRevision=1; $global:posts=0
Add-Type 'public static class AuraSmokeForeground { public static string Current = "notepad.exe"; public static string Basename() { return Current; } }'
function global:Add-Type { param($TypeDefinition) }
function global:Invoke-RestMethod {
 param($Uri,$TimeoutSec,$Method,$ContentType,$Body)
 if ($Uri.EndsWith('/diagnostics')) {
  return @{api_version=1;diagnostics=@{diagnostic_schema_version=1;
   daemon=@{process_instance_id='fixture';product_version='fixture'};
   runtime=@{selected_profile_id=$null;active_profile_id=$null;dirty=$false;document_revision=1;runtime_revision=1;mutation_revision=1};
   automation=@{foreground_process=$null;debounce_pending=$false;debounce_ms=500;decision_sequence=0;resolved_profile_id=$null;manual_hold=$false;manual_hold_foreground=$null;activation_attempt=0;activation_outcome=$null;activation_target=$null;activation_state='Idle'};
   m605=@{persistent_safety_quarantine=$false;health='Clean';session_generation=1}}}
 }
 if ($Method) {
  $global:posts++
  throw 'Unexpected mutation POST in pre-mutation failure scenario'
 }
 return @{automation_configuration_available=$true; document_revision=$global:smokeRevision;
  device_profile_automation=$global:smokeConfig;profiles=@(
   @{id='11111111-1111-4111-8111-111111111111';name='Desktop'},
   @{id='22222222-2222-4222-8222-222222222222';name='Gaming'},
   @{id='33333333-3333-4333-8333-333333333333';name='Gaming'}
  )}
}
function global:Start-Process {
 param($FilePath)
 if ($FilePath -match 'charmap') { [AuraSmokeForeground]::Current = 'charmap.exe' } else { [AuraSmokeForeground]::Current = 'notepad.exe' }
}
& ./tools/hardware/run-profile-automation-smoke.ps1 -AllowHardwareWrites ARGS -OutputDirectory 'OUTPUT'
@{posts=$global:posts} | ConvertTo-Json | Set-Content 'OUTPUT/posts.json'
'''
        # 7. A/B resolve same GUID -> fail before mutation
        with tempfile.TemporaryDirectory(prefix="aura-sameguid-") as temp:
            code = fail_template.replace("ARGS", "-ProfileA 'Desktop' -ProfileB 'Desktop'").replace("OUTPUT", temp.replace("'", "''").replace("\\", "/"))
            res = powershell(code)
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            summary = json.loads((Path(temp) / "hardware-smoke-summary.json").read_text(encoding="utf-8-sig"))
            posts = json.loads((Path(temp) / "posts.json").read_text(encoding="utf-8-sig"))
            self.assertEqual(summary["overall"], "failed")
            self.assertEqual(posts["posts"], 0)
            self.assertEqual(summary["restoration"], "not-required")
            Path(temp + ".zip").unlink()

        # 8. Program A/B same basename -> fail before mutation
        with tempfile.TemporaryDirectory(prefix="aura-sameprog-") as temp:
            code = fail_template.replace("ARGS", "-ProfileA 'Desktop' -ProfileB 'Gaming' -ProgramA notepad.exe -ProgramB notepad.exe").replace("OUTPUT", temp.replace("'", "''").replace("\\", "/"))
            res = powershell(code)
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            summary = json.loads((Path(temp) / "hardware-smoke-summary.json").read_text(encoding="utf-8-sig"))
            posts = json.loads((Path(temp) / "posts.json").read_text(encoding="utf-8-sig"))
            self.assertEqual(summary["overall"], "failed")
            self.assertEqual(posts["posts"], 0)
            self.assertEqual(summary["restoration"], "not-required")
            Path(temp + ".zip").unlink()

        # 9. missing Program B -> fail before mutation
        with tempfile.TemporaryDirectory(prefix="aura-missingprog-") as temp:
            code = fail_template.replace("ARGS", "-ProfileA 'Desktop' -ProfileB 'Gaming' -ProgramB non_existent_file_xyz.exe").replace("OUTPUT", temp.replace("'", "''").replace("\\", "/"))
            res = powershell(code)
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            summary = json.loads((Path(temp) / "hardware-smoke-summary.json").read_text(encoding="utf-8-sig"))
            posts = json.loads((Path(temp) / "posts.json").read_text(encoding="utf-8-sig"))
            self.assertEqual(summary["overall"], "failed")
            self.assertEqual(posts["posts"], 0)
            self.assertEqual(summary["restoration"], "not-required")
            Path(temp + ".zip").unlink()

        # 4. Unknown Profile name -> fail before mutation
        with tempfile.TemporaryDirectory(prefix="aura-unknownprofile-") as temp:
            code = fail_template.replace("ARGS", "-ProfileA 'NonExistent' -ProfileB 'Gaming'").replace("OUTPUT", temp.replace("'", "''").replace("\\", "/"))
            res = powershell(code)
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            summary = json.loads((Path(temp) / "hardware-smoke-summary.json").read_text(encoding="utf-8-sig"))
            posts = json.loads((Path(temp) / "posts.json").read_text(encoding="utf-8-sig"))
            self.assertEqual(summary["overall"], "failed")
            self.assertEqual(posts["posts"], 0)
            self.assertEqual(summary["restoration"], "not-required")
            Path(temp + ".zip").unlink()

        # 5. Duplicate Profile names -> fail before mutation
        with tempfile.TemporaryDirectory(prefix="aura-dupnames-") as temp:
            code = fail_template.replace("ARGS", "-ProfileA 'Gaming' -ProfileB 'Desktop'").replace("OUTPUT", temp.replace("'", "''").replace("\\", "/"))
            res = powershell(code)
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            summary = json.loads((Path(temp) / "hardware-smoke-summary.json").read_text(encoding="utf-8-sig"))
            posts = json.loads((Path(temp) / "posts.json").read_text(encoding="utf-8-sig"))
            self.assertEqual(summary["overall"], "failed")
            self.assertEqual(posts["posts"], 0)
            self.assertEqual(summary["restoration"], "not-required")
            Path(temp + ".zip").unlink()

        # 13. Summary stores requested name + resolved GUID without full paths
        with tempfile.TemporaryDirectory(prefix="aura-summaryevidence-") as temp:
            success_code = r'''
$env:CI='false'; $env:GITHUB_ACTIONS='false'
$global:smokeConfig=@{schema_version=1;enabled=$false;fallback_profile_id=$null;bindings=@()}
$global:smokeRevision=1; $global:posts=0
Add-Type 'public static class AuraSmokeForeground { public static string Current = "notepad.exe"; public static string Basename() { return Current; } }'
function global:Add-Type { param($TypeDefinition) }
function global:Invoke-RestMethod {
 param($Uri,$TimeoutSec,$Method,$ContentType,$Body)
 if ($Uri.EndsWith('/diagnostics')) {
  $fg = [AuraSmokeForeground]::Basename()
  $target = if ($fg -eq 'charmap.exe') { '22222222-2222-4222-8222-222222222222' } else { '11111111-1111-4111-8111-111111111111' }
  return @{api_version=1;diagnostics=@{diagnostic_schema_version=1;
   daemon=@{process_instance_id='fixture';product_version='fixture'};
   runtime=@{selected_profile_id=$target;active_profile_id=$target;dirty=$false;document_revision=1;runtime_revision=1;mutation_revision=1};
   automation=@{foreground_process=$fg;debounce_pending=$false;debounce_ms=500;decision_sequence=1;resolved_profile_id=$target;manual_hold=$false;manual_hold_foreground=$null;activation_attempt=1;activation_outcome='succeeded';activation_target=$target;activation_state='Idle'};
   m605=@{persistent_safety_quarantine=$false;health='Clean';session_generation=1}}}
 }
 if ($Method) {
  $global:posts++
  $request=$Body | ConvertFrom-Json -AsHashtable
  $global:smokeConfig=$request.device_profile_automation
  $global:smokeRevision++
 }
 return @{automation_configuration_available=$true; document_revision=$global:smokeRevision;
  device_profile_automation=$global:smokeConfig;profiles=@(
   @{id='11111111-1111-4111-8111-111111111111';name='Desktop'},
   @{id='22222222-2222-4222-8222-222222222222';name='Gaming'}
  )}
}
function global:Start-Process {
 param($FilePath)
 if ($FilePath -match 'charmap') { [AuraSmokeForeground]::Current = 'charmap.exe' } else { [AuraSmokeForeground]::Current = 'notepad.exe' }
}
& ./tools/hardware/run-profile-automation-smoke.ps1 -AllowHardwareWrites -ProfileA 'Desktop' -ProfileB 'Gaming' -StageTimeoutSeconds 5 -OutputDirectory 'OUTPUT'
'''
            code = success_code.replace("OUTPUT", temp.replace("'", "''").replace("\\", "/"))
            res = powershell(code)
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            raw_summary = (Path(temp) / "hardware-smoke-summary.json").read_text(encoding="utf-8-sig")
            summary = json.loads(raw_summary)
            self.assertEqual(summary["profile_a"]["requested"], "Desktop")
            self.assertEqual(summary["profile_a"]["resolved_name"], "Desktop")
            self.assertEqual(summary["profile_a"]["resolved_id"], "11111111-1111-4111-8111-111111111111")
            self.assertEqual(summary["profile_b"]["requested"], "Gaming")
            self.assertEqual(summary["profile_b"]["resolved_name"], "Gaming")
            self.assertEqual(summary["profile_b"]["resolved_id"], "22222222-2222-4222-8222-222222222222")
            self.assertEqual(summary["program_a"]["requested"], "notepad.exe")
            self.assertEqual(summary["program_a"]["observed_foreground"], "notepad.exe")
            self.assertEqual(summary["program_b"]["requested"], "charmap.exe")
            self.assertEqual(summary["program_b"]["observed_foreground"], "charmap.exe")
            # Verify no full filesystem paths leaked into summary
            self.assertNotIn(":\\", raw_summary)
            self.assertNotIn("/system32", raw_summary.lower())
            Path(temp + ".zip").unlink()

if __name__ == "__main__":
    unittest.main(verbosity=2)
