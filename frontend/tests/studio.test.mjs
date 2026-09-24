import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import Blockly, { loadSafeWorkspaceJson, assertNoLegacyEventPulse, LEGACY_EVENT_PULSE_MESSAGE } from '../src/blockly/index.js';
import { registerCustomBlocks } from '../src/blockly/customBlocks.js';
import { JsTranspiler } from '../src/blockly/jsTranspiler.js';
import { CppTranspiler } from '../src/blockly/cppTranspiler.js';
import { stageEffect, ensureStudioRuntime, ensurePublishReady, fetchPublishReadiness, effectConfig, getEffectLifecycleStatus, sanitizeEffectName, getNextCloneName, renameEffectInConfig } from '../src/utils/applyEffect.js';
import { EFFECT_STUDIO_TOOLBOX } from '../src/blockly/toolboxes.js';
import { EFFECT_PRESETS } from '../src/blockly/presets.js';
import {
  BOOLEAN_GSI_STATES,
  BOOLEAN_STATE_DROPDOWN_OPTIONS
} from '../src/constants/gsiDictionary.js';
registerCustomBlocks();
const num = n => ({ type: 'math_number', fields: { NUM: n } });
const color = (r,g,b) => ({ type: 'color_rgb', inputs: { R: { block: num(r) }, G: { block: num(g) }, B: { block: num(b) } } });
const fill = c => ({ type: 'key_fill_all', inputs: { COLOR: { block: color(...c) } } });
const wait = n => ({ type: 'effect_wait_ms', inputs: { MS: { block: num(n) } } });
function chain(...blocks) { for(let i=0;i<blocks.length-1;i++) blocks[i].next={block:blocks[i+1]}; return blocks[0]; }
function workspace(blocks, variables) { const w=new Blockly.Workspace(); Blockly.serialization.workspaces.load({blocks:{languageVersion:0,blocks}, ...(variables ? {variables}: {})},w); return w; }
const red=[255,0,0],blue=[0,0,255];
function sequence() { return workspace([chain(fill(red),wait(100),fill(blue),wait(100))]); }

test('wait preserves frame and resumes at its deadline; backwards time resets', () => {
 const w=sequence(), run=JsTranspiler.compile(w);
 for(const [t,c] of [[0,red],[99,red],[100,blue],[199,blue],[200,blue],[240,red],[0,red]]) assert.deepEqual(run(t)[0],c);
 w.dispose();
});
test('repeat resumes inside its body and exits after exactly two iterations', () => {
 const w=workspace([chain({type:'controls_repeat_ext',inputs:{TIMES:{block:num(2)},DO:{block:chain(fill(red),wait(100))}}},fill(blue))]);
 const run=JsTranspiler.compile(w);
 assert.deepEqual(run(0)[0],red); assert.deepEqual(run(100)[0],red); assert.deepEqual(run(200)[0],blue); w.dispose();
});
test('loop budget yields instead of locking the browser', () => {
 const w=workspace([{type:'controls_whileUntil',fields:{MODE:'WHILE'},inputs:{BOOL:{block:{type:'logic_boolean',fields:{BOOL:'TRUE'}}}}}]);
 const run=JsTranspiler.compile(w); assert.equal(run(0).length,68); assert.equal(run(40).length,68); w.dispose();
});
test('failed compile or reload cannot change an applied profile; drafts retain previous build', async () => {
 const original={profiles:{demo:{type:'plugin',plugin_name:'good'}},blockly_effects:{demo:{applied_plugin_name:'good'}}};
 assert.equal(effectConfig(original,'demo',{blocks:[]}).profiles.demo.plugin_name,'good');
 const oldFetch=globalThis.fetch; const w=sequence();
 try {
  globalThis.fetch=async url=>url==='/api/status'?{ok:true,json:async()=>({web_api_version:2})}:url==='/api/gsi/current'?{ok:true,json:async()=>({studio_runtime:2})}:{ok:false,json:async()=>({success:false,message:'compile failed'})};
  await assert.rejects(stageEffect('demo',w,CppTranspiler),/compile failed/);
  let count=0; globalThis.fetch=async url=>url==='/api/status'?{ok:true,json:async()=>({web_api_version:2})}:url==='/api/gsi/current'?{ok:true,json:async()=>({studio_runtime:2})}:{ok:true,json:async()=>++count===1?{success:true}:{success:true,daemon_synced:false}};
  await assert.rejects(stageEffect('demo',w,CppTranspiler),/尚未确认/);
  assert.equal(original.profiles.demo.plugin_name,'good');
 } finally {globalThis.fetch=oldFetch;w.dispose();}
});
test('publish guard distinguishes an old Web UI, old daemon, and offline daemon before compiling', async () => {
 const oldFetch=globalThis.fetch;
 try {
  globalThis.fetch=async()=>({ok:true,json:async()=>({status:'ok'})});
  await assert.rejects(ensureStudioRuntime(),/Web UI 版本不兼容/);
  globalThis.fetch=async url=>({ok:true,json:async()=>url==='/api/status'?{web_api_version:2}:{studio_runtime:1}});
  await assert.rejects(ensureStudioRuntime(),/Studio Runtime.*当前 v1.*要求 v2/);
  globalThis.fetch=async url=>({ok:true,json:async()=>url==='/api/status'?{web_api_version:2}:{daemon_running:false}});
  await assert.rejects(ensureStudioRuntime(),/daemon 不在线/);
 } finally {globalThis.fetch=oldFetch;}
});

