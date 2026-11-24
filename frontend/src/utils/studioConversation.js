import { assistantContext, parseProposal, StudioProposalSession } from './studioAssistant.js';
import { EFFECT_PRESETS } from '../blockly/presets.js';
import { describeGraph, GRAPH_OPERATION_SCHEMA, GRAPH_LIMITS, prepareGraphCandidate } from './studioGraphTools.js';
import Blockly from '../blockly/index.js';
import { registerCustomBlocks } from '../blockly/customBlocks.js';
import { EFFECT_BLOCK_TYPES } from './effectCapabilities.js';

export async function projectFingerprint(snapshot) {
  const bytes = new TextEncoder().encode(JSON.stringify(snapshot));
  const digest = await globalThis.crypto.subtle.digest('SHA-256', bytes);
  return [...new Uint8Array(digest)].map(b => b.toString(16).padStart(2, '0')).join('');
}
const copy = value => JSON.parse(JSON.stringify(value));
const summary = value => { if (!value) return null; const { preview, ...metadata } = value; return copy(metadata); };
function exact(value, keys) {
  if (!value || Array.isArray(value) || typeof value !== 'object' || Object.keys(value).length !== keys.length || keys.some(k => !Object.hasOwn(value, k))) throw new Error('工具参数无效');
}
const bytes = value => new TextEncoder().encode(JSON.stringify(value)).length;
const boundedResult = value => {
  if (bytes(value) > 49152) throw new Error('工具结果超过 48 KiB；请缩小工程或修改范围，结果未截断');
  return copy(value);
};
const diagnostic = error => String(error?.message || error).replace(/[A-Za-z]:[\\/][^\s]+|\\\\[^\s]+|\bBearer\s+\S+|\bsk-[\w-]+/gi, '[已隐藏]').slice(0, 2048);
let blockSchema;
function graphSchema() {
  if (!blockSchema) {
    registerCustomBlocks(); const workspace = new Blockly.Workspace(); Blockly.Events.disable();
    try {
      blockSchema = [...EFFECT_BLOCK_TYPES].map(type => {
        const node = workspace.newBlock(type);
        const fields = node.inputList.flatMap(input => input.fieldRow).filter(field => field.name).map(field => ({
          name: field.name, type: field.name === 'VAR' ? 'variable_id' : typeof field.getValue(),
          ...(field.name === 'VAR' ? {} : { default: field.getValue() }),
          ...(field.name !== 'VAR' && typeof field.getOptions === 'function' ? { values: field.getOptions(false).map(option => option[1]) } : {}),
          ...(Number.isFinite(field.getMin?.()) ? { min: field.getMin() } : {}),
          ...(Number.isFinite(field.getMax?.()) ? { max: field.getMax() } : {})
        }));
        return { type, fields, inputs: node.inputList.filter(input => input.connection).map(input => ({ name: input.name,
          kind: input.connection.type === Blockly.ConnectionType.INPUT_VALUE ? 'value' : 'statement', accepts: input.connection.getCheck() })),
          output: node.outputConnection?.getCheck() ?? null, previous: !!node.previousConnection, next: !!node.nextConnection,
          ...(type === 'controls_if' ? { dynamic_inputs: 'IF0/DO0, IF1/DO1..IF32/DO32 from elseIfCount; ELSE from hasElse' } : {}) };
      });
    } finally { workspace.dispose(); Blockly.Events.enable(); }
  }
  return { limits: GRAPH_LIMITS, operations: GRAPH_OPERATION_SCHEMA, blocks: blockSchema };
}
// Candidate operations reuse the MVP validator/renderer/transaction. No tool writes a draft.
export class StudioConversationTools {
  constructor(session = new StudioProposalSession()) { this.session = session; this.active = null; this.previous = null; this.cancelled = false; this.proposals = 0; this.epoch = 0; }
  async begin(snapshot, requestId = null) {
    const epoch = ++this.epoch;
    this.previous = this.base?.name === snapshot.name ? this.active || this.previous : null;
    const fingerprint = await projectFingerprint(snapshot);
    if (epoch !== this.epoch) throw new Error('请求已经变化');
    this.base = copy(snapshot); this.fingerprint = fingerprint; this.cancelled = false; this.preset = null;
    if (this.previous) this.previous.stale = this.previous.status === 'proposed' && this.previous.fingerprint !== this.fingerprint;
    this.session.begin(snapshot, requestId); this.active = null; this.proposals = 0;
    return { ...assistantContext(snapshot, 'modify'), fingerprint: this.fingerprint, active_proposal: summary(this.previous) };
  }
  cancel() { this.epoch++; this.cancelled = true; this.session.reject(); this.active = null; }
  clear() { this.cancel(); this.previous = null; }
  mark(status) { if (this.active) this.active.status = status; else if (this.previous) this.previous.status = status; }
  async execute(name, args, current, diagnostics = {}, { getCurrent = () => current } = {}) {
    const epoch = this.epoch, fingerprint = await projectFingerprint(current);
    if (this.cancelled || epoch !== this.epoch || fingerprint !== this.fingerprint) throw new Error('项目已变化或请求已取消；后续建议须基于最新草稿');
    if (bytes(args) > (name === 'propose_graph_change' ? 73728 : 8192)) throw new Error('工具参数超过限制');
    const reads = ['get_current_effect_summary', 'get_capabilities', 'get_validation_errors', 'get_build_errors', 'get_active_proposal', 'validate_candidate', 'simulate_candidate', 'get_project_graph', 'get_graph_schema'];
    if (reads.includes(name)) {
      exact(args, []);
      if (name === 'get_current_effect_summary') return assistantContext(current, 'modify');
      if (name === 'get_capabilities') return assistantContext(current, 'modify').capabilities;
      if (name === 'get_validation_errors') return { diagnostic: String(diagnostics.validation || '').slice(0, 2048) };
      if (name === 'get_build_errors') return { diagnostic: String(diagnostics.build || '').slice(0, 2048) };
      if (name === 'get_project_graph') return boundedResult({ fingerprint: this.fingerprint, ...describeGraph(current) });
      if (name === 'get_graph_schema') return boundedResult(graphSchema());
      if (name === 'get_active_proposal') return boundedResult(summary(this.active || this.previous) || { status: 'none' });
      if (name === 'validate_candidate') {
        if (!this.active || !this.session.pending) return { valid: false, native_compiled: false, diagnostic: this.active?.diagnostic || '没有候选建议', simulated: false };
        const active = this.active;
        try {
          await this.session.validateNative(current, { requestId: this.session.requestId, candidateId: active.candidate_id, fingerprint: this.fingerprint, getCurrent });
          if (this.cancelled || this.active !== active || await projectFingerprint(getCurrent()) !== this.fingerprint) throw new Error('请求或工程已变化；编译结果已丢弃');
          active.valid = true; active.native_compiled = true; active.diagnostic = '';
        } catch (error) {
          if (this.active !== active || this.cancelled) throw error;
          active.valid = false; active.native_compiled = false; active.diagnostic = diagnostic(error);
        }
        return boundedResult(active);
      }
      return this.active ? { valid: this.active.valid, native_compiled: this.active.native_compiled, diagnostic: this.active.diagnostic, simulated: this.active.simulated, preview: this.active.preview } : { valid: false, native_compiled: false, diagnostic: '没有候选建议', simulated: false };
    }
    if (name === 'get_preset') {
      exact(args, ['preset_id']); const preset = EFFECT_PRESETS.find(p => p.id === args.preset_id);
      if (!preset) return { available: false, diagnostic: '当前可用工具无法安全生成这一效果；请选择已有模板' };
      this.preset = preset.id;
      return assistantContext(current, 'generate', '', 'none', preset.id).presets[0];
    }
    if (!['propose_effect_change', 'propose_graph_change'].includes(name)) throw new Error('工具不在安全允许列表中');
    if (++this.proposals > 3) throw new Error('每个请求最多三个候选（含两次修复）；请开始新请求');
    this.session.reject();
    let proposal;
    if (name === 'propose_graph_change') {
      exact(args, ['summary', 'operations', 'preserve_rgb']);
      if (typeof args.summary !== 'string' || args.summary.length > 4000 || typeof args.preserve_rgb !== 'boolean') throw new Error('结构建议摘要或颜色保护选项无效');
    } else {
      proposal = parseProposal(JSON.stringify(args));
      if (proposal.action !== 'propose' || proposal.preset !== null && proposal.preset !== this.preset) throw new Error('建议采用了未验证模板');
      if (proposal.preset !== null && proposal.edits.length) throw new Error('模板生成不允许使用当前草稿的数值节点标识');
    }
    const result = { summary: args.summary, fingerprint: this.fingerprint, candidate_id: null, valid: false, native_compiled: false, simulated: false, diagnostic: '', changes: [], status: 'proposed' };
    try {
      const staged = name === 'propose_graph_change'
        ? this.session.stagePrepared({ ...prepareGraphCandidate(this.base, args.operations, { preserveRgb: args.preserve_rgb }), summary: args.summary })
        : this.session.stage(proposal, proposal.preset ? 'generate' : 'modify');
      result.changes = name === 'propose_graph_change' ? staged.changes.map(change => ({ field: `${change.kind} · ${change.id} · ${change.type || 'variable'}`, before: JSON.stringify(change.before), after: JSON.stringify(change.after) })) : staged.changes;
      result.candidate_id = staged.candidateId; result.simulated = true; result.preview = staged.validation.preview;
      result.diagnostic = '结构和本地模拟通过；请调用 validate_candidate 完成原生编译后再申请应用';
      boundedResult(result);
    } catch (error) { this.session.reject(); result.candidate_id = null; result.changes = []; result.diagnostic = diagnostic(error); }
    this.active = result; return boundedResult(result);
  }
}
