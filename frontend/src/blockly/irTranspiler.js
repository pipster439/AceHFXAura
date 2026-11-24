import { compileSequencePlan } from './sequenceCompiler.js';
import { effectVariables, validateEffectVariableTypes } from './workspaceSafety.js';
import { normalizePublication } from './publication.js';

const number = value => ({ op: 'number', value: Number(value) });
const boolean = value => ({ op: 'boolean', value });
const text = value => ({ op: 'text', value });
const expr = (op, ...args) => ({ op, args });
const rgb = (r, g, b) => expr('rgb', number(r), number(g), number(b));
const compare = value => ({ '==': 'eq', '!=': 'ne', '<': 'lt', '<=': 'le', '>': 'gt', '>=': 'ge', EQ: 'eq', NEQ: 'ne', LT: 'lt', LTE: 'le', GT: 'gt', GTE: 'ge' })[value] || 'eq';

// This target shares the exact production control-flow plan with JS and C++.
// Its output is data for the native bounded interpreter, never executable text.
export class IrTranspiler {
  static transpile(workspace, options = {}) {
    validateEffectVariableTypes(workspace);
    const variables = effectVariables(workspace);
    if (variables.length > 64) throw new Error('受限运行时最多支持 64 个数值变量');
    const variableIndex = block => {
      const id = block.getField('VAR')?.getVariable?.()?.getId();
      const index = variables.findIndex(variable => variable.getId() === id);
      if (index < 0) throw new Error('变量引用不存在');
      return index;
    };
    const input = (block, name, fallback = number(0)) => {
      const node = block.getInputTargetBlock(name);
      return node ? value(node) : fallback;
    };
    const gsi = (kind, path, fallback) => ({ op: 'gsi_' + kind, path, default: fallback });
    const value = block => {
      const field = (name, fallback) => block.getFieldValue(name) ?? fallback;
      const get = (name, fallback) => input(block, name, fallback);
      switch (block.type) {
        case 'math_number': return number(field('NUM', 0));
        case 'variables_get': return { op: 'var', index: variableIndex(block) };
        case 'text': return text(field('TEXT', ''));
        case 'logic_boolean': return boolean(field('BOOL', 'FALSE') === 'TRUE');
        case 'time_elapsed_ms': return { op: 'elapsed' };
        case 'time_phase': return expr('time_phase', get('PERIOD', number(2000)));
        case 'geometry_coords': return { op: 'coords', field: field('FIELD', 'physical_x') };
        case 'geometry_distance': return expr('distance', ...['X1', 'Y1', 'X2', 'Y2'].map(name => get(name)));
        case 'geometry_radial_phase': return expr('radial_phase', get('CX', number(7.5)), get('CY', number(3)), get('WAVELENGTH', number(4)), get('SPEED', number(2)));
        case 'math_waveform': return { ...expr('waveform', get('PHASE'), get('AMPLITUDE', number(1)), get('OFFSET')),
          wave: ({ SINE: 'sine', TRIANGLE: 'triangle', SQUARE: 'square', SAWTOOTH: 'saw', SAW: 'saw' })[field('TYPE', 'SINE')] || 'saw' };
        case 'color_rgb': return expr('rgb', get('R', number(255)), get('G'), get('B'));
        case 'color_hsv': return expr('hsv', get('H'), get('S', number(100)), get('V', number(100)));
        case 'color_lerp': return expr('lerp', get('COLOR_A', rgb(0, 0, 0)), get('COLOR_B', rgb(255, 255, 255)), get('RATIO', number(.5)));
        case 'color_brightness': return expr('brightness', get('COLOR', rgb(255, 255, 255)), get('SCALE', number(1)));
        case 'color_cycle': return expr('cycle', get('COLOR_A', rgb(0, 180, 255)), get('COLOR_B', rgb(255, 0, 128)), get('PERIOD_SEC', number(2)));
        case 'key_is_pressed': return { op: 'pressed' };
        case 'key_decay': return expr('decay', get('DECAY_RATE', number(.85)));
        case 'logic_negate': return expr('not', get('BOOL', boolean(false)));
        case 'logic_operation': return expr(field('OP', 'AND') === 'OR' ? 'or' : 'and', get('A', boolean(true)), get('B', boolean(true)));
        case 'logic_compare': return expr(compare(field('OP', 'EQ')), get('A'), get('B'));
        case 'math_arithmetic': return expr(({ ADD: 'add', MINUS: 'sub', MULTIPLY: 'mul', DIVIDE: 'div', POWER: 'pow' })[field('OP', 'ADD')], get('A'), get('B'));
        case 'math_single': return expr(({ ROOT: 'sqrt', ABS: 'abs', NEG: 'neg', LN: 'ln', LOG10: 'log10', EXP: 'exp', POW10: 'pow10' })[field('OP', 'ROOT')], get('NUM'));
        case 'math_trig': return expr(String(field('OP', 'SIN')).toLowerCase(), get('NUM'));
        case 'math_round': return expr(({ ROUND: 'round', ROUNDUP: 'ceil', ROUNDDOWN: 'floor' })[field('OP', 'ROUND')], get('NUM'));
        case 'math_modulo': return expr('mod', get('DIVIDEND'), get('DIVISOR', number(1)));
        case 'gsi_get_number': return gsi('number', field('PATH', 'player.state.health'), get('DEFAULT', number(100)));
        case 'gsi_get_string': return gsi('text', field('PATH', 'round.bomb'), get('DEFAULT', text('')));
        case 'gsi_get_boolean': return gsi('bool', field('PATH', 'player.state.helmet'), get('DEFAULT', boolean(false)));
        case 'gsi_enum_constant': return text(field('VALUE', ''));
        case 'gsi_state_match': return expr(compare(field('OP', '==')), gsi('text', field('STATE', 'round.bomb'), text('')), text(field('VALUE', '')));
        case 'gsi_numeric_compare': return expr(compare(field('OP', '<')), gsi('number', field('FIELD', 'player.state.health'), number(0)), number(field('VALUE', 0)));
        case 'gsi_player_health_condition': return expr(compare(field('OP', '<')), gsi('number', 'player.state.health', number(100)), number(field('VALUE', 25)));
        case 'gsi_c4_state_condition': return expr('eq', gsi('text', 'round.bomb', text('')), text(field('STATE', 'planted')));
        case 'sensor_audio_feature': return { op: 'audio_feature', feature: field('FEATURE', 'rms'), default: get('DEFAULT', number(0)) };
        case 'sensor_screen_color': return { op: 'screen_color', default: get('DEFAULT', rgb(0, 0, 0)) };
        default: throw new Error(`受限运行时不支持表达式：${block.type}`);
      }
    };
    const plan = compileSequencePlan(workspace);
    if (plan.nodes.length > 2048 || plan.slots > 64) throw new Error('受限运行时控制流超过容量');
    const nodes = plan.nodes.map(node => {
      const { block } = node;
      const result = { kind: node.kind, ...(block?.id ? { source: block.id } : {}),
        ...(node.keySlot != null ? { keySlot: node.keySlot } : {}) };
      const loopMode = block?.type === 'key_for_each' ? 'keys' : block?.type === 'controls_whileUntil' ? 'while' : 'repeat';
      switch (node.kind) {
        case 'end': return result;
        case 'jump': return { ...result, next: node.next };
        case 'if': return { ...result, condition: input(block, node.input, boolean(false)), yes: node.yes, no: node.no };
        case 'wait': return { ...result, duration: input(block, 'MS'), next: node.next };
        case 'increment': return { ...result, slot: node.slot, next: node.next };
        case 'loopInit': return { ...result, slot: node.slot, next: node.next, mode: loopMode === 'keys' ? 'keys' : 'repeat',
          ...(loopMode === 'keys' ? {} : { count: loopMode === 'while' ? number(0) : input(block, 'TIMES', number(10)) }) };
        case 'loopTest': return { ...result, slot: node.slot, mode: loopMode, body: node.body, after: node.after,
          ...(loopMode === 'keys' ? { zone: block.getFieldValue('ZONE') || 'all' } : {}),
          ...(loopMode === 'while' ? { condition: input(block, 'BOOL', boolean(false)), until: block.getFieldValue('MODE') === 'UNTIL' } : {}) };
        case 'action': {
          const base = { ...result, next: node.next };
          if (block.type === 'key_fill_all') return { ...base, op: 'fill', color: input(block, 'COLOR', rgb(255, 128, 0)) };
          if (block.type === 'key_set_color') return { ...base, op: 'setKey', led: input(block, 'LED_ID', { op: 'coords', field: 'led_id' }), color: input(block, 'COLOR', rgb(255, 128, 0)) };
          if (block.type === 'variables_set' || block.type === 'math_change') return { ...base, op: block.type === 'variables_set' ? 'setVar' : 'changeVar', index: variableIndex(block), value: input(block, block.type === 'variables_set' ? 'VALUE' : 'DELTA', number(block.type === 'variables_set' ? 0 : 1)) };
          if (block.type === 'key_ripple_effect') return { ...base, op: 'ripple', key: block.getFieldValue('KEY') || 'W', speed: number(Number(block.getFieldValue('SPEED')) || 2.5), color: input(block, 'COLOR', rgb(0, 200, 255)) };
          throw new Error(`受限运行时不支持动作：${block.type}`);
        }
        default: throw new Error('未知控制流');
      }
    });
    return { schema_version: 1, entry: plan.entry, variables: variables.length, slots: plan.slots, publication: normalizePublication(options), nodes };
  }
}