test('publish readiness checks distinguish missing SDK and missing MSVC without breaking runtime health check', async () => {
 const oldFetch = globalThis.fetch;
 try {
  // 1. Missing SDK
  globalThis.fetch = async url => ({
    ok: true,
    json: async () => url === '/api/status'
      ? { web_api_version: 2, studio_publish_ready: false, sdk_headers_found: false, msvc_found: true }
      : { studio_runtime: 2 }
  });
  const statusMissingSdk = await fetchPublishReadiness();
  assert.equal(statusMissingSdk.ready, false);
  assert.equal(statusMissingSdk.sdkFound, false);
  assert.equal(statusMissingSdk.msvcFound, true);
  await assert.rejects(ensurePublishReady(), /SDK 缺失或损坏/);
  await assert.doesNotReject(ensureStudioRuntime());

  // 2. Missing MSVC
  globalThis.fetch = async url => ({
    ok: true,
    json: async () => url === '/api/status'
      ? { web_api_version: 2, studio_publish_ready: false, sdk_headers_found: true, msvc_found: false }
      : { studio_runtime: 2 }
  });
  const statusMissingMsvc = await fetchPublishReadiness();
  assert.equal(statusMissingMsvc.ready, false);
  assert.equal(statusMissingMsvc.sdkFound, true);
  assert.equal(statusMissingMsvc.msvcFound, false);
  await assert.rejects(ensurePublishReady(), /C\+\+ Desktop workload/);
  await assert.doesNotReject(ensureStudioRuntime());

  // 3. Both ready
  globalThis.fetch = async url => ({
    ok: true,
    json: async () => url === '/api/status'
      ? { web_api_version: 2, studio_publish_ready: true, sdk_headers_found: true, msvc_found: true }
      : { studio_runtime: 2 }
  });
  const statusReady = await fetchPublishReadiness();
  assert.equal(statusReady.ready, true);
  assert.equal(statusReady.sdkFound, true);
  assert.equal(statusReady.msvcFound, true);
  await assert.doesNotReject(ensurePublishReady());
 } finally {
  globalThis.fetch = oldFetch;
 }
});
const hasGpp = (() => {
  try {
    execFileSync('g++', ['--version'], { stdio: 'ignore' });
    return true;
  } catch {
    return false;
  }
})();

(hasGpp ? test : test.skip)('generated native C++ executes the same wait sequence (portable frame harness)', () => {
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'aura-sequence-'));
 const w=sequence();
 try {
  fs.writeFileSync(path.join(dir,'windows.h'),'#pragma once\n#include <cstdint>\n#define __stdcall\n#define __declspec(x)\nusing LONG=int32_t; using ULONG=uint32_t; struct GUID {uint32_t a;uint16_t b,c;uint8_t d[8];};\n');
  const code=CppTranspiler.transpile('test',w);
  fs.writeFileSync(path.join(dir,'run.cpp'),code+'\n#include <cassert>\nint main(){ aura::Effect_test e; aura::FrameBuffer f; aura::Keymap k; e.Render(0,f,k); assert(f.buffer[0]==255 && f.buffer[2]==0); e.Render(99,f,k); assert(f.buffer[0]==255); e.Render(100,f,k); assert(f.buffer[0]==0 && f.buffer[2]==255); e.Render(199,f,k); assert(f.buffer[2]==255); e.Render(200,f,k); e.Render(240,f,k); assert(f.buffer[0]==255); }');
  execFileSync('g++',['-std=c++17','-I'+dir,'-I'+path.resolve('../include'),path.join(dir,'run.cpp'),'-o',path.join(dir,'run')],{stdio:'pipe'});
  execFileSync(path.join(dir,'run'));
 } finally {w.dispose();fs.rmSync(dir,{recursive:true,force:true});}
});

