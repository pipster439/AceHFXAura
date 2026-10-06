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
