import test from 'node:test';
import assert from 'node:assert/strict';
import Blockly, { loadSafeWorkspaceJson } from '../src/blockly/index.js';
import { registerCustomBlocks } from '../src/blockly/customBlocks.js';
import { EFFECT_PRESETS } from '../src/blockly/presets.js';
import { loadAiSnapshot, StudioProposalSession } from '../src/utils/studioAssistant.js';
import { durableStudioDraft } from '../src/utils/studioStorage.js';
import { createBundlePayload, importedDraft } from '../src/utils/studioBundle.js';
import { effectConfig } from '../src/utils/applyEffect.js';
registerCustomBlocks();
// Persisted JSON has no object prototype identity; Blockly returns null-prototype maps.
const save = ws => JSON.parse(JSON.stringify(Blockly.serialization.workspaces.save(ws)));

test('actual empty Blockly 13 workspace and variable-only draft roundtrip', () => {
  const ws = new Blockly.Workspace();
  try {
    const empty = save(ws); assert.deepEqual(empty, {});
    loadSafeWorkspaceJson(empty, ws);
    assert.deepEqual(save(ws), empty);
    ws.getVariableMap().createVariable('保留变量', '', 'variable-fixture');
    const variables = save(ws);
    loadSafeWorkspaceJson(variables, ws);
    assert.deepEqual(save(ws), variables);
    assert.equal(JSON.stringify(durableStudioDraft({ blockly_effects: { empty: { blockly_json: empty } } }, 'empty').json), '{}');
    loadAiSnapshot(ws, empty); assert.deepEqual(save(ws), empty);
    loadAiSnapshot(ws, variables); assert.deepEqual(save(ws), variables);
    const bundle = createBundlePayload({ name: 'empty', json: empty, publication: { mode: 'continuous', fade_out_ms: 0 } });
    assert.deepEqual(importedDraft(bundle, {}, 'restored').json, empty);
  } finally { ws.dispose(); }
});

test('actual Blockly 13 workspace comments roundtrip without admitting unknown serializers', () => {
  const ws = new Blockly.Workspace();
  try {
    Blockly.serialization.workspaceComments.append({ id: 'comment-fixture', text: '工程说明', x: 12, y: 24, width: 200, height: 100 }, ws);
    const state = save(ws); assert.ok(state.workspaceComments.length);
    loadSafeWorkspaceJson(state, ws);
    assert.deepEqual(save(ws), state);
    assert.throws(() => loadSafeWorkspaceJson({ ...state, arbitrarySerializer: [] }, ws));
  } finally { ws.dispose(); }
});

test('legal large graph at 2048 blocks loads and saved graph remains loadable', () => {
  const ws = new Blockly.Workspace();
  try {
    const graph = { blocks: { languageVersion: 0, blocks: Array.from({ length: 2048 }, (_, i) => ({ type: 'math_number', id: `large_${i}`, fields: { NUM: i } })) } };
    loadSafeWorkspaceJson(graph, ws);
    assert.equal(ws.getAllBlocks(false).length, 2048);
    const state = save(ws); loadSafeWorkspaceJson(state, ws);
    assert.equal(ws.getAllBlocks(false).length, 2048);
  } finally { ws.dispose(); }
});

test('AI load/Undo/Redo preserves ordinary history and published authority', async () => {
  const ws = new Blockly.Workspace();
  const settle = async () => { await new Promise(r => setTimeout(r, 0)); await new Promise(r => setTimeout(r, 0)); };
  try {
    const block = ws.newBlock('math_number'); block.setFieldValue(3, 'NUM');
    await settle(); const history = ws.getUndoStack().slice();
    const before = save(ws), after = structuredClone(before); after.blocks.blocks[0].fields.NUM = 7;
    loadAiSnapshot(ws, after); await settle();
    assert.deepEqual(ws.getUndoStack().slice(0, history.length), history);
    ws.undo(false); assert.equal(ws.getBlockById(block.id).getFieldValue('NUM'), 3);
    ws.undo(true); assert.equal(ws.getBlockById(block.id).getFieldValue('NUM'), 7);
    const config = { revision: 13, profiles: { fixture: { type: 'plugin', plugin_name: 'published' } }, blockly_effects: { fixture: { applied_plugin_name: 'published', applied_revision: 9, applied_blockly_json: before } } };
    const saved = effectConfig(config, 'fixture', save(ws));
    assert.equal(saved.profiles.fixture.plugin_name, 'published');
    assert.equal(saved.blockly_effects.fixture.applied_revision, 9);
    assert.deepEqual(saved.blockly_effects.fixture.applied_blockly_json, before);
    assert.equal(config.revision, 13);
  } finally { ws.dispose(); }
});

test('preset Bundle import and stale typed proposals preserve original published and draft records', () => {
  const project = { name: 'fixture', json: structuredClone(EFFECT_PRESETS[0].blocklyJson), publication: { mode: 'continuous', fade_out_ms: 0 } };
  const catalog = { fixture: { applied_plugin_name: 'immutable_generation', blockly_json: project.json } };
  const original = structuredClone(catalog);
  const bundle = createBundlePayload(project); const imported = importedDraft(bundle, catalog, 'imported');
  assert.equal(imported.name, 'imported'); assert.deepEqual(catalog, original);
  const session = new StudioProposalSession(); session.begin(project);
  session.stage({ action: 'propose', summary: 'fixture', preset: EFFECT_PRESETS[1].id, edits: [] }, 'generate');
  const concurrent = { ...project, name: 'concurrent' };
  assert.throws(() => session.apply(concurrent), /工程已变化/);
  assert.deepEqual(catalog, original);
});
