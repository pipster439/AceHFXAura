import Blockly, { loadSafeWorkspaceJson } from '../blockly/index.js';
import { IrTranspiler } from '../blockly/irTranspiler.js';
import { JsTranspiler } from '../blockly/jsTranspiler.js';
import { normalizePublication } from '../blockly/publication.js';
import { deriveEffectCapabilities } from './effectCapabilities.js';
import { validateEffectWorkspace } from '../blockly/workspaceSafety.js';

export const PROGRAM_CATALOG_LIMITS = Object.freeze({ requestBytes: 240 * 1024, resultBytes: 48 * 1024, sourceBytes: 128 * 1024, nameBytes: 128, version: 48 });
const ID = /^[a-f0-9]{32}$/;
const HASH = /^[a-f0-9]{64}$/;
const VERSION = /^[A-Za-z0-9][A-Za-z0-9._-]{0,47}$/;
const clone = value => structuredClone(value);
const utf8 = value => new TextEncoder().encode(value).length;
const fail = message => { throw new Error(`灯效程序目录：${message}`); };

export function newOperationId(random = globalThis.crypto?.randomUUID) {
  if (typeof random !== 'function') fail('无法生成操作 ID');
  const value = random().replaceAll('-', '').toLowerCase();
  if (!ID.test(value)) fail('操作 ID 生成结果无效');
  return value;
}

function exact(value, required, optional = []) {
  if (!value || Array.isArray(value) || typeof value !== 'object' || required.some(key => !Object.hasOwn(value, key)) ||
      Object.keys(value).some(key => !required.includes(key) && !optional.includes(key))) fail('响应或请求字段无效');
}
function id(value, field = 'ID') { if (typeof value !== 'string' || !ID.test(value)) fail(`${field} 无效`); }
function hash(value) { if (typeof value !== 'string' || !HASH.test(value)) fail('版本哈希无效'); }
function responseJson(response, max = PROGRAM_CATALOG_LIMITS.resultBytes) {
  return response.text().then(text => { if (utf8(text) > max) fail('服务结果超过 48 KiB，未截断'); try { return JSON.parse(text); } catch { fail('服务返回 JSON 已损坏'); } });
}
function errorResponse(response, data) {
  const message = data?.message || `HTTP ${response.status}`; const error = new Error(message); error.code = data?.error || 'http'; error.status = response.status; return error;
}
function transportOf(transport = {}) { return { fetchImpl: globalThis.fetch, ...transport }; }

export function validateCatalogList(payload) {
  exact(payload, ['schema_version', 'revision', 'entries', 'diagnostic', 'receipt_count']);
  if (payload.schema_version !== 1 || !Number.isInteger(payload.revision) || payload.revision < 0 || !Array.isArray(payload.entries) ||
      payload.entries.length > 100 || typeof payload.diagnostic !== 'string' || !Number.isInteger(payload.receipt_count) || payload.receipt_count < 0) fail('目录列表无效');
  for (const entry of payload.entries) {
    exact(entry, ['id', 'plugin_name', 'execution_tier', 'loaded', 'generation', 'name', 'active', 'enabled', 'removed', 'versions']);
    id(entry.id, '程序 ID'); hash(entry.active);
    if (entry.plugin_name !== `ir_${entry.id}` || entry.execution_tier !== 'restricted_ir_v1' || typeof entry.name !== 'string' || utf8(entry.name) > PROGRAM_CATALOG_LIMITS.nameBytes ||
        typeof entry.loaded !== 'boolean' || !Number.isInteger(entry.generation) || entry.generation < 0 || typeof entry.enabled !== 'boolean' || typeof entry.removed !== 'boolean' || !Array.isArray(entry.versions)) fail('目录条目无效');
    for (const version of entry.versions) {
      exact(version, ['hash', 'version', 'inputs', 'source_hash']); hash(version.hash);
      if (!VERSION.test(version.version) || !version.inputs || typeof version.inputs.keyboard !== 'boolean' || typeof version.inputs.cs2_gsi !== 'boolean' ||
          (version.source_hash !== null && !HASH.test(version.source_hash))) fail('目录版本无效');
    }
  }
  return clone(payload);
}

export function validateReceipt(payload) {
  exact(payload, ['status', 'operation_id', 'id', 'plugin_name', 'revision', 'active', 'enabled', 'removed', 'running_instances']);
  if (payload.status !== 'ok' || !ID.test(payload.operation_id) || !ID.test(payload.id) || payload.plugin_name !== `ir_${payload.id}` || !Number.isInteger(payload.revision) ||
      payload.revision < 0 || !HASH.test(payload.active) || typeof payload.enabled !== 'boolean' || typeof payload.removed !== 'boolean' || typeof payload.running_instances !== 'string') fail('操作回执无效');
  return clone(payload);
}

export async function listPrograms(transport = {}) {
  const { fetchImpl, signal } = transportOf(transport); const response = await fetchImpl('/api/programs', { cache: 'no-store', signal }); const data = await responseJson(response);
  if (!response.ok) throw errorResponse(response, data); return validateCatalogList(data);
}
export async function queryProgramReceipt(operationId, transport = {}) {
  id(operationId, '操作 ID'); const { fetchImpl, signal } = transportOf(transport); const response = await fetchImpl(`/api/programs?operation_id=${operationId}`, { cache: 'no-store', signal }); const data = await responseJson(response);
  if (!response.ok) throw errorResponse(response, data); return validateReceipt(data);
}

