import React, { useEffect, useRef, useState } from 'react';
import { commandAvailability, filterStudioCommands, executeStudioCommand } from '../utils/studioHost.js';

export default function StudioCommandPalette({ state }) {
  const [open, setOpen] = useState(false), [query, setQuery] = useState(''), [index, setIndex] = useState(0), [message, setMessage] = useState('');
  const input = useRef(null), previousFocus = useRef(null); const commands = filterStudioCommands(query);
  const close = () => { setOpen(false); previousFocus.current?.focus?.(); };
  const show = () => { previousFocus.current = document.activeElement; setOpen(true); setQuery(''); setIndex(0); setMessage(''); };
  useEffect(() => {
    const key = e => { if (e.ctrlKey && e.shiftKey && e.key.toLowerCase() === 'p') { e.preventDefault(); show(); } };
    const command = e => { if ((e.data || e.detail)?.command === 'palette') show(); };
    window.addEventListener('keydown', key); window.addEventListener('studio-command', command); window.chrome?.webview?.addEventListener('message', command);
    return () => { window.removeEventListener('keydown', key); window.removeEventListener('studio-command', command); window.chrome?.webview?.removeEventListener('message', command); };
  }, []);
  useEffect(() => { if (open) input.current?.focus(); }, [open]);
  useEffect(() => { document.querySelector('[data-palette-selected="true"]')?.scrollIntoView({ block: 'nearest' }); }, [index]);
  const execute = c => {
    if (!c) return; const reason = commandAvailability(c.id, state);
    if (reason) { setMessage(reason); return; } close(); executeStudioCommand(c.id);
  };
  if (!open) return null;
  return <div className="fixed inset-0 z-[110] bg-black/40 p-3 flex justify-center items-start pt-[min(10vh,64px)]" onMouseDown={e => { if (e.target === e.currentTarget) close(); }}>
    <section role="dialog" aria-modal="true" aria-label="工作室命令面板" data-studio-palette className="studio-command-panel rounded-xl border border-md-outline-variant bg-md-surface-container shadow-xl w-full max-w-xl min-w-0 p-3"
      onKeyDown={e => {
        if (e.key === 'Escape') { e.preventDefault(); close(); }
        if (['ArrowDown', 'ArrowUp'].includes(e.key)) { e.preventDefault(); setIndex(i => commands.length ? (i + (e.key === 'ArrowDown' ? 1 : commands.length - 1)) % commands.length : 0); }
        if (e.key === 'Enter') { e.preventDefault(); execute(commands[Math.min(index, commands.length - 1)]); }
        if (e.key === 'Tab') { e.preventDefault(); input.current?.focus(); }
      }}>
      <div className="flex justify-between items-center gap-3"><strong>工作室命令</strong><button onClick={close} aria-label="关闭命令面板">Esc</button></div>
      <input ref={input} aria-label="搜索工作室命令" aria-controls="studio-command-list" aria-activedescendant={commands[index] ? `palette-${commands[index].id}` : undefined}
        value={query} onChange={e => { setQuery(e.target.value); setIndex(0); setMessage(''); }} placeholder="搜索命令… Ctrl+Shift+P" className="w-full min-w-0 rounded-lg border border-md-outline-variant bg-transparent p-2 my-2"/>
      <div id="studio-command-list" role="listbox" aria-label="可用命令" className="max-h-[55vh] overflow-auto">
        {commands.map((c, i) => { const reason = commandAvailability(c.id, state); return <button key={c.id} id={`palette-${c.id}`} role="option" aria-selected={i === index} aria-disabled={!!reason}
          data-palette-command={c.id} data-palette-selected={i === index ? 'true' : 'false'} onMouseEnter={() => setIndex(i)} onClick={() => execute(c)} tabIndex={-1}
          className={`block text-left w-full min-w-0 rounded-lg p-2 break-words ${i === index ? 'bg-md-primary-container text-md-on-primary-container' : ''}`}>
          <span>{c.label}</span>{reason && <span className="block text-xs opacity-80">{reason}</span>}</button>; })}
        {!commands.length && <p role="status">没有匹配命令。</p>}
      </div><p role="status" className="text-xs mt-2">{message || '↑↓ 选择 · Enter 执行 · Esc 关闭；发布仅在你明确执行命令后开始。'}</p>
    </section>
  </div>;
}
