import test from 'node:test';
import assert from 'node:assert/strict';
import { deriveEffectCapabilities as derive, recommendStudioScenarios } from '../src/utils/effectCapabilities.js';
import { EFFECT_PRESETS } from '../src/blockly/presets.js';
import { assistantContext } from '../src/utils/studioAssistant.js';
import { StudioTestBench, BUILTIN_SCENARIOS } from '../src/utils/studioTestBench.js';
import { registerCustomBlocks } from '../src/blockly/customBlocks.js';
registerCustomBlocks();
const project = (...blocks) => ({ name: 'fixture', json: { languageVersion: 0, blocks }, publication: { mode: 'continuous', fade_out_ms: 0 } });
test('old project graph derives on load and stale manifest cannot become authority', () => {
  const old = { blockly_json: EFFECT_PRESETS[0].blocklyJson, manifest: { inputs: ['hid_write'] } };
  assert.deepEqual(derive(old), derive(EFFECT_PRESETS[0].blocklyJson)); assert.deepEqual(derive(old).inputs, ['time']);
});
test('keyboard-only graph and preset/gallery metadata come from semantics', () => {
  const reactive = EFFECT_PRESETS.find(p => p.id === 'template_reactive');
  assert.deepEqual(reactive.manifest.inputs, ['keyboard']); assert.deepEqual(reactive.manifest.outputs, ['keyboard_rgb']);
  assert.ok(reactive.manifest.features.includes('event_driven')); assert.ok(reactive.manifest.features.includes('stateful'));
  assert.deepEqual(reactive.requiredInputs, ['keyboard']);
  for (const preset of EFFECT_PRESETS) assert.deepEqual(preset.manifest, derive(preset.blocklyJson));
});
test('GSI dependency includes only used canonical paths, health and C4 shortcuts', () => {
  const manifest = derive(project({ type: 'gsi_get_number', fields: { PATH: 'player.state.health' } }, { type: 'gsi_state_match', fields: { STATE: 'round.phase' } }, { type: 'gsi_c4_state_condition' }));
  assert.deepEqual(manifest.inputs, ['cs2_gsi']); assert.deepEqual(manifest.gsi_fields, ['player.state.health', 'round.bomb', 'round.phase']);
});
test('existing foreground block is identified with a clear Effect/Automation boundary', () => {
  const p = project({ type: 'orch_current_process' }); const manifest = derive(p);
  assert.deepEqual(manifest.inputs, ['foreground_process']); assert.deepEqual(manifest.diagnostics, ['foreground_automation_only']);
  assert.ok(!manifest.features.includes('simulation_supported')); assert.deepEqual(recommendStudioScenarios(manifest), ['前台进程切换']);
  assert.throws(() => new StudioTestBench(p, BUILTIN_SCENARIOS[5]), /自动化工作室/);
});
test('mixed keyboard GSI and time graph derives deterministic sorted metadata', () => {
  const p = project({ type: 'key_is_pressed' }, { type: 'gsi_player_health_condition' }, { type: 'time_elapsed_ms' });
  assert.deepEqual(derive(p).inputs, ['cs2_gsi', 'keyboard', 'time']); assert.deepEqual(derive(p), derive(structuredClone(p)));
});
test('graph-only derivation ignores incidental text and checks unsupported declarations', () => {
  const p = project({ type: 'text', fields: { TEXT: 'gsi_get_number key_decay orch_current_process' } });
  assert.deepEqual(derive(p).inputs, []); p.json.declared_capabilities = ['hid_write']; assert.deepEqual(derive(p).diagnostics, ['unsupported_declaration']);
  assert.deepEqual(derive(project({ type: 'native_shell' })).diagnostics, ['unsupported_block']);
  assert.deepEqual(derive(project({ type: 'gsi_get_boolean', fields: { PATH: 'event.kill' } })).diagnostics, ['legacy_event_pulse']);
});
test('inactive fallback shadows do not add false dependencies to the actual graph', () => {
  const p = project({ type: 'key_fill_all', inputs: { COLOR: { block: { type: 'color_rgb' }, shadow: { type: 'gsi_get_number', fields: { PATH: 'player.state.health' } } } } });
  assert.deepEqual(derive(p).inputs, []); assert.deepEqual(derive(p).gsi_fields, []);
});
test('AI modify/explain context filters GSI fields and unrelated GSI presets', () => {
  const p = { ...project(), json: EFFECT_PRESETS.find(x => x.id === 'template_reactive').blocklyJson };
  for (const intent of ['modify', 'explain', 'error_analysis']) {
    const c = assistantContext(p, intent); assert.deepEqual(c.capabilities.gsi_fields, []);
    assert.ok(c.presets.every(x => !x.required_inputs.includes('cs2_gsi'))); assert.ok(!JSON.stringify(c).includes('player.state'));
  }
  assert.equal(assistantContext(p, 'generate', '', 'validation', EFFECT_PRESETS[0].id).presets.length, 1);
});
test('Test Bench recommends input-relevant built-ins and retains custom field compatibility', () => {
  assert.deepEqual(recommendStudioScenarios(derive(project({ type: 'key_decay' }))), ['单键轻按', '按住后释放', '快速重复按键']);
  assert.deepEqual(recommendStudioScenarios(derive(project({ type: 'gsi_get_number', fields: { PATH: 'player.state.round_kills' } }))), ['CS2 本回合击杀状态']);
  const custom = project({ type: 'gsi_get_number', fields: { PATH: 'custom.future' } }); const before = structuredClone(custom);
  assert.deepEqual(derive(custom).diagnostics, ['unsupported_gsi_field']); assert.deepEqual(custom, before);
});
test('all current effect presets remain accepted by the existing simulation path', () => {
  for (const p of EFFECT_PRESETS) { assert.ok(p.manifest.features.includes('simulation_supported'), p.id); assert.ok(new StudioTestBench({ ...project(), json: p.blocklyJson }, BUILTIN_SCENARIOS[0]).run().passed); }
});
