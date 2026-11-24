import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
import Blockly, { loadSafeWorkspaceJson } from '../src/blockly/index.js';
import { registerCustomBlocks } from '../src/blockly/customBlocks.js';
import { validateCandidate } from '../src/utils/studioAssistant.js';
import { CppTranspiler } from '../src/blockly/cppTranspiler.js';
import { JsTranspiler } from '../src/blockly/jsTranspiler.js';
import { EFFECT_PRESETS } from '../src/blockly/presets.js';
import { effectVariableSymbol } from '../src/blockly/workspaceSafety.js';
registerCustomBlocks();

const publication = { mode: 'continuous', fade_out_ms: 0 };
const number = value => ({ type: 'math_number', fields: { NUM: value } });
const project = json => ({ name: 'compiler_fixture', publication, json });

function connectedWorkspace(json) {
  const workspace = new Blockly.Workspace();
  try { loadSafeWorkspaceJson(json, workspace, true); }
  catch (error) { workspace.dispose(); throw error; }
  return workspace;
}

test('Blockly 13 variable map declares persistent numeric variables in both compilers', () => {
  const json = { variables: [{ id: 'counter-id', name: 'counter', type: '' }], blocks: { blocks: [
    { type: 'variables_set', fields: { VAR: { id: 'counter-id' } }, inputs: { VALUE: { block: number(7) } },
      next: { block: { type: 'key_set_color', inputs: { LED_ID: { block: number(0) }, COLOR: { block: { type: 'color_rgb' } } } } } }
  ] } };
  const workspace = connectedWorkspace(json);
  try {
    const cpp = CppTranspiler.transpile('compiler_fixture', workspace);
    const symbol = effectVariableSymbol(workspace.getVariableMap().getVariableById('counter-id'));
    assert.ok(cpp.includes(`double ${symbol} = 0.0;`));
    assert.ok(cpp.includes(`${symbol} = static_cast<double>(7)`));
    const run = JsTranspiler.compile(workspace);
    run(0);
    run(40);
    assert.equal(globalThis[symbol], undefined);
  } finally { workspace.dispose(); }
});

test('numeric Blockly variables can be emitted in expressions without global writes', () => {
  const json = { variables: [{ id: 'level-id', name: 'level', type: '' }], blocks: { blocks: [
    { type: 'variables_set', fields: { VAR: { id: 'level-id' } }, inputs: { VALUE: { block: number(91) } } },
    { type: 'key_fill_all', inputs: { COLOR: { block: { type: 'color_rgb', inputs: {
      R: { block: { type: 'variables_get', fields: { VAR: { id: 'level-id' } } } }, G: { block: number(0) }, B: { block: number(0) }
    } } } } }
  ] } };
  const workspace = connectedWorkspace(json);
  try {
    const symbol = effectVariableSymbol(workspace.getVariableMap().getVariableById('level-id'));
    assert.ok(CppTranspiler.transpile('compiler_fixture', workspace).includes(`double ${symbol} = 0.0;`));
    const run = JsTranspiler.compile(workspace);
    assert.equal(run(0)[0][0], 91);
    assert.equal(globalThis[symbol], undefined);
  } finally { workspace.dispose(); }
});

test('numeric variables persist per renderer and reset with the deterministic clock', () => {
  const json = { variables: [{ id: 'counter-id', name: '计数', type: '' }], blocks: { blocks: [
    { type: 'math_change', fields: { VAR: { id: 'counter-id' } }, inputs: { DELTA: { block: number(1) } }, next: { block:
      { type: 'key_fill_all', inputs: { COLOR: { block: { type: 'color_rgb', inputs: {
        R: { block: { type: 'variables_get', fields: { VAR: { id: 'counter-id' } } } }, G: { block: number(0) }, B: { block: number(0) }
      } } } } }
    } }
  ] } };
  const workspace = connectedWorkspace(json);
  try {
    const first = JsTranspiler.compile(workspace), second = JsTranspiler.compile(workspace);
    assert.equal(first(0)[0][0], 1);
    assert.equal(first(40)[0][0], 2);
    assert.equal(second(0)[0][0], 1);
    assert.equal(first(0)[0][0], 1);
  } finally { workspace.dispose(); }
});

