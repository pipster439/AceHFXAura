import test from 'node:test';
import assert from 'node:assert/strict';
import { listenStudioCommands, initialStudioProject } from '../src/utils/studioHost.js';
import { buildEffect } from '../src/utils/applyEffect.js';

test('typed native commands reject unknown commands/fields and clean up', () => {
  let listener, removed, calls = [];
  const webview = { addEventListener: (_, f) => listener = f, removeEventListener: (_, f) => removed = f };
  const dispose = listenStudioCommands(webview, { save: () => calls.push('save'), publish: () => calls.push('publish') });
  listener({ data: { type: 'studio_command', command: 'shell' } });
  listener({ data: { type: 'studio_command', command: 'publish', path: 'bad' } });
  listener({ data: { type: 'studio_command', command: 'save' } });
  assert.deepEqual(calls, ['save']); dispose(); assert.equal(removed, listener);
});
test('build only compiles; no reload, publication or config write', async () => {
  const old = globalThis.fetch; const requests = [];
  globalThis.fetch = async (url, opts) => { requests.push(url); return { ok: true, json: async () => url === '/api/status' ? { studio_publish_ready: true } : { success: true } }; };
  try { await buildEffect('sample', {}, { transpile: () => 'fixture' }); }
  finally { globalThis.fetch = old; }
  assert.deepEqual(requests, ['/api/status', '/api/compile_effect']);
});
test('first asynchronous config selects its actual project while recovered drafts win', () => {
  assert.equal(initialStudioProject({ fixture: {} }), 'fixture');
  assert.equal(initialStudioProject({ fixture: {} }, { name: 'unsaved', json: { blocks: [] } }), 'unsaved');
  assert.equal(initialStudioProject({ fixture: {} }, { name: '../invalid', json: {} }), 'fixture');
});
