import test from 'node:test';
import assert from 'node:assert/strict';
import Blockly from '../src/blockly/index.js';
import {registerAutomationBlocks,automationWorkspace,serializeAutomation,installAutomationIdentityGuard,lifecycleSummary} from '../src/blockly/automationV2.js';
import {automationRequest,sparsePatch} from '../src/utils/automationAuthoring.js';
import {renameEffectInConfig} from '../src/utils/applyEffect.js';
const catalog=[{label:'base',reference:{kind:'profile_effect',name:'base'},available:true},{label:'published',reference:{kind:'plugin',name:'published'},available:true},{label:'draft',reference:{kind:'plugin',name:'draft'},available:false}];
registerAutomationBlocks(Blockly,catalog,['base'],['event.kill','event.damage']);
function rule(mode,kind='profile_effect') {return {id:mode,model:'automation_v2',when:{mode,condition:mode==='event'?{event:'event.kill'}:{field:'player.state.health',op:'<',value:20}},action:{type:'trigger_effect',lifetime:mode==='state'?'while_true':'one_shot',effect:{kind,name:kind==='plugin'?'published':'base'},priority:20,composition:'overlay',blend:'alpha',...(mode==='state'?{}:{retrigger:'queue'})}};}
function roundtrip(rules,check) {const w=new Blockly.Workspace();try{Blockly.serialization.workspaces.load(automationWorkspace(rules),w);check(serializeAutomation(w,catalog),w);}finally{w.dispose();}}
test('three containers author V2 directly with stable order and typed references',()=>{
 const rules=['state','rising','event'].map(m=>rule(m));
 roundtrip(rules,out=>{assert.deepEqual(out.map(r=>r.id),rules.map(r=>r.id));for(const [i,r]of out.entries()){assert.equal(r.model,'automation_v2');assert.equal(r.action.lifetime,i===0?'while_true':'one_shot');assert.equal(r.action.retrigger,i===0?undefined:'queue');for(const key of ['event','condition','process'])assert.equal(r.action[key],undefined);assert.deepEqual(r.action.effect,catalog[0].reference);}assert.equal(JSON.stringify(out[2]).match(/event.kill/g).length,1);});
 roundtrip([rule('event','plugin')],out=>assert.deepEqual(out[0].action.effect,catalog[1].reference));
});
test('unavailable references cannot silently save',()=>{const r=rule('event');r.action.effect=catalog[2].reference;assert.throws(()=>roundtrip([r],()=>{}),/已发布/);});
test('Activate Profile stays state-only with rule-level DND',()=>{
 const r=rule('state');r.action={type:'activate_profile',profile:'base'};r.dnd=true;roundtrip([r],out=>{assert.deepEqual(out[0].action,r.action);assert.equal(out[0].dnd,true);});
 const w=new Blockly.Workspace();try{const event=w.newBlock('av2_event_mode'),profile=w.newBlock('av2_profile');assert.equal(w.connectionChecker.canConnect(event.getInput('ACTION').connection,profile.previousConnection,false),false);}finally{w.dispose();}
});
test('nested WHEN and scope restore',()=>{const r=rule('event');r.when.condition={and:[{event:'event.kill'},{not:{field:'player.state.health',op:'==',value:0}}]};r.scope={field:'process',op:'==',value:'cs2.exe'};roundtrip([r],out=>{assert.deepEqual(out[0].when,r.when);assert.deepEqual(out[0].scope,r.scope);});});
test('transport preserves revisions and structured errors',async()=>{
 let sent;await automationRequest('v2/rules','PUT',{expected_revision:'old',rules:[]},async(url,options)=>{sent={url,options};return{ok:true,json:async()=>({revision:'new'})};});assert.equal(sent.options.method,'PUT');assert.equal(JSON.parse(sent.options.body).expected_revision,'old');
 const conflict={error:'revision_conflict',message:'Refresh'};await assert.rejects(automationRequest('v2/rules','PUT',{},async()=>({ok:false,json:async()=>conflict})),e=>e.result===conflict);assert.deepEqual(sparsePatch({a:1,b:2},{a:3}),{b:null,a:3});
});
test('effect rename preserves publication metadata and blocks referenced V2 effects',()=>{
 const c={blockly_effects:{old:{name:'old',applied_plugin_name:'immutable'}},profiles:{old:{type:'static',title:'old'}},default_profile:'old'};const renamed=renameEffectInConfig(c,'old','new');assert.equal(renamed.blockly_effects.new.name,'new');assert.equal(renamed.blockly_effects.new.applied_plugin_name,'immutable');assert.equal(renamed.profiles.new.title,'new');assert.equal(renamed.default_profile,'new');c.orchestration={rules:[{action:{type:'trigger_effect',effect:{kind:'profile_effect',name:'old'}}}]};assert.throws(()=>renameEffectInConfig(c,'old','new'),/Automation/);
});

