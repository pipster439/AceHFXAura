"""Launcher orchestration tests: fake processes/commands, never launch Aura or HID."""
import json
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools/dev/run-winui.ps1'


@unittest.skipUnless(os.name == 'nt', 'Windows PowerShell launcher')
class DevelopmentLauncherTests(unittest.TestCase):
    def run_launcher(self, setup='', arguments='-NoBuild -NoLaunch -IsolatedData'):
        # Native command mocks deliberately include a nonzero exit test. No fake PASS launch.
        command = r'''
$ErrorActionPreference = 'Stop'
function Get-Process { @() }
function Get-NetTCPConnection { @() }
function Test-Path { $true }
function Start-Process { throw 'Unexpected launch in software test' }
function cmake { throw 'Unexpected build in NoBuild test' }
function dotnet {
    $global:LASTEXITCODE = 0
    '{"Properties":{"TargetDir":"G:\\mock-output","AssemblyName":"Aura","WindowsPackageType":"None"}}'
}
'''
        env = os.environ.copy()
        env.pop('CI', None)
        env.pop('GITHUB_ACTIONS', None)
        return subprocess.run(['pwsh', '-NoProfile', '-Command', command + setup +
                               f"\n& '{SCRIPT}' {arguments}"], text=True,
                              capture_output=True, encoding='utf-8', env=env)

    def test_no_build_no_launch_uses_evaluated_output_and_isolated_data(self):
        result = self.run_launcher()
        self.assertEqual(result.returncode, 0, result.stderr)
        output = json.loads(result.stdout)
        self.assertFalse(output['launched'])
        self.assertIsNone(output['package_identity'])
        self.assertEqual(output['configuration'], 'Debug')
        self.assertTrue(output['data_root'].endswith('build\\dev-winui\\data'))
        self.assertEqual(output['executable'], 'G:\\mock-output\\Aura.exe')

    def test_changed_packaging_model_fails_closed(self):
        result = self.run_launcher('''
function dotnet {
 $global:LASTEXITCODE = 0
 '{"Properties":{"TargetDir":"G:\\\\mock","AssemblyName":"Aura","WindowsPackageType":"MSIX"}}'
}
''')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('packaging model changed', result.stderr)

    def test_external_process_is_not_closed_even_with_restart(self):
        result = self.run_launcher('''
function Get-Process { [pscustomobject]@{Id=-1;Path='external.exe';StartTime=[DateTime]::UtcNow} }
function Get-Content { '{"ui_pid":123,"executable":"other.exe"}' }
''', '-NoBuild -NoLaunch -Restart')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Existing external', result.stderr)

    def test_native_build_failure_is_not_hidden(self):
        result = self.run_launcher('function cmake { $global:LASTEXITCODE = 23 }', '-NoLaunch')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('exit 23', result.stderr)

    def test_restart_accepts_owned_process_with_roundtrip_timestamp(self):
        result = self.run_launcher(r'''
function Get-Content {
 '{"ui_pid":123,"executable":"owned.exe","ui_started_at_utc":"2026-10-01T10:00:00.1234567Z","daemon_binary":"daemon.exe","daemon_instance_id":null}'
}
function Get-Process {
 param($Name)
 if ($Name -eq 'Aura') {
  $p = [pscustomobject]@{Id=123;Path='owned.exe';StartTime=[DateTime]::Parse('2026-10-01T10:00:00.1234567Z').ToUniversalTime()}
  $p | Add-Member -MemberType ScriptMethod -Name CloseMainWindow -Value { $true }
  $p | Add-Member -MemberType ScriptMethod -Name WaitForExit -Value { $true }
  $p
 }
}
''', '-NoBuild -NoLaunch -Restart')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_no_restart_never_closes_owned_process(self):
        result = self.run_launcher(r'''
function Get-Content {
 '{"ui_pid":123,"executable":"owned.exe","ui_started_at_utc":"2026-10-01T10:00:00Z","daemon_binary":"daemon.exe","daemon_instance_id":null}'
}
function Get-Process {
 param($Name)
 if ($Name -eq 'Aura') { [pscustomobject]@{Id=123;Path='owned.exe';StartTime=[DateTime]::Parse('2026-10-01T10:00:00Z').ToUniversalTime()} }
}
''')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('already running', result.stderr)

    def test_ci_cannot_launch(self):
        result = self.run_launcher("$env:CI='true'", '-NoBuild -IsolatedData')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('local-only', result.stderr)


if __name__ == '__main__':
    unittest.main()
