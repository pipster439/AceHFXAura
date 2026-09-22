import Blockly from './index.js';
import {
  DISCRETE_STATE_DROPDOWN_OPTIONS,
  getStateOptions,
  NUMERIC_STATE_DROPDOWN_OPTIONS,
  BOOLEAN_STATE_DROPDOWN_OPTIONS,
  EVENT_DROPDOWN_OPTIONS,
  getDefaultThreshold,
  ALL_GSI_ENUM_VALUES
} from '../constants/gsiDictionary.js';

let blocksRegistered = false;

/**
 * Register all domain-specific blocks for:
 * 1. Lighting Effect Studio (Clock, Geometry, Color, Keyboard, Dynamics, Atomic GSI)
 * 2. Profile & GSI Event Orchestrator (Root, Event Overlays, Process Rules, Condition Trees)
 */
export function registerCustomBlocks() {
  if (blocksRegistered) return;
  blocksRegistered = true;

  // =========================================================================
  // 1. CLOCK & TIME BLOCKS
  // =========================================================================
  Blockly.common.defineBlocksWithJsonArray([
    {
      type: 'time_elapsed_ms',
      message0: '运行毫秒 elapsed_ms',
      output: 'Number',
      style: 'clock_blocks',
      tooltip: '自光效启动以来流逝的绝对毫秒数 (uint64_t)',
      helpUrl: ''
    },
    {
      type: 'time_phase',
      message0: '周期相位 周期: %1 ms',
      args0: [
        {
          type: 'input_value',
          name: 'PERIOD',
          check: 'Number'
        }
      ],
      output: 'Number',
      style: 'clock_blocks',
      tooltip: '基于当前运行时间与周期计算归一化相位 (0.0 ~ 1.0)',
      helpUrl: ''
    },
    {
      type: 'math_waveform',
      message0: '振荡波形 %1 相位: %2 幅度: %3 偏置: %4',
      args0: [
        {
          type: 'field_dropdown',
          name: 'TYPE',
          options: [
            ['正弦波 (Sine)', 'SINE'],
            ['三角波 (Triangle)', 'TRIANGLE'],
            ['方波 (Square)', 'SQUARE'],
            ['锯齿波 (Sawtooth)', 'SAW']
          ]
        },
        { type: 'input_value', name: 'PHASE', check: 'Number' },
        { type: 'input_value', name: 'AMPLITUDE', check: 'Number' },
        { type: 'input_value', name: 'OFFSET', check: 'Number' }
      ],
      output: 'Number',
      style: 'clock_blocks',
      tooltip: '根据相位生成周期波形振荡输出',
      helpUrl: ''
    },

    // =========================================================================
    // 2. GEOMETRY & COORDINATE BLOCKS
    // =========================================================================
    {
      type: 'geometry_coords',
      message0: '当前按键 %1',
      args0: [
        {
          type: 'field_dropdown',
          name: 'FIELD',
          options: [
            ['物理 X (0~15.5)', 'physical_x'],
            ['物理 Y (1.0~5.0)', 'physical_y'],
            ['矩阵 行 (row 1~5)', 'row'],
            ['矩阵 列 (col 1~15)', 'col'],
            ['LED 索引 (led_id 0~67)', 'led_id']
          ]
        }
      ],
      output: 'Number',
      style: 'geometry_blocks',
      tooltip: '获取当前正在计算的按键几何拓扑与矩阵硬件属性',
      helpUrl: ''
    },
    {
      type: 'geometry_distance',
      message0: '欧氏距离 从 (%1, %2) 到 (%3, %4)',
      args0: [
        { type: 'input_value', name: 'X1', check: 'Number' },
        { type: 'input_value', name: 'Y1', check: 'Number' },
        { type: 'input_value', name: 'X2', check: 'Number' },
        { type: 'input_value', name: 'Y2', check: 'Number' }
      ],
      output: 'Number',
      style: 'geometry_blocks',
      tooltip: '计算两点间的二维欧氏距离 sqrt(dx*dx + dy*dy)',
      helpUrl: ''
    },
    {
      type: 'geometry_radial_phase',
      message0: '径向波纹相位 中心: (%1, %2) 波长: %3 速度: %4',
      args0: [
        { type: 'input_value', name: 'CX', check: 'Number' },
        { type: 'input_value', name: 'CY', check: 'Number' },
        { type: 'input_value', name: 'WAVELENGTH', check: 'Number' },
        { type: 'input_value', name: 'SPEED', check: 'Number' }
      ],
      output: 'Number',
      style: 'geometry_blocks',
      tooltip: '根据距指定中心的距离与当前时间计算径向涟漪相位',
      helpUrl: ''
    },

    // =========================================================================
    // 3. COLOR & GRADIENT BLOCKS
    // =========================================================================
    {
      type: 'color_rgb',
      message0: 'RGB 色彩 R: %1 G: %2 B: %3',
      args0: [
        { type: 'input_value', name: 'R', check: 'Number' },
        { type: 'input_value', name: 'G', check: 'Number' },
        { type: 'input_value', name: 'B', check: 'Number' }
      ],
      output: 'Color',
      style: 'color_blocks',
      tooltip: '由红绿蓝三原色分量 (0~255) 构造颜色',
      helpUrl: ''
    },
    {
      type: 'color_hsv',
      message0: 'HSV 色彩 色相(H): %1 饱和度(S): %2 明度(V): %3',
      args0: [
        { type: 'input_value', name: 'H', check: 'Number' },
        { type: 'input_value', name: 'S', check: 'Number' },
        { type: 'input_value', name: 'V', check: 'Number' }
      ],
      output: 'Color',
      style: 'color_blocks',
      tooltip: '由色相 (0~360)、饱和度 (0~100) 和明度 (0~100) 构造色彩',
      helpUrl: ''
    },
    {
      type: 'color_lerp',
      message0: '色彩线性插值 %1 到 %2 比例: %3',
      args0: [
        { type: 'input_value', name: 'COLOR_A', check: 'Color' },
        { type: 'input_value', name: 'COLOR_B', check: 'Color' },
        { type: 'input_value', name: 'RATIO', check: 'Number' }
      ],
      output: 'Color',
      style: 'color_blocks',
      tooltip: '在两色之间根据比例 (0.0 ~ 1.0) 进行平滑插值',
      helpUrl: ''
    },
    {
      type: 'color_brightness',
      message0: '亮度缩放 色彩: %1 倍率: %2',
      args0: [
        { type: 'input_value', name: 'COLOR', check: 'Color' },
        { type: 'input_value', name: 'SCALE', check: 'Number' }
      ],
      output: 'Color',
      style: 'color_blocks',
      tooltip: '将指定色彩的各通道乘以亮度系数 (0.0 ~ 1.0)',
      helpUrl: ''
    },
    {
      type: 'color_cycle',
      message0: '在 %1 和 %2 之间循环，周期 %3 秒',
      args0: [
        { type: 'input_value', name: 'COLOR_A', check: 'Color' },
        { type: 'input_value', name: 'COLOR_B', check: 'Color' },
        { type: 'input_value', name: 'PERIOD_SEC', check: 'Number' }
      ],
      output: 'Color',
      style: 'color_blocks',
      tooltip: '在两色之间往复呼吸/渐变循环，周期以秒为单位',
      helpUrl: ''
    },

    // =========================================================================
    // 4. KEYBOARD OPERATIONS BLOCKS
    // =========================================================================
    {
      type: 'key_fill_all',
      message0: '将全部按键设为 %1',
      args0: [
        { type: 'input_value', name: 'COLOR', check: 'Color' }
      ],
      previousStatement: null,
      nextStatement: null,
      style: 'keyboard_blocks',
      tooltip: '将键盘全部 68 颗按键快速填充为指定颜色',
      helpUrl: ''
    },
    {
      type: 'key_ripple_effect',
      message0: '从按键 %1 向外扩散色彩 %2 速度 %3',
      args0: [
        {
          type: 'field_dropdown',
          name: 'KEY',
          options: [
            ['W 键', 'W'],
            ['A 键', 'A'],
            ['S 键', 'S'],
            ['D 键', 'D'],
            ['空格 (SPACE)', 'SPACE'],
            ['ESC 键', 'ESC'],
            ['回车 (ENTER)', 'ENTER'],
            ['Q 键', 'Q'],
            ['E 键', 'E'],
            ['R 键', 'R'],
            ['F 键', 'F'],
            ['G 键', 'G'],
            ['中心 (H 键)', 'H']
          ]
        },
        { type: 'input_value', name: 'COLOR', check: 'Color' },
        {
          type: 'field_dropdown',
          name: 'SPEED',
          options: [
            ['慢速', '1.0'],
            ['中速', '2.5'],
            ['快速', '5.0'],
            ['极速', '8.0']
          ]
        }
      ],
      previousStatement: null,
      nextStatement: null,
      style: 'keyboard_blocks',
      tooltip: '以指定按键为中心，向外扩散周期性动态波纹光效',
      helpUrl: ''
    },
    {
      type: 'key_set_color',
      message0: '设置按键 LED %1 色彩 %2',
      args0: [
        { type: 'input_value', name: 'LED_ID', check: 'Number' },
        { type: 'input_value', name: 'COLOR', check: 'Color' }
      ],
      previousStatement: null,
      nextStatement: null,
      style: 'keyboard_blocks',
      tooltip: '为指定 LED 硬件索引 (0~67) 赋予颜色',
      helpUrl: ''
    },
    {
      type: 'key_for_each',
      message0: '遍历按键 分区: %1 执行: %2',
      args0: [
        {
          type: 'field_dropdown',
          name: 'ZONE',
          options: [
            ['全键盘 (All 68 Keys)', 'all'],
            ['WASD 方向区', 'wasd'],
            ['方向导航键 (Arrows)', 'arrows'],
            ['数字行 (Numeric 1-0)', 'numeric'],
            ['主字母区 (Alpha A-Z)', 'alpha'],
            ['功能修饰键 (Modifiers)', 'modifier'],
            ['右侧导航列 (Navigation)', 'navigation']
          ]
        },
        {
          type: 'input_statement',
          name: 'DO'
        }
      ],
      previousStatement: null,
      nextStatement: null,
      style: 'keyboard_blocks',
      tooltip: '逐键遍历指定键位集合，并在循环体内通过当前键位属性赋色',
      helpUrl: ''
    },

    // =========================================================================
    // 5. KEY DYNAMICS BLOCKS
    // =========================================================================
    {
      type: 'key_is_pressed',
      message0: '按键处于按下状态',
      output: 'Boolean',
      style: 'dynamics_blocks',
      tooltip: '判断当前处理的物理按键当前是否被用户按下',
      helpUrl: ''
    },
    {
      type: 'key_decay',
      message0: '按键衰减寄存器 衰减系数: %1',
      args0: [
        { type: 'input_value', name: 'DECAY_RATE', check: 'Number' }
      ],
      output: 'Number',
      style: 'dynamics_blocks',
      tooltip: '获取当前键位的指数衰减能量 (每次松开后按系数递减，0.0 ~ 1.0)',
      helpUrl: ''
    },

    // =========================================================================
    // 6. ATOMIC GSI SENSOR BLOCKS
    // =========================================================================
    {
      type: 'gsi_get_number',
      message0: '获取 GSI 数值 %1 缺省: %2',
      args0: [
        {
          type: 'field_dropdown',
          name: 'PATH',
          options: NUMERIC_STATE_DROPDOWN_OPTIONS
        },
        { type: 'input_value', name: 'DEFAULT', check: 'Number' }
      ],
      output: 'Number',
      style: 'gsi_blocks',
      tooltip: '从 CS2 Game State Integration 中提取原子级数值，未连接时返回缺省值',
      helpUrl: ''
    },
    {
      type: 'gsi_get_string',
      message0: '获取 GSI 文本 %1 缺省: %2',
      args0: [
        {
          type: 'field_dropdown',
          name: 'PATH',
          options: DISCRETE_STATE_DROPDOWN_OPTIONS
        },
        { type: 'input_value', name: 'DEFAULT', check: 'String' }
      ],
      output: 'String',
      style: 'gsi_blocks',
      tooltip: '从 CS2 GSI 遥测中提取原子级字符串',
      helpUrl: ''
    },
    {
      type: 'gsi_get_boolean',
      message0: '获取 GSI 布尔值 %1 缺省: %2',
      args0: [
        {
          type: 'field_dropdown',
          name: 'PATH',
          options: BOOLEAN_STATE_DROPDOWN_OPTIONS
        },
        { type: 'input_value', name: 'DEFAULT', check: 'Boolean' }
      ],
      output: 'Boolean',
      style: 'gsi_blocks',
      tooltip: '从 CS2 GSI 提取布尔状态或瞬时事件脉冲',
      helpUrl: ''
    },
    {
      type: 'gsi_player_health_condition',
      message0: '玩家血量 %1 %2',
      args0: [
        {
          type: 'field_dropdown',
          name: 'OP',
          options: [
            ['低于 (<)', '<'],
            ['低于等于 (<=)', '<='],
            ['高于 (>)', '>'],
            ['高于等于 (>=)', '>='],
            ['等于 (==)', '==']
          ]
        },
        {
          type: 'field_number',
          name: 'VALUE',
          value: 25,
          min: 0,
          max: 100
        }
      ],
      output: ['Boolean', 'Condition'],
      style: 'condition_blocks',
      tooltip: '判定 CS2 玩家血量是否满足阈值条件 (底层映射 player.state.health)',
      helpUrl: ''
    },
    {
      type: 'gsi_c4_state_condition',
      message0: 'C4 状态是 %1',
      args0: [
        {
          type: 'field_dropdown',
          name: 'STATE',
          options: [
            ['已安放 (planted)', 'planted'],
            ['随身携带 (carried)', 'carried'],
            ['掉落 (dropped)', 'dropped'],
            ['已拆除 (defused)', 'defused'],
            ['已引爆 (exploded)', 'exploded']
          ]
        }
      ],
      output: ['Boolean', 'Condition'],
      style: 'condition_blocks',
      tooltip: '判定 CS2 炸弹当前状态 (底层映射 round.bomb)',
      helpUrl: ''
    },

    // =========================================================================
    // 7. ORCHESTRATOR STUDIO BLOCKS
    // =========================================================================
    {
      type: 'orchestrator_root',
      message0: '⚡ ROG 方案与事件编排总线',
      message1: '兜底基础方案: %1',
      message2: '瞬态事件脉冲覆盖 (Event Overlays): %1',
      message3: '进程与状态联动规则 (Process Rules): %1',
      args1: [
        {
          type: 'field_input',
          name: 'FALLBACK_PROFILE',
          text: 'desktop'
        }
      ],
      args2: [
        {
          type: 'input_statement',
          name: 'OVERLAYS'
        }
      ],
      args3: [
        {
          type: 'input_statement',
          name: 'RULES'
        }
      ],
      style: 'root_blocks',
      tooltip: '方案编排总线根积木：定义默认基础方案、瞬态事件拦截栈与进程状态规则树',
      helpUrl: ''
    },
    {
      type: 'event_overlay',
      message0: '瞬态事件 %1 覆盖光效 %2 持续: %3 ms 淡出: %4 ms 优先级: %5',
      args0: [
        {
          type: 'field_dropdown',
          name: 'EVENT',
          options: EVENT_DROPDOWN_OPTIONS
        },
        {
          type: 'field_input',
          name: 'EFFECT',
          text: 'kill_pulse'
        },
        {
          type: 'field_number',
          name: 'DURATION',
          value: 1200,
          min: 100,
          max: 60000
        },
        {
          type: 'field_number',
          name: 'FADE',
          value: 400,
          min: 0,
          max: 10000
        },
        {
          type: 'field_number',
          name: 'PRIORITY',
          value: 10,
          min: 1,
          max: 100
        }
      ],
      previousStatement: null,
      nextStatement: null,
      style: 'overlay_blocks',
      tooltip: '瞬时事件脉冲覆盖：触发时非阻塞叠加播放该光效指定时长，并在线性淡出后平滑回退',
      helpUrl: ''
    },
    {
      type: 'match_process',
      message0: '匹配前台进程 %1 激活方案: %2 游戏免打扰: %3',
      message1: '前置条件: %1',
      args0: [
        {
          type: 'field_input',
          name: 'PROCESS',
          text: 'cs2.exe'
        },
        {
          type: 'field_input',
          name: 'TARGET_PROFILE',
          text: 'cs2_gamer'
        },
        {
          type: 'field_checkbox',
          name: 'DND',
          checked: true
        }
      ],
      args1: [
        {
          type: 'input_value',
          name: 'CONDITION',
          check: ['Condition', 'Boolean']
        }
      ],
      previousStatement: null,
      nextStatement: null,
      style: 'process_blocks',
      tooltip: '当该进程处于前台活动窗口且满足条件树时切换至目标方案，并可静默挂起网页端服务',
      helpUrl: ''
    },
    {
      type: 'condition_and',
      message0: '条件与 (AND) 条件A: %1 条件B: %2',
      args0: [
        { type: 'input_value', name: 'COND0', check: ['Condition', 'Boolean'] },
        { type: 'input_value', name: 'COND1', check: ['Condition', 'Boolean'] }
      ],
      output: ['Condition', 'Boolean'],
      style: 'condition_blocks',
      tooltip: '逻辑与：两个子条件同时为真时判定为命中',
      helpUrl: ''
    },
    {
      type: 'condition_or',
      message0: '条件或 (OR) 条件A: %1 条件B: %2',
      args0: [
        { type: 'input_value', name: 'COND0', check: ['Condition', 'Boolean'] },
        { type: 'input_value', name: 'COND1', check: ['Condition', 'Boolean'] }
      ],
      output: ['Condition', 'Boolean'],
      style: 'condition_blocks',
      tooltip: '逻辑或：任一子条件为真时即判定为命中',
      helpUrl: ''
    },
    {
      type: 'condition_not',
      message0: '条件非 (NOT) %1',
      args0: [
        { type: 'input_value', name: 'COND', check: ['Condition', 'Boolean'] }
      ],
      output: ['Condition', 'Boolean'],
      style: 'condition_blocks',
      tooltip: '逻辑非：反转子条件判断结果',
      helpUrl: ''
    },
    {
      type: 'condition_compare',
      message0: '高级遥测比对（通用） %1 %2 %3',
      args0: [
        {
          type: 'field_dropdown',
          name: 'FIELD',
          options: [
            ['玩家血量 (player.state.health)', 'player.state.health'],
            ['玩家护甲 (player.state.armor)', 'player.state.armor'],
            ['C4状态 (round.bomb)', 'round.bomb'],
            ['回合阶段 (round.phase)', 'round.phase'],
            ['所属阵营 (player.team)', 'player.team'],
            ['地图名称 (map.name)', 'map.name'],
            ['本局击杀 (player.state.round_kills)', 'player.state.round_kills']
          ]
        },
        {
          type: 'field_dropdown',
          name: 'OP',
          options: [
            ['等于 (==)', '=='],
            ['不等于 (!=)', '!='],
            ['大于 (>)', '>'],
            ['大于等于 (>=)', '>='],
            ['小于 (<)', '<'],
            ['小于等于 (<=)', '<='],
            ['包含 (contains)', 'contains']
          ]
        },
        {
          type: 'field_input',
          name: 'VALUE',
          text: '50'
        }
      ],
      output: ['Condition', 'Boolean'],
      style: 'condition_blocks',
      tooltip: '高级通用条件比对：通用比对 GSI 遥测字段或前台进程名（推荐优先使用直观的玩家血量、C4 状态等专用条件积木）',
      helpUrl: ''
    },

    // =========================================================================
    // 8. SCRATCH-STYLE ATOMIC EFFECT & ORCHESTRATION BLOCKS
    // =========================================================================
    {
      type: 'effect_wait_ms',
      message0: '等待 %1 毫秒',
      args0: [
        { type: 'input_value', name: 'MS', check: 'Number' }
      ],
      previousStatement: null,
      nextStatement: null,
      style: 'clock_blocks',
      tooltip: '非阻塞时序延迟：在动画循环中提供时间步进控制',
      helpUrl: ''
    },
    {
      type: 'orch_root_flow',
      message0: '⚡ ROG 方案编排控制流',
      message1: '默认基础方案: %1',
      message2: '规则堆栈:\n%1',
      args1: [
        {
          type: 'field_input',
          name: 'FALLBACK_PROFILE',
          text: 'default'
        }
      ],
      args2: [
        {
          type: 'input_statement',
          name: 'DO'
        }
      ],
      style: 'root_blocks',
      tooltip: 'Scratch 风格方案编排主入口：支持多层如果...那么...否则条件树与动作执行',
      helpUrl: ''
    },
    {
      type: 'orch_action_switch_profile',
      message0: '切换方案为: %1',
      args0: [
        {
          type: 'field_input',
          name: 'PROFILE',
          text: 'cs2_gamer'
        }
      ],
      previousStatement: null,
      nextStatement: null,
      style: 'process_blocks',
      tooltip: '将当前活动方案切换为指定内建方案或工坊自定义光效',
      helpUrl: ''
    },
    {
      type: 'orch_action_overlay_pulse',
      message0: '当事件 %5 发生时，播放 %1 持续 %2 ms 淡出 %3 ms 优先级 %4 混合 %6',
      args0: [
        {
          type: 'field_input',
          name: 'EFFECT',
          text: 'rainbow_wave'
        },
        {
          type: 'field_number',
          name: 'DURATION',
          value: 1200,
          min: 50,
          max: 60000
        },
        {
          type: 'field_number',
          name: 'FADE',
          value: 400,
          min: 0,
          max: 10000
        },
        {
          type: 'field_number',
          name: 'PRIORITY',
          value: 20,
          min: 1,
          max: 100
        }
        ,{ type: 'field_input', name: 'EVENT', text: 'event.kill' },
        { type: 'field_dropdown', name: 'BLEND', options: [['混合', 'blend'], ['叠加亮度', 'add'], ['覆盖', 'replace']] }
      ],
      previousStatement: null,
      nextStatement: null,
      style: 'overlay_blocks',
      tooltip: '非阻塞触发瞬态事件光效覆盖，超时后平滑线性淡出',
      helpUrl: ''
    },
    {
      type: 'orch_action_overlay_state',
      message0: '条件成立期间叠加 %1 优先级 %2 混合 %3',
      args0: [
        { type: 'field_input', name: 'EFFECT', text: 'danger_red' },
        { type: 'field_number', name: 'PRIORITY', value: 10, min: 1, max: 100 },
        { type: 'field_dropdown', name: 'BLEND', options: [['混合', 'blend'], ['叠加亮度', 'add'], ['覆盖', 'replace']] }
      ],
      previousStatement: null, nextStatement: null, style: 'overlay_blocks',
      tooltip: '放在如果积木中；条件变为假时立即结束这一层。数值越大越靠上。'
    },
    {
      type: 'orch_action_set_dnd',
      message0: '设置游戏免打扰 %1',
      args0: [
        {
          type: 'field_dropdown',
          name: 'DND',
          options: [
            ['开启 (抑制网页服务)', 'TRUE'],
            ['关闭', 'FALSE']
          ]
        }
      ],
      previousStatement: null,
      nextStatement: null,
      style: 'process_blocks',
      tooltip: '在运行全屏游戏时挂起网页后台服务，节约 CPU 并消除干扰',
      helpUrl: ''
    },
    {
      type: 'orch_action_wait_ms',
      message0: '等待 %1 毫秒',
      args0: [
        {
          type: 'field_number',
          name: 'MS',
          value: 1000,
          min: 10,
          max: 60000
        }
      ],
      previousStatement: null,
      nextStatement: null,
      style: 'clock_blocks',
      tooltip: '非阻塞时序延迟：由守护进程定时状态机驱动，绝不阻塞硬件推流',
      helpUrl: ''
    },
    {
      type: 'orch_current_process',
      message0: '当前前台窗口进程名',
      output: 'String',
      style: 'process_blocks',
      tooltip: '获取当前处于系统最前台焦点的窗口进程名（例如 "cs2.exe", "code.exe"）',
      helpUrl: ''
    },
    {
      type: 'orch_event_triggered',
      message0: '发生突发事件 %1 ?',
      args0: [
        {
          type: 'field_dropdown',
          name: 'EVENT',
          options: EVENT_DROPDOWN_OPTIONS
        }
      ],
      output: 'Boolean',
      style: 'overlay_blocks',
      tooltip: '检测指定 CS2 游戏突发事件当前是否被激活触发',
      helpUrl: ''
    },
    {
      type: 'orch_gsi_num',
      message0: '读取 GSI 数值 %1 缺省: %2',
      args0: [
        {
          type: 'field_dropdown',
          name: 'PATH',
          options: NUMERIC_STATE_DROPDOWN_OPTIONS
        },
        { type: 'input_value', name: 'DEFAULT', check: 'Number' }
      ],
      output: 'Number',
      style: 'condition_blocks',
      tooltip: '从 CS2 游戏遥测中提取原子数值',
      helpUrl: ''
    },
    {
      type: 'orch_gsi_str',
      message0: '读取 GSI 文本 %1 缺省: %2',
      args0: [
        {
          type: 'field_dropdown',
          name: 'PATH',
          options: DISCRETE_STATE_DROPDOWN_OPTIONS
        },
        { type: 'input_value', name: 'DEFAULT', check: 'String' }
      ],
      output: 'String',
      style: 'condition_blocks',
      tooltip: '从 CS2 游戏遥测中提取原子字符串',
      helpUrl: ''
    },
    {
      type: 'orch_gsi_bool',
      message0: '读取 GSI 布尔值 %1 缺省: %2',
      args0: [
        {
          type: 'field_dropdown',
          name: 'PATH',
          options: BOOLEAN_STATE_DROPDOWN_OPTIONS
        },
        { type: 'input_value', name: 'DEFAULT', check: 'Boolean' }
      ],
      output: 'Boolean',
      style: 'condition_blocks',
      tooltip: '从 CS2 游戏遥测中提取原子布尔值或瞬时事件脉冲',
      helpUrl: ''
    },
    {
      type: 'orch_text_equals',
      message0: '文本 %1 等于 %2',
      args0: [
        { type: 'input_value', name: 'A', check: 'String' },
        { type: 'input_value', name: 'B', check: 'String' }
      ],
      output: 'Boolean',
      style: 'condition_blocks',
      tooltip: '判断两个文本是否一致',
      helpUrl: ''
    }
  ]);

  // =========================================================================
  // 9. DYNAMIC ENCAPSULATED CS2 GSI BLOCKS (ZERO-GUESSWORK DROPDOWNS)
  // =========================================================================

  // 1. GSI 状态智能匹配积木 (联动下拉选择状态与取值)
  Blockly.Blocks['gsi_state_match'] = {
    init: function() {
      const stateField = new Blockly.FieldDropdown(
        DISCRETE_STATE_DROPDOWN_OPTIONS,
        function(newState) {
          const block = this.getSourceBlock ? this.getSourceBlock() : null;
          if (block) {
            const valueField = block.getField('VALUE');
            if (valueField) {
              const opts = getStateOptions(newState);
              const validVals = opts.map((o) => o[1]);
              const currentVal = valueField.getValue();
              if (!validVals.includes(currentVal) && opts.length > 0) {
                valueField.setValue(opts[0][1]);
              }
            }
          }
          return newState;
        }
      );

      const valueDropdown = new Blockly.FieldDropdown(function() {
        const block = this?.getSourceBlock ? this.getSourceBlock() : null;
        const currentState = block ? block.getFieldValue('STATE') : DISCRETE_STATE_DROPDOWN_OPTIONS[0][1];
        return getStateOptions(currentState || DISCRETE_STATE_DROPDOWN_OPTIONS[0][1]);
      });

      this.appendDummyInput()
        .appendField('GSI 状态')
        .appendField(stateField, 'STATE')
        .appendField(new Blockly.FieldDropdown([
          ['等于 (==)', '=='],
          ['不等于 (!=)', '!=']
        ]), 'OP')
        .appendField(valueDropdown, 'VALUE');

      this.setOutput(true, ['Boolean', 'Condition']);
      this.setStyle('condition_blocks');
      this.setTooltip('针对 CS2 官方 GSI 离散状态进行精准匹配，根据所选状态动态筛选合法内容，彻底杜绝输入拼写错误');
    },
    saveExtraState: function() {
      return {
        state: this.getFieldValue('STATE'),
        value: this.getFieldValue('VALUE')
      };
    },
    loadExtraState: function(extraState) {
      if (extraState?.state) {
        this.setFieldValue(extraState.state, 'STATE');
      }
      if (extraState?.value) {
        this.setFieldValue(extraState.value, 'VALUE');
      }
    }
  };

  // 2. GSI 数值智能比对积木 (自适应经典阈值建议)
  Blockly.Blocks['gsi_numeric_compare'] = {
    init: function() {
      const fieldDropdown = new Blockly.FieldDropdown(
        NUMERIC_STATE_DROPDOWN_OPTIONS,
        function(newField) {
          const block = this?.getSourceBlock ? this.getSourceBlock() : null;
          if (block) {
            const defaultMeta = getDefaultThreshold(newField);
            const opField = block.getField('OP');
            const valField = block.getField('VALUE');
            if (opField && defaultMeta.op) {
              opField.setValue(defaultMeta.op);
            }
            if (valField && defaultMeta.value !== undefined) {
              valField.setValue(defaultMeta.value);
            }
          }
          return newField;
        }
      );

      this.appendDummyInput()
        .appendField('GSI 数值')
        .appendField(fieldDropdown, 'FIELD')
        .appendField(new Blockly.FieldDropdown([
          ['小于 (<)', '<'],
          ['小于等于 (<=)', '<='],
          ['等于 (==)', '=='],
          ['大于等于 (>=)', '>='],
          ['大于 (>)', '>'],
          ['不等于 (!=)', '!=']
        ]), 'OP')
        .appendField(new Blockly.FieldNumber(20), 'VALUE');

      this.setOutput(true, ['Boolean', 'Condition']);
      this.setStyle('condition_blocks');
      this.setTooltip('针对 CS2 官方 GSI 实时数值遥测（血量/护甲/金钱/致盲等）进行阈值比对判定');
    }
  };

  // 3. GSI 预设枚举常量积木 (供 Scratch 式文本比对灵活插接)
  Blockly.Blocks['gsi_enum_constant'] = {
    init: function() {
      this.appendDummyInput()
        .appendField('GSI 枚举取值')
        .appendField(new Blockly.FieldDropdown(ALL_GSI_ENUM_VALUES), 'VALUE');
      this.setOutput(true, 'String');
      this.setStyle('condition_blocks');
      this.setTooltip('CS2 GSI 官方离散枚举字符串常量，可直接连接至 Scratch 文本比较槽，杜绝手动打字拼错');
    }
  };
}

