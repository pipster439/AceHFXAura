"""Real deletion only inside isolated Git fixtures. Never touches release evidence."""
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def ps(code, cwd=ROOT):
    return subprocess.run(['pwsh', '-NoProfile', '-Command', code], cwd=cwd,
                          capture_output=True, text=True, encoding='utf-8', errors='replace')


def quote(value):
    return "'" + str(value).replace("'", "''") + "'"


def reviewed_header_fixture(review_newline=b'\n'):
    # Reproduce the exact reviewed proof inputs: CRLF ABI and LF review text.
    # Checkout autocrlf must not change this positive fixture's identity. The
    # different native-input hash remains protected in the negative case below.
    template = (ROOT / 'tools/AuraOwnershipExperiment/ReviewedEvidence.h.in').read_bytes()
    for token, source, newline in (
            ('AURA_ABI_HASH', 'src/AuraWorker/CanonicalAuraAbi.h', b'\r\n'),
            ('AURA_REVIEW_HASH', 'docs/research/aura-ownership-callsite-analysis.md', review_newline)):
        data = (ROOT / source).read_bytes().replace(b'\r\n', b'\n').replace(b'\n', newline)
        template = template.replace(f'@{token}@'.encode(), hashlib.sha256(data).hexdigest().encode())
    return template.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')


