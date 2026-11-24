import test from 'node:test';
import assert from 'node:assert/strict';
import Blockly, { loadSafeWorkspaceJson } from '../src/blockly/index.js';
import { JsTranspiler, PREVIEW_68_KEYS } from '../src/blockly/jsTranspiler.js';
import { CppTranspiler } from '../src/blockly/cppTranspiler.js';
import { registerCustomBlocks } from '../src/blockly/customBlocks.js';
import { prepareGraphCandidate, describeGraph, parseGraphOperations, GRAPH_OPERATION_SCHEMA } from '../src/utils/studioGraphTools.js';
registerCustomBlocks();

const empty = () => ({ name: 'graph_fixture', publication: { mode: 'continuous', fade_out_ms: 0 }, json: {} });
const create = (id, type, fields = {}, state) => ({ op: 'create', id, type, fields, ...(state ? { state } : {}) });
const connect = (parent, name, child) => ({ op: 'connect', parent, slot: name === 'next' ? { kind: 'next' } : { kind: 'input', name }, child });
function builder() {
  const ops = []; let counter = 0;
  return { ops,
    node(type, inputs = {}, fields = {}, name = `node_${counter++}`, state) {
      ops.push(create(name, type, fields, state));
      for (const [input, child] of Object.entries(inputs)) ops.push(connect(name, input, child));
      return name;
    },
    num(value, name = `number_${counter++}`) { ops.push(create(name, 'math_number', { NUM: value })); return name; },
    chain(...ids) { for (let i = 1; i < ids.length; i++) ops.push(connect(ids[i - 1], 'next', ids[i])); return ids[0]; }
  };
}
function rgb(b, values, prefix = 'rgb') {
  return b.node('color_rgb', Object.fromEntries(['R', 'G', 'B'].map((channel, index) => [channel, b.num(values[index], `${prefix}_${channel}`)])), {}, prefix);
}
function staticProject(color = [24, 80, 160]) {
  const b = builder(); b.node('key_fill_all', { COLOR: rgb(b, color) }, {}, 'fill');
  return prepareGraphCandidate(empty(), b.ops).candidate;
}
function render(candidate) {
  const workspace = new Blockly.Workspace();
  loadSafeWorkspaceJson(candidate.json, workspace, true);
  try { CppTranspiler.transpile(candidate.name, workspace, candidate.publication); return JsTranspiler.compile(workspace, candidate.publication); }
  finally { workspace.dispose(); }
}
function frozen(value) { if (value && typeof value === 'object') { Object.values(value).forEach(frozen); Object.freeze(value); } return value; }

test('empty project creates a breathing effect with real time/color Blockly connections', () => {
  const b = builder(), color = rgb(b, [20, 100, 200]);
  const phase = b.node('time_phase', { PERIOD: b.num(1000) });
  const wave = b.node('math_waveform', { PHASE: phase, AMPLITUDE: b.num(.5), OFFSET: b.num(.5) }, { TYPE: 'SINE' });
  b.node('key_fill_all', { COLOR: b.node('color_brightness', { COLOR: color, SCALE: wave }) }, {}, 'fill');
  const original = frozen(empty()), result = prepareGraphCandidate(original, b.ops);
  const run = render(result.candidate);
  assert.deepEqual(run(0)[0], [10, 50, 100]); assert.deepEqual(run(250)[0], [20, 100, 200]);
  assert.deepEqual(run(750)[0], [0, 0, 0]); assert.deepEqual(original.json, {});
  assert.equal(result.validation.native_compiled, false); assert.equal(result.validation.samples.length, 10);
  assert.ok(result.changes.some(change => change.id === 'fill' && change.kind === 'create'));
});

test('health condition selects actual different colors and emits structural connection diff', () => {
  const b = builder();
  const low = b.node('gsi_player_health_condition', {}, { OP: '<', VALUE: 25 }, 'low');
  const red = b.node('key_fill_all', { COLOR: rgb(b, [255, 0, 0], 'red') }, {}, 'red_fill');
  const green = b.node('key_fill_all', { COLOR: rgb(b, [0, 255, 0], 'green') }, {}, 'green_fill');
  b.node('controls_if', { IF0: low, DO0: red, ELSE: green }, {}, 'health_if', { hasElse: true });
  const result = prepareGraphCandidate(empty(), b.ops), run = render(result.candidate);
  assert.deepEqual(run(0, { player: { state: { health: 10 } } })[0], [255, 0, 0]);
  assert.deepEqual(run(40, { player: { state: { health: 80 } } })[0], [0, 255, 0]);
  const changed = result.changes.find(item => item.id === 'red_fill');
  assert.equal(changed.after.parent, 'health_if'); assert.equal(changed.after.connection, 'DO0');
});

