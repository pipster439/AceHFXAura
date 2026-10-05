// Narrow RPC to the native data-root journal. No filesystem paths or settings
// credentials cross this bridge, and there is no config/publish operation.
import Blockly, { loadSafeWorkspaceJson } from '../blockly/index.js';
import { normalizePublication } from '../blockly/publication.js';
export function durableStudioDraft(config, name) {
  const effect = config?.blockly_effects?.[name]; const ws = new Blockly.Workspace();
  const group = Blockly.Events.getGroup(), undo = Blockly.Events.getRecordUndo();
  try {
    loadSafeWorkspaceJson(effect?.blockly_json || { blocks: [] }, ws, true);
    return { name, json: Blockly.serialization.workspaces.save(ws), publication: normalizePublication(effect?.publication) };
  } finally { ws.dispose(); Blockly.Events.setGroup(group); Blockly.Events.setRecordUndo(undo); }
}
export function createStudioStorage(webview, status) {
  const pending = new Map();
  const receive = event => {
    const m = event.data;
    if (m?.type === 'studio_storage_status' && typeof m.name === 'string' && typeof m.text === 'string') status?.(m);
    if (m?.type !== 'studio_storage_result' || !pending.has(m.request_id)) return;
    const task = pending.get(m.request_id); pending.delete(m.request_id); clearTimeout(task.timer);
    if (m.error) task.reject(new Error(String(m.error).slice(0, 1024))); else task.resolve(m.result);
  };
  webview?.addEventListener('message', receive);
  const request = (operation, draft, extra = {}) => new Promise((resolve, reject) => {
    if (!webview?.postMessage) { reject(new Error('草稿恢复与快照需要 Aura 桌面宿主。')); return; }
    if (!['observe', 'autosave', 'saved', 'snapshot', 'list', 'restore_recovery', 'discard', 'restore_snapshot'].includes(operation) ||
        Object.keys(extra).some(k => !['reason', 'snapshot_id'].includes(k))) { reject(new Error('未知草稿操作')); return; }
    const request_id = crypto.randomUUID().replaceAll('-', '');
    const timer = setTimeout(() => { pending.delete(request_id); reject(new Error('草稿存储响应超时')); }, 10000);
    pending.set(request_id, { resolve, reject, timer }); webview.postMessage({ type: 'studio_storage', request_id, operation, draft, ...extra });
  });
  return { request, dispose() { webview?.removeEventListener('message', receive); for (const task of pending.values()) { clearTimeout(task.timer); task.reject(new Error('编辑器已关闭')); } pending.clear(); } };
}
