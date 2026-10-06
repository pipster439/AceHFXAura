import { deriveEffectCapabilities } from './effectCapabilities.js';
import { validateCandidate } from './studioAssistant.js';
import { normalizePublication } from '../blockly/publication.js';

const exact = (v, keys, optional = []) => {
  if (!v || typeof v !== 'object' || Array.isArray(v) || keys.some(k => !Object.hasOwn(v, k)) || Object.keys(v).some(k => ![...keys, ...optional].includes(k))) throw new Error('工程包字段无效');
};
const text = (v, max) => {
  if (typeof v !== 'string' || v.length > max || /(?:^|[^a-z0-9_])sk-[a-z0-9_-]{8,}|Bearer\s+\S+|(?:api[_ -]?key|password|token|secret)\s*[:=]|[a-z]:[\\/]|\\\\|\/(?:home|Users|tmp|var)\/|<script/i.test(v)) throw new Error('工程包不能包含密钥、本地路径或脚本');
};
export function validateBundlePayload(payload) {
  exact(payload, ['manifest', 'project']);
  const { manifest: m, project: p } = payload;
  exact(m, ['schema_version', 'name', 'description', 'tags', 'capabilities', 'created_with_version'], ['author']);
  exact(p, ['name', 'json', 'publication']);
  if (m.schema_version !== 1 || !/^[A-Za-z_][A-Za-z0-9_]{0,47}$/.test(p.name) || m.name !== p.name) throw new Error('工程包版本或名称无效');
  text(m.description, 1024); text(m.created_with_version, 80); if ('author' in m) text(m.author, 80);
  if (!Array.isArray(m.tags) || m.tags.length > 12 || new Set(m.tags).size !== m.tags.length) throw new Error('工程包标签无效');
  m.tags.forEach(t => { text(t, 40); if (!t.trim()) throw new Error('工程包标签不能为空'); });
  if (new TextEncoder().encode(JSON.stringify(payload)).length > 286720) throw new Error('工程包超过大小限制');
  const publication = normalizePublication(p.publication);
  exact(p.publication, ['mode', 'fade_out_ms']);
  if (Object.keys(publication).some(k => publication[k] !== p.publication[k])) throw new Error('工程包播放方式无效');
  const scan = (v, depth = 0) => {
    if (depth > 48) throw new Error('工程包层级超过限制');
    if (typeof v === 'string') text(v, 262144);
    else if (v && typeof v === 'object') for (const [k, n] of Object.entries(v)) {
      if (/api[_ -]?key|credential|password|token|secret|diagnostic|compiler_path|applied_|published_|^(code|script|source_code|binary|dll|exe)$/i.test(k)) throw new Error('工程包不能包含密钥、脚本或运行记录'); scan(n, depth + 1);
    }
  }; scan(p.json);
  // The graph is authoritative. Compare all exact manifest fields, not an LLM
  // claim or bundle cache; unsupported graphs never reach the confirmation UI.
  const derived = deriveEffectCapabilities(p);
  exact(m.capabilities, Object.keys(derived));
  if (derived.diagnostics.length || Object.keys(derived).some(k => JSON.stringify(derived[k]) !== JSON.stringify(m.capabilities[k]))) throw new Error('声明能力与实际工程不一致，或工程使用了不支持的积木');
  validateCandidate(p);
  return structuredClone(payload);
}
export function createBundlePayload(project, metadata = {}) {
  const json = structuredClone(project.json);
  // Blockly block IDs are opaque, regenerated on load, and need not travel.
  // Preserve variable IDs and references, coordinates, fields and mutations.
  const normalizeBlockIds = block => {
    if (!block || typeof block !== 'object') return;
    delete block.id;
    for (const input of Object.values(block.inputs || {})) { normalizeBlockIds(input.block); normalizeBlockIds(input.shadow); }
    normalizeBlockIds(block.next?.block);
  };
  for (const block of (Array.isArray(json) ? json : Array.isArray(json.blocks) ? json.blocks : json.blocks?.blocks) || []) normalizeBlockIds(block);
  const p = { name: project.name, json: Array.isArray(json) ? { blocks: json } : json, publication: normalizePublication(project.publication) };
  return validateBundlePayload({ manifest: { schema_version: 1, name: p.name, description: metadata.description || '', tags: metadata.tags || [],
    capabilities: deriveEffectCapabilities(p), created_with_version: 'Studio-alpha.8', ...(metadata.author ? { author: metadata.author } : {}) }, project: p });
}
export function importedDraft(payload, catalog, requestedName) {
  const value = validateBundlePayload(payload);
  if (!/^[A-Za-z_][A-Za-z0-9_]{0,47}$/.test(requestedName) || Object.hasOwn(catalog || {}, requestedName)) throw new Error('请使用未占用的工程名称；导入不会覆盖现有工程');
  return { ...value.project, name: requestedName };
}
export function nextImportedName(name, catalog) {
  for (let i = 0; i < 200; i++) { const suffix = i ? `_import_${i}` : '_import'; const candidate = name.slice(0, 48 - suffix.length) + suffix; if (!Object.hasOwn(catalog || {}, candidate)) return candidate; }
  throw new Error('工程数量超过限制');
}
export function createBundleBridge(webview) {
  const pending = new Map();
  const receive = e => { const m = e.data; if (m?.type !== 'studio_bundle_result' || !pending.has(m.request_id)) return;
    const task = pending.get(m.request_id); pending.delete(m.request_id); clearTimeout(task.timer); m.error ? task.reject(new Error(m.error)) : task.resolve(m.result); };
  webview?.addEventListener('message', receive);
  return { request(operation, payload) { return new Promise((resolve, reject) => {
    if (!webview?.postMessage || !['import', 'export'].includes(operation)) { reject(new Error('工程包需要 Aura 桌面宿主')); return; }
    const request_id = crypto.randomUUID().replaceAll('-', ''); const timer = setTimeout(() => { pending.delete(request_id); reject(new Error('工程包选择已超时，请重新打开')); }, 300000);
    pending.set(request_id, { resolve, reject, timer }); webview.postMessage({ type: 'studio_bundle', request_id, operation, ...(payload ? { payload } : {}) });
  }); }, dispose() { webview?.removeEventListener('message', receive); for (const t of pending.values()) { clearTimeout(t.timer); t.reject(new Error('编辑器已关闭')); } pending.clear(); } };
}
