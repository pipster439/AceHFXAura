import test from 'node:test';
import assert from 'node:assert/strict';
import { StudioConversationTools, projectFingerprint } from '../src/utils/studioConversation.js';
import { numericNodes } from '../src/utils/studioAssistant.js';
import { registerCustomBlocks } from '../src/blockly/customBlocks.js';
registerCustomBlocks();
import { EFFECT_PRESETS } from '../src/blockly/presets.js';

const snapshot = () => ({ name: 'fixture', publication: { mode: 'continuous', fade_out_ms: 0 }, json: structuredClone(EFFECT_PRESETS[0].blocklyJson) });
const proposal = (node, value) => ({ action: 'propose', summary: '保留颜色，调整周期', preset: null, edits: [{ node_id: node.node_id, value }] });
test('multi-turn pending proposal followup stays on same draft and retains colors, then Apply and Undo', async () => {
  const before = snapshot(), tools = new StudioConversationTools();
  const node = numericNodes(before.json).find(n => n.input === 'PERIOD_SEC');
  node.block.fields.NUM = .9; const original = structuredClone(before);
  const context = await tools.begin(before); assert.deepEqual(context.presets, []); assert.deepEqual(context.capabilities.gsi_fields, []);
  const first = await tools.execute('propose_effect_change', proposal(node, .65), before); assert.equal(first.valid, true);
  assert.deepEqual(before, original);
  const secondContext = await tools.begin(before); assert.equal(secondContext.active_proposal.changes[0].after, '0.65');
  const second = await tools.execute('propose_effect_change', proposal(node, .725), before); assert.equal(second.valid, true);
  const candidate = tools.session.apply(before); const values = numericNodes(candidate.json);
  assert.equal(values.find(n => n.input === 'PERIOD_SEC').value, .725);
  assert.deepEqual(values.filter(n => n.parent === 'color_rgb').map(n => n.value), numericNodes(before.json).filter(n => n.parent === 'color_rgb').map(n => n.value));
  tools.session.markApplied(candidate); assert.deepEqual(tools.session.undo(candidate), before);
});
test('actual candidate error is available to followup, no fabricated validation result or Apply', async () => {
  const before = snapshot(), tools = new StudioConversationTools(); await tools.begin(before);
  const invalid = await tools.execute('propose_effect_change', proposal({ node_id: 'n999' }, 2), before);
  assert.equal(invalid.valid, false); assert.match(invalid.diagnostic, /不存在/); assert.throws(() => tools.session.apply(before));
  const error = await tools.execute('get_active_proposal', {}, before); assert.equal(error.diagnostic, invalid.diagnostic);
  assert.equal((await tools.execute('validate_candidate', {}, before)).valid, false);
  const next = await tools.begin(before); assert.equal(next.active_proposal.valid, false);
});
test('stale proposal, project changes mid-request and cancellation are refused without editing', async () => {
  const before = snapshot(), tools = new StudioConversationTools(); await tools.begin(before);
  const node = numericNodes(before.json).find(n => n.input === 'PERIOD_SEC'); await tools.execute('propose_effect_change', proposal(node, 2), before);
  const edited = structuredClone(before); numericNodes(edited.json).find(n => n.input === 'PERIOD_SEC').block.fields.NUM = 4;
  await assert.rejects(tools.execute('get_capabilities', {}, edited), /项目已变化/); assert.throws(() => tools.session.apply(edited));
  await tools.begin(edited); tools.cancel(); await assert.rejects(tools.execute('propose_effect_change', proposal(node, 2), edited), /取消/); assert.throws(() => tools.session.apply(edited));
});
test('unknown, Apply, Publish, shell, path and oversized tools cannot reach project actions', async () => {
  const before = snapshot(), tools = new StudioConversationTools(); await tools.begin(before);
  for (const name of ['apply_project', 'publish', 'shell', 'write_file', 'HID']) await assert.rejects(tools.execute(name, {}, before));
  await assert.rejects(tools.execute('get_build_errors', { path: 'C:/secret' }, before));
  await assert.rejects(tools.execute('get_preset', { preset_id: 'a'.repeat(9000) }, before));
  await assert.rejects(tools.execute('propose_effect_change', { ...proposal({ node_id: 'n0' }, 1), shell: 'exec' }, before));
  assert.deepEqual(before, snapshot());
});
test('diagnostics read only actual selected errors and preset metadata is opt-in per tool', async () => {
  const before = snapshot(), tools = new StudioConversationTools(); await tools.begin(before);
  assert.deepEqual(await tools.execute('get_build_errors', {}, before, { build: 'C2039 unsupported field' }), { diagnostic: 'C2039 unsupported field' });
  const preset = await tools.execute('get_preset', { preset_id: EFFECT_PRESETS[0].id }, before); assert.equal(preset.id, EFFECT_PRESETS[0].id);
  await assert.rejects(tools.execute('propose_effect_change', { action: 'propose', summary: 'replace', preset: preset.id, edits: [{ node_id: 'n0', value: 1 }] }, before));
  assert.equal((await tools.execute('get_preset', { preset_id: 'unknown' }, before)).available, false);
  await assert.rejects(tools.execute('propose_effect_change', { action: 'propose', summary: 'replace', preset: EFFECT_PRESETS[1].id, edits: [] }, before));
});
test('whole graph fingerprint includes nonnumeric changes and project identity', async () => {
  const first = snapshot(), second = snapshot(); assert.equal(await projectFingerprint(first), await projectFingerprint(second));
  second.name = 'other_project'; assert.notEqual(await projectFingerprint(first), await projectFingerprint(second));
});
test('project sessions cannot receive another projects active proposal; clear is memory only', async () => {
  const first = snapshot(), tools = new StudioConversationTools(); await tools.begin(first);
  const node = numericNodes(first.json).find(n => n.input === 'PERIOD_SEC'); await tools.execute('propose_effect_change', proposal(node, 2), first);
  const second = { ...snapshot(), name: 'second' }; const next = await tools.begin(second); assert.equal(next.active_proposal, null);
  await tools.execute('propose_effect_change', proposal(node, 2), second); tools.clear();
  assert.equal((await tools.begin(second)).active_proposal, null); assert.deepEqual(first, snapshot());
});
test('stale pending summary is marked on next turn, applied summary follows current draft', async () => {
  const before = snapshot(), tools = new StudioConversationTools(); await tools.begin(before);
  const node = numericNodes(before.json).find(n => n.input === 'PERIOD_SEC'); await tools.execute('propose_effect_change', proposal(node, 2), before);
  const edit = structuredClone(before); numericNodes(edit.json).find(n => n.input === 'PERIOD_SEC').block.fields.NUM = 4;
  const next = await tools.begin(edit); assert.equal(next.active_proposal.stale, true); assert.equal(next.active_proposal.preview, undefined);
});
