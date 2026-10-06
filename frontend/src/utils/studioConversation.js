import { assistantContext, parseProposal, StudioProposalSession } from './studioAssistant.js';
import { EFFECT_PRESETS } from '../blockly/presets.js';

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
// Candidate operations reuse the MVP validator/renderer/transaction. No tool writes a draft.
export class StudioConversationTools {
  constructor(session = new StudioProposalSession()) { this.session = session; this.active = null; this.previous = null; this.cancelled = false; }
  async begin(snapshot) {
    this.previous = this.base?.name === snapshot.name ? this.active || this.previous : null;
    this.base = copy(snapshot); this.fingerprint = await projectFingerprint(snapshot); this.cancelled = false; this.preset = null;
    if (this.previous) this.previous.stale = this.previous.status === 'proposed' && this.previous.fingerprint !== this.fingerprint;
    this.session.begin(snapshot); this.active = null;
    return { ...assistantContext(snapshot, 'modify'), fingerprint: this.fingerprint, active_proposal: summary(this.previous) };
  }
  cancel() { this.cancelled = true; this.session.reject(); this.active = null; }
  clear() { this.cancel(); this.previous = null; }
  mark(status) { if (this.active) this.active.status = status; else if (this.previous) this.previous.status = status; }
  async execute(name, args, current, diagnostics = {}) {
    if (this.cancelled || await projectFingerprint(current) !== this.fingerprint) throw new Error('项目已变化或请求已取消；后续建议须基于最新草稿');
    if (JSON.stringify(args).length > 8192) throw new Error('工具参数超过限制');
    const reads = ['get_current_effect_summary', 'get_capabilities', 'get_validation_errors', 'get_build_errors', 'get_active_proposal', 'validate_candidate', 'simulate_candidate'];
    if (reads.includes(name)) {
      exact(args, []);
      if (name === 'get_current_effect_summary') return assistantContext(current, 'modify');
      if (name === 'get_capabilities') return assistantContext(current, 'modify').capabilities;
      if (name === 'get_validation_errors') return { diagnostic: String(diagnostics.validation || '').slice(0, 2048) };
      if (name === 'get_build_errors') return { diagnostic: String(diagnostics.build || '').slice(0, 2048) };
      if (name === 'get_active_proposal') return summary(this.active || this.previous) || { status: 'none' };
      return this.active ? { valid: this.active.valid, diagnostic: this.active.diagnostic, simulated: this.active.valid } : { valid: false, diagnostic: '没有候选建议', simulated: false };
    }
    if (name === 'get_preset') {
      exact(args, ['preset_id']); const preset = EFFECT_PRESETS.find(p => p.id === args.preset_id);
      if (!preset) return { available: false, diagnostic: '当前可用工具无法安全生成这一效果；请选择已有模板' };
      this.preset = preset.id;
      return assistantContext(current, 'generate', '', 'none', preset.id).presets[0];
    }
    if (name !== 'propose_effect_change') throw new Error('工具不在安全允许列表中');
    const proposal = parseProposal(JSON.stringify(args));
    if (proposal.action !== 'propose' || proposal.preset !== null && proposal.preset !== this.preset) throw new Error('建议采用了未验证模板');
    if (proposal.preset !== null && proposal.edits.length) throw new Error('模板生成不允许使用当前草稿的数值节点标识');
    const result = { summary: proposal.summary, fingerprint: this.fingerprint, valid: false, diagnostic: '', changes: [], status: 'proposed' };
    try {
      const staged = this.session.stage(proposal, proposal.preset ? 'generate' : 'modify');
      result.changes = staged.changes; result.valid = true; result.preview = staged.validation.preview;
    } catch (error) { result.diagnostic = error.message; }
    this.active = result; return copy(result);
  }
}
