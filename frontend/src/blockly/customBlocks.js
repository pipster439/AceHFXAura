import * as Blockly from 'blockly/core';

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

    // =========================================================================
    // 4. KEYBOARD OPERATIONS BLOCKS
    // =========================================================================
    {
      type: 'key_fill_all',
      message0: '整盘填充色彩 %1',
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
          options: [
            ['玩家血量 (player.state.health)', 'player.state.health'],
            ['玩家护甲 (player.state.armor)', 'player.state.armor'],
            ['金钱储备 (player.state.money)', 'player.state.money'],
            ['本局击杀 (player.state.round_kills)', 'player.state.round_kills'],
            ['被致盲度 (player.state.flashed)', 'player.state.flashed'],
            ['燃烧伤害 (player.state.burning)', 'player.state.burning'],
            ['当前回合 (map.round)', 'map.round']
          ]
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
          options: [
            ['C4状态 (round.bomb)', 'round.bomb'],
            ['回合状态 (round.phase)', 'round.phase'],
            ['所属阵营 (player.team)', 'player.team'],
            ['地图名称 (map.name)', 'map.name'],
            ['游戏模式 (map.mode)', 'map.mode'],
            ['玩家状态 (player.activity)', 'player.activity']
          ]
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
          options: [
            ['装备头盔 (player.state.helmet)', 'player.state.helmet'],
            ['携带拆弹器 (player.state.defusekit)', 'player.state.defusekit']
          ]
        },
        { type: 'input_value', name: 'DEFAULT', check: 'Boolean' }
      ],
      output: 'Boolean',
      style: 'gsi_blocks',
      tooltip: '从 CS2 GSI 提取布尔状态',
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
          options: [
            ['击杀敌人 (event.kill)', 'event.kill'],
            ['致盲白屏 (event.flash)', 'event.flash'],
            ['C4已安放 (event.bomb_planted)', 'event.bomb_planted'],
            ['C4已拆除 (event.bomb_defused)', 'event.bomb_defused'],
            ['回合获胜/MVP (event.round_mvp)', 'event.round_mvp'],
            ['受到伤害 (event.damage_taken)', 'event.damage_taken']
          ]
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
          check: 'Condition'
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
        { type: 'input_value', name: 'COND0', check: 'Condition' },
        { type: 'input_value', name: 'COND1', check: 'Condition' }
      ],
      output: 'Condition',
      style: 'condition_blocks',
      tooltip: '逻辑与：两个子条件同时为真时判定为命中',
      helpUrl: ''
    },
    {
      type: 'condition_or',
      message0: '条件或 (OR) 条件A: %1 条件B: %2',
      args0: [
        { type: 'input_value', name: 'COND0', check: 'Condition' },
        { type: 'input_value', name: 'COND1', check: 'Condition' }
      ],
      output: 'Condition',
      style: 'condition_blocks',
      tooltip: '逻辑或：任一子条件为真时即判定为命中',
      helpUrl: ''
    },
    {
      type: 'condition_not',
      message0: '条件非 (NOT) %1',
      args0: [
        { type: 'input_value', name: 'COND', check: 'Condition' }
      ],
      output: 'Condition',
      style: 'condition_blocks',
      tooltip: '逻辑非：反转子条件判断结果',
      helpUrl: ''
    },
    {
      type: 'condition_compare',
      message0: '遥测比对 %1 %2 %3',
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
      output: 'Condition',
      style: 'condition_blocks',
      tooltip: '判断 GSI 字段或进程状态是否满足比较运算',
      helpUrl: ''
    }
  ]);
}
