import test from 'node:test';
import assert from 'node:assert/strict';
import { registerCustomBlocks } from '../src/blockly/customBlocks.js';
import { EFFECT_PRESETS } from '../src/blockly/presets.js';
import { PREVIEW_68_KEYS } from '../src/blockly/jsTranspiler.js';
import { BUILTIN_SCENARIOS, parseScenario, exportScenario, StudioTestBench, compileBenchProject } from '../src/utils/studioTestBench.js';
registerCustomBlocks();
const project = id => ({ name: 'fixture', publication: { mode: 'continuous', fade_out_ms: 0 }, json: structuredClone(EFFECT_PRESETS.find(p => p.id === id).blocklyJson) });
const sample = s => new StudioTestBench(project('template_reactive'), s);
const w = PREVIEW_68_KEYS.findIndex(k => k.name === 'W');
const basic = () => ({ schema_version: 1, name: 'fixture', duration_ms: 240, events: [] });

test('parser and JSON export round-trip preserve normalized scenario and initial state', () => {
  for (const s of BUILTIN_SCENARIOS) assert.deepEqual(parseScenario(exportScenario(s)), s);
  assert.equal(parseScenario(basic()).frame_ms, 40);
});
test('parser rejects unknown types/fields negative unsorted excessive timing paths/code and secrets', () => {
  const invalid = [
    { ...basic(), schema_version: 2 }, { ...basic(), duration_ms: -1 }, { ...basic(), duration_ms: 60001 },
    { ...basic(), events: [{ at_ms: 0, type: 'shell', code: 'exec' }] },
    { ...basic(), api_key: 'fixture' }, { ...basic(), name: '../file' }, { ...basic(), frame_ms: 1 },
    { ...basic(), events: [{ at_ms: -1, type: 'timeline' }] },
    { ...basic(), events: [{ at_ms: 40, type: 'timeline' }, { at_ms: 0, type: 'reset' }] },
    { ...basic(), events: Array.from({ length: 513 }, () => ({ at_ms: 0, type: 'reset' })) },
    { ...basic(), events: [{ at_ms: 0, type: 'foreground_process', process: 'C:\\local\\app.exe' }] },
    { ...basic(), events: [{ at_ms: 0, type: 'gsi_patch', values: { 'event.kill': true } }] },
    { ...basic(), events: [{ at_ms: 0, type: 'gsi_patch', values: { 'player.state.health': 101 } }] },
    { ...basic(), events: [{ at_ms: 0, type: 'key_down', key: 'unknown', path: '/tmp/x' }] },
    { ...basic(), assertions: [{ type: 'execute' }] }
  ];
  for (const s of invalid) assert.throws(() => parseScenario(s));
  assert.throws(() => parseScenario('{')); assert.throws(() => parseScenario(' '.repeat(65537)));
});
test('explicit frame schedule includes boundary/event times and yields deterministic count', () => {
  const bench = sample({ ...basic(), events: [{ at_ms: 15, type: 'timeline' }], assertions: [{ type: 'frame_count', value: 8 }, { type: 'completes' }] });
  assert.deepEqual(bench.times, [0, 15, 40, 80, 120, 160, 200, 240]);
  const result = bench.run(); assert.equal(result.passed, true); assert.equal(result.frame_count, 8);
  assert.deepEqual(bench.step(), result.frame ? bench.current : null);
});
test('key tap samples existing reactive renderer held then released and remains local', () => {
  const bench = sample({ ...basic(), events: [{ at_ms: 80, type: 'key_down', key: 'W' }, { at_ms: 160, type: 'key_up', key: 'W' }], assertions: [
    { type: 'sample', at_ms: 80, key: 'W', rgb: [0, 180, 255] }, { type: 'sample', at_ms: 160, key: 'W', rgb: [0, 0, 0] },
    { type: 'state', at_ms: 80, field: 'key:W', value: true }, { type: 'state', at_ms: 160, field: 'key:W', value: false }
  ] });
  const old = globalThis.fetch; globalThis.fetch = () => { throw new Error('No transport permitted'); };
  try { assert.equal(bench.run().passed, true); } finally { globalThis.fetch = old; }
});
test('hold/release and rapid repeated presses have stable existing-key output', () => {
  for (const scenario of [BUILTIN_SCENARIOS[1], BUILTIN_SCENARIOS[2]]) {
    const bench = sample(scenario), states = [bench.current];
    while (!bench.current.completed) states.push(bench.step());
    for (const s of states) assert.deepEqual(s.frame[w], s.held_keys.includes('W') ? [0, 180, 255] : [0, 0, 0]);
    assert.equal(states.at(-1).held_keys.length, 0);
  }
});
test('GSI health state changes drive the actual health preset', () => {
  const bench = new StudioTestBench(project('template_low_health_warning'), BUILTIN_SCENARIOS[3]);
  const frames = [];
  while (!bench.current.completed) { const value = bench.step(); if ([400, 800].includes(value.time_ms)) frames.push(value); }
  assert.equal(frames[0].gsi.player.state.health, 50); assert.deepEqual(frames[0].frame[w], [0, 120, 255]);
  assert.equal(frames[1].gsi.player.state.health, 10); assert.notDeepEqual(frames[1].frame[w], frames[0].frame[w]);
});
test('kill scenario uses existing round_kills state, never legacy event pulse', () => {
  const bench = new StudioTestBench(project('kill_wave'), BUILTIN_SCENARIOS[4]);
  assert.equal(bench.run().gsi.player.state.round_kills, 2);
  assert.ok(!exportScenario(BUILTIN_SCENARIOS[4]).includes('event.'));
});
test('foreground A to B state is deterministic without inventing Effect inputs', () => {
  const bench = sample(BUILTIN_SCENARIOS[5]); assert.equal(bench.current.foreground_process, 'app_a.exe');
  assert.equal(bench.run().foreground_process, 'app_b.exe'); assert.ok(bench.results.every(r => r.passed));
});
test('repeated runs and loop reset have identical frames, assertions and state', () => {
  const input = project('template_smooth_breath'), scenario = BUILTIN_SCENARIOS[3];
  const collect = bench => { const frames = [structuredClone(bench.current)]; while (!bench.current.completed) frames.push(bench.step()); return frames; };
  const first = new StudioTestBench(input, scenario); const before = structuredClone(input); const a = collect(first);
  first.reset(); assert.deepEqual(collect(first), a); assert.deepEqual(collect(new StudioTestBench(input, scenario)), a); assert.deepEqual(input, before);
});
test('pause/step never read wall clock; reset events reinitialize existing renderer', () => {
  const bench = sample({ ...basic(), events: [{ at_ms: 0, type: 'key_down', key: 'W' }, { at_ms: 80, type: 'reset' }] });
  const old = Date.now; Date.now = () => { throw new Error('Wall clock must not advance simulation'); };
  try { assert.equal(bench.step().time_ms, 40); assert.deepEqual(bench.current.frame[w], [0, 180, 255]); }
  finally { Date.now = old; }
  assert.equal(bench.step().time_ms, 80); assert.deepEqual(bench.current.frame[w], [0, 0, 0]);
});
test('existing effect simulation parity and failure assertions remain explicit', () => {
  for (const preset of EFFECT_PRESETS) {
    const input = project(preset.id), renderer = compileBenchProject(input), bench = new StudioTestBench(input, basic());
    const check = value => assert.deepEqual(value.frame, renderer(value.time_ms, value.gsi, {}), preset.id);
    check(bench.current); while (!bench.current.completed) check(bench.step());
  }
  const bench = sample({ ...basic(), assertions: [{ type: 'sample', at_ms: 0, key: 'W', rgb: [255, 0, 0] }] });
  assert.equal(bench.run().passed, false);
  assert.throws(() => sample({ ...basic(), initial_state: { held_keys: ['bad'], gsi: {}, foreground_process: 'app.exe' } }));
});
