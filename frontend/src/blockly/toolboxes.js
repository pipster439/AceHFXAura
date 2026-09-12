/**
 * Blockly Toolboxes for ROG Falchion Ace HFX
 * 1. Effect Studio Toolbox (5 Scratch-Style Domain Categories: Control, Math & Logic, Variables, Sensing, Color & Keys)
 * 2. Orchestrator Studio Toolbox (Control Flow, Sensing & Conditions, Actions, Legacy)
 */

export const EFFECT_STUDIO_TOOLBOX = {
  kind: 'categoryToolbox',
  contents: [
    {
      kind: 'category',
      name: '🕹️ 控制 (Control)',
      colour: '#FFAB19',
      contents: [
        { kind: 'block', type: 'controls_if' },
        {
          kind: 'block',
          type: 'controls_repeat_ext',
          inputs: {
            TIMES: { shadow: { type: 'math_number', fields: { NUM: 10 } } }
          }
        },
        { kind: 'block', type: 'controls_whileUntil' },
        { kind: 'block', type: 'key_for_each' },
        {
          kind: 'block',
          type: 'effect_wait_ms',
          inputs: {
            MS: { shadow: { type: 'math_number', fields: { NUM: 50 } } }
          }
        },
        { kind: 'block', type: 'controls_flow_statements' }
      ]
    },
    {
      kind: 'category',
      name: '🧮 运算与逻辑 (Math & Logic)',
      colour: '#59C059',
      contents: [
        { kind: 'block', type: 'math_number' },
        {
          kind: 'block',
          type: 'math_arithmetic',
          inputs: {
            A: { shadow: { type: 'math_number', fields: { NUM: 1 } } },
            B: { shadow: { type: 'math_number', fields: { NUM: 1 } } }
          }
        },
        { kind: 'block', type: 'math_single' },
        { kind: 'block', type: 'math_trig' },
        { kind: 'block', type: 'math_round' },
        { kind: 'block', type: 'math_modulo' },
        {
          kind: 'block',
          type: 'math_waveform',
          inputs: {
            PHASE: { shadow: { type: 'math_number', fields: { NUM: 0 } } },
            AMPLITUDE: { shadow: { type: 'math_number', fields: { NUM: 1 } } },
            OFFSET: { shadow: { type: 'math_number', fields: { NUM: 0 } } }
          }
        },
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
        },
        {
          kind: 'block',
          type: 'logic_compare',
          inputs: {
            A: { shadow: { type: 'math_number', fields: { NUM: 10 } } },
            B: { shadow: { type: 'math_number', fields: { NUM: 20 } } }
          }
        },
        { kind: 'block', type: 'logic_operation' },
        { kind: 'block', type: 'logic_negate' },
        { kind: 'block', type: 'logic_boolean' }
      ]
    },
    {
      kind: 'category',
      name: '📦 变量 (Variables)',
      colour: '#FF8C1A',
      custom: 'VARIABLE'
    },
    {
      kind: 'category',
      name: '👁️ 侦测与传感 (Sensing)',
      colour: '#4CBFE6',
      contents: [
        { kind: 'block', type: 'gsi_state_match' },
        { kind: 'block', type: 'gsi_numeric_compare' },
        { kind: 'block', type: 'gsi_enum_constant' },
        { kind: 'block', type: 'geometry_coords' },
        { kind: 'block', type: 'time_elapsed_ms' },
        {
          kind: 'block',
          type: 'time_phase',
          inputs: {
            PERIOD: { shadow: { type: 'math_number', fields: { NUM: 2000 } } }
          }
        },
        { kind: 'block', type: 'key_is_pressed' },
        {
          kind: 'block',
          type: 'key_decay',
          inputs: {
            DECAY_RATE: { shadow: { type: 'math_number', fields: { NUM: 0.85 } } }
          }
        },
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
    {
      kind: 'category',
      name: '🎨 色彩与按键 (Color & Keys)',
      colour: '#9966FF',
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
        },
        {
          kind: 'block',
          type: 'key_set_color',
          inputs: {
            LED_ID: { shadow: { type: 'math_number', fields: { NUM: 0 } } }
          }
        },
        {
          kind: 'block',
          type: 'key_fill_all'
        }
      ]
    }
  ]
};

export const ORCHESTRATOR_STUDIO_TOOLBOX = {
  kind: 'categoryToolbox',
  contents: [
    {
      kind: 'category',
      name: '🕹️ 控制流 (Control Flow)',
      colour: '#FFAB19',
      contents: [
        { kind: 'block', type: 'orch_root_flow' },
        { kind: 'block', type: 'controls_if' },
        { kind: 'block', type: 'orch_action_wait_ms' }
      ]
    },
    {
      kind: 'category',
      name: '🔍 侦测与条件 (Sensing & Conditions)',
      colour: '#4CBFE6',
      contents: [
        { kind: 'block', type: 'gsi_state_match' },
        { kind: 'block', type: 'gsi_numeric_compare' },
        { kind: 'block', type: 'gsi_enum_constant' },
        { kind: 'block', type: 'orch_current_process' },
        {
          kind: 'block',
          type: 'orch_gsi_num',
          inputs: {
            DEFAULT: { shadow: { type: 'math_number', fields: { NUM: 100 } } }
          }
        },
        {
          kind: 'block',
          type: 'orch_gsi_str',
          inputs: {
            DEFAULT: { shadow: { type: 'text', fields: { TEXT: '' } } }
          }
        },
        {
          kind: 'block',
          type: 'orch_gsi_bool',
          inputs: {
            DEFAULT: { shadow: { type: 'logic_boolean', fields: { BOOL: 'FALSE' } } }
          }
        },
        { kind: 'block', type: 'orch_event_triggered' },
        {
          kind: 'block',
          type: 'orch_text_equals',
          inputs: {
            A: { shadow: { type: 'text', fields: { TEXT: 'cs2.exe' } } },
            B: { shadow: { type: 'text', fields: { TEXT: 'cs2.exe' } } }
          }
        },
        {
          kind: 'block',
          type: 'logic_compare',
          inputs: {
            A: { shadow: { type: 'math_number', fields: { NUM: 100 } } },
            B: { shadow: { type: 'math_number', fields: { NUM: 30 } } }
          }
        },
        { kind: 'block', type: 'logic_operation' },
        { kind: 'block', type: 'logic_negate' },
        { kind: 'block', type: 'logic_boolean' },
        { kind: 'block', type: 'math_number' },
        { kind: 'block', type: 'text' }
      ]
    },
    {
      kind: 'category',
      name: '⚡ 执行动作 (Actions)',
      colour: '#9966FF',
      contents: [
        { kind: 'block', type: 'orch_action_switch_profile' },
        { kind: 'block', type: 'orch_action_overlay_pulse' },
        { kind: 'block', type: 'orch_action_set_dnd' }
      ]
    },
    { kind: 'sep' },
    {
      kind: 'category',
      name: '🏛️ 经典模块 (Legacy)',
      colour: '#705D00',
      contents: [
        { kind: 'block', type: 'orchestrator_root' },
        { kind: 'block', type: 'event_overlay' },
        { kind: 'block', type: 'match_process' },
        { kind: 'block', type: 'condition_compare' },
        { kind: 'block', type: 'condition_and' },
        { kind: 'block', type: 'condition_or' },
        { kind: 'block', type: 'condition_not' }
      ]
    }
  ]
};
