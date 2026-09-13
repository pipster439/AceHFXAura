import { EFFECT_PRESETS } from './presets.js';

const n = value => ({ type: 'math_number', fields: { NUM: value } });
const input = block => ({ block });
const rgb = (r, g, b) => ({ type: 'color_rgb', inputs: { R: input(n(r)), G: input(n(g)), B: input(n(b)) } });
const arithmetic = (op, a, b) => ({ type: 'math_arithmetic', fields: { OP: op }, inputs: { A: input(a), B: input(b) } });
const distance = () => ({ type: 'geometry_distance', inputs: { X1: input(n(3)), Y1: input(n(3)), X2: input({ type: 'geometry_coords', fields: { FIELD: 'physical_x' } }), Y2: input({ type: 'geometry_coords', fields: { FIELD: 'physical_y' } }) } });
const radius = () => arithmetic('DIVIDE', { type: 'time_elapsed_ms' }, n(80));
const ring = { type: 'logic_compare', fields: { OP: 'LT' }, inputs: { A: input({ type: 'math_single', fields: { OP: 'ABS' }, inputs: { NUM: input(arithmetic('MINUS', distance(), radius())) } }), B: input(n(0.8)) } };
const wave = { blocks: { languageVersion: 0, blocks: [{ type: 'key_fill_all', inputs: { COLOR: input(rgb(0,0,0)) }, next: input({ type: 'key_for_each', fields: { ZONE: 'all' }, inputs: { DO: input({ type: 'controls_if', inputs: { IF0: input(ring), DO0: input({ type: 'key_set_color', inputs: { COLOR: input(rgb(255,180,30)) } }) } }) } }) }] } };

// Loading this example edits a draft only. Its effects are staged on Apply.
export function studioExample(fallback = 'desktop') {
  const health = EFFECT_PRESETS.find(p => p.id === 'cs2_health_bar').blocklyJson;
  const pulse = { type: 'orch_action_overlay_pulse', fields: { EVENT: 'event.kill', EFFECT: 'studio_kill_wave', DURATION: 800, FADE: 200, PRIORITY: 20 } };
  const lowHealth = { type: 'controls_if', inputs: { IF0: input({ type: 'gsi_numeric_compare', fields: { FIELD: 'player.state.health', OP: '<', VALUE: '20' } }), DO0: input({ type: 'orch_action_overlay_state', fields: { EFFECT: 'studio_low_health', PRIORITY: 10 } }) } };
  pulse.next = input(lowHealth);
  const base = { type: 'orch_action_switch_profile', fields: { PROFILE: 'studio_health' }, next: input(pulse) };
  const root = { type: 'orch_root_flow', fields: { FALLBACK_PROFILE: fallback }, inputs: { DO: input({ type: 'controls_if', inputs: { IF0: input({ type: 'orch_text_equals', inputs: { A: input({ type: 'orch_current_process' }), B: input({ type: 'text', fields: { TEXT: 'cs2.exe' } }) } }), DO0: input(base) } }) } };
  return {
    blocklyJson: { blocks: { languageVersion: 0, blocks: [root] } },
    effects: { studio_health: { name: 'studio_health', blockly_json: health }, studio_kill_wave: { name: 'studio_kill_wave', blockly_json: wave } },
    profiles: { studio_low_health: { type: 'breathing', color1: [255,0,0], color2: [5,0,0], period_ms: 1000 } }
  };
}
