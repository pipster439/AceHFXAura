"""Packaged desktop harness must never fall back to developer runtime assets."""
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('studio_smoke_paths', ROOT / 'tools/ci/studio_smoke_runtime.py')
paths = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(paths)


class PackagedStudioPaths(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.package = Path(self.temp.name) / 'package'; self.output = Path(self.temp.name) / 'evidence'
        self.output.mkdir(); (self.package / 'runtime-payload/web').mkdir(parents=True)
        (self.package / 'Aura.exe').write_bytes(b'fixture')
        (self.package / 'runtime-payload/web/index.html').write_text('fixture')
        self.version = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()
        (self.package / 'runtime-payload/runtime-manifest.json').write_text(json.dumps({'version': self.version, 'build_id': 'fixture'}))
        self.args = SimpleNamespace(package=self.package, ui=self.package / 'Aura.exe', bin=self.package / 'runtime-payload', web_root=None)

    def test_exact_packaged_assets_without_development_environment(self):
        self.assertEqual(paths.configure_package(self.args, ROOT, self.output), self.package)
        self.assertEqual(self.args.web_root, self.package / 'runtime-payload/web')
        env = {'AURA_DEV_ROOT': 'checkout', 'AURA_DEV_BIN': 'build'}
        paths.configure_environment(env, self.package, self.output)
        self.assertNotIn('AURA_DEV_ROOT', env); self.assertNotIn('AURA_DEV_BIN', env)

    def test_checkout_gui_sidecar_and_web_rejected(self):
        for field, value in [('ui', ROOT / 'Aura.exe'), ('bin', ROOT / 'build/Release'), ('web_root', ROOT / 'web')]:
            with self.subTest(field=field):
                previous = getattr(self.args, field); setattr(self.args, field, value)
                with self.assertRaisesRegex(RuntimeError, 'Packaged smoke'):
                    paths.configure_package(self.args, ROOT, self.output)
                setattr(self.args, field, previous)

    def test_persistence_secret_and_wrong_bundle_version_rejected(self):
        (self.output / 'config.json').write_text('{"value":"fixture-only-key"}')
        with self.assertRaisesRegex(RuntimeError, 'credential leaked'):
            paths.assert_clean_persistence(self.output, self.output, self.package)
        (self.output / 'config.json').write_text('{}')
        import zipfile
        with zipfile.ZipFile(self.output / 'fixture.auraeffect', 'w') as bundle:
            bundle.writestr('manifest.json', json.dumps({'created_with_version': 'wrong'})); bundle.writestr('project.json', '{}')
        with self.assertRaisesRegex(RuntimeError, 'wrong product version'):
            paths.assert_clean_persistence(self.output, self.output, self.package)

    def test_native_rejection_fixture_checks_semantics_and_authority(self):
        source = (ROOT / 'winui/Validation/StudioAiValidation.cs').read_text(encoding='utf-8')
        self.assertNotIn('aiStatus.Text.Contains("不符合")', source)
        for guard in ('Rejected action retained an executable candidate', 'Missing explicit rejection status',
                      'Rejected action changed draft or published/runtime authority', 'Rejected action made a mutating HTTP request'):
            self.assertIn(guard, source)
        self.assertIn('"_chatCandidate"', source); self.assertIn('"_aiProposalReady"', source)
        for file in ('StudioAiValidation.cs', 'StudioToolingValidation.cs'):
            fixture = (ROOT / 'winui/Validation' / file).read_text(encoding='utf-8')
            self.assertLess(fixture.index('"Fixture XAML root not ready"'), fixture.index('window.Content.XamlRoot.RasterizationScale'))

    def test_mock_rejection_cases_are_strict_actions_with_safe_text(self):
        spec = importlib.util.spec_from_file_location('conversation_mock', ROOT / 'tools/ci/studio_conversation_mock.py')
        mock = importlib.util.module_from_spec(spec); spec.loader.exec_module(mock)
        context = {'current_project': {'studio': {'nodes': [], 'capabilities': {}, 'presets': []}, 'fingerprint': 'same'},
                   'conversation': [{'role': 'user', 'text': 'invalid-action'}], 'tool_rounds': []}
        for case, name in [('publish', 'publish'), ('unknown', 'unknown_tool'), ('malformed', 'propose_effect_change')]:
            result = mock.reply_for(context, [], case)
            self.assertTrue(result['message']); self.assertEqual(result['tool_calls'][0]['name'], name)
            if case == 'malformed': self.assertEqual(result['tool_calls'][0]['arguments']['action'], 'apply')

    def test_runtime_readiness_waits_for_snapshot_and_rejects_other_owners(self):
        import sys
        from unittest.mock import patch
        with patch.object(sys, 'path', [str(ROOT / 'tools/ci'), *sys.path]):
            spec = importlib.util.spec_from_file_location('ai_smoke', ROOT / 'tools/ci/run-studio-ai-smoke.py')
            smoke = importlib.util.module_from_spec(spec); spec.loader.exec_module(smoke)
        self.assertFalse(smoke.owned_runtime_ready({'identity': {'process_id': 0}}, 123))
        self.assertTrue(smoke.owned_runtime_ready({'identity': {'process_id': 123}}, 123))
        with self.assertRaisesRegex(RuntimeError, 'Unexpected runtime owner'):
            smoke.owned_runtime_ready({'identity': {'process_id': 456}}, 123)