test('retrigger control is hidden on state and visible on one-shot containers',()=>{roundtrip(['state','rising','event'].map(m=>rule(m)),(_,w)=>{for(const b of w.getAllBlocks(false).filter(b=>b.type==='av2_play')){b.updateTriggerVisibility();assert.equal(b.getInput('RETRIGGER').isVisible(),b.getSurroundParent().type!=='av2_state');}});});

test('duplicated rules get new stable IDs that survive workspace restore',async()=>{
 const w=new Blockly.Workspace();installAutomationIdentityGuard(Blockly,w);
 try{
  const aRule=rule('event');aRule.id='kill-wave';
  Blockly.serialization.workspaces.load(automationWorkspace([aRule]),w);
  await new Promise(resolve=>setTimeout(resolve,0));
  const root=w.getBlocksByType('av2_root',false)[0];
  const a=root.getInputTargetBlock('RULES');
  const duplicate=async source=>{
   const state=Blockly.serialization.blocks.save(source,{addNextBlocks:false,saveIds:false});
   const copy=Blockly.serialization.blocks.append(state,w,{recordUndo:true});
   source.nextConnection.connect(copy.previousConnection);
   await new Promise(resolve=>setTimeout(resolve,0));
   return copy;
  };
  const b=await duplicate(a);
  b.getInputTargetBlock('WHEN').setFieldValue('event.damage','EVENT');
  const firstSave=serializeAutomation(w,catalog);
  assert.notEqual(firstSave[0].id,firstSave[1].id);
  assert.equal(firstSave[1].when.condition.event,'event.damage');
  let submitted;
  const saveResult=await automationRequest('v2/rules','PUT',{expected_revision:'rev-a',rules:firstSave},async(_url,options)=>{
   submitted=JSON.parse(options.body);
   return {ok:true,json:async()=>({revision:'rev-b'})};
  });
  assert.equal(saveResult.revision,'rev-b');
  assert.equal(new Set(submitted.rules.map(r=>r.id)).size,2);

  const restoredState=Blockly.serialization.workspaces.save(w);
  const restored=new Blockly.Workspace();installAutomationIdentityGuard(Blockly,restored);
  try{
   Blockly.serialization.workspaces.load(restoredState,restored);
   await new Promise(resolve=>setTimeout(resolve,0));
   const restoredRules=serializeAutomation(restored,catalog);
   assert.deepEqual(restoredRules.map(r=>r.id),firstSave.map(r=>r.id));
   const restoredB=restored.getBlocksByType('av2_event_mode',false).find(block=>JSON.parse(block.data).id===firstSave[1].id);
   const c=await (async()=>{
    const state=Blockly.serialization.blocks.save(restoredB,{addNextBlocks:false,saveIds:false});
    const copy=Blockly.serialization.blocks.append(state,restored,{recordUndo:true});
    restoredB.nextConnection.connect(copy.previousConnection);
    await new Promise(resolve=>setTimeout(resolve,0));
    return copy;
   })();
   assert.ok(c);
   const finalRules=serializeAutomation(restored,catalog);
   assert.equal(new Set(finalRules.map(r=>r.id)).size,3);
   assert.deepEqual(finalRules.slice(0,2).map(r=>r.id),firstSave.map(r=>r.id));
  }finally{restored.dispose();}
 }finally{w.dispose();}
});

test('serializer rejects duplicate stable IDs instead of silently rewriting them',()=>{
 const w=new Blockly.Workspace();
 try{
  Blockly.serialization.workspaces.load(automationWorkspace([rule('event'),rule('event')]),w);
  assert.throws(()=>serializeAutomation(w,catalog),/规则 ID 重复/);
 }finally{w.dispose();}
});