test('effect lifecycle: draft, unpublished changes, and published states', () => {
  // 1. Initial draft without published_at
  const draft = { name: 'test_draft', blockly_json: {} };
  assert.equal(getEffectLifecycleStatus(draft).status, 'draft');

  // 2. Published effect
  const now = Date.now();
  const published = {
    name: 'test_pub',
    published_at: now,
    source_updated_at: now,
    applied_plugin_name: 'studio_test_pub_123'
  };
  assert.equal(getEffectLifecycleStatus(published).status, 'published');

  // 3. Modified after publishing
  const modified = {
    ...published,
    source_updated_at: now + 5000
  };
  assert.equal(getEffectLifecycleStatus(modified).status, 'modified');

  // 4. effectConfig updating timestamps
  const base = { blockly_effects: {}, profiles: {} };
  const savedDraft = effectConfig(base, 'fx1', { blocks: [] });
  assert.ok(savedDraft.blockly_effects.fx1.source_updated_at > 0);
  assert.equal(savedDraft.blockly_effects.fx1.published_at, undefined);
  assert.equal(getEffectLifecycleStatus(savedDraft.blockly_effects.fx1).status, 'draft');

  const pubBuild = { pluginName: 'fx1_dll', revision: 'abc' };
  const publishedCfg = effectConfig(savedDraft, 'fx1', { blocks: [] }, pubBuild);
  assert.ok(publishedCfg.blockly_effects.fx1.published_at > 0);
  assert.equal(Object.hasOwn(publishedCfg.profiles.fx1, 'fps'), false, 'publication keeps global FPS inheritance');
  assert.equal(getEffectLifecycleStatus(publishedCfg.blockly_effects.fx1).status, 'published');

  // Modify draft on top of published
  const modifiedDraft = effectConfig(publishedCfg, 'fx1', { blocks: [{ id: 1 }] });
  assert.ok(modifiedDraft.blockly_effects.fx1.source_updated_at >= modifiedDraft.blockly_effects.fx1.published_at);
  assert.equal(modifiedDraft.blockly_effects.fx1.applied_plugin_name, 'fx1_dll');
});

test('creation metadata uses the existing publication contract and preserves an existing draft', () => {
  const base={blockly_effects:{},profiles:{}};
  const continuous=effectConfig(base,'ambient',{blocks:{languageVersion:0,blocks:[]}},undefined,{mode:'continuous',fade_out_ms:300});
  assert.deepEqual(continuous.blockly_effects.ambient.publication,{mode:'continuous',fade_out_ms:0});
  const once=effectConfig(continuous,'kill',{blocks:{languageVersion:0,blocks:[]}},undefined,{mode:'one_shot',fade_out_ms:300});
  assert.deepEqual(once.blockly_effects.kill.publication,{mode:'one_shot',fade_out_ms:300});
  assert.deepEqual(once.blockly_effects.ambient.publication,{mode:'continuous',fade_out_ms:0});
  const edited=effectConfig(once,'kill',{blocks:{languageVersion:0,blocks:[]}});
  assert.deepEqual(edited.blockly_effects.kill.publication,{mode:'one_shot',fade_out_ms:300});
});

