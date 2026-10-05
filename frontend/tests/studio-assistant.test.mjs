import test from 'node:test';
import assert from 'node:assert/strict';
import Blockly from '../src/blockly/index.js';
import { registerCustomBlocks } from '../src/blockly/customBlocks.js';
import { EFFECT_PRESETS } from '../src/blockly/presets.js';
import { assistantContext, numericNodes, parseProposal, prepareProposal, validateCandidate, StudioProposalSession, loadAiSnapshot } from '../src/utils/studioAssistant.js';
registerCustomBlocks();
const snapshot = () => ({ name: 'fixture', publication: { mode: 'continuous', fade_out_ms: 0 }, json: structuredClone(EFFECT_PRESETS[0].blocklyJson) });
const reply = (extra = {}) => ({ action: 'propose', summary: '提高速度', preset: null, edits: [{ node_id: 'n6', value: 2 }], ...extra });

test('mock generate uses existing preset and validates in a disposable workspace', () => {
  const before = snapshot(); const text = JSON.stringify(reply({ preset: EFFECT_PRESETS[0].id, edits: [] }));
  const result = prepareProposal(before, parseProposal(text), 'generate');
  assert.equal(validateCandidate(result.candidate).valid, true); assert.deepEqual(before, snapshot());
  assert.equal(result.changes[0].field, '模板');
});
test('modify numeric speed field without mutating current document', () => {
  const before = snapshot(); const period = numericNodes(before.json).find(n => n.input === 'PERIOD_SEC');
  const result = prepareProposal(before, parseProposal(JSON.stringify(reply({ edits: [{ node_id: period.node_id, value: 2 }] }))), 'modify');
  assert.equal(validateCandidate(result.candidate).valid, true);
  assert.equal(numericNodes(before.json).find(n => n.node_id === period.node_id).value, 3);
  assert.equal(result.changes[0].after, '2');
});
test('explain and error analysis are typed, minimal context', () => {
  for (const intent of ['explain', 'error_analysis']) {
    const context = assistantContext(snapshot(), intent, 'error C2086');
    assert.equal(context.diagnostic, intent === 'error_analysis' ? 'error C2086' : '');
    assert.ok(!JSON.stringify(context).includes('blockly_json'));
    assert.ok(context.nodes.every(n => !('block' in n)));
    const proposal = parseProposal(JSON.stringify(reply({ action: 'explain', edits: [] })));
    assert.equal(prepareProposal(snapshot(), proposal, intent), null);
  }
});
test('reject unknown actions/fields paths commands malformed huge duplicate proposals', () => {
  const invalid = [reply({ action: 'publish' }), reply({ file: '../config.json' }), reply({ preset: '../../evil' }),
    reply({ edits: [{ node_id: '/tmp/file', value: 1 }] }), reply({ edits: [{ node_id: 'n0', value: 'shell' }] }),
    reply({ edits: [{ node_id: 'n0', value: 1, command: 'exec' }] }), reply({ edits: [{ node_id: 'n0', value: 1 }, { node_id: 'n0', value: 2 }] }),
    reply({ action: 'explain', edits: [{ node_id: 'n0', value: 1 }] })];
  for (const proposal of invalid) assert.throws(() => parseProposal(JSON.stringify(proposal)));
  assert.throws(() => parseProposal('{')); assert.throws(() => parseProposal(' '.repeat(65537)));
});
test('candidate rejects missing nodes invalid values and template replacement during modify', () => {
  assert.throws(() => prepareProposal(snapshot(), reply({ edits: [{ node_id: 'n999', value: 2 }] }), 'modify'));
  assert.throws(() => prepareProposal(snapshot(), reply({ edits: [{ node_id: 'n0', value: 999 }] }), 'modify'));
  assert.throws(() => prepareProposal(snapshot(), reply({ preset: EFFECT_PRESETS[0].id, edits: [] }), 'modify'));
  const bad = snapshot(); bad.json.blocks = [{ type: 'unknown_native_code' }]; assert.throws(() => validateCandidate(bad));
});
test('preview apply reject undo are separate and stale edits are preserved', () => {
  const session = new StudioProposalSession(); const before = snapshot(); session.begin(before);
  const prepared = session.stage(reply(), 'modify'); assert.equal(prepared.validation.valid, true);
  assert.deepEqual(before, snapshot()); // Preview has not applied or published anything.
  const after = session.apply(before); session.markApplied(after);
  assert.notDeepEqual(after, before); assert.deepEqual(session.undo(after), before);
  session.begin(before); session.stage(reply(), 'modify'); session.reject(); assert.throws(() => session.apply(before));
  session.stage(reply(), 'modify'); const edited = structuredClone(before); edited.name = 'later_edit';
  assert.throws(() => session.apply(edited)); assert.equal(edited.name, 'later_edit');
  const next = session.apply(before); session.markApplied(next); const later = structuredClone(next); later.name = 'later_edit';
  assert.throws(() => session.undo(later)); assert.equal(later.name, 'later_edit');
});
test('validation failure cannot apply and repair budget is bounded', () => {
  const session = new StudioProposalSession(); session.begin(snapshot());
  assert.throws(() => session.stage(reply({ edits: [{ node_id: 'n0', value: 999 }] }), 'modify'));
  assert.throws(() => session.apply(snapshot()));
  session.reserveRepair(); session.reserveRepair(); assert.throws(() => session.reserveRepair());
  session.begin(snapshot()); assert.equal(session.repairs, 0);
});
test('AI transaction retains publication metadata and never calls network', () => {
  const before = snapshot(); const session = new StudioProposalSession(); session.begin(before);
  const old = globalThis.fetch; globalThis.fetch = () => { throw new Error('AI must not use network for apply/undo'); };
  try { session.stage(reply(), 'modify'); const after = session.apply(before); assert.deepEqual(after.publication, before.publication); assert.deepEqual(session.undo(after), before); }
  finally { globalThis.fetch = old; }
});
test('AI snapshot loading preserves ordinary Blockly undo history', async () => {
  const workspace = new Blockly.Workspace();
  const until = async predicate => { for (let i = 0; i < 100 && !predicate(); i++) await new Promise(resolve => setTimeout(resolve, 0)); assert.ok(predicate()); };
  try {
    const block = workspace.newBlock('math_number'); block.setFieldValue(3, 'NUM');
    await until(() => workspace.getUndoStack().length >= 2);
    const ordinary = [...workspace.getUndoStack()]; const state = Blockly.serialization.workspaces.save(workspace);
    state.blocks.blocks[0].fields.NUM = 2; loadAiSnapshot(workspace, state);
    await until(() => workspace.getUndoStack().length > ordinary.length);
    assert.deepEqual(workspace.getUndoStack().slice(0, ordinary.length), ordinary);
    workspace.undo(); assert.equal(workspace.getBlockById(block.id).getFieldValue('NUM'), 3);
  } finally { workspace.dispose(); }
});
test('presets carry capabilities and each validates using both real transpilers', () => {
  const ids = new Set();
  for (const p of EFFECT_PRESETS) {
    assert.ok(!ids.has(p.id)); ids.add(p.id); assert.ok(p.name && p.description && p.tags.length && p.capabilities.includes('local_simulation'));
    const value = { ...snapshot(), json: structuredClone(p.blocklyJson) };
    assert.equal(validateCandidate(value).valid, true, p.id);
    if (JSON.stringify(p.blocklyJson).includes('gsi_')) assert.ok(p.requiredInputs.includes('gsi_player_state'));
    if (JSON.stringify(p.blocklyJson).includes('key_decay')) assert.ok(p.requiredInputs.includes('key_press_simulation'));
  }
  assert.ok(ids.has('template_static') && ids.has('template_reactive') && ids.has('template_gradient') && ids.has('kill_wave'));
});
test('diagnostic entry points carry only selected error and its kind', () => {
  for (const kind of ['validation', 'build', 'plugin_load']) {
    const context = assistantContext(snapshot(), 'error_analysis', 'fixture error', kind);
    assert.equal(context.diagnostic_kind, kind); assert.equal(context.diagnostic, 'fixture error');
    assert.deepEqual(Object.keys(context).sort(), ['diagnostic', 'diagnostic_kind', 'intent', 'name', 'nodes', 'presets', 'publication']);
  }
  assert.equal(assistantContext(snapshot(), 'modify', 'unrelated logs', 'plugin_load').diagnostic, '');
});
