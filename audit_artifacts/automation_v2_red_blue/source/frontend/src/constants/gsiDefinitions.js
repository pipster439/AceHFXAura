export const GSI_FIELD_DEFINITIONS = [
  // 1. 玩家核心生理状态
  { field: 'player_state.health', label: '生命值 (HP)', range: '0 - 100', category: '玩家核心状态', spectator: false, desc: '实时血量数值，低于 20 时自动触发残血红光预警' },
  { field: 'player_state.armor', label: '护甲值 (AP)', range: '0 - 100', category: '玩家核心状态', spectator: false, desc: '当前防弹衣剩余护甲点数' },
  { field: 'player_state.helmet', label: '防弹头盔', range: 'true / false', category: '玩家核心状态', spectator: false, desc: '是否装备了高阶防弹头盔' },
  { field: 'player_state.flashed', label: '闪光致盲深度', range: '0 - 255', category: '玩家核心状态', spectator: false, desc: '致盲深度，> 0 时视野受损，= 255 为完全全白致盲' },
  { field: 'player_state.smoked', label: '烟雾遮挡深度', range: '0 - 255', category: '玩家核心状态', spectator: false, desc: '处于烟雾弹边缘或内部时的视野遮蔽深度' },
  { field: 'player_state.burning', label: '燃烧弹灼烧', range: '0 - 255', category: '玩家核心状态', spectator: false, desc: '受到燃烧弹/燃烧瓶烈火灼烧伤害' },
  { field: 'player_state.money', label: '持有金钱', range: '$0 - $16000', category: '玩家核心状态', spectator: false, desc: '当前账户现金，用于经济局与全甲全弹判断' },
  { field: 'player_state.round_kills', label: '本回合击杀数', range: '0 - 5', category: '玩家核心状态', spectator: false, desc: '单回合内已击杀敌方玩家人数' },
  { field: 'player_state.round_killhs', label: '本回合爆头数', range: '0 - 5', category: '玩家核心状态', spectator: false, desc: '单回合内以精准爆头方式击杀敌方的人数' },
  { field: 'player_state.round_damage', label: '本回合累计伤害', range: '0 - 1000+', category: '玩家核心状态', spectator: false, desc: '当前回合累计对敌方造成的有效伤害值' },

  // 2. 武器槽位与弹药状态
  { field: 'player_weapons.weapon_0.ammo_clip', label: '主武器当前弹药', range: '0 - 100', category: '武器与弹药', spectator: false, desc: '主武器当前弹匣剩余可用子弹' },
  { field: 'player_weapons.weapon_0.ammo_reserve', label: '主武器备弹数', range: '0 - 300', category: '武器与弹药', spectator: false, desc: '背包内携带的主武器剩余备用子弹' },
  { field: 'player_weapons.weapon_0.state', label: '主武器状态', range: 'active / holstered / reloading', category: '武器与弹药', spectator: false, desc: '主武器当前处于手持/收起/换弹中' },
  { field: 'player_weapons.weapon_1.ammo_clip', label: '副武器当前弹药', range: '0 - 30', category: '武器与弹药', spectator: false, desc: '手枪弹匣剩余子弹' },
  { field: 'player_weapons.weapon_1.ammo_reserve', label: '副武器备弹数', range: '0 - 150', category: '武器与弹药', spectator: false, desc: '手枪备用子弹' },
  { field: 'player_weapons.weapon_0.name', label: '主武器代号', range: 'weapon_ak47, weapon_m4a1 等', category: '武器与弹药', spectator: false, desc: '当前装备的主武器英文代号' },
  { field: 'player_weapons.weapon_1.name', label: '副武器代号', range: 'weapon_deagle, weapon_glock 等', category: '武器与弹药', spectator: false, desc: '当前装备的手枪英文代号' },

  // 3. 回合与 C4 炸弹
  { field: 'round.bomb', label: 'C4 炸弹状态', range: 'planted / exploded / defused', category: '回合与C4', spectator: false, desc: 'C4 下包、爆炸或被成功拆除状态' },
  { field: 'round.phase', label: '回合阶段', range: 'freezetime / live / over', category: '回合与C4', spectator: false, desc: '当前处于冻结准备、交火进行或回合结算' },
  { field: 'round.win_team', label: '回合获胜方', range: 'T / CT', category: '回合与C4', spectator: false, desc: '刚刚赢得本回合的阵营' },
  { field: 'map.round', label: '当前比赛回合数', range: '1 - 30+', category: '地图与对局', spectator: false, desc: '当前正在进行的对局回合编号' },
  { field: 'map.name', label: '地图代号', range: 'de_dust2, de_mirage 等', category: '地图与对局', spectator: false, desc: '当前所载入的官方或社区地图标识' },
  { field: 'map.mode', label: '比赛模式', range: 'competitive / casual / deathmatch', category: '地图与对局', spectator: false, desc: '当前游戏模式（竞技排位、休闲或死斗）' },

  // 4. 全场战绩
  { field: 'player_match_stats.kills', label: '全场击杀总数', range: '0 - 50+', category: '全场战绩', spectator: false, desc: '整场比赛累计击杀敌人数' },
  { field: 'player_match_stats.assists', label: '全场助攻总数', range: '0 - 30+', category: '全场战绩', spectator: false, desc: '整场比赛累计助攻数' },
  { field: 'player_match_stats.deaths', label: '全场阵亡总数', range: '0 - 30+', category: '全场战绩', spectator: false, desc: '整场比赛个人阵亡总数' },
  { field: 'player_match_stats.mvps', label: 'MVP 次数', range: '0 - 15+', category: '全场战绩', spectator: false, desc: '获得本回合最有价值选手 (MVP) 的次数' },
  { field: 'player_match_stats.score', label: '个人综合得分', range: '0 - 150+', category: '全场战绩', spectator: false, desc: '个人天梯综合贡献积分' },

  // 5. 动态衍生事件脉冲
  { field: 'event.kill', label: '击杀脉冲 (Kill)', range: 'true / false', category: '实时事件', spectator: false, desc: '成功消灭敌方玩家时产生的 1.5s 瞬时脉冲' },
  { field: 'event.headshot', label: '爆头脉冲 (Headshot)', range: 'true / false', category: '实时事件', spectator: false, desc: '爆头消灭敌方玩家时产生的 1.5s 瞬时脉冲' },
  { field: 'event.damage', label: '受击脉冲 (Damage)', range: 'true / false', category: '实时事件', spectator: false, desc: '遭受敌人子弹或投掷物攻击时产生的 1.0s 瞬时红光脉冲' },
  { field: 'event.flashed', label: '致盲脉冲 (Flashed)', range: 'true / false', category: '实时事件', spectator: false, desc: '被闪光弹高强度致盲时产生的 2.0s 瞬时全白脉冲' },
  { field: 'event.round_won', label: '胜利脉冲 (Round Won)', range: 'true / false', category: '实时事件', spectator: false, desc: '回合获胜时产生的 3.0s 绿色欢庆光效脉冲' },
  { field: 'event.round_lost', label: '失败脉冲 (Round Lost)', range: 'true / false', category: '实时事件', spectator: false, desc: '回合失利时产生的 3.0s 红色警示光效脉冲' },

  // 6. GOTV / 观战模式专属
  { field: 'phase_countdowns.phase_ends_in', label: 'C4/倒计时精确秒数', range: '0.0 - 40.0', category: 'GOTV/观战专属', spectator: true, desc: '精确到毫秒级的倒计时剩余秒数' },
  { field: 'allplayers_id', label: '全场 10 人 SteamID', range: 'JSON Array', category: 'GOTV/观战专属', spectator: true, desc: '全场所有选手的 64 位 Steam 身份标识' },
  { field: 'allplayers_state', label: '全场 10 人血量/护甲矩阵', range: 'JSON Object', category: 'GOTV/观战专属', spectator: true, desc: '全场所有玩家的实时 HP、AP 及装备状态' },
  { field: 'allplayers_weapons', label: '全场 10 人武器分布', range: 'JSON Object', category: 'GOTV/观战专属', spectator: true, desc: '全场双方阵营所有选手当前手持与背负的武器' },
  { field: 'allgrenades', label: '全场投掷物动态', range: 'JSON Object', category: 'GOTV/观战专属', spectator: true, desc: '场上正在飞行或爆炸的所有手雷、闪光、烟雾及燃烧弹' },
  { field: 'player_position', label: '选手 3D 空间坐标', range: 'X, Y, Z 坐标', category: 'GOTV/观战专属', spectator: true, desc: '当前主视角选手的 3D 绝对空间坐标矢量' }
];