test('an existing effect gains a condition while retaining RGB identities and values', () => {
  const original = frozen(staticProject()), result = prepareGraphCandidate(original, [
    create('condition', 'controls_if'), create('health', 'gsi_player_health_condition', { OP: '>=', VALUE: 20 }),
    connect('condition', 'IF0', 'health'), connect('condition', 'DO0', 'fill')
  ], { preserveRgb: true });
  const run = render(result.candidate);
  assert.deepEqual(run(0, { player: { state: { health: 10 } } })[0], [0, 0, 0]);
  assert.deepEqual(run(40, { player: { state: { health: 100 } } })[0], [24, 80, 160]);
  assert.equal(result.changes.find(value => value.id === 'fill').before.parent, null);
  assert.equal(result.changes.find(value => value.id === 'fill').after.parent, 'condition');
});

test('preserveRgb rejects channel edits, deletion/replacement, and changed numeric dependencies', () => {
  const original = frozen(staticProject()), saved = JSON.stringify(original);
  for (const ops of [
    [{ op: 'set_field', id: 'rgb_R', field: 'NUM', value: 99 }],
    [{ op: 'delete', id: 'rgb' }, create('replacement', 'color_rgb'), connect('fill', 'COLOR', 'replacement')],
    [{ op: 'disconnect', id: 'rgb_R' }, { op: 'delete', id: 'rgb_R' }, create('different', 'math_number', { NUM: 90 }), connect('rgb', 'R', 'different')]
  ]) assert.throws(() => prepareGraphCandidate(original, ops, { preserveRgb: true }), /保留原 RGB/);
  assert.equal(JSON.stringify(original), saved);
});

test('preserveRgb allows animation wrapping without mutating original palette', () => {
  const original = frozen(staticProject()), b = builder();
  b.ops.push({ op: 'disconnect', id: 'rgb' });
  const phase = b.node('time_phase', { PERIOD: b.num(1000, 'period') }, {}, 'phase');
  b.node('color_brightness', { COLOR: 'rgb', SCALE: phase }, {}, 'brightness'); b.ops.push(connect('fill', 'COLOR', 'brightness'));
  const result = prepareGraphCandidate(original, b.ops, { preserveRgb: true }), run = render(result.candidate);
  assert.deepEqual(run(500)[0], [12, 40, 80]);
  const names = describeGraph(result.candidate).nodes.filter(value => value.type === 'color_rgb').map(value => value.id);
  assert.deepEqual(names, ['rgb']);
});

test('preserveRgb includes RGB variable assignments and their control-flow placement', () => {
  const b = builder(); b.ops.push({ op: 'create_variable', id: 'red_level', name: 'RedLevel' });
  const assignment = b.node('variables_set', { VALUE: b.num(20, 'level_value') }, { VAR: 'red_level' }, 'assignment');
  const color = b.node('color_rgb', { R: b.node('variables_get', {}, { VAR: 'red_level' }), G: b.num(40), B: b.num(80) }, {}, 'rgb');
  const fill = b.node('key_fill_all', { COLOR: color }, {}, 'fill'); b.chain(assignment, fill);
  const original = frozen(prepareGraphCandidate(empty(), b.ops).candidate);
  assert.throws(() => prepareGraphCandidate(original, [{ op: 'set_field', id: 'level_value', field: 'NUM', value: 99 }], { preserveRgb: true }), /保留原 RGB/);
  assert.throws(() => prepareGraphCandidate(original, [
    { op: 'disconnect', id: 'fill' }, connect('fill', 'next', 'assignment')
  ], { preserveRgb: true }), /保留原 RGB/);
});

test('preserveRgb refuses adding a replacement palette while keeping old colors hidden', () => {
  const original = frozen(staticProject()), b = builder();
  b.node('key_fill_all', { COLOR: rgb(b, [255, 0, 0], 'new_rgb') }, {}, 'replacement');
  b.ops.push(connect('fill', 'next', 'replacement'));
  assert.throws(() => prepareGraphCandidate(original, b.ops, { preserveRgb: true }), /RGB 调色板/);
  const swapped = builder(); swapped.ops.push({ op: 'disconnect', id: 'rgb' });
  swapped.node('key_fill_all', { COLOR: 'rgb' }, {}, 'irrelevant_fill');
  const hsv = swapped.node('color_hsv'); swapped.ops.push(connect('fill', 'COLOR', hsv));
  assert.throws(() => prepareGraphCandidate(original, swapped.ops, { preserveRgb: true }), /RGB 调色板|保留原 RGB/);
});

