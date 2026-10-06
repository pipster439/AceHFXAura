import Blockly, { loadSafeWorkspaceJson, assertNoLegacyEventPulse } from '../blockly/index.js';
import { CppTranspiler } from '../blockly/cppTranspiler.js';
import { JsTranspiler } from '../blockly/jsTranspiler.js';
import { EFFECT_PRESETS } from '../blockly/presets.js';
import { normalizePublication } from '../blockly/publication.js';
import { deriveEffectCapabilities } from './effectCapabilities.js';

export const ASSISTANT_INTENTS = Object.freeze(['generate', 'modify', 'explain', 'error_analysis']);
const clone = value => JSON.parse(JSON.stringify(value));
function exact(value, keys) {
  if (!value || Array.isArray(value) || typeof value !== 'object' || Object.keys(value).length !== keys.length || keys.some(k => !Object.hasOwn(value, k))) throw new Error('建议字段无效');
}
export function numericNodes(json) {
  const nodes = []; let count = 0;
  const visit = (block, parent = '', input = '') => {
    if (!block) return;
    if (++count > 256) throw new Error('工程节点超过 AI MVP 限制（256）');
    if (block.type === 'math_number' && typeof block.fields?.NUM === 'number') {
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
  const nodes = numericNodes(snapshot.json).map(({ block, ...n }) => n);
  const capabilities = deriveEffectCapabilities(snapshot);
  return { intent, name: /^[a-zA-Z_][a-zA-Z0-9_]{0,47}$/.test(snapshot.name) ? snapshot.name : 'current_effect',
    publication: normalizePublication(snapshot.publication), nodes, capabilities,
    presets: EFFECT_PRESETS.filter(p => intent === 'generate' && p.id === selectedPresetId).map(p => ({ id: p.id, name: p.name, description: p.description, tags: p.tags, required_inputs: p.requiredInputs, capabilities: p.capabilities })),
    diagnostic: intent === 'error_analysis' ? diagnostic.slice(0, 2048) : '',
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

// Blockly's grouped serialization events preserve ordinary history. AI also
// keeps one explicit snapshot transaction for its separate Undo button.
export function loadAiSnapshot(workspace, json) {
  assertNoLegacyEventPulse(json);
  const state = json.blocks && !Array.isArray(json.blocks) ? json : { blocks: json };
  const group = Blockly.Events.getGroup(); const recordUndo = Blockly.Events.getRecordUndo(); Blockly.Events.setGroup(true);
  try { Blockly.serialization.workspaces.load(state, workspace, { recordUndo: true }); }
  finally { Blockly.Events.setRecordUndo(recordUndo); Blockly.Events.setGroup(group); }
}
export class StudioProposalSession {
  constructor() { this.pending = null; this.undoState = null; this.repairs = 0; }
  begin(snapshot) { this.base = clone(snapshot); this.pending = null; this.repairs = 0; }
  stage(proposal, intent) {
    this.pending = null;
    const prepared = prepareProposal(this.base, proposal, intent);
    if (!prepared) return null;
    const validation = validateCandidate(prepared.candidate);
    this.pending = { ...prepared, validation }; return clone(this.pending);
  }
  reserveRepair() {
    if (++this.repairs > 2) throw new Error('每个请求最多两次修正建议');
    this.pending = null;
  }
  apply(current) {
    if (!this.pending || JSON.stringify(current) !== JSON.stringify(this.base)) throw new Error('工程已变化或没有通过验证的建议，请重新生成');
    validateCandidate(this.pending.candidate);
    this.undoState = { before: clone(current), after: clone(this.pending.candidate) };
    const result = clone(this.pending.candidate); this.pending = null; return result;
  }
  markApplied(actual) { if (this.undoState) this.undoState.after = clone(actual); }
  reject() { this.pending = null; }
  undo(current) {
    if (!this.undoState || JSON.stringify(current) !== JSON.stringify(this.undoState.after)) throw new Error('AI 应用后工程已变化；为保留后续编辑，无法撤销');
    const result = clone(this.undoState.before); this.undoState = null; this.pending = null; return result;
  }
}
