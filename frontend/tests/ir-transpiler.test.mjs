import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
import Blockly, { loadSafeWorkspaceJson } from '../src/blockly/index.js';
import { registerCustomBlocks } from '../src/blockly/customBlocks.js';
import { EFFECT_PRESETS } from '../src/blockly/presets.js';
import { IrTranspiler } from '../src/blockly/irTranspiler.js';
import { JsTranspiler, PREVIEW_68_KEYS } from '../src/blockly/jsTranspiler.js';
registerCustomBlocks();
const repo = fileURLToPath(new URL('../../', import.meta.url));

test('preview centers and logical rows/columns exactly match every calibrated key', () => {
  const calibrated = JSON.parse(fs.readFileSync(path.join(repo, 'calibrated_keymap.json'), 'utf8')).keys;
  assert.equal(PREVIEW_68_KEYS.length, 68);
  for (const preview of PREVIEW_68_KEYS) {
    const actual = calibrated[preview.name];
    assert.ok(actual, preview.name);
    assert.deepEqual([preview.physical_x, preview.physical_y, preview.row, preview.col],
      [actual.physical_x, actual.physical_y, actual.physical_row, actual.physical_col], preview.name);
  }
  for (const preview of PREVIEW_68_KEYS) assert.equal(preview.led_id, calibrated[preview.name].led_id, preview.name);
  assert.notEqual(PREVIEW_68_KEYS.findIndex(key => key.name === 'W'), calibrated.W.led_id,
    'preview array offsets and hardware frame LED IDs are distinct');
});

test('explicit LED ID selects the same named calibrated key in JS and native IR', t => {
  const binary = process.env.AURA_IR_FIXTURE;
  if (!binary || !fs.existsSync(binary)) { t.skip('Set AURA_IR_FIXTURE for native LED mapping'); return; }
  const calibrated = JSON.parse(fs.readFileSync(path.join(repo, 'calibrated_keymap.json'), 'utf8')).keys;
  const ws = new Blockly.Workspace(), n = value => ({ block: { type: 'math_number', fields: { NUM: value } } });
  try {
    loadSafeWorkspaceJson({ blocks: { languageVersion: 0, blocks: [{ type: 'key_set_color', inputs: {
      LED_ID: n(calibrated.W.led_id), COLOR: { block: { type: 'color_rgb', inputs: { R: n(123), G: n(45), B: n(6) } } }
    } }] } }, ws, true);
    const program = IrTranspiler.transpile(ws), js = JsTranspiler.compile(ws)(0, {}, {});
    assert.deepEqual(js[PREVIEW_68_KEYS.findIndex(key => key.name === 'W')], [123, 45, 6]);
    assert.deepEqual(js[PREVIEW_68_KEYS.findIndex(key => key.name === 'E')], [0, 0, 0]);
    const root = path.join(repo, 'audit_repros', 'studio_platform_build', 'ir-fixtures'); fs.mkdirSync(root, { recursive: true });
    const file = path.join(root, 'explicit_led.json'); fs.writeFileSync(file, JSON.stringify({ program, frames: [{ time_ms: 0 }] }));
    const output = execFileSync(binary, ['--fixture', file, path.join(repo, 'calibrated_keymap.json')], { encoding: 'utf8', cwd: root });
    const native = JSON.parse(output.split(/\r?\n/).find(line => line.startsWith('AURA_IR_RESULT ')).slice(15));
    for (const key of native.frames[0].keys) assert.deepEqual(key.rgb, js[PREVIEW_68_KEYS.findIndex(preview => preview.name === key.name)], key.name);
  } finally { ws.dispose(); }
});

test('every supported preset produces bounded data from the shared control-flow plan', () => {
  for (const preset of EFFECT_PRESETS) {
    const ws = new Blockly.Workspace();
    try {
      loadSafeWorkspaceJson(preset.blocklyJson, ws, true);
      const program = IrTranspiler.transpile(ws);
      assert.equal(program.schema_version, 1);
      assert.ok(program.nodes.length <= 2048 && program.nodes[program.entry]);
      assert.ok(!JSON.stringify(program).includes('#include'));
      assert.deepEqual(program, IrTranspiler.transpile(ws));
    } finally { ws.dispose(); }
  }
});

test('real native IR interpreter renders every existing preset against JS deterministic frames', t => {
  const binary = process.env.AURA_IR_FIXTURE;
  if (!binary || !fs.existsSync(binary)) { t.skip('Set AURA_IR_FIXTURE to the freshly built test_effect_program executable'); return; }
  const root = path.join(repo, 'audit_repros', 'studio_platform_build', 'ir-fixtures');
  fs.mkdirSync(root, { recursive: true });
  for (const preset of EFFECT_PRESETS) {
    const ws = new Blockly.Workspace();
    try {
      loadSafeWorkspaceJson(preset.blocklyJson, ws, true);
      const program = IrTranspiler.transpile(ws), simulate = JsTranspiler.compile(ws);
      const frames = [0, 40, 100, 1000].map(time_ms => ({ time_ms,
        gsi: { player: { state: { health: 20, round_kills: 1 } }, round: { bomb: 'planted', phase: 'live' } },
        keyboard: { available: true, observed_at_ms: time_ms, keys: [{ name: 'W', held: time_ms < 100, has_release: time_ms >= 100, last_up_ms: 100 }] } }));
      const file = path.join(root, preset.id + '.json'); fs.writeFileSync(file, JSON.stringify({ program, frames }));
      const output = execFileSync(binary, ['--fixture', file, path.join(repo, 'calibrated_keymap.json')], { encoding: 'utf8', cwd: root });
      const result = JSON.parse(output.split(/\r?\n/).find(line => line.startsWith('AURA_IR_RESULT ')).slice(15));
      assert.equal(result.frames.length, frames.length);
      for (let i = 0; i < frames.length; i++) {
        const frame = frames[i];
        const js = simulate(frame.time_ms, frame.gsi, { schema_version: 1, source_available: true, observed_at_ms: frame.time_ms,
          keys: { W: { held: frame.time_ms < 100, released_at_ms: frame.time_ms >= 100 ? 100 : undefined } } });
        assert.equal(result.frames[i].status.faulted, false, `${preset.id}: ${JSON.stringify(result.frames[i].status)}`);
        assert.equal(result.frames[i].keys.length, 68);
        for (const nativeKey of result.frames[i].keys) for (let channel = 0; channel < 3; channel++) {
          const previewIndex = PREVIEW_68_KEYS.findIndex(key => key.name === nativeKey.name);
          assert.ok(previewIndex >= 0, nativeKey.name);
          const actual = nativeKey.rgb[channel];
          assert.ok(Math.abs(actual - js[previewIndex][channel]) <= 2, `${preset.id} t=${frame.time_ms} key=${nativeKey.name} c=${channel} native=${actual} js=${js[previewIndex][channel]}`);
        }
      }
    } finally { ws.dispose(); }
  }
});