test('common fields restore to friendly selector while unknown fields preserve their canonical values',async()=>{
 const fields=[
  {key:'process.name',label:'前台程序',category:'系统',description:'前台可执行文件',type:'string',operators:['==','!=']},
  {key:'player.state.health',label:'生命值',category:'玩家',description:'生命值',type:'number',operators:['==','!=','<','<=','>','>=']},
  {key:'player.state.helmet',label:'头盔',category:'玩家',description:'是否有头盔',type:'bool',operators:['==','!=']},
  {key:'round.phase',label:'回合阶段',category:'回合',description:'阶段',type:'string',operators:['==','!=','contains'],values:{freezetime:'冻结时间',live:'回合进行中',over:'回合结束'}}
 ];
 registerAutomationBlocks(Blockly,catalog,['base'],[{id:'event.kill',label:'击杀',category:'战斗',description:'发生一次'}],fields);
 const rules=['player.state.health','process.name','player.state.helmet','round.phase','custom.dynamic.leaf'].map((key,i)=>{
  const r=rule('state');r.id=`field-${i}`;r.when.condition={field:key,op:key==='player.state.health'?'<':key==='round.phase'?'contains':'==',value:[20,'cs2.exe',true,'live','任意值'][i]};return r;
 });
 const w=new Blockly.Workspace();
 try {
  Blockly.serialization.workspaces.load(automationWorkspace(rules,fields),w);
  assert.equal(w.getBlocksByType('av2_common_compare',false).length,4);
  assert.equal(w.getBlocksByType('av2_compare',false).length,1);
  const restored=serializeAutomation(w,catalog,fields);
  assert.deepEqual(restored.map(r=>r.when.condition),rules.map(r=>r.when.condition));
  const process=w.getBlocksByType('av2_common_compare',false).find(b=>b.getFieldValue('FIELD')==='process.name');
  assert.deepEqual(process.getField('OP').getOptions().map(([,op])=>op),['==','!=']);
  process.getField('OP').setValue('<');
  assert.equal(process.getFieldValue('OP'),'==');
  const health=w.getBlocksByType('av2_common_compare',false).find(b=>b.getFieldValue('FIELD')==='player.state.health');
  assert.deepEqual(health.getField('OP').getOptions().map(([,op])=>op),['==','!=','<','<=','>','>=']);
  assert.equal(health.getFieldValue('OP'),'<');
  health.setFieldValue('player.state.helmet','FIELD');
  await new Promise(resolve=>setTimeout(resolve,0));
  assert.deepEqual(health.getField('OP').getOptions().map(([,op])=>op),['==','!=']);
  assert.equal(health.getFieldValue('OP'),'==');
  health.setFieldValue('round.phase','FIELD');
  await new Promise(resolve=>setTimeout(resolve,0));
  health.setFieldValue('contains','OP');
  assert.equal(health.getFieldValue('OP'),'contains');
  assert.deepEqual(health.getField('OP').getOptions().map(([,op])=>op),['==','!=','contains']);
  health.setFieldValue('player.state.health','FIELD');
  await new Promise(resolve=>setTimeout(resolve,0));
  assert.equal(health.getFieldValue('OP'),'==');
  assert.deepEqual(health.getField('OP').getOptions().map(([,op])=>op),['==','!=','<','<=','>','>=']);
  const custom=w.getBlocksByType('av2_compare',false)[0];
  assert.equal(custom.getFieldValue('FIELD'),'custom.dynamic.leaf');
  assert.deepEqual(custom.getField('OP').getOptions().map(([,op])=>op),['==','!=','<','<=','>','>=','contains']);
  health.setFieldValue('abc','VALUE');
  assert.throws(()=>serializeAutomation(w,catalog,fields),/需要数值/);
 } finally {w.dispose();}
});

test('event labels and lifecycle summaries never change canonical action/event data',()=>{
 const once={...catalog[0],lifecycle:{kind:'studio_publication',publication:{mode:'one_shot',fade_out_ms:300}}};
 const continuous={...catalog[0],lifecycle:{kind:'studio_publication',publication:{mode:'continuous',fade_out_ms:0}}};
 const native={...catalog[0],lifecycle:{kind:'lifecycle_capable'}};
 assert.match(lifecycleSummary(once),/单次光效.*300 ms/);
 assert.equal(lifecycleSummary(continuous),'持续光效');
 assert.match(lifecycleSummary(native),/原生生命周期/);
 assert.match(lifecycleSummary(catalog[0]),/1200 ms.*400 ms/);
 assert.match(lifecycleSummary(catalog[0]),/主机兼容生命周期/);
 assert.doesNotMatch(lifecycleSummary(catalog[0]),/Host compatibility envelope/);
 const r=rule('event');
 const w=new Blockly.Workspace();
 try {
  Blockly.serialization.workspaces.load(automationWorkspace([r]),w);
  const event=w.getBlocksByType('av2_event',false)[0];
  assert.equal(event.getFieldValue('EVENT'),'event.kill');
  assert.ok(event.getField('EVENT').getOptions().some(([label,id])=>label==='战斗 · 击杀' && id==='event.kill'));
  assert.equal(serializeAutomation(w,catalog)[0].when.condition.event,'event.kill');
 } finally {w.dispose();}
});
