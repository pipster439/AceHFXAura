// Direct V2 authoring. The daemon parser/resolver is the validation authority.
export const effectKey = reference => JSON.stringify(reference);
const choices = rows => rows.length ? rows : [['暂无已发布光效', '']];
const ruleBlockTypes = new Set(['av2_state','av2_rising','av2_event_mode']);

const ruleMetadata = block => {
  if(!block?.data)return {};
  try{return JSON.parse(block.data);}catch{return {};}
};

const ruleIdentity = block => ruleMetadata(block).id || block.id;

export function installAutomationIdentityGuard(Blockly, workspace) {
  const listener=event=>{
    if(event.type!==Blockly.Events.BLOCK_CREATE || !event.recordUndo)return;
    for(const id of event.ids||[]) {
      const block=workspace.getBlockById(id);
      if(!ruleBlockTypes.has(block?.type))continue;
      const original=ruleMetadata(block);
      // Blockly Duplicate/copy-paste clones block.data. A copied rule is a new
      // identity even when the source no longer exists in this workspace.
      if(original.id)block.data=JSON.stringify({...original,id:block.id});
    }
  };
  workspace.addChangeListener(listener);
  return ()=>workspace.removeChangeListener(listener);
}

export function lifecycleSummary(entry, mode = 'event') {
  const lifecycle = entry?.lifecycle;
  if (lifecycle?.kind === 'studio_publication') {
    const publication = lifecycle.publication;
    return publication?.mode === 'one_shot'
      ? `单次光效 · 序列结束后淡出 ${publication.fade_out_ms} ms`
      : '持续光效';
  }
  if (lifecycle?.kind === 'lifecycle_capable') return '支持原生生命周期的插件';
  return mode === 'state' ? '旧式光效 · 条件成立期间运行' : '兼容单次播放 · 主机兼容生命周期（约 1200 ms，末尾约 400 ms 淡出）';
}

