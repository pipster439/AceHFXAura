import Blockly, { loadSafeWorkspaceJson } from '../blockly/index.js';
import { registerCustomBlocks } from '../blockly/customBlocks.js';
import { validateEffectWorkspace } from '../blockly/workspaceSafety.js';
import { CppTranspiler } from '../blockly/cppTranspiler.js';
import { JsTranspiler } from '../blockly/jsTranspiler.js';
import { normalizePublication } from '../blockly/publication.js';
import { deriveEffectCapabilities, EFFECT_BLOCK_TYPES } from './effectCapabilities.js';

export const GRAPH_LIMITS = Object.freeze({ operations: 256, bytes: 65536, blocks: 2048, variables: 256 });
const clone = value => structuredClone(value);
const fail = message => { throw new Error('结构修改已拒绝：' + message); };
const identifier = { type: 'string', minLength: 1, maxLength: 128 };
const fieldValue = { anyOf: [{ type: 'string', maxLength: 256 }, { type: 'number' }, { type: 'boolean' }] };
const slotSchema = { oneOf: [
  { type: 'object', additionalProperties: false, required: ['kind', 'name'], properties: { kind: { const: 'input' }, name: { type: 'string', pattern: '^[A-Za-z][A-Za-z0-9_]{0,47}$' } } },
  { type: 'object', additionalProperties: false, required: ['kind'], properties: { kind: { const: 'next' } } }
] };
const operation = (op, properties, required = Object.keys(properties)) => ({ type: 'object', additionalProperties: false,
  required: ['op', ...required], properties: { op: { const: op }, ...properties } });
export const GRAPH_OPERATION_SCHEMA = Object.freeze({ type: 'array', minItems: 1, maxItems: GRAPH_LIMITS.operations, items: { oneOf: [
  operation('create', { id: identifier, type: { type: 'string', enum: [...EFFECT_BLOCK_TYPES] }, fields: { type: 'object', additionalProperties: fieldValue },
    state: { type: 'object', additionalProperties: false, properties: { elseIfCount: { type: 'integer', minimum: 0, maximum: 32 }, hasElse: { type: 'boolean' } } } }, ['id', 'type']),
  operation('delete', { id: identifier }),
  operation('set_field', { id: identifier, field: { type: 'string', pattern: '^[A-Z][A-Z0-9_]{0,47}$' }, value: fieldValue }),
  operation('connect', { parent: identifier, slot: slotSchema, child: identifier }),
  operation('disconnect', { id: identifier }),
  operation('move', { id: identifier, x: { type: 'number', minimum: -100000, maximum: 100000 }, y: { type: 'number', minimum: -100000, maximum: 100000 } }),
  operation('reorder', { parent: identifier, slot: slotSchema, ids: { type: 'array', minItems: 1, maxItems: 256, uniqueItems: true, items: identifier } }),
  operation('create_variable', { id: identifier, name: { type: 'string', minLength: 1, maxLength: 64 } })
] } });

