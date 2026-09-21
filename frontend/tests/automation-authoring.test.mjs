import test from 'node:test';
import assert from 'node:assert/strict';
import Blockly from '../src/blockly/index.js';
import {OrchestratorSerializer} from '../src/blockly/orchestratorSerializer.js';
import {canonicalConfig,hasAutomationV2,replaceSimpleRows,processRows,gsiRows} from '../src/utils/orchestration.js';
import {automationRequest,sparsePatch,ruleForPairing} from '../src/utils/automationAuthoring.js';
import {effectConfig} from '../src/utils/applyEffect.js';

function config() {return {unknown:{nested:[1,null,'retain']},rules:[{process:'cs2',profile:'base',suppress_web_ui:true}],gsi_bindings:[],profiles:{base:{type:'static'}},
  orchestration:{extra:true,rules:[{id:'stable',model:'automation_v2',future_metadata:{keep:true},when:{mode:'state',condition:{field:'process',value:'cs2'}},action:{type:'activate_profile',profile:'base'}}]}};}
test('opening projections never migrates or rewrites V2 configs; legacy serializer fails before clearing',()=>{
 const original=config(),bytes=JSON.stringify(original);
 assert.equal(hasAutomationV2(original),true);assert.equal(canonicalConfig(original),original);
 assert.equal(processRows(original),original.rules);assert.equal(gsiRows(original),original.gsi_bindings);
 for(const kind of ['process','gsi']) assert.throws(()=>replaceSimpleRows(original,kind,[]),/只读/);
 const workspace=new Blockly.Workspace();workspace.newBlock('math_number');
 try {assert.throws(()=>OrchestratorSerializer.restoreWorkspace(workspace,original),/只读/);assert.equal(workspace.getAllBlocks(false).length,1);}finally{workspace.dispose();}
 assert.equal(JSON.stringify(original),bytes);
 const unrelated=effectConfig(original,'new_effect',{});
 assert.equal(JSON.stringify(unrelated.orchestration),JSON.stringify(original.orchestration));
 assert.equal(JSON.stringify(unrelated.rules),JSON.stringify(original.rules));assert.equal(JSON.stringify(original),bytes);
});
test('sparse edits preserve unknown fields and explicitly remove only deleted fields',()=>{
 const before=config().orchestration.rules[0],after=structuredClone(before);after.action.profile='other';after.description='label';
 assert.deepEqual(sparsePatch(before,after),{action:{profile:'other'},description:'label'});
 delete after.future_metadata;assert.equal(sparsePatch(before,after).future_metadata,null);
});
test('client uses advertised pairings without implementing schema validation',()=>{
 for(const pair of [{mode:'state',action:'activate_profile'},{mode:'state',action:'trigger_effect',lifetime:'while_true'},
  {mode:'rising',action:'trigger_effect',lifetime:'one_shot'},{mode:'event',action:'trigger_effect',lifetime:'one_shot'}]) {
  const value=ruleForPairing(pair,'base');assert.ok(value.id);assert.equal(value.when.mode,pair.mode);assert.equal(value.action.type,pair.action);
  assert.equal(value.action.lifetime,pair.lifetime);assert.equal(value.model,'automation_v2');
 }
});
test('transport preserves revision, stable identity, sparse patch and structured conflicts',async()=>{
 const request={expected_revision:'original',id:'stable',patch:{description:'changed'}};let sent;
 const result=await automationRequest('v2/rules','PATCH',request,async(url,options)=>{sent={url,options};return {ok:true,json:async()=>({revision:'next'})};});
 assert.equal(sent.url,'/api/automation/v2/rules');assert.equal(sent.options.method,'PATCH');assert.deepEqual(JSON.parse(sent.options.body),request);assert.equal(result.revision,'next');
 const conflict={error:'shadow_acknowledgement_required',errors:[{path:'/acknowledge_shadowing',rule_id:'stable',code:'shadow_acknowledgement_required',message:'Review'}],shadowing:[{category:'same_process_different_profile'}]};
 await assert.rejects(automationRequest('v2/rules','POST',{},async()=>({ok:false,json:async()=>conflict})),error=>error.result===conflict);
});