class CleanupFixture(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='aura-cleanup-fixture-')
        self.repo = Path(self.temp.name) / 'repo'
        self.repo.mkdir()
        subprocess.run(['git', 'init', '-q', str(self.repo)], check=True)
        self.script = ROOT / 'tools/dev/cleanup-generated.ps1'

    def tearDown(self):
        self.temp.cleanup()

    def run_cleanup(self, extra='', cwd=None):
        output = Path(self.temp.name) / 'decision.json'
        result = ps(f'& {quote(self.script)} -RepoRoot {quote(self.repo)} -OutputJson {quote(output)} {extra}', cwd=cwd or ROOT)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return json.loads(output.read_text(encoding='utf-8-sig'))

    def run_dir(self, name, status='passed', age=10, category='EPHEMERAL', marked=True):
        base = 'build/ci' if category != 'SHORT_RETENTION' else 'artifacts/ci/runs'
        path = self.repo / base / name
        path.mkdir(parents=True, exist_ok=True)
        payload = path / 'fixture.obj'
        payload.write_bytes(b'x' * 4096)
        when = dt.datetime.now(dt.timezone.utc) - dt.timedelta(days=age)
        os.utime(payload, (when.timestamp(), when.timestamp()))
        if marked:
            marker = path / '.aura-generated.json'
            marker.write_text(json.dumps(dict(schema_version=1, owner='Aura.local-ci', repo=str(self.repo),
                path=str(path), category=category, status=status, created_utc=when.isoformat(),
                completed_utc=when.isoformat() if status != 'running' else None, pid=0)), encoding='utf-8')
            os.utime(marker, (when.timestamp(), when.timestamp()))
        return path

    def test_dryrun_age_count_failed_active_unknown_and_release(self):
        old = self.run_dir('oldest', age=20)
        keep = self.run_dir('keep', age=10)
        recent = self.run_dir('recent', age=.1)
        failed = self.run_dir('failed', status='failed', age=30)
        active = self.run_dir('active', status='running')
        unknown = self.run_dir('unknown', marked=False)
        release = self.run_dir('release', category='RELEASE / AUDIT')
        result = self.run_cleanup('-DryRun -KeepLast 1')
        actions = {Path(d['path']).name: d['action'] for d in result['decisions']}
        self.assertEqual(actions['oldest'], 'would-delete')
        self.assertEqual(actions['keep'], 'would-delete')
        self.assertEqual(actions['recent'], 'keep')
        for name in ('failed', 'active', 'unknown', 'release'):
            self.assertEqual(actions[name], 'skip')
        self.assertTrue(all(p.exists() for p in (old, keep, recent, failed, active, unknown, release)))
        self.run_cleanup('-Apply -KeepLast 1')
        self.assertFalse(old.exists())
        self.assertFalse(keep.exists())
        self.assertTrue(all(p.exists() for p in (recent, failed, active, unknown, release)))

    def test_failed_evidence_retains_last_and_recent_independently(self):
        old = self.run_dir('failed-old', status='failed', age=30)
        keep = self.run_dir('failed-keep', status='failed', age=20)
        recent = self.run_dir('failed-recent', status='failed', age=.1)
        success = self.run_dir('success', age=40)
        self.run_cleanup('-Apply -KeepLast 1 -IncludeFailedRuns')
        self.assertFalse(old.exists())
        self.assertFalse(keep.exists())
        self.assertTrue(recent.exists())
        self.assertTrue(success.exists())

    def test_live_process_and_exclusive_lease_protect_completed_marker(self):
        active = self.run_dir('leased', age=30)
        self.run_dir('newer', age=10)
        lock = active / '.aura-active.lock'
        output = Path(self.temp.name) / 'decision.json'
        code = f'''$lease=[IO.File]::Open({quote(lock)},'OpenOrCreate','ReadWrite','None')
try {{ & {quote(self.script)} -RepoRoot {quote(self.repo)} -Apply -KeepLast 1 -OlderThanDays 0 -OutputJson {quote(output)} }} finally {{ $lease.Dispose() }}'''
        result = ps(code)
        self.assertTrue((active / 'fixture.obj').exists(), result.stdout + result.stderr)
        decisions = json.loads(output.read_text(encoding='utf-8-sig'))['decisions']
        leased = next(d for d in decisions if d['path'] == str(active))
        self.assertIn(leased['action'], ('skip', 'error'), result.stdout + result.stderr)
        self.assertTrue(leased['reason'], result.stdout)
        self.assertTrue((active / 'fixture.obj').exists())
        marker = active / '.aura-generated.json'
        data = json.loads(marker.read_text()); data['pid'] = os.getpid(); marker.write_text(json.dumps(data))
        result = self.run_cleanup('-Apply -KeepLast 1 -OlderThanDays 0')
        decision = next(d for d in result['decisions'] if d['path'] == str(active))
        self.assertEqual(decision['action'], 'skip')

    def test_reparse_escape_and_protected_data_are_never_deleted(self):
        escaped = self.run_dir('escape', age=30)
        protected = self.run_dir('user-data', age=30)
        self.run_dir('newer', age=10)
        outside = Path(self.temp.name) / 'outside'; outside.mkdir()
        sentinel = outside / 'keep.txt'; sentinel.write_text('must survive')
        junction = escaped / 'escape'
        result = ps(f'New-Item -ItemType Junction -Path {quote(junction)} -Target {quote(outside)} | Out-Null')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        (protected / 'real.auraeffect').write_text('protected user project')
        try:
            result = self.run_cleanup('-Apply -KeepLast 1 -OlderThanDays 0')
            actions = {Path(d['path']).name: d['action'] for d in result['decisions']}
            self.assertEqual(actions['escape'], 'skip')
            self.assertEqual(actions['user-data'], 'skip')
            self.assertEqual(sentinel.read_text(), 'must survive')
        finally:
            junction.rmdir()  # Remove the fixture junction itself, never its target.

    def test_path_traversal_root_cwd_and_manifest_mismatch(self):
        victim = self.run_dir('victim', age=30)
        self.run_dir('newer', age=10)
        result = self.run_cleanup('-Apply -KeepLast 1 -OlderThanDays 0', cwd=victim)
        decision = next(d for d in result['decisions'] if d['path'] == str(victim))
        self.assertEqual(decision['action'], 'skip')
        module = ROOT / 'tools/dev/generated-common.ps1'
        for unsafe in (str(self.repo), str(self.repo.anchor), str(self.repo / 'build/ci/../escape')):
            result = ps(f'. {quote(module)}; $ctx=Get-AuraSafetyContext {quote(self.repo)}; Assert-AuraDeleteTarget {quote(unsafe)} @({quote(self.repo / "build/ci")}) $ctx')
            self.assertNotEqual(result.returncode, 0, unsafe)
        marker = victim / '.aura-generated.json'
        data = json.loads(marker.read_text()); data['repo'] = str(Path(self.temp.name)); marker.write_text(json.dumps(data))
        result = self.run_cleanup('-Apply -KeepLast 1 -OlderThanDays 0')
        self.assertTrue(victim.exists())

    def test_retention_requires_both_age_and_count_and_logs_separate(self):
        old = self.run_dir('old', age=30)
        self.run_dir('new', age=10)
        logs = self.run_dir('logs', age=40, category='SHORT_RETENTION')
        result = self.run_cleanup('-Apply -KeepLast 1')
        self.assertFalse(old.exists())
        self.assertTrue(logs.exists())
        self.assertGreater(result['deleted_logical_bytes'], 4096)
        self.assertTrue((self.repo / 'build/ci/new').exists())

    def test_owner_marker_does_not_authorize_source_config_or_manual_evidence(self):
        self.run_dir('new', age=10)
        paths = []
        for name in ('source.cpp', 'private.config', 'owner-settings.json', 'manual.md'):
            path = self.run_dir(name.split('.')[0], age=30)
            file = path / name
            file.write_text('non-generated sentinel')
            when = (dt.datetime.now(dt.timezone.utc) - dt.timedelta(days=30)).timestamp()
            os.utime(file, (when, when))
            paths.append(file)
        result = self.run_cleanup('-Apply -KeepLast 1')
        self.assertTrue(all(path.exists() for path in paths))
        self.assertTrue(all(d['action'] == 'skip' for d in result['decisions'] if Path(d['path']).name != 'new'))

    def test_low_disk_refuses_fresh_build_and_releases_canonical_lock(self):
        module = ROOT / 'tools/dev/generated-common.ps1'
        build = self.repo / 'build/ci/new'
        output = self.repo / 'custom-evidence'; output.mkdir()
        result = ps(f'''. {quote(module)}
function Get-AuraFreeBytes {{ return 0L }}
Start-AuraLocalCiStorage {quote(self.repo)} {quote(build)} 'new' {quote(output)}''')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Low disk', result.stderr)
        self.assertIn('estimated requirement', result.stderr)
        self.assertIn('cleanup-generated.ps1 -DryRun', result.stderr)
        self.assertFalse(build.exists())
        result = ps(f"$l=[IO.File]::Open({quote(self.repo / 'build/ci/.canonical.lock')},'Open','ReadWrite','None');$l.Dispose()")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_reviewed_header_input_newline_drift_remains_protected(self):
        old = self.run_dir('newline-drift-old', age=30)
        self.run_dir('newline-drift-new', age=10)
        header = old / 'aura-gate-a-software/ReviewedEvidence.h'
        header.parent.mkdir()
        data = reviewed_header_fixture(b'\r\n')
        self.assertNotEqual(data, reviewed_header_fixture())
        header.write_bytes(data)
        when = (dt.datetime.now(dt.timezone.utc) - dt.timedelta(days=30)).timestamp()
        os.utime(header, (when, when))
        for mode in ('-DryRun', '-Apply'):
            result = self.run_cleanup(mode + ' -KeepLast 1')
            decision = next(d for d in result['decisions'] if d['path'] == str(old))
            self.assertEqual(decision['action'], 'skip', decision)
            self.assertIn('Unproven source file protected', decision['reason'])
            self.assertEqual(header.read_bytes(), data)

    def test_canonical_generated_probe_header_and_config_object_are_prunable(self):
        # Produce the actual configure-time header in the isolated fixture, not a fake whitelist.
        old = self.run_dir('canonical-old', age=30)
        self.run_dir('canonical-new', age=10)
        header = old / 'mta-probe/ProbeAbi.h'
        header.parent.mkdir()
        subprocess.run(['python', str(ROOT / 'tools/AuraMtaCrashProbe/generate_abi.py'),
                        str(ROOT / 'src/AuraWorker/CanonicalAuraAbi.h'), str(header)], check=True)
        obj = old / 'config_writer_util.obj'
        obj.write_bytes(b'generated object fixture')
        readonly = old / 'aura-enumeration-characterizer/ReadOnlyAuraAbi.h'
        readonly.parent.mkdir()
        subprocess.run(['python', str(ROOT / 'tools/AuraEnumerationCharacterizer/generate_readonly_abi.py'),
                        str(ROOT / 'src/AuraWorker/CanonicalAuraAbi.h'), str(readonly)], check=True)
        reviewed = old / 'aura-gate-a-software/ReviewedEvidence.h'
        reviewed.parent.mkdir()
        reviewed.write_bytes(reviewed_header_fixture())
        when = (dt.datetime.now(dt.timezone.utc) - dt.timedelta(days=30)).timestamp()
        for file in (header, obj, readonly, reviewed):
            os.utime(file, (when, when))
        preview = self.run_cleanup('-DryRun -KeepLast 1')
        decision = next(d for d in preview['decisions'] if d['path'] == str(old))
        self.assertEqual(decision['action'], 'would-delete', decision)
        self.assertTrue(header.exists())
        header.write_text('manually modified header; must survive')
        refused = self.run_cleanup('-Apply -KeepLast 1 -OlderThanDays 0')
        decision = next(d for d in refused['decisions'] if d['path'] == str(old))
        self.assertEqual(decision['action'], 'skip', decision)
        self.assertTrue(header.exists())
        subprocess.run(['python', str(ROOT / 'tools/AuraMtaCrashProbe/generate_abi.py'),
                        str(ROOT / 'src/AuraWorker/CanonicalAuraAbi.h'), str(header)], check=True)
        os.utime(header, (when, when))
        applied = self.run_cleanup('-Apply -KeepLast 1')
        decision = next(d for d in applied['decisions'] if d['path'] == str(old))
        self.assertEqual(decision['action'], 'deleted', decision)
        self.assertFalse(old.exists())

    def test_default_policy_bounds_both_statuses_and_second_prune_is_noop(self):
        for status in ('passed', 'failed'):
            for index in range(7):
                self.run_dir(f'{status}-{index}', status=status, age=30-index)
        self.run_dir('passed-6', age=24, category='SHORT_RETENTION')
        result = self.run_cleanup('-Apply -IncludeFailedRuns')
        self.assertEqual(sum(d['action'] == 'deleted' for d in result['decisions']), 8)
        self.assertEqual(len(list((self.repo / 'build/ci').iterdir())), 6)
        self.assertEqual(result['retained_run_count'], 6)
        self.assertEqual(result['retained_artifact_entries'], 7)
        again = self.run_cleanup('-Apply -IncludeFailedRuns')
        self.assertEqual(again['deleted_logical_bytes'], 0)
        self.assertEqual(again['estimated_reclaimable_bytes'], 0)

    def test_registered_worktree_is_protected_even_with_generated_owner_marker(self):
        (self.repo / 'tracked.txt').write_text('fixture')
        subprocess.run(['git', '-C', str(self.repo), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(self.repo), '-c', 'user.name=fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'fixture'], check=True)
        path = self.repo / 'build/ci/owned-worktree'
        subprocess.run(['git', '-C', str(self.repo), 'worktree', 'add', '-q', '--detach', str(path), 'HEAD'], check=True)
        self.run_dir('owned-worktree', age=30)
        self.run_dir('newer', age=10)
        result = self.run_cleanup('-Apply -KeepLast 1')
        decision = next(d for d in result['decisions'] if d['path'] == str(path))
        self.assertEqual(decision['action'], 'skip')
        self.assertIn('Registered worktree', decision['reason'])
        self.assertTrue(path.exists())

    def test_live_relative_build_command_is_protected(self):
        active = self.run_dir('relative-active', age=30)
        self.run_dir('newer', age=10)
        child = subprocess.Popen(['pwsh', '-NoProfile', '-Command',
            "Write-Output READY; Start-Sleep 20 # build/ci/relative-active"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            self.assertEqual(child.stdout.readline().strip(), 'READY')
            result = self.run_cleanup('-Apply -KeepLast 1')
            decision = next(d for d in result['decisions'] if d['path'] == str(active))
            self.assertEqual(decision['action'], 'skip')
            self.assertIn('Active command', decision['reason'])
            self.assertTrue(active.exists())
        finally:
            child.terminate(); child.communicate(timeout=10)

    def test_success_removes_intermediates_and_failure_retains_them(self):
        module = ROOT / 'tools/dev/generated-common.ps1'
        for outcome in ('passed', 'failed'):
            build = self.repo / f'build/ci/{outcome}'
            output = self.repo / f'custom-{outcome}'; output.mkdir()
            result = ps(f'''. {quote(module)}
$state=Start-AuraLocalCiStorage {quote(self.repo)} {quote(build)} {quote(outcome)} {quote(output)}
[IO.File]::WriteAllBytes((Join-Path $state.build 'fixture.obj'),[byte[]]::new(4096))
[IO.File]::WriteAllBytes((Join-Path $state.build 'fixture.exe'),[byte[]]::new(4096))
Complete-AuraLocalCiStorage $state {quote(outcome)}
$state.telemetry | ConvertTo-Json''')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((build / 'fixture.obj').exists(), outcome == 'failed')
            self.assertTrue((build / 'fixture.exe').exists())
            self.assertEqual(json.loads((build / '.aura-generated.json').read_text())['status'], outcome)
            self.assertIn('approximate_build_bytes', result.stdout)
            for name in ('cleanup-start.json', 'cleanup-end.json'):
                record = json.loads((output / name).read_text(encoding='utf-8-sig'))
                self.assertEqual(record['mode'], 'apply')
                self.assertIn('decisions', record)

    def test_worktree_helper_protects_unknown_dirty_ignored_and_current(self):
        (self.repo / 'tracked.txt').write_text('fixture source')
        (self.repo / '.gitignore').write_text('build/\n*.generated\n')
        subprocess.run(['git', '-C', str(self.repo), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(self.repo), '-c', 'user.name=fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'fixture'], check=True)
        roots = {}
        for name in ('stale', 'dirty', 'ignored', 'unknown'):
            path = self.repo / 'build/dev-worktrees' / name
            subprocess.run(['git', '-C', str(self.repo), 'worktree', 'add', '-q', '--detach', str(path), 'HEAD'], check=True)
            roots[name] = path
        (roots['dirty'] / 'tracked.txt').write_text('uncommitted changes')
        (roots['ignored'] / 'owner.generated').write_text('unproven ignored data')
        when = (dt.datetime.now(dt.timezone.utc) - dt.timedelta(days=20)).isoformat()
        records = [dict(path=str(path), status='completed', completed_utc=when, pid=0) for name, path in roots.items() if name != 'unknown']
        (self.repo / '.git/aura-temporary-worktrees.json').write_text(json.dumps(dict(schema_version=1, owner='Aura.dev-worktrees', worktrees=records)))
        script = ROOT / 'tools/dev/temporary-worktrees.ps1'
        output = Path(self.temp.name) / 'worktrees.json'
        result = ps(f'& {quote(script)} -RepoRoot {quote(self.repo)} -Apply -OutputJson {quote(output)}')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(roots['stale'].exists())
        self.assertTrue(all(roots[name].exists() for name in ('dirty', 'ignored', 'unknown')))
        self.assertTrue(self.repo.exists())


if __name__ == '__main__':
    unittest.main(verbosity=2)
