/**
 * Blockly Toolboxes for ROG Falchion Ace HFX
 * 1. Effect Studio Toolbox (6 Domain Categories + Standard Logic/Math/Loops/Variables)
 * 2. Orchestrator Studio Toolbox (4 Domain Categories)
 */

export const EFFECT_STUDIO_TOOLBOX = {
  kind: 'categoryToolbox',
  contents: [
    {
      kind: 'category',
      name: 'Clock & Time',
      categorystyle: 'clock_category',
      contents: [
        { kind: 'block', type: 'time_elapsed_ms' },
        {
          kind: 'block',
          type: 'time_phase',
          inputs: {
            PERIOD: {
              shadow: { type: 'math_number', fields: { NUM: 2000 } }
            }
          }
        },
        {
          kind: 'block',
          type: 'math_waveform',
          inputs: {
            PHASE: { shadow: { type: 'math_number', fields: { NUM: 0 } } },
            AMPLITUDE: { shadow: { type: 'math_number', fields: { NUM: 1 } } },
            OFFSET: { shadow: { type: 'math_number', fields: { NUM: 0 } } }
          }
        }
      ]
    },
    {
      kind: 'category',
      name: 'Geometry & Coords',
      categorystyle: 'geometry_category',
      contents: [
        { kind: 'block', type: 'geometry_coords' },
        {
          kind: 'block',
          type: 'geometry_distance',
          inputs: {
            X1: { shadow: { type: 'math_number', fields: { NUM: 7.5 } } },
            Y1: { shadow: { type: 'math_number', fields: { NUM: 3 } } },
            X2: { shadow: { type: 'math_number', fields: { NUM: 0 } } },
            Y2: { shadow: { type: 'math_number', fields: { NUM: 0 } } }
          }
        },
        {
          kind: 'block',
          type: 'geometry_radial_phase',
          inputs: {
            CX: { shadow: { type: 'math_number', fields: { NUM: 7.5 } } },
            CY: { shadow: { type: 'math_number', fields: { NUM: 3.0 } } },
            WAVELENGTH: { shadow: { type: 'math_number', fields: { NUM: 4.0 } } },
            SPEED: { shadow: { type: 'math_number', fields: { NUM: 2.0 } } }
          }
        }
      ]
    },
    {
      kind: 'category',
      name: 'Color & Gradients',
      categorystyle: 'color_category',
      contents: [
        {
          kind: 'block',
          type: 'color_rgb',
          inputs: {
            R: { shadow: { type: 'math_number', fields: { NUM: 255 } } },
            G: { shadow: { type: 'math_number', fields: { NUM: 128 } } },
            B: { shadow: { type: 'math_number', fields: { NUM: 0 } } }
          }
        },
        {
          kind: 'block',
          type: 'color_hsv',
          inputs: {
            H: { shadow: { type: 'math_number', fields: { NUM: 180 } } },
            S: { shadow: { type: 'math_number', fields: { NUM: 100 } } },
            V: { shadow: { type: 'math_number', fields: { NUM: 100 } } }
          }
        },
        {
          kind: 'block',
          type: 'color_lerp',
          inputs: {
            RATIO: { shadow: { type: 'math_number', fields: { NUM: 0.5 } } }
          }
        },
        {
          kind: 'block',
          type: 'color_brightness',
          inputs: {
            SCALE: { shadow: { type: 'math_number', fields: { NUM: 1.0 } } }
          }
        }
      ]
    },
    {
      kind: 'category',
      name: 'Keyboard Operations',
      categorystyle: 'keyboard_category',
      contents: [
        { kind: 'block', type: 'key_fill_all' },
        { kind: 'block', type: 'key_set_color' },
        { kind: 'block', type: 'key_for_each' }
      ]
    },
    {
      kind: 'category',
      name: 'Key Dynamics',
      categorystyle: 'dynamics_category',
      contents: [
        { kind: 'block', type: 'key_is_pressed' },
        {
          kind: 'block',
          type: 'key_decay',
          inputs: {
            DECAY_RATE: { shadow: { type: 'math_number', fields: { NUM: 0.85 } } }
          }
        }
      ]
    },
    {
      kind: 'category',
      name: 'Atomic GSI Sensors',
      categorystyle: 'gsi_category',
      contents: [
        {
          kind: 'block',
          type: 'gsi_get_number',
          inputs: {
            DEFAULT: { shadow: { type: 'math_number', fields: { NUM: 100 } } }
          }
        },
        {
          kind: 'block',
          type: 'gsi_get_string',
          inputs: {
            DEFAULT: { shadow: { type: 'text', fields: { TEXT: '' } } }
          }
        },
        {
          kind: 'block',
          type: 'gsi_get_boolean',
          inputs: {
            DEFAULT: { shadow: { type: 'logic_boolean', fields: { BOOL: 'FALSE' } } }
          }
        }
      ]
    },
    { kind: 'sep' },
    {
      kind: 'category',
      name: 'Logic & Control',
      colour: '#5C6BC0',
      contents: [
        { kind: 'block', type: 'controls_if' },
        { kind: 'block', type: 'logic_compare' },
        { kind: 'block', type: 'logic_operation' },
        { kind: 'block', type: 'logic_negate' },
        { kind: 'block', type: 'logic_boolean' }
      ]
    },
    {
      kind: 'category',
      name: 'Math',
      colour: '#AB47BC',
      contents: [
        { kind: 'block', type: 'math_number' },
        { kind: 'block', type: 'math_arithmetic' },
        { kind: 'block', type: 'math_single' },
        { kind: 'block', type: 'math_trig' },
        { kind: 'block', type: 'math_round' },
        { kind: 'block', type: 'math_modulo' }
      ]
    },
    {
      kind: 'category',
      name: 'Variables',
      colour: '#E91E63',
      custom: 'VARIABLE'
    }
  ]
};

export const ORCHESTRATOR_STUDIO_TOOLBOX = {
  kind: 'categoryToolbox',
  contents: [
    {
      kind: 'category',
      name: 'Root & Fallback',
      categorystyle: 'root_category',
      contents: [
        { kind: 'block', type: 'orchestrator_root' }
      ]
    },
    {
      kind: 'category',
      name: 'Event Overlays',
      categorystyle: 'overlay_category',
      contents: [
        { kind: 'block', type: 'event_overlay' }
      ]
    },
    {
      kind: 'category',
      name: 'Process Rules',
      categorystyle: 'process_category',
      contents: [
        { kind: 'block', type: 'match_process' }
      ]
    },
    {
      kind: 'category',
      name: 'GSI Conditions',
      categorystyle: 'condition_category',
      contents: [
        { kind: 'block', type: 'condition_and' },
        { kind: 'block', type: 'condition_or' },
        { kind: 'block', type: 'condition_not' },
        { kind: 'block', type: 'condition_compare' }
      ]
    }
  ]
};
