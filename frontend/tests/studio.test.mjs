import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import Blockly from '../src/blockly/index.js';
import { registerCustomBlocks } from '../src/blockly/customBlocks.js';
import { JsTranspiler } from '../src/blockly/jsTranspiler.js';
import { CppTranspiler } from '../src/blockly/cppTranspiler.js';
import { OrchestratorSerializer as S } from '../src/blockly/orchestratorSerializer.js';
import { canonicalConfig, processRows, replaceSimpleRows } from '../src/utils/orchestration.js';
import { stageEffect, effectConfig } from '../src/utils/applyEffect.js';
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
test('generated native C++ executes the same wait sequence (portable frame harness)', () => {
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

test('actual native overlay manager: gating, repeat events, state lifetime and desktop cleanup', () => {
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

test('complete CS2 example previews and generates valid C++ for per-key wave and health', async () => {
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