function validateOperationRequest(request) {
  exact(request, ['operation', 'operation_id', 'expected_revision', 'id'], ['name', 'version', 'program', 'source', 'hash']);
  if (!['install', 'update', 'enable', 'disable', 'uninstall', 'rollback'].includes(request.operation)) fail('不支持的目录操作');
  id(request.operation_id, '操作 ID'); id(request.id, '程序 ID');
  if (!Number.isInteger(request.expected_revision) || request.expected_revision < 0) fail('目录 revision 无效');
  if (request.operation === 'install' || request.operation === 'update') {
    if (typeof request.name !== 'string' || !request.name || utf8(request.name) > PROGRAM_CATALOG_LIMITS.nameBytes || /[\x00-\x1f\x7f]/.test(request.name) || !VERSION.test(request.version)) fail('程序名称或版本无效');
    if (!request.program || typeof request.program !== 'object') fail('受限程序 IR 缺失');
    if (request.source !== undefined && (!request.source || typeof request.source !== 'object')) fail('源工程字段无效');
  }
  if (request.operation === 'rollback') hash(request.hash);
  const serialized = JSON.stringify(request); if (utf8(serialized) > PROGRAM_CATALOG_LIMITS.requestBytes) fail('目录请求超过 240 KiB');
  return clone(request);
}

export async function catalogOperation(request, transport = {}) {
  const safeRequest = validateOperationRequest(request); const { fetchImpl, signal } = transportOf(transport);
  const response = await fetchImpl('/api/programs', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(safeRequest), signal });
  const data = await responseJson(response); if (!response.ok) throw errorResponse(response, data); return validateReceipt(data);
}

// An interrupted HTTP request leaves the server mutation ambiguous. Query the
// same durable operation ID; never resend a mutation with a new ID.
export async function catalogOperationWithReceipt(request, transport = {}) {
  try { return { kind: 'completed', receipt: await catalogOperation(request, transport) }; }
  catch (error) {
    if (!(error?.name === 'AbortError' || error?.code === 'timeout' || error?.code === 'network' || error?.status === 408 || error?.status === 503 || error?.status === 504 || error?.name === 'TypeError')) throw error;
    try { return { kind: 'completed', receipt: await queryProgramReceipt(request.operation_id, transport) }; }
    catch (receiptError) {
      return { kind: 'pending', operation_id: request.operation_id, request: clone(request), diagnostic: receiptError.code === 'receipt_not_found' || receiptError.status === 404
        ? '操作结果尚未可查询；保留原操作 ID，禁止重复提交。' : `操作状态暂不可确认：${receiptError.message}；保留原操作 ID。` };
    }
  }
}

export function prepareProgramPackage(snapshot, metadata = {}) {
  if (!snapshot || typeof snapshot !== 'object' || !/^[A-Za-z_][A-Za-z0-9_]{0,47}$/.test(snapshot.name)) fail('当前工程名称无效');
  if (typeof metadata.name !== 'string' || !metadata.name || utf8(metadata.name) > PROGRAM_CATALOG_LIMITS.nameBytes || /[\x00-\x1f\x7f]/.test(metadata.name) || !VERSION.test(metadata.version)) fail('程序名称或版本无效');
  const workspace = new Blockly.Workspace(); const publication = normalizePublication(snapshot.publication); const recordUndo = Blockly.Events.getRecordUndo(); const group = Blockly.Events.getGroup();
  try {
    loadSafeWorkspaceJson(snapshot.json, workspace, true); const program = IrTranspiler.transpile(workspace, publication); const capabilities = deriveEffectCapabilities(snapshot);
    if (capabilities.diagnostics.length) fail('工程能力不支持受限运行时：' + capabilities.diagnostics.join(', '));
    const simulate = JsTranspiler.compile(workspace, publication); const preview = [0, 100, 500].map(time => simulate(time, {}, {}));
    if (preview.some(frame => !Array.isArray(frame) || frame.length !== 68 || frame.some(rgb => rgb.some(value => !Number.isFinite(value) || value < 0 || value > 255)))) fail('本地模拟帧无效');
    const source = { name: snapshot.name, json: clone(snapshot.json), publication }; const sourceBytes = utf8(JSON.stringify(source));
    if (sourceBytes > PROGRAM_CATALOG_LIMITS.sourceBytes) fail('源工程超过 128 KiB');
    const inputs = { keyboard: capabilities.inputs.includes('keyboard'), cs2_gsi: capabilities.inputs.includes('cs2_gsi') };
    return { name: metadata.name, version: metadata.version, program, source, inputs, capabilities, preview };
  } finally { workspace.dispose(); Blockly.Events.setRecordUndo(recordUndo); Blockly.Events.setGroup(group); }
}

export function installRequest(snapshot, metadata, expectedRevision, operationId = newOperationId()) {
  const prepared = prepareProgramPackage(snapshot, metadata); return validateOperationRequest({ operation: 'install', operation_id: operationId, expected_revision: expectedRevision, id: metadata.id, name: prepared.name, version: prepared.version, program: prepared.program, source: prepared.source });
}
