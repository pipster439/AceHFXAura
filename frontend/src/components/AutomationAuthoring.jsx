import React,{useEffect,useLayoutEffect,useRef,useState} from 'react';
import Blockly from '../blockly/index.js';
import { dismissForOverlay } from '../blockly/dismissForOverlay.js';
import {DEFAULT_INJECT_OPTIONS} from '../blockly/theme.js';
import {registerAutomationBlocks,automationToolbox,automationWorkspace,serializeAutomation,effectKey,installAutomationIdentityGuard} from '../blockly/automationV2.js';
import {automationRequest} from '../utils/automationAuthoring.js';

export default function AutomationAuthoring({onConfigChanged,overlayOpen=false}) {
  const host=useRef(null),workspace=useRef(null),catalog=useRef([]),fields=useRef([]),events=useRef([]),revision=useRef('');
  const overlayOpenRef=useRef(overlayOpen),restoreOverlayRef=useRef(null);
  overlayOpenRef.current=overlayOpen;
  useLayoutEffect(()=>{
    restoreOverlayRef.current?.();
    restoreOverlayRef.current=overlayOpen ? dismissForOverlay(workspace.current) : null;
  },[overlayOpen]);
  const [error,setError]=useState(''),[busy,setBusy]=useState(false),[ready,setReady]=useState(false),[preview,setPreview]=useState('');
  const load=async()=>{
    const [data,caps,effects]=await Promise.all([automationRequest('v2/records'),automationRequest('v2/capabilities'),automationRequest('v2/effects')]);
    if(data.revision!==effects.revision)throw new Error('配置已变化，请重新加载');
    catalog.current=[...effects.effects];
    fields.current=caps.field_metadata || [];
    events.current=caps.event_metadata || caps.events;
    for(const {record} of data.records)if(record.action.type==='trigger_effect'&&!catalog.current.some(e=>effectKey(e.reference)===effectKey(record.action.effect)))catalog.current.push({label:record.action.effect.name,reference:record.action.effect,available:false});
    restoreOverlayRef.current?.(); restoreOverlayRef.current=null;
    workspace.current?.dispose(); workspace.current=null;
    registerAutomationBlocks(Blockly,catalog.current,Object.keys(data.profiles),events.current,fields.current);
    workspace.current=Blockly.inject(host.current,{...DEFAULT_INJECT_OPTIONS,toolbox:automationToolbox});
    installAutomationIdentityGuard(Blockly,workspace.current);
    Blockly.serialization.workspaces.load(automationWorkspace(data.records.map(r=>r.record),fields.current),workspace.current);
    for(const block of workspace.current.getAllBlocks(false))block.updateTriggerVisibility?.();
    Blockly.svgResize(workspace.current);
    if(overlayOpenRef.current)restoreOverlayRef.current=dismissForOverlay(workspace.current);
    requestAnimationFrame(()=>{if(workspace.current){Blockly.svgResize(workspace.current);workspace.current.zoomToFit();}});
    revision.current=data.revision;setReady(true);
  };
  const run=async work=>{setBusy(true);setError('');try{await work();}catch(e){setError(e.message);}finally{setBusy(false);}};
  useEffect(()=>{run(load);const observer=new ResizeObserver(()=>{if(workspace.current)Blockly.svgResize(workspace.current);});observer.observe(host.current);return()=>{observer.disconnect();restoreOverlayRef.current?.();workspace.current?.dispose();};},[]);
  const save=()=>run(async()=>{
    const rules=serializeAutomation(workspace.current,catalog.current,fields.current);
    const result=await automationRequest('v2/rules','PUT',{expected_revision:revision.current,rules});
    revision.current=result.revision;await load();await onConfigChanged?.();
  });
  return <section className="flex flex-col flex-1 h-full min-h-0 gap-2 p-2 text-md-on-surface">
    <h2 className="text-lg font-bold">自动化</h2>
    <div className="flex gap-4"><button disabled={busy} onClick={()=>run(load)}>重新加载</button><button disabled={busy||!ready} onClick={save}>保存并应用</button><button disabled={!ready} onClick={()=>run(async()=>setPreview(JSON.stringify(serializeAutomation(workspace.current,catalog.current,fields.current),null,2)))}>查看 V2 JSON</button></div>
    <details className="rounded-md-lg border border-md-outline-variant px-3 py-2 text-xs"><summary className="cursor-pointer font-semibold">字段与事件说明</summary>
      <p className="mt-2">常用字段显示中文名称，保存时使用原始字段 key；未知遥测请用“自定义字段”。事件每发生一次只触发一次。</p>
      <div className="mt-2 grid max-h-40 gap-2 overflow-auto sm:grid-cols-2">
        <div><strong>常用字段</strong>{fields.current.map(f=><p key={f.key}>{f.category} · {f.label}（{f.key}）：{f.description}{f.values ? ` 可用值：${Object.entries(f.values).map(([key,label])=>`${label} (${key})`).join('、')}` : ''}</p>)}</div>
        <div><strong>事件</strong>{events.current.map(e=><p key={e.id || e}>{e.category} · {e.label}（{e.id}）：{e.description}</p>)}</div>
      </div>
    </details>
    {error&&<p role="alert">{error}</p>}
    <div ref={host} className="w-full flex-1 min-h-[280px]" style={{position:'relative'}}/>
    {preview&&<details open><summary>V2 记录</summary><pre className="overflow-auto">{preview}</pre></details>}
  </section>;
}