const comparisonOps = ['==','!=','<','<=','>','>=','contains'];
const fieldOptions = fields => choices(fields.map(f => [`${f.category} · ${f.label}`, f.key]));
export function registerAutomationBlocks(Blockly, catalog, profiles, events, fields = []) {
  const eventRows = events.map(e => typeof e === 'string' ? {id:e,label:e,category:'事件',description:e} : e);
  const eventMap = new Map(eventRows.map(e => [e.id,e]));
  const fieldMap = new Map(fields.map(f => [f.key,f]));
  const definitions = [
    {type:'av2_root', message0:'自动化规则 %1', args0:[{type:'input_statement',name:'RULES',check:'V2Rule'}],colour:210},
    ...[['state','当条件成立期间'],['rising','当条件首次成立时'],['event','当事件发生时']].map(([mode,label])=>({
      type:mode==='event'?'av2_event_mode':`av2_${mode}`,message0:`${label} %1`,args0:[{type:'input_value',name:'WHEN',check:'V2Condition'}],
      message1:'仅在范围内 %1',args1:[{type:'input_value',name:'SCOPE',check:'V2Condition'}],
      message2:'启用 %1 执行 %2',args2:[{type:'field_checkbox',name:'ENABLED',checked:true},{type:'input_statement',name:'ACTION',check:mode==='state'?['V2Play','V2Profile']:'V2Play'}],
      previousStatement:'V2Rule',nextStatement:'V2Rule',colour:210})),
    {type:'av2_profile',message0:'激活方案 %1 免打扰 %2',args0:[{type:'field_dropdown',name:'PROFILE',options:choices(profiles.map(p=>[p,p]))},{type:'field_checkbox',name:'DND',checked:false}],previousStatement:'V2Profile',colour:160},
    {type:'av2_event',message0:'事件 %1',args0:[{type:'field_dropdown',name:'EVENT',options:choices(eventRows.map(e=>[`${e.category} · ${e.label}`,e.id]))}],output:'V2Condition',colour:40},
    {type:'av2_common_compare',message0:'常用字段 %1 %2 值 %3',args0:[{type:'field_dropdown',name:'FIELD',options:fieldOptions(fields)},{type:'field_dropdown',name:'OP',options:comparisonOps.map(x=>[x,x])},{type:'field_input',name:'VALUE',text:'20'}],output:'V2Condition',colour:40},
    {type:'av2_compare',message0:'自定义字段 %1 %2 值 %3',args0:[{type:'field_input',name:'FIELD',text:'player.state.health'},{type:'field_dropdown',name:'OP',options:comparisonOps.map(x=>[x,x])},{type:'field_input',name:'VALUE',text:'20'}],output:'V2Condition',colour:40},
    ...['and','or'].map(op=>({type:`av2_${op}`,message0:`%1 ${op==='and'?'且':'或'} %2`,args0:[{type:'input_value',name:'A',check:'V2Condition'},{type:'input_value',name:'B',check:'V2Condition'}],output:'V2Condition',colour:40})),
    {type:'av2_not',message0:'非 %1',args0:[{type:'input_value',name:'A',check:'V2Condition'}],output:'V2Condition',colour:40}
  ];
  Blockly.defineBlocksWithJsonArray(definitions);
  Blockly.Blocks.av2_event.init = function() {
    this.jsonInit(definitions.find(d => d.type === 'av2_event'));
    this.setTooltip(() => eventMap.get(this.getFieldValue('EVENT'))?.description || '事件发生一次，对应一次触发。');
  };
  Blockly.Blocks.av2_common_compare.init = function() {
    this.jsonInit(definitions.find(d => d.type === 'av2_common_compare'));
    const operatorField = this.getField('OP');
    operatorField.setOptions(function() {
      const field = fieldMap.get(this.getSourceBlock()?.getFieldValue('FIELD'));
      return (field?.operators?.length ? field.operators : comparisonOps).map(op => [op,op]);
    });
    this.setOnChange(() => {
      const options = operatorField.getOptions();
      if (!options.some(([,op]) => op === this.getFieldValue('OP'))) this.setFieldValue(options[0][1], 'OP');
    });
    this.setTooltip(() => {
      const field = fieldMap.get(this.getFieldValue('FIELD'));
      if (!field) return '选择一个常用字段；未知字段请使用自定义字段积木。';
      const values = field.values ? ` 可用值：${Object.entries(field.values).map(([key,label]) => `${label} (${key})`).join('、')}。` : '';
      return `${field.description} 类型：${field.type}。${values}`;
    });
  };
  Blockly.Blocks.av2_play={init() {
    this.appendDummyInput().appendField('播放光效').appendField(new Blockly.FieldDropdown(choices(catalog.map(e=>[`${e.label}${e.available?'':'（不可用）'}`,effectKey(e.reference)]))),'EFFECT');
    this.appendDummyInput('LIFECYCLE').appendField('生命周期：待选择');
    this.appendDummyInput().appendField('优先级').appendField(new Blockly.FieldNumber(20,-2147483648,2147483647,1),'PRIORITY');
    this.appendDummyInput().appendField('合成方式').appendField(new Blockly.FieldDropdown([
      ['透明度混合（非黑覆盖）','overlay/alpha'],['亮度叠加','overlay/additive'],['完全覆盖','replace/alpha'],['亮度叠加（全覆盖）','replace/additive']]),'COMPOSITION');
    this.appendDummyInput('RETRIGGER').appendField('重触发').appendField(new Blockly.FieldDropdown([['重新开始','restart'],['活动时忽略','ignore_while_active'],['叠放','stack'],['排队','queue']]),'RETRIGGER');
    this.setPreviousStatement(true,'V2Play'); this.setColour(160);
    this.updateTriggerVisibility=()=>{const parent=this.getSurroundParent();const visible=!!parent && parent.type!=='av2_state';const input=this.getInput('RETRIGGER');if(input.isVisible()!==visible){input.setVisible(visible);if(this.rendered)this.render();}};
    this.setOnChange(()=>{
      this.updateTriggerVisibility();
      const selected = catalog.find(e => effectKey(e.reference) === this.getFieldValue('EFFECT'));
      const mode = this.getSurroundParent()?.type === 'av2_state' ? 'state' : 'event';
      const label = `生命周期：${lifecycleSummary(selected, mode)}`;
      const field = this.getInput('LIFECYCLE')?.fieldRow?.[0];
      if (field?.getValue() !== label) field?.setValue(label);
    });
  }};
}

export const automationToolbox={kind:'flyoutToolbox',contents:['root','state','rising','event','play','profile','common_compare','compare','event_leaf','and','or','not'].map(name=>({kind:'block',type:name==='event_leaf'?'av2_event':'av2_'+(name==='event'?'event_rule':name)}))};
// Event containers and event leaves have deliberately different block identities.
automationToolbox.contents[3].type='av2_event_mode';

