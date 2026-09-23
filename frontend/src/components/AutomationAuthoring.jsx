import React,{useEffect,useLayoutEffect,useRef,useState} from 'react';
import Blockly from '../blockly/index.js';
import { dismissForOverlay } from '../blockly/dismissForOverlay.js';
import {DEFAULT_INJECT_OPTIONS} from '../blockly/theme.js';
import {registerAutomationBlocks,automationToolbox,automationWorkspace,serializeAutomation,effectKey,installAutomationIdentityGuard} from '../blockly/automationV2.js';
import {automationRequest} from '../utils/automationAuthoring.js';
import GsiSimulation from './GsiSimulation.jsx';

export default function AutomationAuthoring({onConfigChanged,overlayOpen=false}) {
  const host=useRef(null),workspace=useRef(null),catalog=useRef([]),revision=useRef('');
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
    for(const {record} of data.records)if(record.action.type==='trigger_effect'&&!catalog.current.some(e=>effectKey(e.reference)===effectKey(record.action.effect)))catalog.current.push({label:record.action.effect.name,reference:record.action.effect,available:false});
    restoreOverlayRef.current?.(); restoreOverlayRef.current=null;
    workspace.current?.dispose(); workspace.current=null;
    registerAutomationBlocks(Blockly,catalog.current,Object.keys(data.profiles),caps.events);
    workspace.current=Blockly.inject(host.current,{...DEFAULT_INJECT_OPTIONS,toolbox:automationToolbox});
    installAutomationIdentityGuard(Blockly,workspace.current);
    Blockly.serialization.workspaces.load(automationWorkspace(data.records.map(r=>r.record)),workspace.current);
    for(const block of workspace.current.getAllBlocks(false))block.updateTriggerVisibility?.();
    Blockly.svgResize(workspace.current);
    if(overlayOpenRef.current)restoreOverlayRef.current=dismissForOverlay(workspace.current);
    requestAnimationFrame(()=>{if(workspace.current){Blockly.svgResize(workspace.current);workspace.current.zoomToFit();}});
    revision.current=data.revision;setReady(true);
  };
  const run=async work=>{setBusy(true);setError('');try{await work();}catch(e){setError(e.message);}finally{setBusy(false);}};
  useEffect(()=>{run(load);const observer=new ResizeObserver(()=>{if(workspace.current)Blockly.svgResize(workspace.current);});observer.observe(host.current);return()=>{observer.disconnect();restoreOverlayRef.current?.();workspace.current?.dispose();};},[]);
  const save=()=>run(async()=>{
    const rules=serializeAutomation(workspace.current,catalog.current);
    const result=await automationRequest('v2/rules','PUT',{expected_revision:revision.current,rules});
    revision.current=result.revision;await load();await onConfigChanged?.();
  });
  return <section className="flex flex-col flex-1 min-h-[600px] gap-3 p-3 text-md-on-surface">
    <h2 className="text-xl font-bold">Automation v2</h2>
    <p>WHEN 决定何时运行，播放光效决定内容与合成。规则按连接顺序保存；Activate Profile 仅用于持续条件。</p>
    <p>光效列表来自配置和已发布插件。Studio 草稿必须发布后才能运行。</p>
    <div className="flex gap-4"><button disabled={busy} onClick={()=>run(load)}>重新加载</button><button disabled={busy||!ready} onClick={save}>保存并应用</button><button disabled={!ready} onClick={()=>run(async()=>setPreview(JSON.stringify(serializeAutomation(workspace.current,catalog.current),null,2)))}>查看 V2 JSON</button></div>
    {error&&<p role="alert">{error}</p>}
    <div ref={host} className="w-full" style={{height:600,position:'relative'}}/>
    {preview&&<details open><summary>V2 记录</summary><pre className="overflow-auto">{preview}</pre></details>}
    <GsiSimulation/>
  </section>;
}