test('two independent ripple start variables compose within a real per-key loop', () => {
  const b = builder();
  b.ops.push({ op: 'create_variable', id: 'start_a', name: 'StartA' }, { op: 'create_variable', id: 'start_b', name: 'StartB' });
  const startA = b.node('variables_set', { VALUE: b.num(0) }, { VAR: 'start_a' }, 'init_a');
  const startB = b.node('variables_set', { VALUE: b.num(250) }, { VAR: 'start_b' }, 'init_b');
  const wave = (variable, center) => {
    const time = b.node('math_arithmetic', { A: b.node('time_elapsed_ms'), B: b.node('variables_get', {}, { VAR: variable }) }, { OP: 'MINUS' });
    const distance = b.node('geometry_distance', { X1: b.node('geometry_coords', {}, { FIELD: 'physical_x' }), Y1: b.node('geometry_coords', {}, { FIELD: 'physical_y' }), X2: b.num(center), Y2: b.num(2) });
    const phase = b.node('math_arithmetic', { A: distance, B: b.node('math_arithmetic', { A: time, B: b.num(1000) }, { OP: 'DIVIDE' }) }, { OP: 'MINUS' });
    return b.node('math_waveform', { PHASE: phase, AMPLITUDE: b.num(.25), OFFSET: b.num(.25) }, { TYPE: 'SINE' });
  };
  const combined = b.node('math_arithmetic', { A: wave('start_a', 3), B: wave('start_b', 7) }, { OP: 'ADD' });
  const color = b.node('color_brightness', { COLOR: rgb(b, [40, 160, 240]), SCALE: combined });
  const fill = b.node('key_set_color', { LED_ID: b.node('geometry_coords', {}, { FIELD: 'led_id' }), COLOR: color });
  const loop = b.node('key_for_each', { DO: fill }, { ZONE: 'all' }, 'keys'); b.chain(startA, startB, loop);
  assert.ok(b.ops.length <= 256);
  const result = prepareGraphCandidate(empty(), b.ops), run = render(result.candidate);
  const first = structuredClone(run(0)), later = structuredClone(run(250));
  assert.notDeepEqual(first, later); assert.equal(first.length, 68);
  assert.equal(result.candidate.json.variables.length, 2);
  const shifted = prepareGraphCandidate(result.candidate, [{ op: 'set_field', id: describeGraph(result.candidate).nodes.find(node => node.parent === 'init_b' && node.connection === 'VALUE').id, field: 'NUM', value: 500 }]);
  assert.notDeepEqual(render(shifted.candidate)(0), first);
});

test('logical key condition is a genuine loop input and responds to simulated key press', () => {
  const b = builder();
  const set = b.node('key_set_color', { LED_ID: b.node('geometry_coords', {}, { FIELD: 'led_id' }), COLOR: rgb(b, [255, 80, 10]) });
  const condition = b.node('controls_if', { IF0: b.node('key_is_pressed'), DO0: set });
  b.node('key_for_each', { DO: condition }, { ZONE: 'all' });
  const result = prepareGraphCandidate(empty(), b.ops), run = render(result.candidate);
  const frame = run(0, {}, { W: 1 }); const index = PREVIEW_68_KEYS.findIndex(key => key.name === 'W');
  assert.deepEqual(frame[index], [255, 80, 10]); assert.deepEqual(frame[0], [0, 0, 0]);
});

test('connection types, cycles, occupied sockets and incomplete programs reject whole candidate', () => {
  const original = frozen(staticProject()), before = JSON.stringify(original);
  for (const ops of [
    [create('n', 'math_number'), connect('fill', 'COLOR', 'n')],
    [create('if', 'controls_if'), connect('if', 'DO0', 'if')],
    [create('dangling', 'math_number')],
    [create('n', 'color_rgb'), connect('fill', 'COLOR', 'n')],
    [{ op: 'disconnect', id: 'rgb' }, create('n', 'math_number'), connect('fill', 'COLOR', 'n')],
    [{ op: 'set_field', id: 'rgb_R', field: 'NUM', value: 7 }, { op: 'delete', id: 'missing' }]
  ]) assert.throws(() => prepareGraphCandidate(original, ops));
  assert.equal(JSON.stringify(original), before);
});

test('reorder only accepts exact connected statement chain and changes executed order', () => {
  const b = builder();
  const red = b.node('key_fill_all', { COLOR: rgb(b, [255, 0, 0], 'red') }, {}, 'red_fill');
  const blue = b.node('key_fill_all', { COLOR: rgb(b, [0, 0, 255], 'blue') }, {}, 'blue_fill');
  b.chain(red, blue);
  b.node('controls_repeat_ext', { TIMES: b.num(1), DO: red }, {}, 'repeat');
  const original = prepareGraphCandidate(empty(), b.ops).candidate;
  assert.deepEqual(render(original)(0)[0], [0, 0, 255]);
  const result = prepareGraphCandidate(original, [{ op: 'reorder', parent: 'repeat', slot: { kind: 'input', name: 'DO' }, ids: ['blue_fill', 'red_fill'] }]);
  assert.deepEqual(render(result.candidate)(0)[0], [255, 0, 0]);
  assert.throws(() => prepareGraphCandidate(original, [{ op: 'reorder', parent: 'repeat', slot: { kind: 'input', name: 'DO' }, ids: ['red_fill'] }]));
});

