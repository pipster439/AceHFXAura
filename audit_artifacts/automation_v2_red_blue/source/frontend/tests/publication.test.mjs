import test from 'node:test';
import assert from 'node:assert/strict';
import Blockly from '../src/blockly/index.js';
import { registerCustomBlocks } from '../src/blockly/customBlocks.js';
import { CppTranspiler } from '../src/blockly/cppTranspiler.js';
import { JsTranspiler } from '../src/blockly/jsTranspiler.js';
import { normalizePublication } from '../src/blockly/publication.js';
import { stageEffect, effectConfig } from '../src/utils/applyEffect.js';
import { ConfigSaveCoordinator } from '../src/utils/configSaveCoordinator.js';
registerCustomBlocks();
function workspace() {
 const w=new Blockly.Workspace();
 const num=n=>({type:'math_number',fields:{NUM:n}});
 const fill=r=>({type:'key_fill_all',inputs:{COLOR:{block:{type:'color_rgb',inputs:{R:{block:num(r)},G:{block:num(0)},B:{block:num(0)}}}}}});
 const first=fill(200);first.next={block:{type:'effect_wait_ms',inputs:{MS:{block:num(10)}},next:{block:fill(100)}}};
 Blockly.serialization.workspaces.load({blocks:{languageVersion:0,blocks:[first]}},w);return w;
}
test('old and explicitly continuous publication loop; explicit one-shot terminates and fades',()=>{
 const w=workspace();try {
  for(const options of [undefined,{mode:'continuous'}]) {
   const run=JsTranspiler.compile(w,options);assert.equal(run(0)[0][0],200);assert.equal(run(10)[0][0],100);assert.equal(run(11)[0][0],200);
   assert.ok(!CppTranspiler.transpile('old',w,options).includes('AuraIsEffectFinished'));
  }
  const run=JsTranspiler.compile(w,{mode:'one_shot',fade_out_ms:10});
  assert.equal(run(0)[0][0],200);assert.equal(run(10)[0][0],100);assert.equal(run(15)[0][0],50);assert.equal(run(20)[0][0],0);assert.equal(run(100)[0][0],0);
  const source=CppTranspiler.transpile('once',w,{mode:'one_shot',fade_out_ms:10});
  assert.match(source,/AURA_PLUGIN_CALL AuraIsEffectFinished/);assert.match(source,/AURA_PLUGIN_CALL AuraGetEffectOpacity/);
 } finally {w.dispose();}
});
test('publication format is explicit; a one-shot draft does not change applied continuous publication',()=>{
 const old={profiles:{demo:{type:'plugin',plugin_name:'old'}},blockly_effects:{demo:{applied_plugin_name:'old'}}};
 assert.deepEqual(normalizePublication(),{mode:'continuous',fade_out_ms:0});
 const draft=effectConfig(old,'demo',{},undefined,{mode:'one_shot',fade_out_ms:100});
 assert.equal(draft.profiles.demo.plugin_name,'old');assert.equal(draft.blockly_effects.demo.applied_publication,undefined);
 assert.equal(draft.blockly_effects.demo.publication.mode,'one_shot');
 const applied=effectConfig(draft,'demo',{}, {pluginName:'new',revision:'r',publication:{mode:'one_shot',fade_out_ms:100}});
 assert.equal(applied.blockly_effects.demo.applied_publication.mode,'one_shot');assert.equal(old.profiles.demo.plugin_name,'old');
 assert.throws(()=>normalizePublication({mode:'guess'}));assert.throws(()=>normalizePublication({mode:'one_shot',fade_out_ms:-1}));
});
test('one-shot publication requires daemon capability confirmation before reference update',async()=>{
 const w=workspace(),originalFetch=globalThis.fetch;const calls=[];
 try {
  let verified=false;
  globalThis.fetch=async(url,request)=>{calls.push([url,request]);return {ok:true,json:async()=>url==='/api/status'?{web_api_version:2}:url==='/api/gsi/current'?{studio_runtime:2}:url==='/api/reload_plugin'?{success:true,daemon_synced:true,lifecycle_verified:verified}:{success:true}};};
  await assert.rejects(stageEffect('demo',w,CppTranspiler,()=>{},{mode:'one_shot'}),/lifecycle/);
  verified=true;const result=await stageEffect('demo',w,CppTranspiler,()=>{},{mode:'one_shot',fade_out_ms:100});
  assert.equal(result.publication.mode,'one_shot');
  const reload=calls.filter(([url])=>url==='/api/reload_plugin').at(-1);assert.equal(JSON.parse(reload[1].body).require_lifecycle,true);
  assert.equal(calls.filter(([url])=>url==='/api/config').length,0);
 } finally {globalThis.fetch=originalFetch;w.dispose();}
});
test('compile/load/config failure leaves applied publication unchanged and draft editable',async()=>{
 const w=workspace(),originalFetch=globalThis.fetch;
 const original={profiles:{demo:{type:'plugin',plugin_name:'old'}},blockly_effects:{demo:{applied_plugin_name:'old'}}};
 try {
  for(const failed of ['/api/compile_effect','/api/reload_plugin']) {
   globalThis.fetch=async url=>({ok:url!==failed,json:async()=>url==='/api/status'?{web_api_version:2}:url==='/api/gsi/current'?{studio_runtime:2}:{success:url!==failed,message:'candidate failure'}});
   await assert.rejects(stageEffect('demo',w,CppTranspiler,()=>{},{mode:'one_shot'}),/candidate failure/);
   assert.equal(original.profiles.demo.plugin_name,'old');assert.ok(effectConfig(original,'demo',{}).blockly_effects.demo);
  }
  let applied=original;const coordinator=new ConfigSaveCoordinator({onSaveSuccess:next=>{applied=next;}});coordinator.setRevision('old');
  const candidate=effectConfig(original,'demo',{}, {pluginName:'candidate',revision:'r',publication:{mode:'one_shot'}});
  for(const status of [409,500]) assert.equal(await coordinator.saveConfig(candidate,async()=>({status,ok:false,json:async()=>({})}),'old'),false);
  assert.equal(applied,original);
  coordinator.beforeSave=()=>coordinator.setRevision('changed');let sent=false;
  assert.equal(await coordinator.saveConfig(candidate,async()=>{sent=true;},'old'),false);assert.equal(sent,false);
 } finally {globalThis.fetch=originalFetch;w.dispose();}
});
