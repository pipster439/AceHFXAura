import React, {useEffect, useState} from 'react';
import {automationRequest, sparsePatch, ruleForPairing} from '../utils/automationAuthoring.js';

export default function AutomationAuthoring({onConfigChanged}) {
  const [data,setData]=useState(null),[caps,setCaps]=useState(null),[draft,setDraft]=useState(''),[selected,setSelected]=useState(null);
  const [position,setPosition]=useState(''),[ack,setAck]=useState(false),[proposal,setProposal]=useState(null),[result,setResult]=useState(null),[busy,setBusy]=useState(false);
  const [appRule,setAppRule]=useState(null),[draftRevision,setDraftRevision]=useState('');
  const refresh=async()=>{ const next=await automationRequest('v2/records');setData(next);setProposal(null);setAck(false);return next; };
  const run=async work=>{setBusy(true);setResult(null);try{await work();}catch(error){setResult(error.result || {message:error.message});}finally{setBusy(false);}};
  useEffect(()=>{run(async()=>{setCaps(await automationRequest('v2/capabilities'));await refresh();});},[]);
  const changed=async()=>{await refresh();await onConfigChanged?.();};
  const edit=row=>{setDraftRevision(data.revision);setSelected(row);setAppRule(null);setDraft(JSON.stringify(row.record,null,2));setPosition(String(row.position));setAck(false);setProposal(null);setResult(null);};
  const body=()=>({expected_revision:draftRevision,...(position===''?{}:{position:Number(position)}),acknowledge_shadowing:ack,
    ...(selected?{id:selected.id,patch:sparsePatch(selected.record,JSON.parse(draft))}:{rule:JSON.parse(draft)})});
  const create=pair=>{setDraftRevision(data.revision);setSelected(null);setAppRule(null);setDraft(JSON.stringify(ruleForPairing(pair,Object.keys(data.profiles)[0]),null,2));setPosition('');setAck(false);setProposal(null);};
  return <section className="p-6 space-y-4 text-md-on-surface">
    <h2 className="text-xl font-bold">Automation</h2>
    <p>WHEN 决定何时运行，DO 选择方案或触发光效。渲染内容在 Studio 编辑。</p>
    <button disabled={busy} onClick={()=>run(refresh)}>刷新规则与版本</button>
    {data && <p>{data.precedence}</p>}
    {caps && <details><summary>支持的 authoring 合约</summary><pre className="overflow-auto text-xs">{JSON.stringify(caps,null,2)}</pre></details>}
    {caps && <div className="flex flex-wrap gap-3">{caps.pairings.map(pair=><button disabled={busy} key={`${pair.mode}/${pair.action}`} onClick={()=>create(pair)}>新建 {pair.mode} → {pair.action}{pair.lifetime?` (${pair.lifetime})`:''}</button>)}</div>}
    {data?.records.map((row,i)=><article key={`${row.path}/${i}`} className="border border-md-outline-variant p-3 space-y-2">
      <strong>{row.id || row.path}</strong> · {row.provenance} · 顺序 {row.position} · profile tier {row.profile_precedence_tier}
      <pre className="overflow-auto text-xs">{JSON.stringify(row.record,null,2)}</pre>
      {row.provenance==='automation_v2' && <div className="flex gap-3">
        <button disabled={busy} onClick={()=>edit(row)}>编辑</button>
        <button disabled={busy} onClick={()=>run(async()=>{await automationRequest('v2/rules','DELETE',{id:row.id,expected_revision:data.revision});await changed();setDraft('');setSelected(null);})}>删除此 ID</button>
      </div>}
      {row.provenance==='application' && <div className="flex gap-3">
        <button disabled={busy} onClick={()=>{setAppRule({...row,revision:data.revision,process:row.record.process,profile:row.record.profile});setProposal(null);}}>通过现有 Application API 编辑</button>
        <button disabled={busy} onClick={()=>run(async()=>{const response=await automationRequest('v2/promotions/propose','POST',{source_index:row.position,expected_revision:data.revision,id:crypto.randomUUID(),...(position===''?{}:{position:Number(position)})});setProposal(response.proposal);})}>准备 Convert-to-v2 提案</button>
      </div>}
    </article>)}
    {appRule && <div className="space-y-2">
      <p>Application Rule 编辑保留其余字段，继续使用 Phase 4 API。</p>
      <label>进程 <input className="bg-md-surface-container" value={appRule.process} onChange={e=>setAppRule({...appRule,process:e.target.value})}/></label>
      <label>方案 <select className="bg-md-surface-container" value={appRule.profile} onChange={e=>setAppRule({...appRule,profile:e.target.value})}>{Object.keys(data.profiles).map(p=><option key={p}>{p}</option>)}</select></label>
      <button disabled={busy} onClick={()=>run(async()=>{await automationRequest(`rules/${appRule.position}`,'PATCH',{expected_revision:appRule.revision,rule:{process:appRule.process,profile:appRule.profile}});await changed();setAppRule(null);})}>保存 Application Rule</button>
    </div>}
    <label className="block">写入 orchestration 位置（留空追加；更新时为移除原规则后的插入位置）<input aria-label="orchestration position" className="bg-md-surface-container ml-2" type="number" min="0" value={position} onChange={e=>{setPosition(e.target.value);setProposal(null);setAck(false);}}/></label>
    {draft && <div className="space-y-3">
      <p>{selected?`更新稳定 ID ${selected.id}，仅提交字段差异`:'新建 V2 规则'}。编辑 WHEN/DO JSON；支持范围由 daemon 校验。</p>
      <textarea aria-label="Automation rule JSON" className="w-full bg-md-surface-container font-mono p-3" rows={18} value={draft} onChange={e=>{setDraft(e.target.value);setAck(false);setResult(null);}}/>
      {result?.shadowing && <label className="block"><input type="checkbox" checked={ack} onChange={e=>setAck(e.target.checked)}/> 我已审阅下方相同进程规则和优先级变化，确认共存并允许覆盖。</label>}
      <div className="flex gap-4"><button disabled={busy} onClick={()=>run(async()=>setResult(await automationRequest('v2/validate','POST',body())))}>仅验证</button>
      <button disabled={busy} onClick={()=>run(async()=>{const payload=body();await automationRequest('v2/rules',selected?'PATCH':'POST',payload);await changed();setDraft('');setSelected(null);})}>保存</button></div>
    </div>}
    {proposal && <div className="border border-md-outline-variant p-3 space-y-3"><h3>转换提案：确认后同时新增 V2 并移除原规则</h3>
      <pre className="overflow-auto text-xs">{JSON.stringify(proposal,null,2)}</pre>
      <button disabled={busy} onClick={()=>run(async()=>{await automationRequest('v2/promotions/commit','POST',{expected_revision:data.revision,proposal,confirm:true});await changed();})}>确认此提案、DND 映射和优先级变化</button>
    </div>}
    {result && <pre role="status" className="whitespace-pre-wrap text-sm">{JSON.stringify(result,null,2)}</pre>}
  </section>;
}
