"""Studio polish: source-only bundle + palette + responsive UI + owned crash/restart desktop smoke: dry-run core and local HTTP fixture only.

No deployment or package generation. Requires current built UI and daemon paths.
Credentials use a random loopback target and are removed by the native harness.
"""
from studio_conversation_mock import reply_for
from studio_smoke_runtime import configure_package, package_projects, configure_environment, assert_clean_persistence, owned_runtime_ready
import argparse
import ctypes
from ctypes import wintypes
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import shutil
import hashlib
import time
import urllib.request

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--bin', required=True, type=Path)
    parser.add_argument('--ui', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--capture', action='store_true')
    parser.add_argument('--web-root', type=Path)
    parser.add_argument('--package', type=Path, help='Actual extracted package; rejects checkout runtime fallback')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    binaries = args.bin.resolve(); ui = args.ui.resolve()
    package = configure_package(args, repo, output)
    subprocess.run(['python', '-B', str(repo / 'tools/ci/check-idle-test-ports.py')], check=True)
    config = json.loads(((package / 'runtime-payload' if package else repo) / 'config.example.json').read_text(encoding='utf-8'))
    generated = json.dumps(package_projects()['fixture']).encode() if package else subprocess.check_output(['node', '--input-type=module', '-e',
        "import {EFFECT_PRESETS} from './src/blockly/presets.js';console.log(JSON.stringify(EFFECT_PRESETS[0].blocklyJson));"], cwd=repo / 'frontend')
    config['blockly_effects'] = {'fixture': {'name': 'fixture', 'version': 2, 'publication': {'mode': 'continuous', 'fade_out_ms': 0}, 'blockly_json': json.loads(generated)}}
    health = json.dumps(package_projects()['fixture_health']).encode() if package else subprocess.check_output(['node', '--input-type=module', '-e',
        "import {EFFECT_PRESETS} from './src/blockly/presets.js';console.log(JSON.stringify(EFFECT_PRESETS.find(p=>p.manifest.gsi_fields.includes('player.state.health')).blocklyJson));"], cwd=repo / 'frontend')
    config['blockly_effects']['fixture_health'] = {'name': 'fixture_health', 'version': 2, 'publication': {'mode': 'continuous', 'fade_out_ms': 0}, 'blockly_json': json.loads(health)}
    def authority(value):
        return {**{k: value.get(k) for k in ('profiles', 'default_profile', 'orchestration')},
            'publication_records': {k: {f: v for f, v in effect.items() if f.startswith(('applied_', 'published_'))} for k, effect in value.get('blockly_effects', {}).items() if k in ('fixture', 'fixture_health')}}
    original_authority = authority(config)
    requests = []
    class Mock(BaseHTTPRequestHandler):
        def log_message(self, *args): pass
        def do_POST(self):
            length = int(self.headers.get('Content-Length', '0'))
            if length > 65536 or self.path != '/v1/chat/completions': self.send_error(400); return
            data = json.loads(self.rfile.read(length))
            if data['messages'][1]['content'] == 'Connection test only.':
                body = json.dumps({'choices': [{'finish_reason': 'stop', 'message': {'content': 'ok'}}]}).encode()
                self.send_response(200); self.send_header('Content-Type', 'application/json'); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
                return
            context = json.loads(data['messages'][1]['content'])
            proposal = reply_for(context, requests)
            body = json.dumps({'choices': [{'finish_reason': 'stop', 'message': {'content': json.dumps(proposal, ensure_ascii=False)}}]}).encode()
            self.send_response(200); self.send_header('Content-Type', 'application/json'); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Mock)
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    with tempfile.TemporaryDirectory(prefix='aura-studio-tooling-', dir=output if package else None) as directory:
        root = Path(directory); (root / '.aura-studio-tooling-fixture').write_text('local mock only')
        path = root / 'config.json'; path.write_text(json.dumps(config), encoding='utf-8')
        env = os.environ.copy(); env.update(AURA_STUDIO_POLISH='1', AURA_DATA_ROOT=str(root), AURA_DEV_ROOT=str(repo), AURA_DEV_BIN=str(binaries), AURA_MAGNETIC_VALIDATION_OFFLINE='1',
            AURA_STUDIO_TOOLING_DIR=str(output), AURA_STUDIO_AI_MOCK_URL=f'http://127.0.0.1:{server.server_port}/v1/', AURA_STUDIO_AI_CAPTURE='1' if args.capture else '0')
        configure_environment(env, package, root)
        # Use current built frontend; never overwrite tracked web/index.html.
        web_root = args.web_root.resolve() if args.web_root else repo / 'build/alpha8_polish/frontend'
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateEventW.argtypes = [wintypes.LPVOID, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]; kernel.CreateEventW.restype = wintypes.HANDLE
        kernel.SetEvent.argtypes = [wintypes.HANDLE]; kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        event_name = 'Local\\AuraStudioToolingSmoke-' + uuid.uuid4().hex
        shutdown_event = kernel.CreateEventW(None, True, False, event_name)
        if not shutdown_event: raise OSError(ctypes.get_last_error())
        core = subprocess.Popen([str(binaries / 'aura_daemon.exe'), '--dry-run', '--config', str(path), '--keymap', str((package / 'runtime-payload' if package else repo) / 'calibrated_keymap.json'),
            '--shutdown-event', event_name, '--web-root', str(web_root), '--sdk-include', str((package / 'runtime-payload' if package else repo) / 'include')], cwd=root if package else binaries, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        gui = None
        try:
            deadline = time.monotonic() + 30
            while True:
                try:
                    with urllib.request.urlopen('http://127.0.0.1:19897/api/runtime/status', timeout=1) as response: status = json.load(response)
                    if not owned_runtime_ready(status, core.pid): raise KeyError('Owner snapshot not ready')
                    with urllib.request.urlopen('http://127.0.0.1:19898/api/status', timeout=1): pass
                    break
                except (OSError, KeyError):
                    if core.poll() is not None or time.monotonic() >= deadline: raise RuntimeError('Dry-run fixture failed to start')
                    time.sleep(.1)
            for stage in ('crash', 'recover', 'clean'):
                env['AURA_STUDIO_TOOLING_STAGE'] = stage
                ready = output / 'capture-ready.json'
                ready.unlink(missing_ok=True)
                gui = subprocess.Popen([str(ui), '--validate-studio-tooling'], cwd=root, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                if args.capture and stage == 'recover':
                    deadline = time.monotonic() + 150
                    while gui.poll() is None and not ready.exists():
                        if time.monotonic() > deadline: raise TimeoutError('Capture readiness')
                        time.sleep(.05)
                    if ready.exists() and gui.poll() is None:
                        assert json.loads(ready.read_text())['process_id'] == gui.pid, 'Capture owner mismatch'
                        for command, destination in [
                            (['screenshot', '--focus', '--output', str(output / 'window-composite.png'), '--json'], 'window-capture.json'),
                            (['inspect', '--json', '--depth', '6'], 'uia-tree.json'),
                        ]:
                            captured = subprocess.run(['winapp', 'ui', *command, '--on', 'local', '-a', str(gui.pid)], capture_output=True, text=True, encoding='utf-8', errors='replace')
                            (output / destination).write_text(captured.stdout + captured.stderr, encoding='utf-8')
                            if captured.returncode: raise RuntimeError('Owned UIA capture failed: ' + destination)
                code = gui.wait(timeout=180)
                result = json.loads((output / (stage + '-results.json')).read_text(encoding='utf-8'))
                if result['error'] is not None: raise RuntimeError(result['error'] or 'Fixture failed without a message')
                assert code == (23 if stage == 'crash' else 0), 'Unexpected fixture exit'
                journal = root / 'Studio/drafts' / (hashlib.sha256(b'fixture').hexdigest() + '.json')
                shutil.copyfile(journal, output / (stage + '-journal.json'))
                assert 'fixture-only-key' not in journal.read_text(encoding='utf-8')
                assert authority(json.loads(path.read_text(encoding='utf-8'))) == original_authority, 'Published/config authority changed automatically'
                print(json.dumps({'stage': stage, 'result': 'PASS', 'phases': len(result['results'])}))
            for source in root.rglob('*.json'):
                assert 'fixture-only-key' not in source.read_text(encoding='utf-8'), 'Secret leaked into JSON persistence'
            import zipfile
            with zipfile.ZipFile(output / 'fixture.auraeffect') as bundle:
                assert bundle.namelist() == ['manifest.json', 'project.json']
                assert all('fixture-only-key' not in bundle.read(n).decode('utf-8') for n in bundle.namelist())
            assert len(requests) == 2 and requests[1]['tools'] == ['propose_effect_change'], 'Unexpected conversation/tool scope'
            assert requests[0]['preset_ids'] == [], 'Unselected preset metadata entered AI context'
            assert requests[0]['capabilities']['gsi_fields'] == [] and requests[0]['capabilities']['inputs'] == ['time'], 'Unrelated GSI input entered AI context'
        finally:
            (output / 'mock-requests.json').write_text(json.dumps(requests, indent=2, ensure_ascii=False), encoding='utf-8')
            # Only test-owned processes; use the private named normal-shutdown event.
            try:
                if gui is not None and gui.poll() is None:
                    subprocess.run(['pwsh', '-NoProfile', '-Command', f'(Get-Process -Id {gui.pid}).CloseMainWindow()'], capture_output=True)
                    gui.wait(timeout=15)
            finally:
                try:
                    if core.poll() is None:
                        kernel.SetEvent(shutdown_event); core.wait(timeout=15)
                finally:
                    kernel.CloseHandle(shutdown_event)
                    server.shutdown(); server.server_close()
                    assert_clean_persistence(root, output, package)

if __name__ == '__main__': main()
