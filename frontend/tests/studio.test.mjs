import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import Blockly, { loadSafeWorkspaceJson } from '../src/blockly/index.js';
import { registerCustomBlocks } from '../src/blockly/customBlocks.js';
import { JsTranspiler } from '../src/blockly/jsTranspiler.js';
import { CppTranspiler } from '../src/blockly/cppTranspiler.js';
import { OrchestratorSerializer as S } from '../src/blockly/orchestratorSerializer.js';
import { canonicalConfig, processRows, replaceSimpleRows } from '../src/utils/orchestration.js';
import { stageEffect, effectConfig, getEffectLifecycleStatus } from '../src/utils/applyEffect.js';
import { EFFECT_STUDIO_TOOLBOX, ORCHESTRATOR_STUDIO_TOOLBOX } from '../src/blockly/toolboxes.js';
import { EFFECT_PRESETS } from '../src/blockly/presets.js';
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
test('conditions survive event overlay serialization and restoration', () => {
 const w=workspace([{type:'orch_root_flow',fields:{FALLBACK_PROFILE:'desktop'},inputs:{DO:{block:{type:'controls_if',inputs:{IF0:{block:{type:'gsi_numeric_compare',fields:{FIELD:'player.state.health',OP:'<',VALUE:'20'}}},DO0:{block:{type:'orch_action_overlay_pulse',fields:{EVENT:'event.kill',EFFECT:'wave',FADE:0}}}}}}}}]);
 const data=S.serializeWorkspace(w); const overlay=data.orchestration.event_overlays[0];
 assert.deepEqual(overlay.condition,{field:'player.state.health',op:'<',value:20}); assert.equal(overlay.fade_ms,0);
 const restored=new Blockly.Workspace(); S.restoreWorkspace(restored,{...data,blockly_orchestrator:undefined});
 assert.deepEqual(S.serializeWorkspace(restored).orchestration.event_overlays[0].condition,overlay.condition);
 w.dispose();restored.dispose();
});
test('else-if overlays exclude preceding branches', () => {
 const w=workspace([{type:'orch_root_flow',inputs:{DO:{block:{type:'controls_if',extraState:{elseIfCount:1},inputs:{IF0:{block:{type:'logic_boolean',fields:{BOOL:'TRUE'}}},DO0:{block:{type:'orch_action_overlay_state'}},IF1:{block:{type:'logic_boolean',fields:{BOOL:'TRUE'}}},DO1:{block:{type:'orch_action_overlay_state'}}}}}}}]);
 const data=S.serializeWorkspace(w).orchestration.event_overlays;
 assert.equal(data.length,2); assert.equal(data[1].condition.conditions[1].type,'not'); w.dispose();
});
test('legacy arrays migrate once; table edits survive reopening Blockly', () => {
 const original={default_profile:'desktop',profiles:{desktop:{},game:{}},rules:[{process:'cs2.exe',profile:'game'}],gsi_bindings:[{field:'player.state.health',operator:'<',value:20,profile:'game'}]};
 let cfg=canonicalConfig(original);assert.equal(cfg.orchestration.rules.length,2);assert.deepEqual(canonicalConfig(cfg),cfg);
 cfg=replaceSimpleRows(cfg,'process',[{...processRows(cfg)[0],process:'code.exe'}]);
 const w=new Blockly.Workspace();S.restoreWorkspace(w,cfg);
 const saved=S.serializeWorkspace(w);assert.equal(saved.orchestration.rules[1].process,'code.exe');assert.deepEqual(saved.orchestration.rules[0].condition,{field:'player.state.health',op:'<',value:20});w.dispose();
});
test('failed compile or reload cannot change an applied profile; drafts retain previous build', async () => {
 const original={profiles:{demo:{type:'plugin',plugin_name:'good'}},blockly_effects:{demo:{applied_plugin_name:'good'}}};
 assert.equal(effectConfig(original,'demo',{blocks:[]}).profiles.demo.plugin_name,'good');
 const oldFetch=globalThis.fetch; const w=sequence();
 try {
  globalThis.fetch=async url=>url==='/api/gsi/current'?{ok:true,json:async()=>({studio_runtime:2})}:{ok:false,json:async()=>({success:false,message:'compile failed'})};
  await assert.rejects(stageEffect('demo',w,CppTranspiler),/compile failed/);
  let count=0; globalThis.fetch=async url=>url==='/api/gsi/current'?{ok:true,json:async()=>({studio_runtime:2})}:{ok:true,json:async()=>++count===1?{success:true}:{success:true,daemon_synced:false}};
  await assert.rejects(stageEffect('demo',w,CppTranspiler),/尚未确认/);
  assert.equal(original.profiles.demo.plugin_name,'good');
 } finally {globalThis.fetch=oldFetch;w.dispose();}
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

(hasGpp ? test : test.skip)('actual native overlay manager: gating, repeat events, state lifetime and desktop cleanup', () => {
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'aura-overlay-'));
 try {
  fs.mkdirSync(path.join(dir,'gsi'));fs.mkdirSync(path.join(dir,'utils'));
  fs.writeFileSync(path.join(dir,'windows.h'),'#pragma once\n#include <cstdint>\n#define __stdcall\n#define __declspec(x)\nusing LONG=int32_t; using ULONG=uint32_t; struct GUID {uint32_t a;uint16_t b,c;uint8_t d[8];};\n');
  fs.writeFileSync(path.join(dir,'utils/logger.h'),'#pragma once\n#define LOG_INFO(x)\n');
  fs.writeFileSync(path.join(dir,'gsi/gsi_adapter.h'),`#pragma once
#include "engine/plugin_interface.h"
namespace aura { class GsiState : public IGsiReader { public: bool online=true, event=false; double sequence=0, hp=100;
bool IsActive() const override {return online;} bool GetBool(const char*,bool=false) const override {return event;}
double GetNumber(const char* key,double=0) const override {return std::string(key)=="health"?hp:sequence;}
const char* GetString(const char*,const char* def="") const override {return def;} }; }
`);
  fs.writeFileSync(path.join(dir,'run.cpp'),`#include "engine/overlay_manager.h"
#include "gsi/gsi_adapter.h"
#include <cassert>
struct TestEffect : aura::Effect { void Render(uint64_t t,aura::FrameBuffer& f,const aura::Keymap&) override {f.Fill(static_cast<uint8_t>(t),0,0);} };
int main(){
 aura::GsiState g; aura::Keymap k; aura::FrameBuffer f; aura::OverlayManager m;
 aura::OverlayBinding b; b.id="kill";b.event_name="event.kill";b.effect=std::make_shared<TestEffect>();b.duration_ms=800;b.fade_out_ms=0;b.blend_mode="replace";
 b.condition=[](const aura::GsiState* g,const std::string&){return g->GetNumber("health")<20;};m.RegisterBinding(b);
 m.UpdateBindingsFromGsi(&g,0,"cs2.exe");g.event=true;g.sequence=1;m.UpdateBindingsFromGsi(&g,20,"cs2.exe");assert(m.GetActiveOverlayCount()==0);
 g.hp=10;m.UpdateBindingsFromGsi(&g,30,"cs2.exe");assert(m.GetActiveOverlayCount()==0);
 g.sequence=2;m.UpdateBindingsFromGsi(&g,40,"cs2.exe");assert(m.GetActiveOverlayCount()==1);
 m.ApplyOverlays(70,f,k,&g);assert(f.buffer[0]==30);
 g.sequence=3;m.UpdateBindingsFromGsi(&g,80,"cs2.exe");m.ApplyOverlays(90,f,k,&g);assert(f.buffer[0]==10);
 aura::OverlayBinding state;state.id="low";state.trigger="state";state.effect=std::make_shared<TestEffect>();state.priority=30;state.blend_mode="replace";state.condition=b.condition;m.RegisterBinding(state);
 m.UpdateBindingsFromGsi(&g,100,"cs2.exe");assert(m.GetActiveOverlayCount()==2);m.ApplyOverlays(120,f,k,&g);assert(f.buffer[0]==20);
 g.hp=100;m.UpdateBindingsFromGsi(&g,130,"cs2.exe");assert(m.GetActiveOverlayCount()==1);
 m.UpdateBindingsFromGsi(&g,140,"explorer.exe");assert(m.GetActiveOverlayCount()==0);
 m.UpdateBindingsFromGsi(&g,150,"cs2.exe");assert(m.GetActiveOverlayCount()==0);
 g.hp=10;m.UpdateBindingsFromGsi(&g,160,"cs2.exe");assert(m.GetActiveOverlayCount()==1);
 m.ApplyOverlays(5000,f,k,&g);assert(m.GetActiveOverlayCount()==1);g.online=false;m.UpdateBindingsFromGsi(&g,5040,"cs2.exe");assert(m.GetActiveOverlayCount()==0);
}`);
  execFileSync('g++',['-std=c++17','-I'+dir,'-I'+path.resolve('../include'),path.join(dir,'run.cpp'),path.resolve('../src/engine/overlay_manager.cpp'),'-o',path.join(dir,'run')],{stdio:'pipe'});
  execFileSync(path.join(dir,'run'));
 } finally {fs.rmSync(dir,{recursive:true,force:true});}
});

