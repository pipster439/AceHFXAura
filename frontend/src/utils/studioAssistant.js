import Blockly, { loadSafeWorkspaceJson, assertNoLegacyEventPulse } from '../blockly/index.js';
import { CppTranspiler } from '../blockly/cppTranspiler.js';
import { JsTranspiler } from '../blockly/jsTranspiler.js';
import { EFFECT_PRESETS } from '../blockly/presets.js';
import { normalizePublication } from '../blockly/publication.js';
import { deriveEffectCapabilities } from './effectCapabilities.js';
import { loadEffectWorkspace, validateEffectWorkspace } from '../blockly/workspaceSafety.js';
import { buildEffect } from './applyEffect.js';

export const ASSISTANT_INTENTS = Object.freeze(['generate', 'modify', 'explain', 'error_analysis']);
const clone = value => JSON.parse(JSON.stringify(value));
const freezeSnapshot = value => {
  if (value && typeof value === 'object') { Object.values(value).forEach(freezeSnapshot); Object.freeze(value); }
  return value;
};
function exact(value, keys) {
  if (!value || Array.isArray(value) || typeof value !== 'object' || Object.keys(value).length !== keys.length || keys.some(k => !Object.hasOwn(value, k))) throw new Error('建议字段无效');
}
export function numericNodes(json) {
  validateEffectWorkspace(json);
  const nodes = []; let count = 0;
  const visit = (block, parent = '', input = '') => {
    if (!block) return;
    if (++count > 2048) throw new Error('工程节点超过安全限制（2048）');
    if (block.type === 'math_number' && typeof block.fields?.NUM === 'number') {
      if (nodes.length >= 256) { const error = new Error('工程超过数值摘要限制；请使用 get_project_graph 和结构工具读取工程'); error.code = 'numeric_capacity'; throw error; }
      let min = -60000, max = 60000;
      if (parent === 'color_rgb') { min = 0; max = 255; }
      if (input === 'PERIOD_SEC') { min = 0.05; max = 120; }
      if (input === 'DECAY_RATE') { min = 0; max = 100; }
      nodes.push({ node_id: `n${nodes.length}`, type: 'math_number', parent, input, value: block.fields.NUM, min, max, block });
    }
    for (const [key, value] of Object.entries(block.inputs || {})) visit(value.block || value.shadow, block.type, key);
    visit(block.next?.block, block.type, 'next');
  };
  for (const block of (Array.isArray(json.blocks) ? json.blocks : json.blocks?.blocks) || []) visit(block);
  return nodes;
}
export function assistantContext(snapshot, intent, diagnostic = '', diagnosticKind = 'validation', selectedPresetId = null) {
  if (!ASSISTANT_INTENTS.includes(intent)) throw new Error('未知工作室动作');
  let nodes = [], capacity = '';
  try { nodes = numericNodes(snapshot.json).map(({ block, ...n }) => n); }
  catch (error) { if (error.code !== 'numeric_capacity') throw error; capacity = error.message; }
  const capabilities = deriveEffectCapabilities(snapshot);
  return { intent, name: /^[a-zA-Z_][a-zA-Z0-9_]{0,47}$/.test(snapshot.name) ? snapshot.name : 'current_effect',
    publication: normalizePublication(snapshot.publication), nodes, capabilities,
    presets: EFFECT_PRESETS.filter(p => intent === 'generate' && p.id === selectedPresetId).map(p => ({ id: p.id, name: p.name, description: p.description, tags: p.tags, required_inputs: p.requiredInputs, capabilities: p.capabilities })),
    diagnostic: capacity || (intent === 'error_analysis' ? diagnostic.slice(0, 2048) : ''),
    diagnostic_kind: intent === 'error_analysis' && ['validation', 'build', 'plugin_load'].includes(diagnosticKind) ? diagnosticKind : 'none' };
}
export function parseProposal(text) {
  if (typeof text !== 'string' || new TextEncoder().encode(text).length > 65536) throw new Error('建议超过大小限制');
  const result = JSON.parse(text); exact(result, ['action', 'summary', 'preset', 'edits']);
  if (!['propose', 'explain'].includes(result.action) || typeof result.summary !== 'string' || result.summary.length > 4000 ||
      !(result.preset === null || EFFECT_PRESETS.some(p => p.id === result.preset)) || !Array.isArray(result.edits) || result.edits.length > 32) throw new Error('未知或无效的建议动作');
  const ids = new Set();
  for (const edit of result.edits) {
    exact(edit, ['node_id', 'value']);
    if (!/^n\d{1,3}$/.test(edit.node_id) || !Number.isFinite(edit.value) || Math.abs(edit.value) > 60000 || ids.has(edit.node_id)) throw new Error('无效或重复的数值节点修改');
    ids.add(edit.node_id);
  }
  if (result.action === 'explain' && (result.preset !== null || result.edits.length)) throw new Error('解释动作不能修改工程');
  if (result.action === 'propose' && result.preset === null && !result.edits.length) throw new Error('建议没有修改');
  return result;
}
export function prepareProposal(snapshot, proposal, intent) {
  if (proposal.action !== 'propose') return null;
  if (['explain', 'error_analysis'].includes(intent) && proposal.preset !== null) throw new Error('错误分析不能替换模板');
  if (intent === 'modify' && proposal.preset !== null) throw new Error('修改当前光效不能替换模板');
  const preset = EFFECT_PRESETS.find(p => p.id === proposal.preset);
  const candidate = { ...clone(snapshot), json: clone(preset?.blocklyJson || snapshot.json) };
  const nodes = numericNodes(candidate.json); const changes = [];
  if (preset) changes.push({ field: '模板', before: '当前工程', after: preset.name });
  for (const edit of proposal.edits) {
    const node = nodes.find(n => n.node_id === edit.node_id);
    if (!node || edit.value < node.min || edit.value > node.max) throw new Error('数值节点不存在或超出允许范围');
    changes.push({ field: `${node.node_id} · ${node.parent}/${node.input}`, before: String(node.value), after: String(edit.value) });
    node.block.fields.NUM = edit.value;
  }
  return { candidate, changes, summary: proposal.summary };
}
export function validateCandidate(snapshot) {
  const recordUndo = Blockly.Events.getRecordUndo(); const group = Blockly.Events.getGroup();
  const workspace = new Blockly.Workspace();
  try {
    loadSafeWorkspaceJson(snapshot.json, workspace, true);
    const publication = normalizePublication(snapshot.publication);
    CppTranspiler.transpile(snapshot.name, workspace, publication);
    const simulate = JsTranspiler.compile(workspace, publication);
    const preview = [0, 100, 1000].map(t => simulate(t));
    if (preview.some(frame => !Array.isArray(frame) || frame.length !== 68 || frame.some(rgb => rgb.length !== 3 || rgb.some(v => !Number.isFinite(v) || v < 0 || v > 255)))) throw new Error('模拟帧无效');
    return { valid: true, preview: preview[1] };
  } finally { workspace.dispose(); Blockly.Events.setRecordUndo(recordUndo); Blockly.Events.setGroup(group); }
}