test('real generated C++ compiles and executes numeric variable frames', t => {
  const candidates = process.env.AURA_TEST_CXX ? [process.env.AURA_TEST_CXX] : process.platform === 'win32' ? ['cl'] : ['g++', 'clang++'];
  const compiler = candidates.find(candidate => {
    const isMsvc = /(^|[\\/])cl(?:\.exe)?$/i.test(candidate);
    try { execFileSync(candidate, isMsvc ? [] : ['--version'], { stdio: 'ignore' }); return true; }
    catch { return false; }
  });
  if (!compiler) { t.skip('No C++ compiler available for generated-source runtime verification'); return; }
  const json = { variables: [{ id: 'counter-id', name: '计数', type: '' }], blocks: { blocks: [
    { type: 'math_change', fields: { VAR: { id: 'counter-id' } }, inputs: { DELTA: { block: number(1) } }, next: { block:
      { type: 'key_fill_all', inputs: { COLOR: { block: { type: 'color_rgb', inputs: {
        R: { block: { type: 'variables_get', fields: { VAR: { id: 'counter-id' } } } }, G: { block: number(0) }, B: { block: number(0) }
      } } } } }
    } }
  ] } };
  const repo = fileURLToPath(new URL('../../', import.meta.url));
  const root = path.join(repo, 'audit_artifacts', 'frontend-compiler-tests');
  fs.mkdirSync(root, { recursive: true });
  const dir = fs.mkdtempSync(path.join(root, 'numeric-'));
  const workspace = connectedWorkspace(json);
  try {
    if (process.platform !== 'win32') fs.writeFileSync(path.join(dir, 'windows.h'), '#pragma once\n#include <cstdint>\n#define __stdcall\n#define __declspec(x)\nusing LONG=int32_t; using ULONG=uint32_t; struct GUID {uint32_t a;uint16_t b,c;uint8_t d[8];};\n');
    const source = path.join(dir, 'run.cpp'), binary = path.join(dir, process.platform === 'win32' ? 'run.exe' : 'run');
    fs.writeFileSync(source, CppTranspiler.transpile('compiler_fixture', workspace) + `\n#include <cassert>\nint main() { aura::Effect_compiler_fixture effect; aura::Keymap keymap; aura::FrameBuffer frame; effect.Render(0, frame, keymap); assert(frame.buffer[0] == 1); effect.Render(40, frame, keymap); assert(frame.buffer[0] == 2); effect.Render(0, frame, keymap); assert(frame.buffer[0] == 1); }\n`);
    const args = /(^|[\\/])cl(?:\.exe)?$/i.test(compiler)
      ? ['/nologo', '/std:c++17', '/EHsc', '/utf-8', '/I' + dir, '/I' + path.join(repo, 'include'), source, '/Fe:' + binary, '/Fo:' + path.join(dir, 'run.obj')]
      : ['-std=c++17', '-I' + dir, '-I' + path.join(repo, 'include'), source, '-o', binary];
    if (process.platform === 'win32' && /clang/.test(compiler)) args.push('-fuse-ld=lld');
    try { execFileSync(compiler, args, { stdio: 'pipe', cwd: dir }); }
    catch (error) { throw new Error(`Generated C++ compilation failed:\n${error.stdout || ''}\n${error.stderr || ''}`); }
    execFileSync(binary, [], { stdio: 'pipe', cwd: dir });
  } finally {
    workspace.dispose();
    if (path.resolve(dir).startsWith(path.resolve(root) + path.sep)) fs.rmSync(dir, { recursive: true, force: true });
  }
});

test('typed or nonnumeric values fail native generation before invalid C++ is emitted', () => {
  const typed = { variables: [{ id: 'text-id', name: 'label', type: 'String' }], blocks: { blocks: [] } };
  assert.throws(() => validateCandidate(project(typed)), /仅支持数值变量/);

  for (const value of [
    { type: 'color_rgb' }, { type: 'color_hsv' }, { type: 'color_cycle' },
    { type: 'text', fields: { TEXT: 'label' } }, { type: 'logic_boolean', fields: { BOOL: 'TRUE' } },
    { type: 'gsi_get_string', fields: { PATH: 'round.bomb' } },
    { type: 'gsi_get_boolean', fields: { PATH: 'player.state.helmet' } }
  ]) {
    const json = { variables: [{ id: 'value-id', name: 'value', type: '' }], blocks: { blocks: [
      { type: 'variables_set', fields: { VAR: { id: 'value-id' } }, inputs: { VALUE: { block: value } } }
    ] } };
    const workspace = connectedWorkspace(json);
    try {
      assert.throws(() => CppTranspiler.transpile('compiler_fixture', workspace), /仅支持数值变量/);
      assert.throws(() => JsTranspiler.compile(workspace), /仅支持数值变量/);
    }
    finally { workspace.dispose(); }
  }
});

