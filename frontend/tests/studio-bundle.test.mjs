import test from 'node:test';
import assert from 'node:assert/strict';
import { registerCustomBlocks } from '../src/blockly/customBlocks.js';
import { EFFECT_PRESETS } from '../src/blockly/presets.js';
import { createBundlePayload, validateBundlePayload, importedDraft, nextImportedName } from '../src/utils/studioBundle.js';
import { numericNodes, assistantContext } from '../src/utils/studioAssistant.js';
import { STUDIO_COMMAND_DEFINITIONS, filterStudioCommands, commandAvailability, listenStudioCommands } from '../src/utils/studioHost.js';
registerCustomBlocks();
const project = () => ({ name: 'fixture', json: structuredClone(EFFECT_PRESETS[0].blocklyJson), publication: { mode: 'continuous', fade_out_ms: 0 } });
test('all current presets roundtrip through source-only payload, old bare block arrays too', () => {
  for (const preset of EFFECT_PRESETS) {
    const p = {...project(), json: structuredClone(preset.blocklyJson), recovery: 'not exported', provider_api_key: 'not exported', applied_plugin: 'not exported'};
    const b = createBundlePayload(p, {description:'分享光效', author:'Owner', tags:['local']});
    assert.deepEqual(validateBundlePayload(JSON.parse(JSON.stringify(b))), b);
    assert.deepEqual(Object.keys(b.project).sort(), ['json','name','publication']);
    assert.ok(!JSON.stringify(b).includes('not exported'));
    const old = structuredClone(p); old.json = old.json.blocks?.blocks || old.json.blocks;
    validateBundlePayload(createBundlePayload(old));
  }
});
test('bundle graph is authoritative; malformed claims and unsupported nodes rejected', () => {
  const b = createBundlePayload(project()); b.manifest.capabilities.inputs.push('cs2_gsi'); assert.throws(()=>validateBundlePayload(b));
  for (const mutate of [b=>b.manifest.schema_version=2, b=>b.project.name='../bad', b=>b.project.json.blocks=[{type:'shell_exec'}], b=>b.manifest.extra='bad']) {
    const b = createBundlePayload(project()); mutate(b); assert.throws(()=>validateBundlePayload(b));
  }
});
test('bundle regenerates only block IDs and preserves variable identity and references', () => {
  const p=project(); p.json.variables=[{name:'level',id:'variable_level',type:''}];
  const blocks=Array.isArray(p.json.blocks)?p.json.blocks:p.json.blocks.blocks;
  blocks.push({type:'variables_set',id:'opaque_block_id',fields:{VAR:{id:'variable_level'}},inputs:{VALUE:{shadow:{type:'math_number',id:'opaque_shadow_id',fields:{NUM:1}}}}});
  const b=createBundlePayload(p); assert.equal(b.project.json.variables[0].id,'variable_level');
  const saved=(Array.isArray(b.project.json.blocks)?b.project.json.blocks:b.project.json.blocks.blocks).at(-1);
  assert.equal(saved.fields.VAR.id,'variable_level'); assert.ok(!Object.hasOwn(saved,'id')); assert.ok(!Object.hasOwn(saved.inputs.VALUE.shadow,'id'));
  assert.equal(p.json.variables[0].id,'variable_level'); assert.equal(blocks.at(-1).id,'opaque_block_id');
});
test('secret and path exclusion in graph and metadata, size bounds', () => {
  for (const value of ['sk-fixtureabcdef123456', 'Bearer abcdef', 'api_key=private', 'C:/Users/private', '/home/owner/private', '<script>bad']) {
    const b = createBundlePayload(project()); b.manifest.description=value; assert.throws(()=>validateBundlePayload(b));
  }
  const b = createBundlePayload(project()); b.project.json.api_key='private'; assert.throws(()=>validateBundlePayload(b));
  const large=createBundlePayload(project()); large.project.json.extra='x'.repeat(300000); assert.throws(()=>validateBundlePayload(large));
});
test('import creates an independent draft and cannot overwrite or publish', () => {
  const b=createBundlePayload(project()), catalog={fixture:{published:'unchanged'}};
  assert.throws(()=>importedDraft(b,catalog,'fixture')); const name=nextImportedName('fixture',catalog);
  const draft=importedDraft(b,catalog,name); draft.json.extra='new'; assert.ok(!b.project.json.extra);
  assert.deepEqual(catalog,{fixture:{published:'unchanged'}}); assert.ok(!('applied_plugin' in draft));
});
test('palette discovers all required commands and searches Chinese and English', () => {
  assert.equal(STUDIO_COMMAND_DEFINITIONS.length,13); assert.equal(new Set(STUDIO_COMMAND_DEFINITIONS.map(c=>c.id)).size,13);
  assert.equal(filterStudioCommands('Test Bench')[0].id,'bench'); assert.equal(filterStudioCommands('验证')[0].id,'validate'); assert.deepEqual(filterStudioCommands('impossible command'),[]);
});
test('command dispatch shares safety gates for native and local surfaces; no duplicate execution', () => {
  let receiver; const webview={addEventListener:(t,f)=>receiver=f,removeEventListener:()=>{}}; let count=0;
  const state={validation:'错误',workType:'effect',embedded:true}; const dispose=listenStudioCommands(webview,{publish:()=>count++},()=>state);
  receiver({data:{type:'studio_command',command:'publish'}}); assert.equal(count,0);
  state.validation='验证通过'; receiver({data:{type:'studio_command',command:'publish'}}); assert.equal(count,1);
  state.busy=true; receiver({data:{type:'studio_command',command:'publish'}}); assert.equal(count,1); dispose();
  assert.ok(commandAvailability('restore_snapshot',{hasSnapshots:false})); assert.ok(commandAvailability('export_bundle',{embedded:false}));
  assert.ok(commandAvailability('publish',{recovery:true})); assert.ok(commandAvailability('shell',{}));
});
test('AI sends only selected preset and active nodes; non-generating contexts have no preset catalog', () => {
  const p=project(); for(const intent of ['modify','explain','error_analysis']) assert.deepEqual(assistantContext(p,intent).presets,[]);
  const c=assistantContext(p,'generate','','validation',EFFECT_PRESETS[0].id); assert.equal(c.presets.length,1); assert.equal(c.presets[0].id,EFFECT_PRESETS[0].id);
  const nodes=numericNodes({blocks:[{type:'color_rgb',inputs:{R:{shadow:{type:'math_number',fields:{NUM:99}},block:{type:'math_number',fields:{NUM:42}}}}}]});
  assert.equal(nodes.length,1); assert.equal(nodes[0].value,42); assert.deepEqual(c.capabilities.gsi_fields,[]);
});