function exact(value, required, optional = []) {
  if (!value || Array.isArray(value) || typeof value !== 'object' || required.some(key => !Object.hasOwn(value, key)) ||
      Object.keys(value).some(key => !required.includes(key) && !optional.includes(key))) fail('操作字段不符合 Schema');
}
function id(value) {
  if (typeof value !== 'string' || value.length < 1 || value.length > 128 || /[\x00-\x1f\x7f]/.test(value)) fail('节点或变量 ID 无效');
}
function safeJson(value) {
  const pending = [[value, 0]], seen = new Set(); let count = 0;
  while (pending.length) {
    const [current, depth] = pending.pop();
    if (++count > 16384 || depth > 12) fail('操作结构超过限制');
    if (current && typeof current === 'object') {
      if (seen.has(current) || !Array.isArray(current) && ![Object.prototype, null].includes(Object.getPrototypeOf(current))) fail('操作必须是独立 JSON 数据');
      seen.add(current);
      for (const [key, child] of Object.entries(current)) {
        if (['__proto__', 'prototype', 'constructor'].includes(key)) fail('禁止的属性');
        pending.push([child, depth + 1]);
      }
    } else if (typeof current === 'number' ? !Number.isFinite(current) : current !== null && !['boolean', 'string'].includes(typeof current)) fail('操作包含非 JSON 值');
  }
  if (new TextEncoder().encode(JSON.stringify(value)).length > GRAPH_LIMITS.bytes) fail('操作超过 64 KiB');
}
function checkSlot(slot) {
  if (slot?.kind === 'next') exact(slot, ['kind']);
  else {
    exact(slot, ['kind', 'name']);
    if (slot.kind !== 'input' || typeof slot.name !== 'string' || !/^[A-Za-z][A-Za-z0-9_]{0,47}$/.test(slot.name)) fail('连接位置无效');
  }
}
export function parseGraphOperations(value) {
  if (typeof value === 'string') {
    if (value.length > GRAPH_LIMITS.bytes) fail('操作超过 64 KiB');
    try { value = JSON.parse(value); } catch { fail('操作 JSON 已损坏'); }
  }
  safeJson(value);
  if (!Array.isArray(value) || !value.length || value.length > GRAPH_LIMITS.operations) fail('每个候选需要 1–256 个操作');
  for (const item of value) {
    switch (item?.op) {
      case 'create':
        exact(item, ['op', 'id', 'type'], ['fields', 'state']); id(item.id);
        if (!EFFECT_BLOCK_TYPES.has(item.type)) fail('不支持的积木类型');
        if (item.fields !== undefined) {
          exact(item.fields, [], Object.keys(item.fields));
          if (Object.keys(item.fields).length > 16) fail('字段数量超过限制');
          for (const [field, fieldValue] of Object.entries(item.fields)) checkField(field, fieldValue);
        }
        if (item.state !== undefined) {
          if (item.type !== 'controls_if') fail('仅条件分支支持变形状态');
          exact(item.state, [], ['elseIfCount', 'hasElse']);
          if (item.state.elseIfCount !== undefined && (!Number.isInteger(item.state.elseIfCount) || item.state.elseIfCount < 0 || item.state.elseIfCount > 32) ||
              item.state.hasElse !== undefined && typeof item.state.hasElse !== 'boolean') fail('条件分支变形状态无效');
        }
        break;
      case 'delete': case 'disconnect': exact(item, ['op', 'id']); id(item.id); break;
      case 'set_field': exact(item, ['op', 'id', 'field', 'value']); id(item.id); checkField(item.field, item.value); break;
      case 'connect': exact(item, ['op', 'parent', 'slot', 'child']); id(item.parent); id(item.child); checkSlot(item.slot); break;
      case 'move':
        exact(item, ['op', 'id', 'x', 'y']); id(item.id);
        if (![item.x, item.y].every(number => Number.isFinite(number) && Math.abs(number) <= 100000)) fail('节点位置无效');
        break;
      case 'reorder':
        exact(item, ['op', 'parent', 'slot', 'ids']); id(item.parent); checkSlot(item.slot);
        if (!Array.isArray(item.ids) || !item.ids.length || item.ids.length > 256 || new Set(item.ids).size !== item.ids.length) fail('重排列表无效');
        item.ids.forEach(id); break;
      case 'create_variable':
        exact(item, ['op', 'id', 'name']); id(item.id);
        if (typeof item.name !== 'string' || !/^[\p{L}_][\p{L}\p{N}_ ]{0,63}$/u.test(item.name)) fail('变量名称无效');
        break;
      default: fail('操作不在允许列表中');
    }
  }
  return clone(value);
}
function checkField(field, value) {
  if (typeof field !== 'string' || !/^[A-Z][A-Z0-9_]{0,47}$/.test(field) ||
      !['string', 'number', 'boolean'].includes(typeof value) || typeof value === 'number' && (!Number.isFinite(value) || Math.abs(value) > 1000000) ||
      typeof value === 'string' && (value.length > 256 || /[\x00-\x1f\x7f]/.test(value))) fail('字段名称或值无效');
}

