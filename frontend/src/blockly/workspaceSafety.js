import { EFFECT_BLOCK_TYPES } from '../utils/effectCapabilities.js';

const fail = message => { throw new Error('积木工程无效：' + message); };
const object = value => value && typeof value === 'object' && !Array.isArray(value);
const keys = (value, allowed) => {
  if (!object(value) || Object.keys(value).some(k => !allowed.includes(k))) fail('不支持的结构或 Schema');
};
const MAX_IF_ELSE_IF = 32;

function validateExtraState(block) {
  if (block.type !== 'controls_if' || block.extraState === undefined) return;
  const state = block.extraState;
  keys(state, ['elseIfCount', 'hasElse']);
  const elseIfCount = state.elseIfCount;
  if (elseIfCount !== undefined && (!Number.isInteger(elseIfCount) || elseIfCount < 0 || elseIfCount > MAX_IF_ELSE_IF)) {
    fail(`条件分支数量超过限制（${MAX_IF_ELSE_IF}）`);
  }
  if (state.hasElse !== undefined && typeof state.hasElse !== 'boolean') fail('条件分支结构无效');
}

function validateVariables(variables) {
  if (variables === undefined) return;
  const ids = new Set();
  for (const variable of variables) {
    keys(variable, ['id', 'name', 'type']);
    if (typeof variable.id !== 'string' || !variable.id || ids.has(variable.id) || typeof variable.name !== 'string' || !variable.name ||
        (variable.type !== undefined && typeof variable.type !== 'string')) fail('变量声明无效');
    ids.add(variable.id);
  }
}

export function effectVariables(workspace) {
  return workspace?.getVariableMap?.().getAllVariables?.() || workspace?.getAllVariables?.() || [];
}

// IDs, not display names, own variables. An injective encoding keeps arbitrary
// Unicode names and names that sanitize identically distinct in both targets.
export function effectVariableSymbol(variable) {
  const id = variable?.getId?.();
  if (typeof id !== 'string' || !id) fail('变量引用不存在');
  return 'var_' + [...id].map(char => char.codePointAt(0).toString(16)).join('_');
}

export function validateEffectVariableTypes(workspace) {
  for (const variable of effectVariables(workspace)) {
    if (variable.type && variable.type !== 'Number') fail('光效仅支持数值变量');
  }
  const numeric = block => !block || block.type === 'variables_get' || block.outputConnection?.getCheck?.()?.includes('Number');
  for (const block of workspace?.getAllBlocks?.(false) || []) {
    if (block.isEnabled?.() === false) continue;
    if (block.type === 'variables_set' && !numeric(block.getInputTargetBlock('VALUE')) ||
        block.type === 'math_change' && !numeric(block.getInputTargetBlock('DELTA'))) fail('光效仅支持数值变量；此赋值不是数值表达式');
    if (['variables_set', 'variables_get', 'math_change'].includes(block.type)) effectVariableSymbol(block.getField('VAR')?.getVariable?.());
    if (block.type === 'variables_get') {
      const expected = block.outputConnection?.targetConnection?.getCheck?.();
      if (expected && !expected.includes('Number')) fail('数值变量不能连接到非数值输入');
    }
  }
}

