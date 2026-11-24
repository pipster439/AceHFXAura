import test from 'node:test';
import assert from 'node:assert/strict';
import Blockly, { loadSafeWorkspaceJson } from '../src/blockly/index.js';
import { registerCustomBlocks } from '../src/blockly/customBlocks.js';
import { EFFECT_PRESETS } from '../src/blockly/presets.js';
import { loadAiSnapshot } from '../src/utils/studioAssistant.js';
registerCustomBlocks();
const graph = blocks => ({ blocks: { languageVersion: 0, blocks } });
const good = graph([{ type: 'math_number', fields: { NUM: 42 } }]);
const snapshot = ws => JSON.stringify(Blockly.serialization.workspaces.save(ws));

for (const [name, bad] of [
  ['unknown block', graph([{ type: 'missing_block' }])],
  ['cross workspace', graph([{ type: 'orch_current_process' }])],
  ['invalid roots', { blocks: { blocks: 'broken' } }],
  ['unknown serializer', { ...good, unexpected: {} }],
  ['unknown input', graph([{ type: 'math_number', inputs: { BAD: { block: { type: 'math_number' } } } }])],
  ['duplicate identity', graph([{ type: 'math_number', id: 'same' }, { type: 'math_number', id: 'same' }])],
  ['oversized graph', graph(Array.from({ length: 2049 }, () => ({ type: 'math_number' })))],
  ['oversized payload', graph([{ type: 'text', fields: { TEXT: 'x'.repeat(262145) } }])],
  ['damaged JSON', '{"blocks":'],
]) test(`reject ${name} before changing the active draft`, () => {
  const ws = new Blockly.Workspace();
  try {
    loadSafeWorkspaceJson(good, ws, true);
    const before = snapshot(ws);
    assert.throws(() => loadSafeWorkspaceJson(bad, ws));
    assert.equal(snapshot(ws), before);
    loadSafeWorkspaceJson(good, ws, true);
    assert.equal(ws.getAllBlocks(false).length, 1);
  } finally { ws.dispose(); }
});

test('reject cycles and deep nesting without recursion overflow', () => {
  const ws = new Blockly.Workspace();
  const cycle = { type: 'math_number' }; cycle.next = { block: cycle };
  let deep = { type: 'math_number' };
  for (let i = 0; i < 60; i++) deep = { type: 'math_arithmetic', inputs: { A: { block: deep } } };
  try {
    for (const bad of [graph([cycle]), graph([deep])]) assert.throws(() => loadSafeWorkspaceJson(bad, ws), /工程|积木/);
  } finally { ws.dispose(); }
});

test('all existing presets and serialized round trips remain supported', () => {
  const ws = new Blockly.Workspace();
  try {
    for (const preset of EFFECT_PRESETS) {
      loadSafeWorkspaceJson(preset.blocklyJson, ws, true);
      const saved = Blockly.serialization.workspaces.save(ws);
      const count = ws.getAllBlocks(false).length;
      loadSafeWorkspaceJson(saved, ws, true);
      assert.equal(ws.getAllBlocks(false).length, count, preset.id);
    }
  } finally { ws.dispose(); }
});

test('AI rejection retains draft, undo history and global event state', () => {
  const ws = new Blockly.Workspace();
  const group = Blockly.Events.getGroup(), undo = Blockly.Events.getRecordUndo();
  try {
    loadSafeWorkspaceJson(good, ws, true);
    const before = snapshot(ws);
    const history = ws.undoStack_.slice();
    assert.throws(() => loadAiSnapshot(ws, graph([{ type: 'missing_block' }])));
    assert.equal(snapshot(ws), before);
    assert.deepEqual(ws.undoStack_, history);
    assert.equal(Blockly.Events.isEnabled(), true);
    assert.equal(Blockly.Events.getGroup(), group);
    assert.equal(Blockly.Events.getRecordUndo(), undo);
  } finally { ws.dispose(); }
});

test('connected real inputs retain inactive fallback shadows', () => {
  const ws = new Blockly.Workspace();
  const state = graph([{ type: 'key_fill_all', inputs: { COLOR: {
    shadow: { type: 'color_rgb' }, block: { type: 'color_rgb' },
  } } }]);
  try {
    loadSafeWorkspaceJson(state, ws, true);
    const saved = Blockly.serialization.workspaces.save(ws);
    assert.ok(saved.blocks.blocks[0].inputs.COLOR.shadow);
    assert.ok(saved.blocks.blocks[0].inputs.COLOR.block);
    loadSafeWorkspaceJson(saved, ws, true);
  } finally { ws.dispose(); }
});

test('disabled Blockly 13 blocks and field-variable serialization remain compatible', () => {
  const ws = new Blockly.Workspace();
  try {
    loadSafeWorkspaceJson(good, ws, true);
    ws.getAllBlocks(false)[0].setDisabledReason(true, 'fixture');
    const saved = Blockly.serialization.workspaces.save(ws);
    assert.ok(saved.blocks.blocks[0].disabledReasons.includes('fixture'));
    loadSafeWorkspaceJson(saved, ws, true);
    assert.equal(ws.getAllBlocks(false)[0].isEnabled(), false);
  } finally { ws.dispose(); }
});

test('unexpected live serializer failure restores the old graph and history', () => {
  const ws = new Blockly.Workspace();
  const original = Blockly.serialization.workspaces.load;
  try {
    loadSafeWorkspaceJson(good, ws, true);
    const before = snapshot(ws), history = ws.undoStack_.slice();
    let fail = true;
    Blockly.serialization.workspaces.load = (state, target, options) => {
      if (target === ws && fail) { fail = false; ws.clear(); throw new Error('fixture live failure'); }
      return original(state, target, options);
    };
    assert.throws(() => loadAiSnapshot(ws, graph([{ type: 'math_number', fields: { NUM: 7 } }])), /恢复原草稿/);
    assert.equal(snapshot(ws), before); assert.deepEqual(ws.undoStack_, history);
    assert.equal(Blockly.Events.isEnabled(), true);
  } finally { Blockly.serialization.workspaces.load = original; ws.dispose(); }
});