function stableState(json) {
  const state = clone(validateEffectWorkspace(json).state), used = new Set();
  const visit = (block, fn) => {
    fn(block);
    for (const input of Object.values(block.inputs || {})) { if (input.block) visit(input.block, fn); if (input.shadow) visit(input.shadow, fn); }
    if (block.next?.block) visit(block.next.block, fn);
  };
  for (const root of state.blocks.blocks) visit(root, block => { if (block.id) used.add(block.id); });
  let sequence = 0;
  for (const root of state.blocks.blocks) visit(root, block => {
    if (block.id) return;
    let name; do { name = `existing_${sequence++}`; } while (used.has(name));
    block.id = name; used.add(name);
  });
  return state;
}
function block(workspace, identifier) {
  const found = workspace.getBlockById(identifier); if (!found) fail(`节点不存在：${identifier}`); return found;
}
function slotConnection(parent, slot) {
  const connection = slot.kind === 'next' ? parent.nextConnection : parent.getInput(slot.name)?.connection;
  if (!connection || ![Blockly.ConnectionType.INPUT_VALUE, Blockly.ConnectionType.NEXT_STATEMENT].includes(connection.type)) fail('父节点没有该输入或 next 连接');
  return connection;
}
function setField(workspace, node, name, value) {
  const field = node.getField(name); if (!field) fail(`未知字段：${node.type}/${name}`);
  if (name === 'VAR') {
    if (typeof value !== 'string' || !workspace.getVariableMap().getVariableById(value)) fail('字段引用了不存在的变量');
  } else if (typeof field.getOptions === 'function') {
    if (!field.getOptions(false).some(option => option[1] === value)) fail(`字段枚举不支持：${name}`);
  } else if (typeof field.getValue() !== typeof value) fail(`字段类型不匹配：${name}`);
  field.setValue(value);
  if (field.getValue() !== value) fail(`字段值被校正或拒绝：${name}`);
}
function connect(workspace, parent, slot, child) {
  const superior = slotConnection(parent, slot);
  const inferior = superior.type === Blockly.ConnectionType.INPUT_VALUE ? child.outputConnection : child.previousConnection;
  if (!inferior || inferior.isConnected() || superior.isConnected()) fail('连接已占用或类型不匹配；须先显式断开');
  if (child.getDescendants(false).includes(parent)) fail('连接会形成循环');
  if (!workspace.connectionChecker.canConnect(superior, inferior, false) || !superior.connect(inferior) || superior.targetBlock() !== child) fail('Blockly 拒绝连接类型或图结构');
}
function disconnect(node) {
  const inferior = node.outputConnection?.isConnected() ? node.outputConnection : node.previousConnection?.isConnected() ? node.previousConnection : null;
  if (!inferior) fail('节点未连接到父节点');
  inferior.targetConnection.setShadowState(null); // Explicit removal must not spawn an unrequested replacement.
  inferior.disconnect();
  if (node.isShadow()) node.setShadow(false);
}
function applyOperation(workspace, item, identities) {
  if (item.op === 'create_variable') {
    const map = workspace.getVariableMap();
    if (identities.has(item.id) || map.getVariable(item.name) || map.getAllVariables().length >= GRAPH_LIMITS.variables) fail('变量 ID/名称已存在或数量超过限制');
    map.createVariable(item.name, '', item.id); identities.add(item.id); return;
  }
  if (item.op === 'create') {
    if (identities.has(item.id) || workspace.getAllBlocks(false).length >= GRAPH_LIMITS.blocks) fail('节点 ID 已使用或数量超过限制');
    if (['variables_get', 'variables_set', 'math_change'].includes(item.type) &&
        (typeof item.fields?.VAR !== 'string' || !workspace.getVariableMap().getVariableById(item.fields.VAR))) fail('变量积木必须显式引用已有变量 ID');
    // Only variable identity must be supplied to the serializer (to avoid
    // implicit anonymous variables); validate every other field before setting.
    const fields = item.fields?.VAR ? { VAR: { id: item.fields.VAR } } : {};
    const node = Blockly.serialization.blocks.append({ type: item.type, id: item.id, fields, ...(item.state ? { extraState: item.state } : {}) }, workspace);
    identities.add(item.id);
    const entries = Object.entries(item.fields || {}).sort(([a], [b]) =>
      Number(['STATE', 'PATH', 'FIELD'].includes(b)) - Number(['STATE', 'PATH', 'FIELD'].includes(a)));
    for (const [name, value] of entries) setField(workspace, node, name, value);
    return;
  }
  if (item.op === 'connect') { connect(workspace, block(workspace, item.parent), item.slot, block(workspace, item.child)); return; }
  if (item.op === 'reorder') {
    const parent = block(workspace, item.parent), connection = slotConnection(parent, item.slot);
    if (connection.type !== Blockly.ConnectionType.NEXT_STATEMENT) fail('只能重排语句链');
    const existing = []; for (let cursor = connection.targetBlock(); cursor; cursor = cursor.getNextBlock()) existing.push(cursor.id);
    if (existing.length !== item.ids.length || item.ids.some(value => !existing.includes(value))) fail('重排必须是原有完整语句链的排列');
    const nodes = item.ids.map(value => block(workspace, value));
    for (const value of existing) { const node = block(workspace, value); if (node.previousConnection.isConnected()) node.previousConnection.disconnect(); }
    connect(workspace, parent, item.slot, nodes[0]);
    for (let index = 1; index < nodes.length; index++) connect(workspace, nodes[index - 1], { kind: 'next' }, nodes[index]);
    return;
  }
  const node = block(workspace, item.id);
  if (item.op === 'delete') {
    if (node.getParent()) disconnect(node);
    node.dispose(false); // Delete the explicit subtree, including its successor chain; diff lists every removed ID.
  } else if (item.op === 'disconnect') disconnect(node);
  else if (item.op === 'set_field') setField(workspace, node, item.field, item.value);
  else if (item.op === 'move') {
    if (node.getParent()) fail('只能移动顶层节点；连接中的节点请先断开');
    const position = node.getRelativeToSurfaceXY(); node.moveBy(item.x - position.x, item.y - position.y);
  }
}