// Bound the entire input before recursive Blockly serializers or graph visitors.
// Both persisted workspace states and the original preset block-container format
// are supported. No serializer from another workspace can execute here.
export function validateEffectWorkspace(json) {
  if (typeof json === 'string') {
    if (json.length > 262144) fail('工程超过大小限制');
    try { json = JSON.parse(json); } catch { fail('JSON 已损坏'); }
  }
  const seen = new Set(), pending = [[json, 0]];
  let values = 0, size = 0;
  while (pending.length) {
    const [value, depth] = pending.pop();
    if (depth > 192 || ++values > 32768) fail('工程层级或结构超过限制');
    if (value && typeof value === 'object') {
      if (seen.has(value)) fail('循环或共享节点');
      seen.add(value);
      if (!Array.isArray(value) && ![Object.prototype, null].includes(Object.getPrototypeOf(value))) fail('非 JSON 对象');
      for (const [key, child] of Object.entries(value)) {
        if (['__proto__', 'prototype', 'constructor'].includes(key)) fail('不支持的属性');
        size += key.length;
        pending.push([child, depth + 1]);
      }
    } else if (typeof value === 'string') size += value.length;
    else if (value !== null && !['boolean', 'number'].includes(typeof value)) fail('非 JSON 数据');
    else if (typeof value === 'number' && !Number.isFinite(value)) fail('数值无效');
    if (size > 262144) fail('工程超过大小限制');
  }
  if (new TextEncoder().encode(JSON.stringify(json)).length > 262144) fail('工程超过大小限制');
  let state = json;
  if (Array.isArray(json)) state = { blocks: { blocks: json } };
  else if (Array.isArray(json?.blocks)) {
    keys(json, ['blocks', 'languageVersion', 'variables', 'workspaceComments']);
    const { variables, workspaceComments, ...container } = json;
    state = { blocks: container, ...(variables ? { variables } : {}), ...(workspaceComments ? { workspaceComments } : {}) };
  }
  keys(state, ['blocks', 'variables', 'workspaceComments']);
  // Blockly's real serializer omits empty collections, including blocks.
  // A blank, variable-only or comment-only workspace is a valid draft.
  if (!Object.hasOwn(state, 'blocks')) state = { ...state, blocks: { blocks: [] } };
  keys(state.blocks, ['blocks', 'languageVersion']);
  if ('languageVersion' in state.blocks && state.blocks.languageVersion !== 0) fail('不支持的 Schema 版本');
  if (!Array.isArray(state.blocks.blocks)) fail('缺少积木列表');
  if ('variables' in state && (!Array.isArray(state.variables) || state.variables.length > 2048)) fail('变量结构无效');
  validateVariables(state.variables);
  if ('workspaceComments' in state) {
    if (!Array.isArray(state.workspaceComments) || state.workspaceComments.length > 2048) fail('注释结构无效');
    const comments = new Set();
    for (const comment of state.workspaceComments) {
      keys(comment, ['id', 'text', 'x', 'y', 'width', 'height', 'collapsed', 'editable', 'movable', 'deletable']);
      if ('text' in comment && typeof comment.text !== 'string') fail('注释文本无效');
      for (const name of ['x', 'y', 'width', 'height']) if (name in comment && !Number.isFinite(comment[name])) fail('注释尺寸无效');
      if (comment.id !== undefined) {
        if (typeof comment.id !== 'string' || !comment.id || comments.has(comment.id)) fail('注释标识重复或无效');
        comments.add(comment.id);
      }
    }
  }
  const ids = new Set(), nodes = state.blocks.blocks.map(block => [block, 0, true]);
  let count = 0, activeCount = 0;
  while (nodes.length) {
    const [block, depth, active] = nodes.pop();
    if (++count > 2048 || depth > 48) fail('工程节点或层级超过限制（2048 / 48）');
    if (active) ++activeCount;
    keys(block, ['type', 'id', 'x', 'y', 'fields', 'inputs', 'next', 'extraState', 'icons', 'data', 'collapsed', 'disabled', 'enabled', 'disabledReasons', 'deletable', 'movable', 'editable', 'inline']);
    if (!EFFECT_BLOCK_TYPES.has(block.type)) fail('不支持的积木：' + String(block.type));
    validateExtraState(block);
    if (block.id !== undefined) {
      if (typeof block.id !== 'string' || !block.id || ids.has(block.id)) fail('积木标识重复或无效');
      ids.add(block.id);
    }
    if (block.fields !== undefined && !object(block.fields)) fail('字段结构无效');
    if (block.inputs !== undefined) {
      if (!object(block.inputs)) fail('输入结构无效');
      for (const input of Object.values(block.inputs)) {
        keys(input, ['block', 'shadow']);
        if (!input.block && !input.shadow) fail('输入缺少节点');
        if (input.block) nodes.push([input.block, depth + 1, active]);
        if (input.shadow) nodes.push([input.shadow, depth + 1, active && !input.block]);
      }
    }
    if (block.next !== undefined) { keys(block.next, ['block']); nodes.push([block.next.block, depth + 1, active]); }
  }
  return { state, count: activeCount };
}

export function loadEffectWorkspace(Blockly, json, workspace, options = {}) {
  if (!workspace) fail('工作区不可用');
  const validated = validateEffectWorkspace(json);
  const state = structuredClone(validated.state), count = validated.count;
  const identify = block => {
    block.id ||= Blockly.utils.idGenerator.genUid();
    for (const input of Object.values(block.inputs || {})) { if (input.block) identify(input.block); if (input.shadow) identify(input.shadow); }
    if (block.next?.block) identify(block.next.block);
  };
  for (const block of state.blocks.blocks) identify(block);
  const candidate = new Blockly.Workspace();
  Blockly.Events.disable();
  try {
    Blockly.serialization.workspaces.load(state, candidate);
    if (candidate.getAllBlocks(false).length !== count) fail('积木未完整加载');
    // Blockly silently ignores unknown input names. Reject these in staging.
    const check = block => {
      // Hidden fallback shadows are serialized state rather than live blocks.
      // Instantiate those only in staging so their schema is checked too.
      const loaded = candidate.getBlockById(block.id) || Blockly.serialization.blocks.append(block, candidate);
      if (loaded) {
        for (const name of Object.keys(block.inputs || {})) if (!loaded.getInput(name)) fail('不支持的输入：' + name);
        for (const name of Object.keys(block.fields || {})) if (!loaded.getField(name)) fail('不支持的字段：' + name);
      }
      for (const input of Object.values(block.inputs || {})) { if (input.block) check(input.block); if (input.shadow) check(input.shadow); }
      if (block.next?.block) check(block.next.block);
    };
    for (const block of state.blocks.blocks) check(block);
  } catch (error) {
    throw new Error('积木数据无法完整加载：' + error.message);
  } finally { candidate.dispose(); Blockly.Events.enable(); }

  const previous = Blockly.serialization.workspaces.save(workspace);
  const undo = workspace.undoStack_?.slice(), redo = workspace.redoStack_?.slice();
  const group = Blockly.Events.getGroup(), recordUndo = Blockly.Events.getRecordUndo();
  try { Blockly.serialization.workspaces.load(state, workspace, options); }
  catch (error) {
    Blockly.Events.disable();
    try {
      Blockly.serialization.workspaces.load(previous, workspace);
      if (undo) workspace.undoStack_ = undo;
      if (redo) workspace.redoStack_ = redo;
    } finally { Blockly.Events.enable(); }
    throw new Error('积木加载失败，已恢复原草稿：' + error.message);
  } finally { Blockly.Events.setGroup(group); Blockly.Events.setRecordUndo(recordUndo); }
}
