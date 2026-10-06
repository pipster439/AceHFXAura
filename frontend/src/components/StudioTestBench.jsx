import React, { useEffect, useRef, useState } from 'react';
import KeyboardVisualizer from './KeyboardVisualizer';
import { BUILTIN_SCENARIOS, StudioTestBench, parseScenario, exportScenario, BENCH_LIMITS } from '../utils/studioTestBench.js';
import { deriveEffectCapabilities, recommendStudioScenarios, CAPABILITY_DIAGNOSTICS } from '../utils/effectCapabilities.js';

export default function TestBench({ getProject, onClose, runToken = 0 }) {
  const [scenario, setScenario] = useState(BUILTIN_SCENARIOS[0]); const [playing, setPlaying] = useState(false);
  const [loop, setLoop] = useState(false); const [error, setError] = useState(''); const [state, setState] = useState(null);
  const [assertions, setAssertions] = useState([]); const runner = useRef(null); const input = useRef(null);
  const manifest = deriveEffectCapabilities(getProject());
  const recommended = recommendStudioScenarios(manifest);
  const reset = () => {
    setPlaying(false); setError(''); setAssertions([]);
    try { runner.current = new StudioTestBench(getProject(), scenario); setState(runner.current.current); }
    catch (e) { runner.current = null; setError(e.message); }
  };
  useEffect(reset, [scenario]);
  useEffect(() => { if (runToken) { reset(); setPlaying(true); } }, [runToken]);
  const step = () => {
    try {
      if (!runner.current) return;
      if (runner.current.current.completed) { if (loop) { setState(runner.current.reset()); setAssertions([]); return; } else { setPlaying(false); return; } }
      setState(runner.current.step()); setAssertions([...runner.current.results]);
      if (runner.current.current.completed && !loop) setPlaying(false);
    } catch (e) { setError(e.message); setPlaying(false); }
  };
  useEffect(() => {
    if (!playing) return;
    const timer = setInterval(step, scenario.frame_ms); return () => clearInterval(timer);
  }, [playing, loop, scenario]);
  const importFile = async event => {
    setPlaying(false); const file = event.target.files?.[0]; event.target.value = '';
    if (!file) return;
    try { if (file.size > BENCH_LIMITS.bytes) throw new Error('场景超过 64 KiB'); setScenario(parseScenario(await file.text())); }
    catch (e) { setError(e.message); }
  };
  const exportFile = () => {
    const url = URL.createObjectURL(new Blob([exportScenario(scenario)], { type: 'application/json' }));
    const link = document.createElement('a'); link.href = url; link.download = 'studio-scenario.json'; link.click(); URL.revokeObjectURL(url);
  };
  return <section data-studio-bench data-time={state?.time_ms ?? 0} data-completed={state?.completed ? 'true' : 'false'} className="flex-1 min-w-0 min-h-0 overflow-y-auto rounded-md-lg border border-md-outline-variant p-3 text-sm">
    <div className="flex flex-wrap gap-2 items-center">
      <strong className="w-full">测试台 · 仅本地模拟</strong><button type="button" onClick={onClose} aria-label="关闭测试台">返回编辑器</button>
      <select className="min-w-0 max-w-full rounded-md bg-md-surface-container p-2" aria-label="测试场景" value={scenario.name} onChange={e => setScenario(BUILTIN_SCENARIOS.find(s => s.name === e.target.value))}>
        {BUILTIN_SCENARIOS.map(s => <option key={s.name}>{s.name}</option>)}
        {!BUILTIN_SCENARIOS.some(s => s.name === scenario.name) && <option>{scenario.name}</option>}
      </select>
      <button data-bench-play disabled={!runner.current || !!error} onClick={() => setPlaying(true)}>播放</button>
      <button data-bench-pause onClick={() => setPlaying(false)}>暂停</button>
      <button data-bench-step disabled={playing || !runner.current} onClick={step}>单步</button>
      <button data-bench-reset onClick={reset}>重置</button>
      <label><input type="checkbox" checked={loop} onChange={e => setLoop(e.target.checked)}/> 循环</label>
      <button onClick={() => input.current.click()}>导入场景 JSON</button><button onClick={exportFile}>导出场景 JSON</button>
      <input ref={input} type="file" accept=".json,application/json" hidden onChange={importFile}/>
    </div>
    <div className="my-3 grid grid-cols-1 sm:grid-cols-2 gap-3"><div className="min-w-0 break-words rounded-lg border border-md-outline-variant p-3"><h3 className="font-semibold">时间与执行</h3><p role="status">模拟时间：{state?.time_ms ?? 0} / {scenario.duration_ms} ms · 帧数：{state?.frame_count ?? 0} · {state?.completed ? '已完成' : playing ? '播放中' : '已暂停'}</p>
    <p data-bench-recommendation className="text-xs">推荐场景：{recommended.join('、') || '此工程可使用任意场景检查模拟时间与帧输出'}</p>
    {manifest.diagnostics.map(code => <p key={code} role="alert">{CAPABILITY_DIAGNOSTICS[code]}</p>)}
    </div><div className="min-w-0 break-words rounded-lg border border-md-outline-variant p-3"><h3 className="font-semibold">模拟输入</h3><p className="break-words text-xs">按键：{state?.held_keys.join(', ') || '无'} · 生命值：{state?.gsi.player?.state?.health ?? '未设置'} · 前台：{state?.foreground_process}</p>
    <details><summary>当前模拟输入</summary><pre data-bench-state className="text-xs whitespace-pre-wrap">{JSON.stringify({ held_keys: state?.held_keys, gsi: state?.gsi, foreground_process: state?.foreground_process }, null, 2)}</pre></details></div></div>
    <p className="text-xs">前台场景仅检查进程状态，不会触发光效。按键模拟在按下时产生输入，释放时清除输入。</p>
    {error && <p role="alert" className="text-md-error">{error}</p>}
    {state && <KeyboardVisualizer activeTab="blockly_effect" localSimulation blocklyFrame={state.frame} isMasterLightOn brightnessVal={1} fpsVal={25} selectedKeyNames={new Set()} onToggleKeySelection={() => {}}/>}
    <details><summary>事件列表（{scenario.events.length}）</summary><ol>{scenario.events.map((e, i) => <li key={i} className="font-mono text-xs">{e.at_ms} ms · {e.type} · {e.key || e.process || JSON.stringify(e.values || {})}</li>)}</ol></details>
    <p className="mt-2 rounded-lg bg-md-surface-container p-2" data-bench-assertions>{assertions.length ? assertions.every(a => a.passed) ? '已执行断言通过' : '断言失败' : '无断言失败'}</p>
  </section>;
}
