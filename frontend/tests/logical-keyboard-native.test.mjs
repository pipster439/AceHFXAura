import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
import Blockly, { loadSafeWorkspaceJson } from '../src/blockly/index.js';
import { CppTranspiler } from '../src/blockly/cppTranspiler.js';
import { JsTranspiler, PREVIEW_68_KEYS } from '../src/blockly/jsTranspiler.js';
import { EFFECT_PRESETS } from '../src/blockly/presets.js';
import { registerCustomBlocks } from '../src/blockly/customBlocks.js';
registerCustomBlocks();

test('generated native reactive plugin reads one logical snapshot per frame with bounded existing color quantization', t => {
  const repo = fileURLToPath(new URL('../../', import.meta.url));
  const workspace = new Blockly.Workspace();
  loadSafeWorkspaceJson(EFFECT_PRESETS.find(p => p.id === 'template_reactive').blocklyJson, workspace, true);
  const source = CppTranspiler.transpile('keyboard_fixture', workspace);
  const run = JsTranspiler.compile(workspace);
  const w = PREVIEW_68_KEYS.findIndex(k => k.name === 'W');
  const inputs = [
    { at: 100, available: true, held: true },
    { at: 200, available: true, held: false, up: 200 },
    { at: 1200, available: true, held: false, up: 200 },
    { at: 1240, available: false, held: true },
  ];
  const expected = inputs.map(input => run(input.at, {}, { schema_version: 1, source_available: input.available,
    observed_at_ms: input.at, keys: { W: { held: input.held, released_at_ms: input.up } } })[w]);
  assert.deepEqual(expected[0], [0, 180, 255]);
  assert.deepEqual(expected[1], [0, 180, 255]);
  assert.ok(expected[2][2] > 0 && expected[2][2] < 255);
  assert.deepEqual(expected[3], [0, 0, 0]);
  workspace.dispose();
  const compiler = process.env.AURA_TEST_CXX || 'cl';
  try { execFileSync(compiler, [], { stdio: 'ignore' }); }
  catch { t.skip('MSVC developer environment is required for actual generated DLL input verification'); return; }
  const root = path.join(repo, 'audit_repros', 'studio_platform_build', 'generated-keyboard');
  fs.mkdirSync(root, { recursive: true });
  const dir = fs.mkdtempSync(path.join(root, 'run-'));
  const cpp = path.join(dir, 'run.cpp'), binary = path.join(dir, 'run.exe');
  const harness = `
#include <cassert>
#include <cstring>
AuraLogicalKeyboardSnapshotV1 input{};
unsigned reads = 0;
uint32_t AURA_PLUGIN_CALL Read(void*, AuraLogicalKeyboardSnapshotV1* out, uint32_t size) {
 assert(size == sizeof(input)); ++reads; *out = input; return 1;
}
int main(int argc, char** argv) {
 aura::Keymap keymap; assert(argc == 2 && keymap.LoadFromJson(argv[1])); int led = -1; assert(keymap.FindLedId("W", led));
 aura::Effect_keyboard_fixture effect; aura::FrameBuffer frame;
 bool refused = false; try { effect.Render(0, frame, keymap); } catch (const std::runtime_error&) { refused = true; } assert(refused);
 AuraLogicalKeyboardHostV1 host{sizeof(host), 1, nullptr, Read};
 assert(AuraBindLogicalKeyboard(&effect, &host) == 1);
 input.struct_size = sizeof(input); input.version = 1; input.source_epoch = 1; input.count = 1;
 std::strcpy(input.keys[0].name, "W");
 ${inputs.map((value, i) => `input.observed_at_ms = ${value.at}; input.source_available = ${value.available ? 1 : 0}; input.keys[0].held = ${value.held ? 1 : 0}; input.keys[0].has_release = ${value.up ? 1 : 0}; input.keys[0].last_up_ms = ${value.up || 0};
 effect.Render(${value.at}, frame, keymap); assert(reads == ${i + 1});
 // Existing JS colors round while native colors truncate; input/time parity is within one channel unit.
 assert(std::abs(int(frame.buffer[led*3]) - ${expected[i][0]}) <= 1 && std::abs(int(frame.buffer[led*3+1]) - ${expected[i][1]}) <= 1 && std::abs(int(frame.buffer[led*3+2]) - ${expected[i][2]}) <= 1);`).join('\n')}
}
`;
  fs.writeFileSync(cpp, source + harness);
  try {
    execFileSync(compiler, ['/nologo', '/std:c++17', '/EHsc', '/utf-8', '/I' + path.join(repo, 'include'),
      cpp, path.join(repo, 'src/aura/keymap.cpp'), '/Fe:' + binary, '/Fo:' + dir + path.sep], { cwd: dir, stdio: 'pipe' });
    execFileSync(binary, [path.join(repo, 'calibrated_keymap.json')], { cwd: dir, stdio: 'pipe' });
  } catch (error) { throw new Error(`Generated logical input C++ test failed:\n${error.stdout || ''}\n${error.stderr || ''}`); }
});