function describeState(state) {
  const nodes = [];
  const visit = (node, parent = null, connection = null, shadow = false) => {
    nodes.push({ id: node.id, type: node.type, fields: clone(node.fields || {}), state: clone(node.extraState || {}),
      parent, connection, shadow, x: node.x ?? null, y: node.y ?? null });
    for (const [name, input] of Object.entries(node.inputs || {})) {
      if (input.block) visit(input.block, node.id, name, false);
      if (input.shadow) visit(input.shadow, node.id, name, true);
    }
    if (node.next?.block) visit(node.next.block, node.id, 'next');
  };
  for (const node of state.blocks?.blocks || []) visit(node);
  return { nodes: nodes.sort((a, b) => a.id.localeCompare(b.id)), variables: clone(state.variables || []).sort((a, b) => a.id.localeCompare(b.id)) };
}
function diff(before, after) {
  const changes = [], old = new Map(before.nodes.map(node => [node.id, node])), next = new Map(after.nodes.map(node => [node.id, node]));
  for (const identifier of new Set([...old.keys(), ...next.keys()])) {
    const a = old.get(identifier), b = next.get(identifier);
    if (JSON.stringify(a) !== JSON.stringify(b)) changes.push({ kind: !a ? 'create' : !b ? 'delete' : 'update', id: identifier, type: b?.type || a.type, before: a || null, after: b || null });
  }
  const variables = new Map(before.variables.map(value => [value.id, value]));
  for (const variable of after.variables) if (!variables.has(variable.id)) changes.push({ kind: 'create_variable', id: variable.id, before: null, after: variable });
  return changes;
}
function rgbProtection(workspace) {
  const placement = node => {
    const chain = [];
    for (let current = node; current; current = current.getParent()) {
      const parent = current.getParent();
      chain.push({ id: current.id, parent: parent?.id || null,
        input: parent?.getNextBlock() === current ? 'next' : parent?.inputList.find(input => input.connection?.targetBlock() === current)?.name || null });
    }
    return chain;
  };
  const semantic = (node, visiting = new Set()) => {
    if (!node) return null;
    if (visiting.has(node.id)) fail('颜色依赖形成循环');
    const next = new Set(visiting); next.add(node.id);
    const fields = Object.fromEntries(node.inputList.flatMap(input => input.fieldRow).filter(field => field.name).map(field => [field.name, field.getValue()]));
    const inputs = Object.fromEntries(node.inputList.filter(input => input.connection?.type === Blockly.ConnectionType.INPUT_VALUE)
      .map(input => [input.name, semantic(input.connection.targetBlock(), next)]));
    let assignments;
    if (node.type === 'variables_get') {
      assignments = workspace.getAllBlocks(false).filter(value => ['variables_set', 'math_change'].includes(value.type) && value.getFieldValue('VAR') === node.getFieldValue('VAR'))
        .map(value => ({ placement: placement(value), value: semantic(value, next) })).sort((a, b) => JSON.stringify(a).localeCompare(JSON.stringify(b)));
    }
    return { type: node.type, fields, inputs, ...(assignments ? { assignments } : {}) };
  };
  const colorConsumer = node => {
    for (let current = node, parent = node.getParent(); parent; current = parent, parent = parent.getParent()) {
      if (['key_fill_all', 'key_set_color', 'key_ripple_effect'].includes(parent.type))
        return { id: parent.id, type: parent.type, input: parent.inputList.find(input => input.connection?.targetBlock() === current)?.name || null };
      if (!['color_brightness', 'color_lerp', 'color_cycle'].includes(parent.type)) break;
    }
    return null;
  };
  // Include HSV constructors too: substituting a new HSV source must not evade
  // an instruction to keep the existing RGB palette by retaining unused RGBs.
  return new Map(workspace.getAllBlocks(false).filter(node => ['color_rgb', 'color_hsv'].includes(node.type))
    .map(node => [node.id, JSON.stringify({ value: semantic(node), consumer: colorConsumer(node) })]));
}