test('stable legacy IDs, explicit coordinates, strict fields and mutator allowlists', () => {
  const legacy = { ...empty(), json: { blocks: [{ type: 'key_fill_all' }] } };
  const first = describeGraph(legacy), second = describeGraph(legacy); assert.deepEqual(first, second);
  const moved = prepareGraphCandidate(legacy, [{ op: 'move', id: first.nodes[0].id, x: 150, y: 300 }]);
  assert.equal(moved.changes[0].after.x, 150); assert.equal(moved.changes[0].after.y, 300);
  for (const ops of [
    [create('bad', 'controls_if', {}, { elseIfCount: 100 })],
    [create('bad', 'key_fill_all', {}, { hasElse: true })],
    [create('bad', 'key_for_each', { ZONE: 'untrusted' })],
    [create('bad', 'variables_get', { VAR: 'missing' })],
    [create('bad', 'gsi_player_health_condition', { VALUE: 99999 })],
    [{ op: 'create_variable', id: 'variable', name: 'Alpha' }, { op: 'create_variable', id: 'other', name: 'Alpha' }],
    [create('fill', 'key_fill_all'), { op: 'delete', id: 'fill' }, create('fill', 'key_fill_all')]
  ]) assert.throws(() => prepareGraphCandidate(empty(), ops));
  assert.equal(GRAPH_OPERATION_SCHEMA.maxItems, 256);
});

test('delete removes explicit subtree while disconnect and reconnect preserve identities', () => {
  const original = frozen(staticProject());
  const result = prepareGraphCandidate(original, [{ op: 'delete', id: 'fill' }]);
  assert.equal(describeGraph(result.candidate).nodes.length, 0);
  assert.equal(result.changes.filter(item => item.kind === 'delete').length, 5);
  const b = builder(); b.ops.push({ op: 'disconnect', id: 'rgb' });
  b.node('color_brightness', { COLOR: 'rgb', SCALE: b.num(.5) }, {}, 'half'); b.ops.push(connect('fill', 'COLOR', 'half'));
  const connected = prepareGraphCandidate(original, b.ops, { preserveRgb: true });
  assert.equal(describeGraph(connected.candidate).nodes.find(item => item.id === 'rgb').parent, 'half');
  assert.deepEqual(render(connected.candidate)(0)[0], [12, 40, 80]);
});

test('operations are deterministic and do not access network or publish authority', () => {
  const original = frozen({ ...staticProject(), applied_plugin_name: 'existing_generation' });
  const oldFetch = globalThis.fetch;
  globalThis.fetch = () => { throw new Error('Network is forbidden in graph tools'); };
  try {
    const operations = [{ op: 'set_field', id: 'rgb_R', field: 'NUM', value: 25 }];
    const a = prepareGraphCandidate(original, operations), b = prepareGraphCandidate(original, operations);
    assert.deepEqual(a, b); assert.equal(a.candidate.applied_plugin_name, 'existing_generation');
    assert.equal(original.json.blocks.blocks[0].inputs.COLOR.block.inputs.R.block.fields.NUM, 24);
  } finally { globalThis.fetch = oldFetch; }
});

test('bounded schemas reject executable/prototype payloads without modifying events', () => {
  const oldGroup = Blockly.Events.getGroup(), oldUndo = Blockly.Events.getRecordUndo(), oldEnabled = Blockly.Events.isEnabled();
  for (const value of [
    Array.from({ length: 257 }, (_, index) => create(`n${index}`, 'key_fill_all')),
    [{ op: 'shell', command: 'execute' }], [{ op: 'create', id: 'x', type: 'key_fill_all', code: 'arbitrary' }],
    JSON.parse('[{"op":"create","id":"x","type":"key_fill_all","__proto__":{}}]'),
    [{ op: 'set_field', id: 'x', field: 'NUM', value: Infinity }]
  ]) assert.throws(() => parseGraphOperations(value));
  assert.equal(Blockly.Events.getGroup(), oldGroup); assert.equal(Blockly.Events.getRecordUndo(), oldUndo); assert.equal(Blockly.Events.isEnabled(), oldEnabled);
  const before = staticProject(); Blockly.Events.disable();
  try { assert.throws(() => prepareGraphCandidate(before, [{ op: 'delete', id: 'unknown' }])); assert.equal(Blockly.Events.isEnabled(), false); }
  finally { Blockly.Events.enable(); }
});
