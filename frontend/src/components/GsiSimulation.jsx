import React,{useEffect,useState} from 'react';

export default function GsiSimulation() {
  const [state,setState]=useState(null),[error,setError]=useState(''),[busy,setBusy]=useState(false);
  const refresh=async()=>{const response=await fetch('/api/gsi/simulation',{cache:'no-store'});if(!response.ok)throw new Error('核心服务不可用');const value=await response.json();setState(value);return value;};
  useEffect(()=>{let alive=true;const poll=()=>refresh().catch(e=>{if(alive)setError(e.message);});poll();const timer=setInterval(poll,1000);return()=>{alive=false;clearInterval(timer);};},[]);
  const update=async patch=>{setBusy(true);setError('');try{const r=await fetch('/api/gsi/simulation',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(patch)});const result=await r.json();if(!r.ok)throw new Error(result.message||result.error);for(let i=0;i<100;i++){const s=await refresh();if(s.applied_sequence>=result.sequence)return;await new Promise(resolve=>setTimeout(resolve,50));}throw new Error('核心服务尚未确认，请刷新状态');}catch(e){setError(e.message);}finally{setBusy(false);}};
  const values=state?.payload?.player?.state||{},round=state?.payload?.round||{};
  return <section className="p-3 space-y-3 border border-md-outline-variant rounded-xl">
    <h3 className="font-bold">{state?.enabled?'模拟':'真实 GSI'}</h3>
    <p>当前数据源：{state?.source==='simulation'?'模拟':state?.source==='real'?'真实 GSI':'等待核心服务'} · 前台：{state?.foreground_process||'未知'}</p>
    <p role="status" aria-live="polite">自动化：{state?.freshness ? <>{state.freshness.fresh?'新鲜':'已过期'} · 数据年龄 {state.freshness.age_ms==null?'尚无数据':`${state.freshness.age_ms} 毫秒`} · 新鲜度阈值 {state.freshness.threshold_ms} 毫秒（核心最近一次评估）</> : '等待核心评估'}</p>
    <label><input type="checkbox" checked={!!state?.enabled} disabled={busy||!state} onChange={e=>update({enabled:e.target.checked})}/> 启用自动化模拟</label>
    {state?.enabled && <>
      <label className="block">模拟前台 <input aria-label="模拟前台" className="bg-md-surface-container" key={state.foreground_process} defaultValue={state.foreground_process} disabled={busy} onBlur={e=>{if(e.target.value!==state.foreground_process)update({foreground_process:e.target.value});}}/></label>
      <label className="block"><input type="checkbox" checked={state.heartbeat} disabled={busy} onChange={e=>update({heartbeat:e.target.checked})}/> 自动心跳（{state.heartbeat_ms} ms，暂停可测试过期）</label>
      {['health','armor','round_kills'].map(key=><label key={key} className="block">{({health:'生命值',armor:'护甲',round_kills:'本回合击杀'})[key]} <input aria-label={({health:'生命值',armor:'护甲',round_kills:'本回合击杀'})[key]} className="bg-md-surface-container w-20" type="number" min="0" max={key==='round_kills'?1000000:100} key={values[key]} defaultValue={values[key]} disabled={busy} onBlur={e=>{if(Number(e.target.value)!==values[key])update({[key]:Number(e.target.value)});}}/></label>)}
      <button disabled={busy} onClick={()=>update({increment_kill:true})}>击杀数 +1</button>
      {[['bomb',['carried','dropped','planting','planted','defusing','defused','exploded']],['round_phase',['freezetime','live','over']]].map(([key,options])=><label className="block" key={key}>{key==='bomb'?'炸弹状态':'回合阶段'} <select className="bg-md-surface-container" disabled={busy} value={round[key==='round_phase'?'phase':key]} onChange={e=>update({[key]:e.target.value})}>{options.map(x=><option key={x} value={x}>{({carried:'随身携带',dropped:'已掉落',planting:'安放中',planted:'已安放',defusing:'拆除中',defused:'已拆除',exploded:'已引爆',freezetime:'准备阶段',live:'进行中',over:'已结束'})[x]}</option>)}</select></label>)}
      <p>真实 GSI 检测 → 自动化 → 灯效合成 → 当前输出。启用模拟期间，真实 CS2 数据暂不参与。</p>
    </>}
    {error && <p role="alert">{error}</p>}
  </section>;
}