test('old event pulse workspaces are rejected before Blockly can substitute a boolean dropdown value',()=>{
  assert.match(LEGACY_EVENT_PULSE_MESSAGE,/事件触发条件已迁移到自动化工作室，请使用自动化事件规则。/);
  assert.doesNotMatch(LEGACY_EVENT_PULSE_MESSAGE,/Automation 事件规则/);
  const old={blocks:{languageVersion:0,blocks:[{type:'gsi_get_boolean',fields:{PATH:'event.kill'}}]}};
  const w=new Blockly.Workspace();
  try {
    assert.throws(()=>assertNoLegacyEventPulse(old),/事件触发条件已迁移到自动化工作室/);
    assert.throws(()=>loadSafeWorkspaceJson(old,w),/事件触发条件已迁移到自动化工作室/);
    assert.equal(w.getAllBlocks(false).length,0);
    assert.throws(()=>assertNoLegacyEventPulse({blocks:[{type:'orch_event_triggered',fields:{EVENT:'event.kill'}}]}),/自动化工作室/);
    const state={blocks:{languageVersion:0,blocks:[{type:'controls_if',inputs:{IF0:{block:{type:'gsi_get_boolean',fields:{PATH:'player.state.helmet'},inputs:{DEFAULT:{block:{type:'logic_boolean',fields:{BOOL:'FALSE'}}}}}}}}]}};
    assert.doesNotThrow(()=>loadSafeWorkspaceJson(state,w,true));
    const block=w.getBlocksByType('controls_if',false)[0];
    assert.match(CppTranspiler.valueToCpp(block,'IF0'),/GetBool\("player.state.helmet"/);
    assert.match(JsTranspiler.valueToJs(block,'IF0'),/player.state.helmet/);
    assert.deepEqual(BOOLEAN_GSI_STATES.map(s=>s.key),['player.state.helmet','player.state.defusekit']);
  } finally {w.dispose();}
});

test('simplified EFFECT_STUDIO_TOOLBOX has 5 core categories and Advanced group', () => {
  const categories = EFFECT_STUDIO_TOOLBOX.contents.filter(c => c.kind === 'category');
  const names = categories.map(c => c.name);
  assert.ok(names.some(n => n.includes('按键')));
  assert.ok(names.some(n => n.includes('颜色')));
  assert.ok(names.some(n => n.includes('时间')));
  assert.ok(names.some(n => n.includes('条件')));
  assert.ok(names.some(n => n.includes('游戏状态')));
  assert.ok(names.some(n => n.includes('高级')));

  const advCategory = categories.find(c => c.name.includes('高级'));
  assert.ok(advCategory);
  assert.ok(Array.isArray(advCategory.contents));
  const advSubNames = advCategory.contents.filter(c => c.kind === 'category').map(c => c.name);
  assert.ok(advSubNames.some(n => n.includes('几何')));
  assert.ok(advSubNames.some(n => n.includes('数学')));
  assert.ok(advSubNames.some(n => n.includes('变量')));
  assert.ok(advSubNames.some(n => n.includes('复杂循环')));
});

test('semantic blocks JS transpilation execution and output validity', () => {
  // 1. color_cycle block plugged into key_fill_all
  const wColor = workspace([{
    type: 'key_fill_all',
    inputs: {
      COLOR: {
        block: {
          type: 'color_cycle',
          inputs: {
            COLOR_A: { block: color(255, 0, 0) },
            COLOR_B: { block: color(0, 0, 255) },
            PERIOD_SEC: { block: num(2) }
          }
        }
      }
    }
  }]);
  const runColor = JsTranspiler.compile(wColor);
  const frame0 = runColor(0).map(c => [...c]);
  assert.equal(frame0.length, 68);
  assert.ok(frame0[0][0] > 200 && frame0[0][2] < 50);

  const frame1000 = runColor(1000).map(c => [...c]);
  assert.equal(frame1000.length, 68);
  assert.ok(frame1000[0][2] > 200 && frame1000[0][0] < 50);
  wColor.dispose();

  // 2. key_ripple_effect block
  const wRipple = workspace([
    chain(
      fill([0, 0, 0]),
      {
        type: 'key_ripple_effect',
        fields: {
          KEY: 'SPACE',
          SPEED: '2.5'
        },
        inputs: {
          COLOR: { block: color(0, 255, 255) }
        }
      }
    )
  ]);
  const runRipple = JsTranspiler.compile(wRipple);
  const ripple0 = runRipple(0).map(c => [...c]);
  const ripple200 = runRipple(200).map(c => [...c]);
  assert.equal(ripple0.length, 68);
  assert.equal(ripple200.length, 68);
  wRipple.dispose();
});

test('semantic blocks C++ transpilation generates zero-heap code without allocations in render loop', () => {
  const wColor = workspace([{
    type: 'key_fill_all',
    inputs: {
      COLOR: {
        block: {
          type: 'color_cycle',
          inputs: {
            COLOR_A: { block: color(255, 0, 0) },
            COLOR_B: { block: color(0, 255, 0) },
            PERIOD_SEC: { block: num(1.5) }
          }
        }
      }
    }
  }]);
  const cppColor = CppTranspiler.transpile('color_test', wColor);
  assert.ok(cppColor.includes('cos('));
  assert.ok(cppColor.includes('LerpRGB'));
  const renderLoopColor = cppColor.slice(cppColor.indexOf('void RenderInternal'), cppColor.indexOf('extern "C"'));
  assert.ok(!renderLoopColor.includes('malloc'));
  assert.ok(!renderLoopColor.includes('new '));
  assert.ok(!renderLoopColor.includes('std::vector'));
  wColor.dispose();

  const wRipple = workspace([
    chain(
      fill([0, 0, 0]),
      {
        type: 'key_ripple_effect',
        fields: {
          KEY: 'SPACE',
          SPEED: '2.5'
        },
        inputs: {
          COLOR: { block: color(255, 0, 255) }
        }
      }
    )
  ]);
  const cppRipple = CppTranspiler.transpile('ripple_test', wRipple);
  assert.ok(cppRipple.includes('ScaleBrightness'));
  assert.ok(cppRipple.includes('hypot') || cppRipple.includes('sqrt'));
  const renderLoopRipple = cppRipple.slice(cppRipple.indexOf('void RenderInternal'), cppRipple.indexOf('extern "C"'));
  assert.ok(!renderLoopRipple.includes('malloc'));
  assert.ok(!renderLoopRipple.includes('new '));
  assert.ok(!renderLoopRipple.includes('std::vector'));
  wRipple.dispose();
});

test('preset templates deserialize and compile cleanly in JS and C++', () => {
  const templateIds = ['template_smooth_breath', 'template_low_health_warning', 'kill_wave', 'cs2_health_bar'];
  for (const id of templateIds) {
    const preset = EFFECT_PRESETS.find(p => p.id === id);
    assert.ok(preset, `Preset ${id} should exist`);
    assert.ok(preset.blocklyJson, `Preset ${id} should have blocklyJson`);

    const w = new Blockly.Workspace();
    loadSafeWorkspaceJson(preset.blocklyJson, w, true);
    
    // JS Transpiler
    const run = JsTranspiler.compile(w);
    const frame = run(100);
    assert.equal(frame.length, 68);
    for (let k = 0; k < 68; k++) {
      assert.ok(Array.isArray(frame[k]) && frame[k].length === 3);
    }

    // C++ Transpiler
    const cpp = CppTranspiler.transpile(preset.id, w);
    assert.ok(cpp.includes(`class Effect_${preset.id}`));
    assert.ok(cpp.includes('void Render('));
    const renderLoop = cpp.slice(cpp.indexOf('void RenderInternal'), cpp.indexOf('extern "C"'));
    assert.ok(!renderLoop.includes('malloc'));
    assert.ok(!renderLoop.includes('new '));

    w.dispose();
  }
});

test('import effect name sanitization handles filename and JSON data.name with non-empty validation', () => {
  // 1. From file name with extensions and spaces
  assert.equal(sanitizeEffectName('My Crazy Rainbow.json'), 'my_crazy_rainbow_json');
  assert.equal(sanitizeEffectName('cool-wave_2'), 'cool_wave_2');
  
  // 2. From JSON data.name with special chars and spaces
  assert.equal(sanitizeEffectName('  CS2 !! Hyper-Beast Wave  '), 'cs2_hyper_beast_wave');
  assert.equal(sanitizeEffectName('__underscore_test__'), 'underscore_test');

  // 3. Non-empty fallback validation
  assert.equal(sanitizeEffectName('', 'imported_effect'), 'imported_effect');
  assert.equal(sanitizeEffectName('    ', 'imported_effect'), 'imported_effect');
  assert.equal(sanitizeEffectName('!@#$%^&*()', 'imported_effect'), 'imported_effect');
  assert.equal(sanitizeEffectName(null, 'imported_effect'), 'imported_effect');
  assert.equal(sanitizeEffectName(undefined, 'imported_effect'), 'imported_effect');
});

test('clone naming follows foo -> foo_copy -> foo_copy_2 -> foo_copy_3 and avoids collisions', () => {
  // 1. Initial clone: foo -> foo_copy
  const existing1 = { foo: {} };
  assert.equal(getNextCloneName('foo', existing1), 'foo_copy');

  // 2. Second clone: foo_copy exists -> foo_copy_2
  const existing2 = { foo: {}, foo_copy: {} };
  assert.equal(getNextCloneName('foo', existing2), 'foo_copy_2');

  // 3. Third clone: foo_copy and foo_copy_2 exist -> foo_copy_3
  const existing3 = { foo: {}, foo_copy: {}, foo_copy_2: {} };
  assert.equal(getNextCloneName('foo', existing3), 'foo_copy_3');

  // 4. Cloning a copy: foo_copy -> foo_copy_2
  assert.equal(getNextCloneName('foo_copy', existing2), 'foo_copy_2');

  // 5. Gap handling: foo_copy_2 exists manually -> foo_copy
  const existingWithGap = { foo: {}, foo_copy_2: {} };
  assert.equal(getNextCloneName('foo', existingWithGap), 'foo_copy');

  // 6. Next after gap filled
  const existingGapFilled = { foo: {}, foo_copy: {}, foo_copy_2: {} };
  assert.equal(getNextCloneName('foo', existingGapFilled), 'foo_copy_3');
});

