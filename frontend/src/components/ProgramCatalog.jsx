import React, { useCallback, useEffect, useMemo, useState } from 'react';
import { ChevronDown, RefreshCw, ShieldCheck } from 'lucide-react';
import { catalogOperationWithReceipt, installRequest, listPrograms, newOperationId, prepareProgramPackage, queryProgramReceipt } from '../utils/programCatalog.js';

const confirmAction = (label, text) => globalThis.confirm?.(`${label}\n\n${text}`) !== false;
const actionNames = { enable: '启用', disable: '停用', uninstall: '卸载', rollback: '回退', update: '更新' };

export default function ProgramCatalog({ snapshot, embedded = false, onStatus, onLinkLighting }) {
  const [catalog, setCatalog] = useState(null); const [error, setError] = useState(''); const [busy, setBusy] = useState(false); const [pending, setPending] = useState(null); const [expanded, setExpanded] = useState(false); const [version, setVersion] = useState('1.0.0');
  const refresh = useCallback(async () => { try { setError(''); setCatalog(await listPrograms()); } catch (e) { setError(e.message); } }, []);
  useEffect(() => { if (embedded) refresh(); }, [embedded, refresh]);
  const entries = useMemo(() => catalog?.entries || [], [catalog]);
  const run = async (entry, operation, extra = {}) => {
    if (busy || !catalog || !confirmAction(`${actionNames[operation] || operation}程序`, `${entry.name} · ${entry.id}\n此操作会写入程序目录，并保留可恢复版本。`)) return;
    const operation_id = newOperationId(); let request = { operation, operation_id, expected_revision: catalog.revision, id: entry.id, ...extra };
    if (operation === 'rollback') request.hash = extra.hash; setBusy(true); setError('');
    try { const result = await catalogOperationWithReceipt(request); if (result.kind === 'pending') { setPending(result); onStatus?.(result.diagnostic); } else { onStatus?.(`程序目录${actionNames[operation] || operation}完成；revision ${result.receipt.revision}`); await refresh(); } }
    catch (e) { setError(e.message); } finally { setBusy(false); }
  };
  const update = async entry => {
    if (busy || !catalog || !snapshot || !confirmAction('更新受限灯效程序', `${entry.name}\n将从当前工程生成新 IR 版本；启用状态保持不变。`)) return;
    setBusy(true); setError(''); try {
      const prepared = prepareProgramPackage(snapshot, { name: entry.name, version });
      const result = await catalogOperationWithReceipt({ operation: 'update', operation_id: newOperationId(), expected_revision: catalog.revision, id: entry.id, name: prepared.name, version: prepared.version, program: prepared.program, source: prepared.source });
      if (result.kind === 'pending') setPending(result); else { onStatus?.(`程序 ${entry.name} 已更新；启用状态保持不变。`); await refresh(); }
    } catch (e) { setError(e.message); } finally { setBusy(false); }
  };
  const install = async () => {
    if (!catalog || busy || !snapshot || !confirmAction('安装受限灯效程序', '安装默认禁用，使用本地 IR，不需要 MSVC；请确认当前工程和版本。')) return;
    setBusy(true); setError(''); const operation_id = newOperationId();
    try { const request = installRequest(snapshot, { id: newOperationId(), name: snapshot.name, version }, catalog.revision, operation_id); const result = await catalogOperationWithReceipt(request); if (result.kind === 'pending') setPending(result); else { onStatus?.('受限灯效程序已安装，默认禁用。'); await refresh(); } }
    catch (e) { setError(e.message); } finally { setBusy(false); }
  };
  const resolvePending = async () => { if (!pending) return; setBusy(true); try { const receipt = await queryProgramReceipt(pending.operation_id); setPending(null); onStatus?.(`操作已完成；revision ${receipt.revision}`); await refresh(); } catch (e) { setError(e.code === 'receipt_not_found' ? '回执仍未生成；请稍后使用相同操作 ID 查询。' : e.message); } finally { setBusy(false); } };
  if (!embedded) return null;
  return <details data-program-catalog className="shrink-0 rounded-md-lg border border-md-outline-variant bg-md-surface-container-low p-3 text-xs" open={expanded} onToggle={event => setExpanded(event.currentTarget.open)}>
    <summary className="flex cursor-pointer items-center justify-between font-semibold"><span>受限灯效程序 · {catalog ? `${entries.filter(entry => entry.enabled && !entry.removed).length} 个运行中 / ${entries.length} 个记录` : '读取中'}</span><ChevronDown className="h-4 w-4" /></summary>
    <div className="mt-2 space-y-2"><p className="text-md-on-surface-variant">restricted_ir_v1 使用真实 Blockly IR 和本地模拟，不需要 MSVC。安装默认禁用；运行版本和历史版本由本地目录保留。</p>
      {error && <p role="alert" className="text-md-error break-words">{error}</p>}
      {pending && <div role="status" className="rounded border border-amber-500/40 bg-amber-500/10 p-2">操作 {pending.operation_id} 结果待查询；不会重复提交。<button type="button" onClick={resolvePending} disabled={busy} className="ml-2 underline">查询回执</button></div>}
      <div className="flex flex-wrap gap-2"><button type="button" onClick={refresh} disabled={busy} className="rounded border px-2 py-1"><RefreshCw className="mr-1 inline h-3 w-3" />刷新</button><button type="button" onClick={install} disabled={busy || !catalog || !snapshot} className="rounded bg-md-primary-container px-2 py-1 font-semibold disabled:opacity-50">安装当前工程（默认禁用）</button><label>版本<input value={version} maxLength={48} onChange={event => setVersion(event.target.value)} className="ml-1 w-20 rounded bg-md-surface px-1" /></label></div>
      <ul className="max-h-56 space-y-1 overflow-y-auto">{entries.map(entry => <li key={entry.id} className="rounded border border-md-outline-variant p-2"><div className="flex flex-wrap items-center justify-between gap-1"><span className="font-mono">{entry.name}</span><span>{entry.enabled && !entry.removed ? '运行中' : entry.removed ? '已卸载（版本保留）' : '已停用'} · {entry.execution_tier}</span></div><div className="text-md-on-surface-variant">{entry.id} · {entry.plugin_name} · active {entry.active.slice(0, 12)}… · {entry.loaded ? `generation ${entry.generation}` : '未加载'}</div><div className="mt-1 flex flex-wrap gap-1"><button type="button" onClick={() => update(entry)} disabled={busy || entry.removed || !snapshot} className="underline disabled:opacity-50">更新当前工程</button>{entry.enabled ? <button type="button" onClick={() => run(entry, 'disable')} disabled={busy} className="underline">停用</button> : <button type="button" onClick={() => run(entry, 'enable')} disabled={busy || entry.removed} className="underline">启用</button>}<button type="button" onClick={() => run(entry, 'uninstall')} disabled={busy || entry.removed} className="underline">卸载</button>{entry.versions.map(item => <button key={item.hash} type="button" onClick={() => run(entry, 'rollback', { hash: item.hash })} disabled={busy || item.hash === entry.active} className="underline">回退 {item.version}</button>)}</div><div className="mt-1 flex flex-wrap gap-1">{entry.versions.map(item => <span key={`${item.hash}-inputs`} className="rounded bg-md-surface-container px-1">v{item.version} · {item.inputs.keyboard ? '键盘' : ''}{item.inputs.cs2_gsi ? ' GSI' : ''}</span>)}</div><button type="button" onClick={() => run(entry, 'rollback', { hash: entry.versions.find(item => item.hash !== entry.active)?.hash || entry.active })} disabled={busy || !entry.versions.length} className="mt-1 underline">恢复到保留版本</button>{onLinkLighting && <button type="button" onClick={() => onLinkLighting(entry)} disabled={busy || entry.removed} className="ml-2 underline">创建 Lighting 方案</button>}</li>)}</ul>
      {!entries.length && catalog && <p className="text-md-on-surface-variant">目录为空；安装当前工程会创建一个默认禁用的受限程序。</p>}
      <p className="text-md-on-surface-variant"><ShieldCheck className="mr-1 inline h-3 w-3" />源工程导入仍使用现有 .auraeffect 确认流程；原生 DLL 开发者发布路径保持独立。</p>
    </div>
  </details>;
}
