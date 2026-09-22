/**
 * CS2 Game State Integration (GSI) Full Dictionary & Metadata Registry
 * Provides discrete enum sets, numeric fields, boolean sensors, and dynamic dropdown options
 * for Google Blockly studios and RuleEngine execution.
 */

export const DISCRETE_GSI_STATES = [
  {
    key: 'round.bomb',
    label: 'C4 炸弹状态 (round.bomb)',
    category: '回合与C4',
    options: [
      ['已安放下包 (planted)', 'planted'],
      ['队员携带中 (carried)', 'carried'],
      ['掉落在地面 (dropped)', 'dropped'],
      ['已被拆除成功 (defused)', 'defused'],
      ['已引爆摧毁 (exploded)', 'exploded']
    ]
  },
  {
    key: 'round.phase',
    label: '回合阶段 (round.phase)',
    category: '回合与C4',
    options: [
      ['开局冻结/购买 (freezetime)', 'freezetime'],
      ['正式战斗交火中 (live)', 'live'],
      ['回合结束结算 (over)', 'over']
    ]
  },
  {
    key: 'player.team',
    label: '所属阵营 (player.team)',
    category: '玩家与战队',
    options: [
      ['恐怖分子 (T 阵营)', 'T'],
      ['反恐精英 (CT 阵营)', 'CT']
    ]
  },
  {
    key: 'round.win_team',
    label: '回合获胜方 (round.win_team)',
    category: '回合与C4',
    options: [
      ['恐怖分子获胜 (T)', 'T'],
      ['反恐精英获胜 (CT)', 'CT']
    ]
  },
  {
    key: 'player.activity',
    label: '玩家活动状态 (player.activity)',
    category: '玩家与战队',
    options: [
      ['游戏中/存活 (playing)', 'playing'],
      ['打字聊天中 (textinput)', 'textinput'],
      ['主菜单/设置中 (menu)', 'menu']
    ]
  },
  {
    key: 'map.phase',
    label: '比赛大阶段 (map.phase)',
    category: '地图与对局',
    options: [
      ['赛前热身 (warmup)', 'warmup'],
      ['正式比赛打响 (live)', 'live'],
      ['中场攻守换边 (intermission)', 'intermission'],
      ['全场决出胜负 (gameover)', 'gameover']
    ]
  },
  {
    key: 'map.mode',
    label: '比赛模式 (map.mode)',
    category: '地图与对局',
    options: [
      ['竞技排位 (competitive)', 'competitive'],
      ['优先排位 (premier)', 'premier'],
      ['休闲模式 (casual)', 'casual'],
      ['死斗模式 (deathmatch)', 'deathmatch'],
      ['搭档模式 (wingman)', 'wingman'],
      ['自定义/工坊 (custom)', 'custom']
    ]
  },
  {
    key: 'player.weapons.weapon_0.state',
    label: '主武器手持状态 (weapon_0.state)',
    category: '武器与弹药',
    options: [
      ['当前手持中 (active)', 'active'],
      ['收起背负 (holstered)', 'holstered'],
      ['装填换弹中 (reloading)', 'reloading']
    ]
  },
  {
    key: 'player.weapons.weapon_1.state',
    label: '副武器手持状态 (weapon_1.state)',
    category: '武器与弹药',
    options: [
      ['当前手持中 (active)', 'active'],
      ['收起背负 (holstered)', 'holstered'],
      ['装填换弹中 (reloading)', 'reloading']
    ]
  }
];