export async function compileCandidateNative(snapshot, { signal, fetchImpl = globalThis.fetch } = {}) {
  snapshot = clone(snapshot); validateCandidate(snapshot);
  const response = await fetchImpl('/api/status', { cache: 'no-store', signal });
  if (!response.ok) throw new Error('原生编译不可用：本地编译服务未响应');
  const readiness = await response.json();
  if (readiness.web_api_version !== 2 || readiness.studio_publish_ready !== true || readiness.sdk_headers_found !== true || readiness.msvc_found !== true)
    throw new Error('原生编译不可用：请检查本地服务版本、Plugin SDK 与 MSVC；候选不能应用');
  const workspace = new Blockly.Workspace(), group = Blockly.Events.getGroup(), undo = Blockly.Events.getRecordUndo();
  try {
    try { loadSafeWorkspaceJson(snapshot.json, workspace, true); }
    finally { Blockly.Events.setGroup(group); Blockly.Events.setRecordUndo(undo); }
    const result = await buildEffect(snapshot.name, workspace, CppTranspiler, normalizePublication(snapshot.publication), { signal, fetchImpl });
    if (result.success !== true) throw new Error('原生编译未返回明确成功；候选不能应用');
    return { success: true };
  } finally { workspace.dispose(); }
}

// Blockly's grouped serialization events preserve ordinary history. AI also
// keeps one explicit snapshot transaction for its separate Undo button.
export function loadAiSnapshot(workspace, json) {
  const { state } = validateEffectWorkspace(json);
  assertNoLegacyEventPulse(state);
  const group = Blockly.Events.getGroup(); const recordUndo = Blockly.Events.getRecordUndo(); Blockly.Events.setGroup(true);
  try { loadEffectWorkspace(Blockly, state, workspace, { recordUndo: true }); }
  finally { Blockly.Events.setRecordUndo(recordUndo); Blockly.Events.setGroup(group); }
}
export class StudioProposalSession {
constructor({ compileCandidate = compileCandidateNative } = {}) { this.compileCandidate = compileCandidate; this.nativeValidation = null; this.nativeReceipt = null; this.nativeAttempts = 0; this.pending = null; this.undoState = null; this.undoHistory = []; this.projectHistories = new Map(); this.projectName = null; this.requestId = null; this.reservation = null; this.generation = 0; this.candidateSequence = 0; this.repairs = 0; }
activateProject(name) {
  if (this.projectName === name) return;
  this.cancelNativeValidation();
  this.reservation = null; this.pending = null; this.requestId = null; this.generation++;
  this.projectName = name; this.undoHistory = this.projectHistories.get(name) || [];
  this.projectHistories.delete(name); this.projectHistories.set(name, this.undoHistory);
  if (this.projectHistories.size > 20) this.projectHistories.delete(this.projectHistories.keys().next().value);
  this.undoState = this.undoHistory.at(-1) || null;
}
bindRequest(requestId) { if (this.reservation || this.pending) throw new Error('AI 建议已经生成或操作正在进行'); this.requestId = requestId; }
begin(snapshot, requestId = null) {
  if (this.reservation) throw new Error('AI 工程操作正在进行');
  this.cancelNativeValidation(); this.nativeAttempts = 0;
  this.activateProject(snapshot.name); this.undoHistory = this.projectHistories.get(snapshot.name) || [];
  this.undoState = this.undoHistory.at(-1) || null; this.requestId = requestId;
  this.base = clone(snapshot); this.pending = null; this.repairs = 0; this.generation++;
}
getUndoableRequestIds(projectName = this.projectName) {
  if (!projectName) return [];
  return (this.projectHistories.get(projectName) || (projectName === this.projectName ? this.undoHistory : []))
    .map(entry => entry.requestId).filter(id => typeof id === 'string').slice(-20);
}
canUndo(current) { return this.projectName === current?.name && !!this.undoState && JSON.stringify(current) === JSON.stringify(this.undoState.after); }
invalidateProject(projectName = this.projectName) {
  if (!projectName) return;
  if (this.reservation?.projectName === projectName) throw new Error('AI 工程操作正在进行');
  this.projectHistories.set(projectName, []);
  if (this.projectName === projectName) {
    this.undoHistory = []; this.undoState = null; this.pending = null; this.generation++;
    this.cancelNativeValidation();
  }
}
  stage(proposal, intent) {
  if (this.reservation) throw new Error('AI 工程操作正在进行');
    this.cancelNativeValidation(); this.pending = null;
    const prepared = prepareProposal(this.base, proposal, intent);
    if (!prepared) return null;
    return this.stagePrepared(prepared);
  }
  stagePrepared(prepared) {
    if (this.reservation) throw new Error('AI 工程操作正在进行');
    this.cancelNativeValidation(); this.pending = null;
    if (prepared.candidate.name !== this.base.name || JSON.stringify(prepared.candidate.publication) !== JSON.stringify(this.base.publication)) throw new Error('候选不能改变工程身份或发布设置');
    const validation = { ...validateCandidate(prepared.candidate), native_compiled: false };
  this.pending = { ...clone(prepared), validation, candidateId: `${this.generation}:${++this.candidateSequence}`, requestId: this.requestId, projectName: this.projectName };
  return clone(this.pending);
  }
  cancelNativeValidation() { this.nativeValidation?.controller.abort(); this.nativeValidation = null; this.nativeReceipt = null; }
  isNativeValidated() {
    const proof = this.nativeReceipt;
    return !!proof && this.pending?.validation.native_compiled === true && proof.candidateId === this.pending.candidateId && proof.requestId === this.requestId &&
      proof.generation === this.generation && proof.candidate === JSON.stringify(this.pending.candidate) && proof.base === JSON.stringify(this.base);
  }
  async validateNative(current, { requestId = this.requestId, candidateId = this.pending?.candidateId, fingerprint = '', getCurrent = () => current } = {}) {
    if (!this.pending || this.reservation || requestId !== this.requestId || candidateId !== this.pending.candidateId || JSON.stringify(current) !== JSON.stringify(this.base)) throw new Error('工程或候选已变化；无法验证原生编译');
    if (this.isNativeValidated()) return clone(this.pending.validation);
    if (this.nativeValidation) throw new Error('候选原生编译正在进行');
    if (++this.nativeAttempts > 3) throw new Error('每个请求最多三次候选原生编译；请开始新请求');
    const token = { controller: new AbortController(), candidateId, requestId, fingerprint, generation: this.generation,
      candidate: JSON.stringify(this.pending.candidate), base: JSON.stringify(this.base) };
    this.nativeValidation = token;
    const timer = setTimeout(() => token.controller.abort(), 30000);
    try {
      const result = await this.compileCandidate(freezeSnapshot(clone(this.pending.candidate)), { signal: token.controller.signal });
      if (token.controller.signal.aborted || this.nativeValidation !== token || this.pending?.candidateId !== candidateId || this.generation !== token.generation ||
          this.requestId !== requestId || JSON.stringify(this.pending.candidate) !== token.candidate || JSON.stringify(getCurrent()) !== token.base)
        throw new Error('请求已取消或工程/候选在编译期间已变化；编译结果已丢弃');
      if (result?.success !== true) throw new Error('候选原生编译未通过');
      this.nativeReceipt = freezeSnapshot({ ...token, controller: undefined });
      this.pending.validation.native_compiled = true;
      return clone(this.pending.validation);
    } catch (error) {
      if (token.controller.signal.aborted) throw new Error('原生编译已取消或超过 30 秒；结果不能应用，未自动重试');
      throw error;
    } finally { clearTimeout(timer); if (this.nativeValidation === token) this.nativeValidation = null; }
  }
  reserveRepair() {
  if (this.reservation) throw new Error('AI 工程操作正在进行');
    if (++this.repairs > 2) throw new Error('每个请求最多两次修正建议');
    this.cancelNativeValidation(); this.pending = null;
  }
reserveApply(current, { requestId = null, candidateId = null, projectName = current?.name, fingerprint = '' } = {}) {
  if (this.reservation) throw new Error('AI 工程操作正在进行');
  if (!this.pending || projectName !== this.projectName || this.pending.projectName !== projectName ||
      (this.pending.requestId !== null && this.pending.requestId !== requestId) || this.pending.candidateId !== candidateId ||
      JSON.stringify(current) !== JSON.stringify(this.base))
    throw new Error('工程已变化或建议已过期；请基于当前工程重新生成');
  if (!this.isNativeValidated()) throw new Error('候选尚未通过绑定本次请求的原生编译，不能应用');
  if (this.nativeReceipt.fingerprint && this.nativeReceipt.fingerprint !== fingerprint) throw new Error('候选编译与当前工程指纹不匹配');
  const reservation = freezeSnapshot({ kind: 'apply', projectName, requestId, candidateId, fingerprint, generation: this.generation, base: clone(this.base), candidate: clone(this.pending.candidate) });
  this.reservation = reservation; return reservation;
}
reserveUndo(current, { requestId, projectName = current?.name, fingerprint = '' } = {}) {
  if (this.reservation) throw new Error('AI 工程操作正在进行');
  const latest = this.undoHistory.at(-1);
  const index = requestId ? this.undoHistory.findIndex(entry => entry.requestId === requestId) : this.undoHistory.length - 1;
  const conflict = (expected, actual) => {
    const error = new Error('AI 应用后工程已变化；回滚与后续编辑冲突，未覆盖草稿。请查看差异。');
    error.code = 'undo_conflict';
    error.conflicts = [{ field: '工程', before: JSON.stringify(expected).slice(0, 8192), after: JSON.stringify(actual).slice(0, 8192) }];
    return error;
  };
  if (!latest || index < 0 || projectName !== this.projectName || latest.projectName !== projectName)
    throw new Error('撤销目标不存在或工程已经变化；没有撤销修改');
  if (JSON.stringify(current) !== JSON.stringify(latest.after)) throw conflict(latest.after, current);
  const suffix = this.undoHistory.slice(index);
  for (let i = 1; i < suffix.length; i++) {
    if (suffix[i].projectName !== projectName || JSON.stringify(suffix[i - 1].after) !== JSON.stringify(suffix[i].before))
      throw conflict(suffix[i - 1].after, suffix[i].before);
  }
  const reservation = freezeSnapshot({ kind: 'undo', projectName, requestId: suffix[0].requestId, fingerprint,
    generation: this.generation, base: clone(current), candidate: clone(suffix[0].before),
    undoIndex: index, undoEntries: clone(suffix), undoneRequests: suffix.map(entry => entry.requestId) });
  this.reservation = reservation; return reservation;
}
isReservationCurrent(reservation) { return this.reservation === reservation && reservation.generation === this.generation && reservation.projectName === this.projectName; }
cancelReservation(reservation) { if (this.reservation === reservation) this.reservation = null; }
commitApply(current, reservation, load = null) {
  if (!this.isReservationCurrent(reservation) || reservation.kind !== 'apply' || (this.pending?.requestId !== null && reservation.requestId !== this.pending?.requestId) ||
      reservation.candidateId !== this.pending?.candidateId || JSON.stringify(current) !== JSON.stringify(reservation.base) ||
      JSON.stringify(this.pending.candidate) !== JSON.stringify(reservation.candidate))
    throw new Error('工程或建议在检查点期间已变化；没有应用修改');
  validateCandidate(reservation.candidate);
  if (!this.isNativeValidated()) throw new Error('候选原生编译验证已失效；没有应用修改');
  const result = clone(reservation.candidate); const actual = load ? load(clone(result)) : result;
  this.undoState = { requestId: reservation.requestId, candidateId: reservation.candidateId, projectName: reservation.projectName, before: clone(current), after: clone(actual) };
  this.undoHistory.push(this.undoState);
  if (this.undoHistory.length > 20) this.undoHistory.shift();
  this.projectHistories.set(this.projectName, this.undoHistory);
  this.pending = null; this.reservation = null; return result;
}
commitUndo(current, reservation, load = null) {
  const latest = this.undoHistory.at(-1);
  if (!this.isReservationCurrent(reservation) || reservation.kind !== 'undo' ||
      JSON.stringify(this.undoHistory.slice(reservation.undoIndex)) !== JSON.stringify(reservation.undoEntries) ||
      JSON.stringify(current) !== JSON.stringify(reservation.base) || JSON.stringify(latest.after) !== JSON.stringify(current))
    throw new Error('工程或撤销目标在操作期间已变化；没有撤销修改');
  const result = clone(reservation.candidate); if (load) load(clone(result));
  this.undoHistory.splice(reservation.undoIndex); this.projectHistories.set(this.projectName, this.undoHistory);
  this.undoState = this.undoHistory.at(-1) || null; this.pending = null; this.reservation = null; return result;
}
apply(current, requestId = null) {
  const reservation = this.reserveApply(current, { requestId, candidateId: this.pending?.candidateId, projectName: this.projectName, fingerprint: this.nativeReceipt?.fingerprint || '' });
  return this.commitApply(current, reservation);
  }
markApplied(actual) { if (this.undoState) { this.undoState.after = clone(actual); this.projectHistories.set(this.projectName, this.undoHistory); } }
reject() { if (!this.reservation) { this.cancelNativeValidation(); this.pending = null; } }
undo(current, requestId = null) {
  const reservation = this.reserveUndo(current, { requestId, projectName: this.projectName });
  return this.commitUndo(current, reservation);
  }
}
