// Native UI is a command surface; the retained editor remains the document owner.
export const STUDIO_COMMAND_DEFINITIONS = Object.freeze([
  ['new', '新建光效', 'New Effect'], ['open', '打开工程', 'Open'], ['save', '保存草稿', 'Save'], ['validate', '验证工程', 'Validate'],
  ['bench', '打开测试台', 'Test Bench'], ['run_scenario', '运行当前场景', 'Run Scenario'], ['ask_ai', '打开 AI 助手', 'Ask AI'],
  ['build', '构建（不发布）', 'Build'], ['publish', '发布光效', 'Publish'], ['restore_snapshot', '恢复草稿快照', 'Restore Snapshot'],
  ['export_bundle', '导出工程包', 'Export Bundle'], ['import_bundle', '导入工程包', 'Import Bundle'], ['diagnostics', '打开诊断', 'Diagnostics']
].map(([id, label, search]) => Object.freeze({ id, label, search })));
export const STUDIO_COMMANDS = Object.freeze([...STUDIO_COMMAND_DEFINITIONS.map(c => c.id), 'select', 'preview', 'details', 'palette']);
export function commandAvailability(id, state = {}) {
  if (!STUDIO_COMMANDS.includes(id)) return '未知命令';
  if (state.busy) return '当前操作尚未完成';
  if (['palette', 'new', 'open', 'select'].includes(id)) return '';
  if (id === 'diagnostics') return state.embedded === false ? '此命令需要 Aura 桌面宿主' : '';
  if (state.workType && state.workType !== 'effect') return '请先打开光效工程';
  if (state.recovery) return '请先恢复或放弃可恢复草稿';
  if (['build', 'publish', 'bench', 'run_scenario', 'export_bundle'].includes(id) && state.validation !== '验证通过') return '请先解决工程验证错误';
  if (id === 'restore_snapshot' && !state.hasSnapshots) return '当前工程没有可恢复快照';
  if (['ask_ai', 'export_bundle', 'import_bundle'].includes(id) && state.embedded === false) return '此命令需要 Aura 桌面宿主';
  return '';
}
export function filterStudioCommands(query) {
  const terms = query.trim().toLowerCase().split(/\s+/).filter(Boolean);
  return STUDIO_COMMAND_DEFINITIONS.filter(c => terms.every(q => `${c.label} ${c.search} ${c.id}`.toLowerCase().includes(q)));
}
export function executeStudioCommand(command, name) {
  globalThis.window?.dispatchEvent(new CustomEvent('studio-command', { detail: { type: 'studio_command', command, ...(name ? { name } : {}) } }));
}
export function listenStudioCommands(webview, handlers, getState) {
  const receive = event => {
    const m = event.data || event.detail;
    if (m?.type !== 'studio_command' || !STUDIO_COMMANDS.includes(m.command)) return;
    if (Object.keys(m).some(k => !['type', 'command', 'name'].includes(k))) return;
    if (m.command === 'select' && (typeof m.name !== 'string' || m.name.length > 128)) return;
    if (getState && commandAvailability(m.command, getState())) return;
    handlers[m.command]?.(m);
  };
  webview?.addEventListener('message', receive); globalThis.window?.addEventListener('studio-command', receive);
  return () => { webview?.removeEventListener('message', receive); globalThis.window?.removeEventListener('studio-command', receive); };
}
export function postStudioState(state) {
  window.chrome?.webview?.postMessage({ type: 'studio_state', ...state });
}
export function initialStudioProject(effects, draft) {
  if (draft?.json && typeof draft.name === 'string' && /^[a-zA-Z_][a-zA-Z0-9_]{0,47}$/.test(draft.name)) return draft.name;
  return Object.keys(effects || {})[0] || 'custom_rainbow';
}
