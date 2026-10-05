import { NUMERIC_GSI_STATES, DISCRETE_GSI_STATES, BOOLEAN_GSI_STATES } from '../constants/gsiDictionary.js';

// Existing Effect JS/C++ semantics; no LLM claims or persisted cache is authoritative.
const supported = new Set(`key_fill_all key_set_color key_ripple_effect key_for_each
controls_if controls_repeat_ext controls_whileUntil controls_flow_statements effect_wait_ms variables_set math_change
math_number time_elapsed_ms time_phase math_waveform geometry_coords geometry_distance geometry_radial_phase
color_rgb color_hsv color_lerp color_brightness color_cycle key_is_pressed key_decay text logic_boolean logic_negate
logic_operation gsi_state_match gsi_numeric_compare gsi_player_health_condition gsi_c4_state_condition gsi_enum_constant
gsi_get_number gsi_get_string gsi_get_boolean math_arithmetic logic_compare variables_get math_single math_trig math_round math_modulo`.split(/\s+/));
const time = new Set(['time_elapsed_ms', 'time_phase', 'geometry_radial_phase', 'color_cycle', 'key_ripple_effect', 'effect_wait_ms']);
const state = new Set(['variables_get', 'variables_set', 'math_change', 'effect_wait_ms', 'controls_repeat_ext', 'controls_whileUntil']);
const gsiFields = new Set([...NUMERIC_GSI_STATES, ...DISCRETE_GSI_STATES, ...BOOLEAN_GSI_STATES].map(x => x.key));
export const CAPABILITY_LABELS = Object.freeze({ keyboard: '模拟按键', cs2_gsi: 'CS2 状态', foreground_process: '前台状态', time: '模拟时间', keyboard_rgb: '键盘光效' });
export const CAPABILITY_DIAGNOSTICS = Object.freeze({ unsupported_block: '工程包含测试台不支持的积木。',
  foreground_automation_only: '前台条件属于自动化工作室，光效模拟器不读取该条件。', legacy_event_pulse: '旧事件布尔脉冲已退役，请使用自动化事件规则。',
  unsupported_gsi_field: '此 GSI 字段不在测试场景允许的状态字典中。', unsupported_declaration: '工程声明了不支持的能力。', invalid_graph: '工程图结构无效或超过能力分析限制。' });

export function deriveEffectCapabilities(project) {
  const graph = project?.json || project?.blockly_json || project;
  const inputs = new Set(), outputs = new Set(), features = new Set(), fields = new Set(), diagnostics = new Set(); let count = 0;
  const field = path => {
    inputs.add('cs2_gsi'); features.add('event_driven');
    if (typeof path !== 'string' || !gsiFields.has(path)) diagnostics.add(typeof path === 'string' && path.startsWith('event.') ? 'legacy_event_pulse' : 'unsupported_gsi_field');
    else fields.add(path);
  };
  const visit = (b, depth = 0) => {
    if (!b || typeof b !== 'object' || Array.isArray(b) || typeof b.type !== 'string' || ++count > 2048 || depth > 48) { diagnostics.add('invalid_graph'); return; }
    const type = b.type;
    if (type === 'orch_current_process') { inputs.add('foreground_process'); diagnostics.add('foreground_automation_only'); }
    else if (!supported.has(type)) diagnostics.add('unsupported_block');
    if (['key_fill_all', 'key_set_color', 'key_ripple_effect'].includes(type)) outputs.add('keyboard_rgb');
    if (['key_is_pressed', 'key_decay'].includes(type)) { inputs.add('keyboard'); features.add('event_driven'); }
    if (time.has(type)) inputs.add('time');
    if (state.has(type) || type === 'key_decay') features.add('stateful');
    if (type === 'gsi_player_health_condition') field('player.state.health');
    if (type === 'gsi_c4_state_condition') field('round.bomb');
    if (type === 'gsi_state_match') field(b.fields?.STATE || 'round.bomb');
    if (type === 'gsi_numeric_compare') field(b.fields?.FIELD || 'player.state.health');
    if (type.startsWith('gsi_get_')) field(b.fields?.PATH || ({ gsi_get_number: 'player.state.health', gsi_get_string: 'round.bomb', gsi_get_boolean: 'player.state.helmet' })[type]);
    // A connected real block hides its fallback shadow in Blockly execution.
    for (const c of Object.values(b.inputs || {})) { const active = c?.block || c?.shadow; if (active) visit(active, depth + 1); }
    if (b.next?.block) visit(b.next.block, depth + 1);
  };
  const roots = Array.isArray(graph?.blocks) ? graph.blocks : graph?.blocks?.blocks;
  if (!Array.isArray(roots)) diagnostics.add('invalid_graph'); else for (const b of roots) visit(b);
  if (graph?.declared_capabilities && (!Array.isArray(graph.declared_capabilities) || graph.declared_capabilities.some(c => !['keyboard', 'cs2_gsi', 'time', 'keyboard_rgb'].includes(c)))) diagnostics.add('unsupported_declaration');
  if (project?.publication?.mode === 'one_shot') { inputs.add('time'); features.add('stateful'); }
  if (!diagnostics.size) features.add('simulation_supported');
  const sort = set => [...set].sort();
  return { schema_version: 1, inputs: sort(inputs), outputs: sort(outputs), features: sort(features), gsi_fields: sort(fields), diagnostics: sort(diagnostics) };
}
export function recommendStudioScenarios(manifest) {
  const names = [];
  if (manifest.inputs.includes('keyboard')) names.push('单键轻按', '按住后释放', '快速重复按键');
  if (manifest.gsi_fields.includes('player.state.health')) names.push('CS2 生命值变化');
  if (manifest.gsi_fields.includes('player.state.round_kills')) names.push('CS2 本回合击杀状态');
  if (manifest.inputs.includes('foreground_process')) names.push('前台进程切换');
  return names;
}
