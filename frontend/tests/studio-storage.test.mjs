import test from 'node:test';
import assert from 'node:assert/strict';
import { createStudioStorage, durableStudioDraft } from '../src/utils/studioStorage.js';
import { registerCustomBlocks } from '../src/blockly/customBlocks.js';
import { EFFECT_PRESETS } from '../src/blockly/presets.js';
import { readFileSync } from 'node:fs';
registerCustomBlocks();
test('storage RPC is typed, correlates replies/errors, status and cleans up', async () => {
  let listener, removed; const messages = [], statuses = [];
  const webview = { addEventListener: (_, f) => listener = f, removeEventListener: (_, f) => removed = f, postMessage: m => messages.push(m) };
  const storage = createStudioStorage(webview, s => statuses.push(s));
  const pending = storage.request('snapshot', { name: 'fixture' }, { reason: 'before_ai_apply' });
  assert.match(messages[0].request_id, /^[a-f0-9]{32}$/); assert.equal(messages[0].operation, 'snapshot');
  listener({ data: { type: 'studio_storage_result', request_id: 'other', result: {} } });
  listener({ data: { type: 'studio_storage_result', request_id: messages[0].request_id, result: { snapshots: [] } } }); assert.deepEqual(await pending, { snapshots: [] });
  const failed = storage.request('saved', {});
  listener({ data: { type: 'studio_storage_result', request_id: messages[1].request_id, error: 'fixture failure' } }); await assert.rejects(failed, /fixture failure/);
  listener({ data: { type: 'studio_storage_status', name: 'fixture', text: 'saved' } }); assert.equal(statuses.length, 1);
  const closing = storage.request('autosave', {}); storage.dispose(); await assert.rejects(closing, /关闭/); assert.equal(removed, listener);
});
test('unsupported storage operations and paths never leave the web bridge', async () => {
  let calls = 0; const storage = createStudioStorage({ addEventListener() {}, removeEventListener() {}, postMessage() { calls++; } });
  await assert.rejects(storage.request('publish', {})); await assert.rejects(storage.request('snapshot', {}, { path: '../file' })); assert.equal(calls, 0); storage.dispose();
});
test('durable draft projection excludes applied authority credentials and config metadata', () => {
  const config = { api_key: 'fixture-secret', profiles: { desktop: {} }, blockly_effects: { fixture: { blockly_json: EFFECT_PRESETS[0].blocklyJson, applied_plugin_name: 'fixture.dll', applied_revision: 99, diagnostics: 'private' } } };
  const value = durableStudioDraft(config, 'fixture'); assert.equal(value.name, 'fixture'); assert.equal(value.publication.mode, 'continuous');
  const serialized = JSON.stringify(value); for (const secret of ['api_key', 'fixture-secret', 'applied_', 'fixture.dll', 'profiles', 'diagnostics']) assert.ok(!serialized.includes(secret));
  assert.ok(value.json.blocks.blocks.length); assert.deepEqual(durableStudioDraft({}, 'blank').json.blocks?.blocks || [], []);
});
test('actual editor checkpoints before AI mutation and publication; save ack follows durable save', () => {
  const source = readFileSync(new URL('../src/components/EffectStudio.jsx', import.meta.url), 'utf8');
  assert.ok(source.indexOf("await checkpointDraft('before_ai_apply')") < source.indexOf("aiSessionRef.current.apply(current())"));
  assert.ok(source.indexOf("await checkpointDraft('before_publish')") < source.indexOf('await stageEffect('));
  assert.ok(source.indexOf("request('saved', currentDraft())") > source.indexOf('await onSaveConfig(next'));
});
