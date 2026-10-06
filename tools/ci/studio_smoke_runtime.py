"""Packaged Studio fixture paths; never grants a checkout fallback to runtime."""
import json
from pathlib import Path
import zipfile

def owned_runtime_ready(status, process_id):
    reported = status.get('identity', {}).get('process_id', 0)
    if not reported:
        return False  # Listener can precede the first owner-thread snapshot.
    if reported != process_id:
        raise RuntimeError('Unexpected runtime owner')
    return True


def configure_package(args, repo, output):
    if args.package is None:
        return None
    package = args.package.resolve()
    payload = package / 'runtime-payload'
    if args.ui.resolve() != package / 'Aura.exe' or args.bin.resolve() != payload:
        raise RuntimeError('Packaged smoke must use the extracted GUI and sidecars')
    if args.web_root is not None and args.web_root.resolve() != payload / 'web':
        raise RuntimeError('Packaged smoke cannot use checkout web assets')
    args.web_root = payload / 'web'
    for name in ('Aura.exe', 'runtime-payload/runtime-manifest.json', 'runtime-payload/web/index.html'):
        if not (package / name).is_file():
            raise RuntimeError('Incomplete extracted package')
    manifest = json.loads((payload / 'runtime-manifest.json').read_text(encoding='utf-8'))
    if manifest['version'] != (repo / 'VERSION').read_text(encoding='utf-8').strip():
        raise RuntimeError('Wrong packaged version')
    (output / 'packaged-runtime.json').write_text(json.dumps({
        'version': manifest['version'], 'build_id': manifest['build_id'],
        'gui': 'Aura.exe', 'core': 'runtime-payload/aura_daemon.exe',
        'web': 'runtime-payload/web/index.html', 'sdk': 'runtime-payload/include',
        'development_environment': False, 'checkout_runtime_fallback': False,
        'fixture_data_only': 'tools/ci/fixtures/studio_smoke_projects.json'
    }, indent=2), encoding='utf-8')
    return package


def package_projects():
    return json.loads((Path(__file__).parent / 'fixtures/studio_smoke_projects.json').read_text(encoding='utf-8'))


def configure_environment(env, package, root):
    if package is not None:
        env.pop('AURA_DEV_ROOT', None)
        env.pop('AURA_DEV_BIN', None)
        env['LOCALAPPDATA'] = str(root)


def assert_clean_persistence(root, output, package):
    if package is None:
        return
    if (package / 'runtime-payload/aura_daemon.log').exists():
        raise RuntimeError('Packaged fixture wrote logs into immutable payload')
    checked = 0
    for file in root.rglob('*'):
        if file.is_file() and file.suffix.lower() in ('.json', '.log', '.txt'):
            if file.stat().st_size > 20 * 1024 * 1024:
                raise RuntimeError('Unbounded persistence fixture')
            if 'fixture-only-key' in file.read_text(encoding='utf-8', errors='replace'):
                raise RuntimeError('Synthetic credential leaked into persistence')
            checked += 1
    bundle = output / 'fixture.auraeffect'
    if bundle.exists():
        with zipfile.ZipFile(bundle) as z:
            if z.namelist() != ['manifest.json', 'project.json']:
                raise RuntimeError('Unexpected exported bundle member')
            if any('fixture-only-key' in z.read(n).decode('utf-8') for n in z.namelist()):
                raise RuntimeError('Synthetic credential leaked into bundle')
            manifest = json.loads(z.read('manifest.json'))
        package_version = json.loads((package / 'runtime-payload/runtime-manifest.json').read_text(encoding='utf-8'))['version']
        if manifest['created_with_version'] != package_version:
            raise RuntimeError('Bundle export has the wrong product version')
    (output / 'packaged-hygiene.json').write_text(json.dumps({
        'result': 'PASS', 'persisted_text_files_checked': checked,
        'synthetic_credential_matches': 0, 'owner_credential_read': False,
        'bundle_created_with_version': manifest['created_with_version'] if bundle.exists() else None
    }, indent=2), encoding='utf-8')