test('Unicode and digit-prefixed names use variable identity without symbol collisions', () => {
  const json = { variables: [
    { id: 'chinese-id', name: '颜色', type: '' }, { id: 'digit-id', name: '1speed', type: '' }
  ], blocks: { blocks: [
    { type: 'variables_set', fields: { VAR: { id: 'chinese-id' } }, inputs: { VALUE: { block: number(2) } } },
    { type: 'variables_set', fields: { VAR: { id: 'digit-id' } }, inputs: { VALUE: { block: number(3) } } }
  ] } };
  const workspace = connectedWorkspace(json);
  try {
    const cpp = CppTranspiler.transpile('compiler_fixture', workspace);
    const symbols = ['chinese-id', 'digit-id'].map(id => effectVariableSymbol(workspace.getVariableMap().getVariableById(id)));
    assert.notEqual(symbols[0], symbols[1]);
    const js = JsTranspiler.generateJsFunctionBody(workspace.getTopBlocks(true));
    for (const symbol of symbols) {
      assert.ok(cpp.includes(`double ${symbol} = 0.0;`));
      assert.ok(js.includes(`let ${symbol} = state.vars.${symbol} ?? 0;`));
    }
    assert.doesNotThrow(() => JsTranspiler.compile(workspace)(0));
  } finally { workspace.dispose(); }

  const collision = { variables: [
    { id: 'a-id', name: '颜色', type: '' }, { id: 'b-id', name: '速度', type: '' }
  ], blocks: { blocks: [
    { type: 'variables_set', fields: { VAR: { id: 'a-id' } } },
    { type: 'variables_set', fields: { VAR: { id: 'b-id' } } }
  ] } };
  const collisionWorkspace = connectedWorkspace(collision);
  try {
    const cpp = CppTranspiler.transpile('compiler_fixture', collisionWorkspace);
    const symbols = ['a-id', 'b-id'].map(id => effectVariableSymbol(collisionWorkspace.getVariableMap().getVariableById(id)));
    assert.notEqual(symbols[0], symbols[1]);
    for (const symbol of symbols) assert.ok(cpp.includes(`double ${symbol} = 0.0;`));
    assert.doesNotThrow(() => JsTranspiler.compile(collisionWorkspace)(0));
  }
  finally { collisionWorkspace.dispose(); }
});

test('live key state and decay never silently compile as constant false or zero', () => {
  for (const type of ['key_is_pressed', 'key_decay']) {
    const preset = structuredClone(EFFECT_PRESETS.find(item => item.id === 'template_reactive').blocklyJson);
    const root = (Array.isArray(preset) ? preset : preset.blocks).at(0);
    if (type === 'key_is_pressed') {
      root.inputs.DO = { block: { type: 'controls_if', inputs: {
        IF0: { block: { type } }, DO0: { block: { type: 'key_fill_all', inputs: { COLOR: { block: { type: 'color_rgb' } } } } }
      } } };
    }
    const workspace = connectedWorkspace(preset);
    try {
      const cpp = CppTranspiler.transpile('compiler_fixture', workspace);
      assert.match(cpp, /AuraBindLogicalKeyboard/);
      assert.match(cpp, type === 'key_is_pressed' ? /KeyPressed\(info\)/ : /KeyDecay\(info,/);
      const run = JsTranspiler.compile(workspace);
      const frame = run(0, {}, { W: 1 });
      assert.ok(frame.some(rgb => rgb.some(channel => channel > 0)));
    } finally { workspace.dispose(); }
  }
});

test('if mutator else-if count is bounded before Blockly creates dynamic inputs', () => {
  for (const count of [-1, .5, '32', 33, 1100]) {
    const invalid = { blocks: [{ type: 'controls_if', extraState: { elseIfCount: count } }] };
    assert.throws(() => connectedWorkspace(invalid), /条件分支数量超过限制/);
    assert.throws(() => validateCandidate(project(invalid)), /条件分支数量超过限制/);
  }
  for (const state of [{ hasElse: 1 }, { unknown: 1 }]) assert.throws(() => connectedWorkspace({ blocks: [{ type: 'controls_if', extraState: state }] }));
  const bounded = connectedWorkspace({ blocks: [{ type: 'controls_if', extraState: { elseIfCount: 32, hasElse: true } }] });
  try { assert.equal(bounded.getTopBlocks(false)[0].inputList.length, 67); }
  finally { bounded.dispose(); }
});

test('variable declarations require unique IDs and valid names', () => {
  for (const variables of [
    [{ id: 'same', name: 'one' }, { id: 'same', name: 'two' }],
    [{ id: '', name: 'empty id' }], [{ id: 'missing-name' }], [{ id: 'non-string', name: 7 }]
  ]) assert.throws(() => validateCandidate(project({ variables, blocks: { blocks: [] } })), /变量声明无效/);
});