export function serializeAutomation(workspace, catalog, fields = []) {
  const fieldMap = new Map(fields.map(f => [f.key, f]));
  const roots=workspace.getTopBlocks(true);
  if(roots.length!==1 || roots[0].type!=='av2_root') throw new Error('请使用一个自动化根节点，并连接所有规则');
  const condition=block=>{
    if(!block) throw new Error('请连接触发条件');
    const field=n=>block.getFieldValue(n);
    if(block.type==='av2_event') return {event:field('EVENT')};
    if(block.type==='av2_compare' || block.type==='av2_common_compare') {
      const key=field('FIELD'),op=field('OP');
      const metadata=block.type==='av2_common_compare' ? fieldMap.get(key) : null;
      if(metadata?.operators && !metadata.operators.includes(op)) throw new Error(`${metadata.label} 仅支持 ${metadata.operators.join(' / ')}`);
      let value;try{value=JSON.parse(field('VALUE'));}catch{value=field('VALUE');}
      if(metadata?.type==='number' && (typeof value!=='number' || !Number.isFinite(value))) throw new Error(`${metadata.label} 需要数值`);
      if(metadata?.type==='bool' && typeof value!=='boolean') throw new Error(`${metadata.label} 需要 true 或 false`);
      if(metadata?.type==='string' && typeof value!=='string') value=field('VALUE');
      return {field:key,op,value};
    }
    if(block.type==='av2_not') return {not:condition(block.getInputTargetBlock('A'))};
    if(['av2_and','av2_or'].includes(block.type)) return {[block.type.slice(4)]:[condition(block.getInputTargetBlock('A')),condition(block.getInputTargetBlock('B'))]};
    throw new Error('不支持的条件积木');
  };
  const rules=[];
  const ids=new Set();
  for(let block=roots[0].getInputTargetBlock('RULES');block;block=block.getNextBlock()) {
    const mode=block.type==='av2_event_mode'?'event':block.type.slice(4);
    if(!['state','rising','event'].includes(mode)) throw new Error('不支持的触发模式');
    const original=block.data?JSON.parse(block.data):{};
    const rule={...original,id:ruleIdentity(block),model:'automation_v2',enabled:block.getFieldValue('ENABLED')==='TRUE',when:{mode,condition:condition(block.getInputTargetBlock('WHEN'))}};
    if(ids.has(rule.id))throw new Error(`规则 ID 重复: ${rule.id}。请重新复制该规则`);
    ids.add(rule.id);
    delete rule.scope; delete rule.dnd;
    if(block.getInputTargetBlock('SCOPE'))rule.scope=condition(block.getInputTargetBlock('SCOPE'));
    const action=block.getInputTargetBlock('ACTION');
    if(!action || action.getNextBlock()) throw new Error('每条规则必须有一个动作');
    if(action.type==='av2_profile') {
      if(mode!=='state')throw new Error('激活方案仅支持持续条件');
      rule.action={type:'activate_profile',profile:action.getFieldValue('PROFILE')};rule.dnd=action.getFieldValue('DND')==='TRUE';
    } else if(action.type==='av2_play') {
      const selected=catalog.find(e=>effectKey(e.reference)===action.getFieldValue('EFFECT'));
      if(!selected?.available)throw new Error('请选择可运行的已发布光效');
      const [composition,blend]=action.getFieldValue('COMPOSITION').split('/');
      const retained=original.action?.type==='trigger_effect'?original.action:{};
      rule.action={...retained,type:'trigger_effect',effect:selected.reference,lifetime:mode==='state'?'while_true':'one_shot',priority:Number(action.getFieldValue('PRIORITY')),composition,blend};
      delete rule.action.retrigger;
      if(mode!=='state')rule.action.retrigger=action.getFieldValue('RETRIGGER');
      else delete rule.action.watchdog_ms;
    } else throw new Error('不支持的动作');
    rules.push(rule);
  }
  return rules;
}

export function automationWorkspace(rules, fields = []) {
  const knownFields = new Set(fields.map(f => f.key));
  const input=block=>({block});
  const condition=c=>{
    if(c.event)return {type:'av2_event',fields:{EVENT:c.event}};
    if(c.field)return {type:knownFields.has(c.field)?'av2_common_compare':'av2_compare',fields:{FIELD:c.field,OP:c.op||'==',VALUE:knownFields.has(c.field) && typeof c.value==='string'?c.value:JSON.stringify(c.value)}};
    const op=c.and?'and':c.or?'or':c.not?'not':c.type||c.op;
    const children=c[op] || c.conditions || (c.condition?[c.condition]:[]);
    if(op==='not')return {type:'av2_not',inputs:{A:input(condition(c.not||children[0]))}};
    if(!['and','or'].includes(op)||!Array.isArray(children)||!children.length)throw new Error('无法恢复条件');
    return children.map(condition).reduce((a,b)=>({type:`av2_${op}`,inputs:{A:input(a),B:input(b)}}));
  };
  let first=null,last=null;
  for(const rule of rules) {
    const a=rule.action,mode=rule.when.mode;
    const action=a.type==='activate_profile'?{type:'av2_profile',fields:{PROFILE:a.profile,DND:rule.dnd?'TRUE':'FALSE'}}:
      {type:'av2_play',fields:{EFFECT:effectKey(a.effect),PRIORITY:a.priority??10,COMPOSITION:`${a.composition||'overlay'}/${a.blend||'alpha'}`,RETRIGGER:a.retrigger||'restart'}};
    const block={type:mode==='event'?'av2_event_mode':`av2_${mode}`,data:JSON.stringify(rule),fields:{ENABLED:rule.enabled===false?'FALSE':'TRUE'},inputs:{WHEN:input(condition(rule.when.condition)),ACTION:input(action)}};
    if(rule.scope)block.inputs.SCOPE=input(condition(rule.scope));
    if(last)last.next=input(block);else first=block;last=block;
  }
  return {blocks:{languageVersion:0,blocks:[{type:'av2_root',inputs:first?{RULES:input(first)}:{}}]}};
}