export function describeGraph(snapshot) {
  return describeState(stableState(snapshot.json));
}

export function prepareGraphCandidate(snapshot, operations, options = {}) {
  exact(options, [], ['preserveRgb']);
  if (options.preserveRgb !== undefined && typeof options.preserveRgb !== 'boolean') fail('颜色保护选项无效');
  if (!snapshot || !/^[A-Za-z_][A-Za-z0-9_]{0,47}$/.test(snapshot.name)) fail('工程名称无效');
  const commands = parseGraphOperations(operations), state = stableState(snapshot.json);
  registerCustomBlocks();
  const workspace = new Blockly.Workspace(), recordUndo = Blockly.Events.getRecordUndo(), group = Blockly.Events.getGroup();
  Blockly.Events.disable();
  try {
    loadSafeWorkspaceJson(state, workspace, true);
    const before = describeState(Blockly.serialization.workspaces.save(workspace));
    const colors = options.preserveRgb ? rgbProtection(workspace) : null;
    const identities = new Set([...before.nodes.map(node => node.id), ...before.variables.map(variable => variable.id)]);
    for (const item of commands) applyOperation(workspace, item, identities);
    const json = Blockly.serialization.workspaces.save(workspace);
    validateEffectWorkspace(json);
    if (colors) {
      const actual = rgbProtection(workspace);
      if (actual.size !== colors.size) fail('必须保留原 RGB 调色板；不能添加或删除颜色节点');
      for (const [identifier, semantic] of colors) if (actual.get(identifier) !== semantic) fail('必须保留原 RGB 节点、颜色通道及其数值依赖');
    }
    const publication = normalizePublication(snapshot.publication), candidate = { ...clone(snapshot), json, publication };
    const capabilities = deriveEffectCapabilities(candidate);
    if (capabilities.diagnostics.length) fail('工程包含不支持的能力：' + capabilities.diagnostics.join(', '));
    CppTranspiler.transpile(snapshot.name, workspace, publication);
    const samples = [], times = [0, 100, 250, 500, 1000];
    for (const health of [100, 20]) {
      const simulate = JsTranspiler.compile(workspace, publication);
      for (const elapsed of times) {
        const frame = clone(simulate(elapsed, { player: { state: { health, armor: 50, helmet: true } }, round: { bomb: 'carried' } }, { W: elapsed < 250 ? 1 : 0, A: elapsed === 100 ? 1 : 0 }));
        if (!Array.isArray(frame) || frame.length !== 68 || frame.some(rgb => !Array.isArray(rgb) || rgb.length !== 3 || rgb.some(value => !Number.isFinite(value) || value < 0 || value > 255))) fail('模拟帧无效');
        samples.push({ elapsed_ms: elapsed, health, frame });
      }
    }
    const changes = diff(before, describeState(json));
    if (!changes.length) fail('操作没有改变工程');
    return { candidate, changes, validation: { valid: true, cpp_transpiled: true, native_compiled: false, simulated: true,
      capabilities, preview: samples[1].frame, samples } };
  } finally { workspace.dispose(); Blockly.Events.enable(); Blockly.Events.setRecordUndo(recordUndo); Blockly.Events.setGroup(group); }
}