export const NUMERIC_GSI_STATES = [
  {
    key: 'player.state.health',
    label: '玩家实时血量 (player.state.health)',
    unit: 'HP',
    range: '0 - 100',
    defaultOp: '<',
    defaultValue: 20
  },
  {
    key: 'player.state.armor',
    label: '防弹护甲值 (player.state.armor)',
    unit: 'AP',
    range: '0 - 100',
    defaultOp: '<',
    defaultValue: 50
  },
  {
    key: 'player.state.money',
    label: '持有现金 (player.state.money)',
    unit: '$',
    range: '$0 - $16000',
    defaultOp: '<',
    defaultValue: 2000
  },
  {
    key: 'player.state.flashed',
    label: '闪光致盲深度 (player.state.flashed)',
    unit: '深度',
    range: '0 - 255',
    defaultOp: '>',
    defaultValue: 100
  },
  {
    key: 'player.state.burning',
    label: '燃烧弹灼烧 (player.state.burning)',
    unit: '烈火',
    range: '0 - 255',
    defaultOp: '>',
    defaultValue: 0
  },
  {
    key: 'player.state.smoked',
    label: '烟雾视野遮蔽 (player.state.smoked)',
    unit: '浓烟',
    range: '0 - 255',
    defaultOp: '>',
    defaultValue: 0
  },
  {
    key: 'player.state.round_kills',
    label: '本回合击杀数 (player.state.round_kills)',
    unit: '人',
    range: '0 - 5',
    defaultOp: '>=',
    defaultValue: 1
  },
  {
    key: 'player.state.round_killhs',
    label: '本回合爆头数 (player.state.round_killhs)',
    unit: '人',
    range: '0 - 5',
    defaultOp: '>=',
    defaultValue: 1
  },
  {
    key: 'map.round',
    label: '当前比赛回合数 (map.round)',
    unit: '回合',
    range: '1 - 30+',
    defaultOp: '==',
    defaultValue: 1
  },
  {
    key: 'player.weapons.weapon_0.ammo_clip',
    label: '主武器当前弹药 (weapon_0.ammo_clip)',
    unit: '发',
    range: '0 - 100',
    defaultOp: '<=',
    defaultValue: 5
  },
  {
    key: 'player.weapons.weapon_1.ammo_clip',
    label: '副武器当前弹药 (weapon_1.ammo_clip)',
    unit: '发',
    range: '0 - 30',
    defaultOp: '==',
    defaultValue: 0
  }
];

export const BOOLEAN_GSI_STATES = [
  { key: 'player.state.helmet', label: '防弹头盔 (player.state.helmet)' },
  { key: 'player.state.defusekit', label: '拆弹器包 (player.state.defusekit)' },
  { key: 'event.kill', label: '瞬时击杀脉冲 (event.kill)' },
  { key: 'event.headshot', label: '瞬时爆头脉冲 (event.headshot)' },
  { key: 'event.damage', label: '瞬时受击脉冲 (event.damage)' },
  { key: 'event.bomb_planted', label: '瞬时C4安放脉冲 (event.bomb_planted)' },
  { key: 'event.bomb_defused', label: '瞬时C4拆除脉冲 (event.bomb_defused)' },
  { key: 'event.round_won', label: '瞬时回合获胜脉冲 (event.round_won)' },
  { key: 'event.round_lost', label: '瞬时回合失利脉冲 (event.round_lost)' }
];

export const DISCRETE_STATE_DROPDOWN_OPTIONS = DISCRETE_GSI_STATES.map((s) => [s.label, s.key]);

export const NUMERIC_STATE_DROPDOWN_OPTIONS = NUMERIC_GSI_STATES.map((s) => [s.label, s.key]);

export const BOOLEAN_STATE_DROPDOWN_OPTIONS = BOOLEAN_GSI_STATES.map((s) => [s.label, s.key]);

export const EVENT_GSI_STATES = BOOLEAN_GSI_STATES.filter((s) => s.key.startsWith('event.'));

export const EVENT_DROPDOWN_OPTIONS = EVENT_GSI_STATES.map((s) => [s.label, s.key]);

/**
 * Get discrete options for a specific GSI state key
 * @param {string} stateKey 
 * @returns {Array<[string, string]>}
 */
export function getStateOptions(stateKey) {
  const found = DISCRETE_GSI_STATES.find((s) => s.key === stateKey);
  if (found && Array.isArray(found.options) && found.options.length > 0) {
    return found.options;
  }
  // Return fallback option to guarantee non-empty dropdown for Blockly
  return [['(默认)', '']];
}

/**
 * Get default threshold metadata for a numeric GSI state key
 * @param {string} key 
 * @returns {{ op: string, value: number }}
 */
export function getDefaultThreshold(key) {
  const found = NUMERIC_GSI_STATES.find((s) => s.key === key);
  if (found) {
    return { op: found.defaultOp, value: found.defaultValue };
  }
  return { op: '==', value: 0 };
}

/**
 * Flattened unique enum value options across all discrete states
 */
export const ALL_GSI_ENUM_VALUES = (() => {
  const seen = new Set();
  const list = [];
  for (const s of DISCRETE_GSI_STATES) {
    for (const [label, val] of s.options) {
      if (!seen.has(val)) {
        seen.add(val);
        list.push([`${label} [${val}]`, val]);
      }
    }
  }
  return list;
})();