(hasGpp ? test : test.skip)('complete CS2 example previews and generates valid C++ for per-key wave and health', async () => {
 const {studioExample}=await import('../src/blockly/studioExample.js');
 const example=studioExample(); const dir=fs.mkdtempSync(path.join(os.tmpdir(),'aura-example-'));
 try {
  fs.writeFileSync(path.join(dir,'windows.h'),'#pragma once\n#include <cstdint>\n#define __stdcall\n#define __declspec(x)\nusing LONG=int32_t; using ULONG=uint32_t; struct GUID {uint32_t a;uint16_t b,c;uint8_t d[8];};\n');
  for(const [name,e] of Object.entries(example.effects)) {
   const w=new Blockly.Workspace();const {loadSafeWorkspaceJson}=await import('../src/blockly/index.js');loadSafeWorkspaceJson(e.blockly_json,w);
   const run=JsTranspiler.compile(w);const frames=[0,80,240,600].map(t=>run(t,{player:{state:{health:50}}}).map(c=>[...c]));
   assert.equal(frames[0].length,68); assert.ok(frames.every(f=>f.flat().every(Number.isFinite)));
   if(name==='studio_kill_wave') assert.notDeepEqual(frames[0],frames[2]);
   const file=path.join(dir,name+'.cpp');fs.writeFileSync(file,CppTranspiler.transpile(name,w) + '\nint main(){ aura::Effect_' + name + ' e; aura::FrameBuffer f; aura::Keymap k; e.Render(0,f,k); }');
   execFileSync('g++',['-std=c++17','-I'+dir,'-I'+path.resolve('../include'),file,'-o',path.join(dir,name)],{stdio:'pipe'});w.dispose();
  }
  const w=new Blockly.Workspace();Blockly.serialization.workspaces.load(example.blocklyJson,w);const config=S.serializeWorkspace(w).orchestration;
  assert.equal(config.event_overlays[0].duration_ms,800);assert.equal(config.event_overlays[1].trigger,'state');assert.equal(config.rules[0].condition.value,'cs2.exe');w.dispose();
 } finally {fs.rmSync(dir,{recursive:true,force:true});}
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
  assert.equal(getEffectLifecycleStatus(publishedCfg.blockly_effects.fx1).status, 'published');

  // Modify draft on top of published
  const modifiedDraft = effectConfig(publishedCfg, 'fx1', { blocks: [{ id: 1 }] });
  assert.ok(modifiedDraft.blockly_effects.fx1.source_updated_at >= modifiedDraft.blockly_effects.fx1.published_at);
  assert.equal(modifiedDraft.blockly_effects.fx1.applied_plugin_name, 'fx1_dll');
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

test('semantic blocks round-trip serialization and deserialization', () => {
  // Test gsi_player_health_condition
  const w1 = workspace([{
    type: 'orch_root_flow',
    inputs: {
      DO: {
        block: {
          type: 'controls_if',
          inputs: {
            IF0: {
              block: {
                type: 'gsi_player_health_condition',
                fields: { OP: '<', VALUE: '25' }
              }
            },
            DO0: {
              block: {
                type: 'orch_action_overlay_pulse',
                fields: { EVENT: 'event.kill', EFFECT: 'wave', FADE: 0 }
              }
            }
          }
        }
      }
    }
  }]);
  const data1 = S.serializeWorkspace(w1);
  assert.deepEqual(data1.orchestration.event_overlays[0].condition, {
    field: 'player.state.health',
    op: '<',
    value: 25
  });

  const restored1 = new Blockly.Workspace();
  S.restoreWorkspace(restored1, data1);
  const data1Restored = S.serializeWorkspace(restored1);
  assert.deepEqual(data1Restored.orchestration.event_overlays[0].condition, {
    field: 'player.state.health',
    op: '<',
    value: 25
  });
  w1.dispose();
  restored1.dispose();

  // Test gsi_c4_state_condition
  const w2 = workspace([{
    type: 'orch_root_flow',
    inputs: {
      DO: {
        block: {
          type: 'controls_if',
          inputs: {
            IF0: {
              block: {
                type: 'gsi_c4_state_condition',
                fields: { STATE: 'planted' }
              }
            },
            DO0: {
              block: {
                type: 'orch_action_overlay_state',
                fields: { EFFECT: 'bomb_warning', BLEND: 'replace' }
              }
            }
          }
        }
      }
    }
  }]);
  const data2 = S.serializeWorkspace(w2);
  assert.deepEqual(data2.orchestration.event_overlays[0].condition, {
    field: 'round.bomb',
    op: '==',
    value: 'planted'
  });
  w2.dispose();
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

test('studio cascade rename, reference safety on deletion, and clone behavior', () => {
  const config = {
    default_profile: 'old_effect',
    profiles: {
      old_effect: { type: 'plugin', plugin_name: 'old_plugin' },
      other_profile: {}
    },
    blockly_effects: {
      old_effect: {
        blockly_json: { blocks: [] },
        applied_plugin_name: 'old_plugin'
      },
      referenced_effect: {
        blockly_json: { blocks: [] }
      }
    },
    orchestration: {
      fallback_profile: 'old_effect',
      rules: [
        { process: 'cs2.exe', target_profile: 'referenced_effect', enabled: true }
      ],
      event_overlays: [
        { trigger: 'state', effect: 'referenced_effect', priority: 10 }
      ]
    }
  };

  // 1. Reference check prevents deleting referenced effect
  const c = canonicalConfig(config);
  const isReferenced = (name) => (
    c.default_profile === name || 
    c.orchestration?.fallback_profile === name || 
    c.orchestration?.rules?.some(r => r.target_profile === name) || 
    c.orchestration?.event_overlays?.some(r => r.effect === name)
  );
  assert.equal(isReferenced('old_effect'), true);
  assert.equal(isReferenced('referenced_effect'), true);
  assert.equal(isReferenced('unrelated_effect'), false);

  // 2. Cascade rename
  const oldName = 'old_effect';
  const clean = 'new_renamed_effect';
  const effects = { ...config.blockly_effects };
  effects[clean] = effects[oldName];
  delete effects[oldName];

  const profiles = { ...config.profiles };
  profiles[clean] = profiles[oldName];
  delete profiles[oldName];

  const orch = JSON.parse(JSON.stringify(config.orchestration));
  if (orch.fallback_profile === oldName) orch.fallback_profile = clean;
  orch.rules.forEach(r => { if (r.target_profile === oldName) r.target_profile = clean; });
  orch.event_overlays.forEach(ov => { if (ov.effect === oldName) ov.effect = clean; });

  assert.equal(orch.fallback_profile, clean);
  assert.ok(effects[clean]);
  assert.equal(effects[oldName], undefined);
  assert.ok(profiles[clean]);
  assert.equal(profiles[oldName], undefined);

  // 3. Clone
  const cloneName = `${clean}_copy`;
  const clonedConfig = effectConfig({ ...config, blockly_effects: effects, profiles }, cloneName, effects[clean].blockly_json);
  assert.ok(clonedConfig.blockly_effects[cloneName]);
  assert.deepEqual(clonedConfig.blockly_effects[cloneName].blockly_json, effects[clean].blockly_json);
});

test('orchestration studio toolbox and semantic condition blocks exist', () => {
  const categories = ORCHESTRATOR_STUDIO_TOOLBOX.contents.filter(c => c.kind === 'category');
  const names = categories.map(c => c.name);
  assert.ok(names.some(n => n.includes('侦测与条件')));

  const allBlocks = JSON.stringify(ORCHESTRATOR_STUDIO_TOOLBOX);
  assert.ok(allBlocks.includes('gsi_player_health_condition'));
  assert.ok(allBlocks.includes('gsi_c4_state_condition'));

  const effectBlocks = JSON.stringify(EFFECT_STUDIO_TOOLBOX);
  assert.ok(effectBlocks.includes('color_cycle'));
  assert.ok(effectBlocks.includes('key_ripple_effect'));
});
