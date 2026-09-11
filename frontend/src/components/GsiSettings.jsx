import React, { useState, useEffect } from 'react';
import {
  Crosshair,
  Radio,
  Download,
  Plus,
  Trash2,
  Heart,
  Shield,
  Activity,
  Zap,
  FolderCheck,
  FolderOpen,
  RefreshCw,
  Search,
  AlertTriangle,
  Info,
  CheckCircle2,
  Clock,
  Sparkles,
  Gamepad2
} from 'lucide-react';
import { GSI_FIELD_DEFINITIONS } from '../constants/gsiDefinitions';

export default function GsiSettings({
  config,
  onUpdateGsiBindings,
  profiles,
  currentProfileName,
  showToast
}) {
  const [gsiCurrent, setGsiCurrent] = useState(null);
  const [cfgInfo, setCfgInfo] = useState(null);
  const [customPath, setCustomPath] = useState('');
  const [isInstalling, setIsInstalling] = useState(false);
  const [searchTerm, setSearchTerm] = useState('');
  const [selectedCategory, setSelectedCategory] = useState('全部');

  const profileList = Object.keys(profiles || {});
  const bindings = config?.gsi_bindings || [];

  // 获取实时 GSI 遥测
  const fetchGsiCurrent = async () => {
    try {
      const res = await fetch('/api/gsi/current', { cache: 'no-store' });
      if (res.ok) {
        const data = await res.json();
        setGsiCurrent(data);
      }
    } catch (e) {
      // 忽略心跳瞬断
    }
  };

  // 获取 CFG 检测信息
  const fetchCfgInfo = async () => {
    try {
      const res = await fetch('/api/gsi/cfg', { cache: 'no-store' });
      if (res.ok) {
        const data = await res.json();
        setCfgInfo(data);
        if (data.detected_path) {
          setCustomPath(data.detected_path);
        }
      }
    } catch (e) {
      // 忽略
    }
  };

  useEffect(() => {
    fetchGsiCurrent();
    fetchCfgInfo();
    const interval = setInterval(fetchGsiCurrent, 1000);
    return () => clearInterval(interval);
  }, []);

  // 一键安装 CFG
  const handleInstallCfg = async () => {
    setIsInstalling(true);
    try {
      const res = await fetch('/api/gsi/install-cfg', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ target_dir: customPath || cfgInfo?.detected_path || '' })
      });
      const data = await res.json();
      if (res.ok) {
        showToast(data.message || 'CFG 配置文件安装成功！');
        fetchCfgInfo();
      } else {
        showToast(data.message || '安装失败', 'error');
      }
    } catch (e) {
      showToast('请求失败: ' + e.message, 'error');
    } finally {
      setIsInstalling(false);
    }
  };

  // 规则操作 (严格过滤空字段与重复项)
  const handleAddBinding = (newBinding = { field: 'player_state.health', operator: '<', value: 20, profile: currentProfileName }) => {
    if (!newBinding.field || newBinding.field.trim() === '') return;
    const exists = bindings.some(b => b.field === newBinding.field && b.operator === newBinding.operator && b.value === newBinding.value);
    if (exists) {
      showToast('该规则已存在', 'info');
      return;
    }
    const updated = [...bindings, newBinding];
    onUpdateGsiBindings(updated);
    showToast('已添加 GSI 规则，已实时生效');
  };

  const handleDeleteBinding = (index) => {
    const updated = bindings.filter((_, i) => i !== index);
    onUpdateGsiBindings(updated);
  };

  const handleUpdateBinding = (index, patch) => {
    const updated = bindings.map((b, i) => (i === index ? { ...b, ...patch } : b));
    onUpdateGsiBindings(updated);
  };

  // 快捷预设
  const quickPresets = [
    { label: '残血红光 (HP < 20)', field: 'player_state.health', operator: '<', value: 20, profile: 'danger_red' },
    { label: 'C4 炸弹脉冲 (安放中)', field: 'round.bomb', operator: '==', value: 'planted', profile: 'bomb_pulse' },
    { label: '击杀光效 (Kill)', field: 'event.kill', operator: '==', value: true, profile: 'rainbow_wave' },
    { label: '致盲全白 (Flashed)', field: 'event.flashed', operator: '==', value: true, profile: 'desktop' }
  ];

  const isConnected = gsiCurrent?.connected === true;
  const isCs2Foreground = gsiCurrent?.is_cs2_foreground === true;
  const foregroundProc = gsiCurrent?.foreground_process || '桌面/未知';
  const rawData = gsiCurrent?.data || {};
  const recentEvents = gsiCurrent?.events || [];

  const categories = ['全部', '玩家核心状态', '武器与弹药', '回合与C4', '地图与对局', '全场战绩', '实时事件', 'GOTV/观战专属'];

  const filteredDefinitions = GSI_FIELD_DEFINITIONS.filter(item => {
    const matchesCategory = selectedCategory === '全部' || item.category === selectedCategory;
    const matchesSearch = !searchTerm ||
      item.field.toLowerCase().includes(searchTerm.toLowerCase()) ||
      item.label.toLowerCase().includes(searchTerm.toLowerCase()) ||
      item.desc.toLowerCase().includes(searchTerm.toLowerCase());
    return matchesCategory && matchesSearch;
  });

  return (
    <div className="flex flex-col gap-6 p-2">
      {/* 头部：大容器无边框 */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-2 pb-3 border-b border-slate-200">
        <div>
          <h3 className="font-bold text-slate-900 text-base flex items-center gap-2">
            <Crosshair className="w-5 h-5 text-slate-900" />
            CS2 Game State Integration (GSI) 游戏深度联动
          </h3>
          <p className="text-xs text-slate-500 mt-0.5">
            基于 Valve 官方 GSI 规范，实时捕获游戏内血量、护甲、C4 炸弹与击杀事件，并与键盘灯效硬件联动。
          </p>
        </div>

        <div className="flex items-center gap-2">
          <span className={`px-2.5 py-1 text-xs font-bold rounded-none border ${
            isConnected
              ? 'bg-slate-900 text-white border-slate-900'
              : 'bg-slate-100 text-slate-600 border-slate-300'
          }`}>
            {isConnected ? `GSI 遥测在线 (包: ${gsiCurrent?.packet_count || 0})` : '等待 CS2 游戏连接'}
          </span>
        </div>
      </div>

      {/* 前台窗口聚焦判定指示器 */}
      <div className={`p-3 rounded-none border flex flex-col sm:flex-row sm:items-center justify-between gap-2 ${
        isCs2Foreground
          ? 'bg-emerald-50 border-emerald-300 text-emerald-900'
          : 'bg-amber-50 border-amber-300 text-amber-900'
      }`}>
        <div className="flex items-center gap-2 text-xs font-bold">
          <Gamepad2 className="w-4 h-4 shrink-0" />
          <span>前台聚焦判定:</span>
          {isCs2Foreground ? (
            <span className="bg-emerald-600 text-white px-2 py-0.5 rounded-none font-mono">
              cs2.exe (前台已聚焦，GSI 绑定激活中)
            </span>
          ) : (
            <span className="bg-amber-600 text-white px-2 py-0.5 rounded-none font-mono">
              {foregroundProc} (前台非 CS2，GSI 联动已隔离挂起)
            </span>
          )}
        </div>
        <span className="text-[11px] opacity-80">
          {isCs2Foreground
            ? '✅ 满足双重条件，下方的 GSI 规则将实时生效至键盘硬件'
            : '🛡️ 保护非游戏体验：切回 CS2 窗口时自动瞬间恢复光效'}
        </span>
      </div>

      {/* 遥测实况数据方块 */}
      <div className="grid grid-cols-2 sm:grid-cols-4 lg:grid-cols-6 gap-3">
        <div className="p-3 bg-white border border-slate-300 rounded-none flex flex-col gap-1">
          <span className="text-[10px] font-bold text-slate-500 flex items-center gap-1">
            <Heart className="w-3.5 h-3.5 text-slate-600" />
            生命值 (HP)
          </span>
          <span className="text-xl font-mono font-black text-slate-900">
            {rawData['player_state.health'] !== undefined ? rawData['player_state.health'] : (rawData['player.state.health'] !== undefined ? rawData['player.state.health'] : '--')}
          </span>
        </div>

        <div className="p-3 bg-white border border-slate-300 rounded-none flex flex-col gap-1">
          <span className="text-[10px] font-bold text-slate-500 flex items-center gap-1">
            <Shield className="w-3.5 h-3.5 text-slate-600" />
            护甲值 (AP)
          </span>
          <span className="text-xl font-mono font-black text-slate-900">
            {rawData['player_state.armor'] !== undefined ? rawData['player_state.armor'] : (rawData['player.state.armor'] !== undefined ? rawData['player.state.armor'] : '--')}
          </span>
        </div>

        <div className="p-3 bg-white border border-slate-300 rounded-none flex flex-col gap-1">
          <span className="text-[10px] font-bold text-slate-500 flex items-center gap-1">
            <Activity className="w-3.5 h-3.5 text-slate-600" />
            C4 炸弹状态
          </span>
          <span className="text-sm font-bold text-slate-900 truncate mt-1">
            {rawData['round.bomb'] || '无'}
          </span>
        </div>

        <div className="p-3 bg-white border border-slate-300 rounded-none flex flex-col gap-1">
          <span className="text-[10px] font-bold text-slate-500 flex items-center gap-1">
            <Zap className="w-3.5 h-3.5 text-slate-600" />
            回合击杀
          </span>
          <span className="text-xl font-mono font-black text-slate-900">
            {rawData['player_state.round_kills'] !== undefined ? rawData['player_state.round_kills'] : 0}
          </span>
        </div>

        <div className="p-3 bg-white border border-slate-300 rounded-none flex flex-col gap-1">
          <span className="text-[10px] font-bold text-slate-500">地图 / 回合</span>
          <span className="text-xs font-bold text-slate-900 truncate mt-1">
            {rawData['map.name'] || '未开始'} (R{rawData['map.round'] || 0})
          </span>
        </div>

        <div className="p-3 bg-white border border-slate-300 rounded-none flex flex-col gap-1">
          <span className="text-[10px] font-bold text-slate-500">监听服务端口</span>
          <span className="text-xs font-mono font-bold text-slate-900 truncate mt-1">
            127.0.0.1:19897
          </span>
        </div>
      </div>

      {/* 实时游戏事件流 (Recent Game Events) */}
      {recentEvents.length > 0 && (
        <div className="p-3.5 bg-white border border-slate-300 rounded-none flex flex-col gap-2">
          <div className="flex items-center justify-between">
            <span className="text-xs font-bold text-slate-900 flex items-center gap-1.5">
              <Sparkles className="w-4 h-4 text-slate-700" />
              实时捕获事件流 (Recent Game Events)
            </span>
            <span className="text-[10px] text-slate-500">自动由游戏状态机推导并驱动瞬时脉冲</span>
          </div>
          <div className="flex flex-wrap gap-2">
            {recentEvents.map((ev, i) => (
              <div key={i} className="flex items-center gap-1.5 px-2.5 py-1 bg-slate-100 border border-slate-300 text-xs rounded-none">
                <span className="w-1.5 h-1.5 bg-slate-900 rounded-none animate-ping" />
                <span className="font-bold text-slate-900">{ev.label}</span>
                <span className="text-[10px] text-slate-500 font-mono">{ev.time_str}</span>
                <span className="text-[11px] text-slate-600">{ev.desc}</span>
              </div>
            ))}
          </div>
        </div>
      )}

      {/* CFG 部署区 */}
      <div className="p-4 bg-white border border-slate-300 rounded-none flex flex-col gap-3">
        <div className="flex items-center justify-between">
          <span className="text-xs font-bold text-slate-900 flex items-center gap-1.5">
            <FolderCheck className="w-4 h-4 text-slate-700" />
            CS2 CFG 配置文件部署
          </span>
          <span className={`text-[11px] font-bold px-2 py-0.5 border ${
            cfgInfo?.installed
              ? 'bg-slate-100 text-slate-800 border-slate-300'
              : 'bg-amber-50 text-amber-700 border-amber-300'
          }`}>
            {cfgInfo?.installed ? '已部署 gamestate_integration_aura.cfg' : '未检测到配置文件'}
          </span>
        </div>

        <div className="flex flex-col sm:flex-row items-center gap-2.5">
          <div className="flex-1 flex items-center gap-2 px-3 py-1.5 bg-slate-50 border border-slate-300 rounded-none w-full">
            <FolderOpen className="w-4 h-4 text-slate-500 shrink-0" />
            <input
              type="text"
              value={customPath}
              onChange={(e) => setCustomPath(e.target.value)}
              placeholder="CS2 cfg 目录路径..."
              className="w-full text-xs font-mono text-slate-900 bg-transparent outline-none"
            />
          </div>

          <button
            onClick={handleInstallCfg}
            disabled={isInstalling}
            className="w-full sm:w-auto h-8 px-3.5 flex items-center justify-center gap-1.5 bg-slate-900 hover:bg-slate-800 text-white rounded-none border border-slate-900 text-xs font-bold cursor-pointer active:scale-95 disabled:opacity-50 transition-all shrink-0"
          >
            <Download className="w-3.5 h-3.5" />
            <span>{isInstalling ? '部署中...' : '一键部署到 CS2 目录'}</span>
          </button>
        </div>
      </div>

      {/* GSI 绑定规则表 */}
      <div className="flex flex-col gap-3">
        <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-2 pb-2 border-b border-slate-200">
          <div>
            <h4 className="font-bold text-slate-900 text-sm">GSI 事件与灯效方案绑定规则 (gsi_bindings)</h4>
            <p className="text-xs text-slate-500 mt-0.5">
              当 CS2 处于前台时，按从上到下的优先级匹配下列条件，由守护进程自动切换为对应 Profile。
            </p>
          </div>

          <button
            onClick={() => handleAddBinding()}
            className="w-8 h-8 aspect-square flex items-center justify-center rounded-none bg-slate-900 text-white hover:bg-slate-800 active:scale-95 transition-all shadow-xs cursor-pointer"
            title="添加 GSI 规则"
          >
            <Plus className="w-4 h-4" />
          </button>
        </div>

        {/* 快捷推荐 */}
        <div className="flex flex-wrap items-center gap-2">
          <span className="text-xs font-semibold text-slate-700">快速推荐:</span>
          {quickPresets.map((preset) => (
            <button
              key={preset.label}
              onClick={() => {
                handleAddBinding({
                  field: preset.field,
                  operator: preset.operator,
                  value: preset.value,
                  profile: profileList.includes(preset.profile) ? preset.profile : currentProfileName
                });
              }}
              className="h-7 px-2.5 flex items-center gap-1.5 rounded-none border border-slate-300 bg-white text-slate-800 hover:border-slate-500 hover:bg-slate-50 active:scale-95 transition-all text-[11px] font-semibold"
            >
              <Zap className="w-3 h-3 text-slate-600" />
              <span>{preset.label}</span>
            </button>
          ))}
        </div>

        {/* 绑定表格 */}
        <div className="overflow-x-auto border border-slate-300 rounded-none bg-white">
          <table className="w-full text-left text-xs border-collapse">
            <thead>
              <tr className="bg-slate-100 border-b border-slate-300 text-slate-700 font-bold">
                <th className="py-2.5 px-3.5">遥测字段 (Field)</th>
                <th className="py-2.5 px-3.5">比较条件</th>
                <th className="py-2.5 px-3.5">触发阈值 (Value)</th>
                <th className="py-2.5 px-3.5">激活灯效方案</th>
                <th className="py-2.5 px-3.5 text-right">操作</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-200">
              {bindings.length === 0 ? (
                <tr>
                  <td colSpan={5} className="py-6 text-center text-slate-400 font-medium">
                    尚未配置任何 GSI 规则，点击右上角按钮或快速推荐添加
                  </td>
                </tr>
              ) : (
                bindings.map((binding, idx) => (
                  <tr key={idx} className="hover:bg-slate-50 transition-colors">
                    <td className="py-2.5 px-3.5">
                      <input
                        type="text"
                        value={binding.field || ''}
                        onChange={(e) => handleUpdateBinding(idx, { field: e.target.value.trim() })}
                        placeholder="例如: player_state.health"
                        className="w-48 px-2 py-1 bg-white border border-slate-300 rounded-none text-slate-900 text-xs font-mono outline-none focus:border-slate-900"
                      />
                    </td>
                    <td className="py-2.5 px-3.5">
                      <select
                        value={binding.operator || '=='}
                        onChange={(e) => handleUpdateBinding(idx, { operator: e.target.value })}
                        className="px-2 py-1 bg-white border border-slate-300 rounded-none text-slate-900 text-xs font-bold outline-none focus:border-slate-900 cursor-pointer"
                      >
                        <option value="==">== (等于)</option>
                        <option value="<">&lt; (小于)</option>
                        <option value="<=">&lt;= (小于等于)</option>
                        <option value=">">&gt; (大于)</option>
                        <option value=">=">&gt;= (大于等于)</option>
                        <option value="!=">!= (不等于)</option>
                      </select>
                    </td>
                    <td className="py-2.5 px-3.5">
                      <input
                        type="text"
                        value={String(binding.value !== undefined ? binding.value : '')}
                        onChange={(e) => {
                          const val = e.target.value;
                          let parsed = val;
                          if (val === 'true') parsed = true;
                          else if (val === 'false') parsed = false;
                          else if (!isNaN(Number(val)) && val.trim() !== '') parsed = Number(val);
                          handleUpdateBinding(idx, { value: parsed });
                        }}
                        placeholder="例如: 20 或 planted"
                        className="w-32 px-2 py-1 bg-white border border-slate-300 rounded-none text-slate-900 text-xs font-mono outline-none focus:border-slate-900"
                      />
                    </td>
                    <td className="py-2.5 px-3.5">
                      <select
                        value={binding.profile}
                        onChange={(e) => handleUpdateBinding(idx, { profile: e.target.value })}
                        className="px-2 py-1 bg-white border border-slate-300 rounded-none text-slate-900 text-xs font-bold outline-none focus:border-slate-900 cursor-pointer"
                      >
                        {profileList.map((p) => (
                          <option key={p} value={p}>
                            {p}
                          </option>
                        ))}
                      </select>
                    </td>
                    <td className="py-2.5 px-3.5 text-right">
                      <button
                        onClick={() => handleDeleteBinding(idx)}
                        className="w-7 h-7 aspect-square inline-flex items-center justify-center text-slate-400 hover:text-slate-900 rounded-none border border-transparent hover:border-slate-300 hover:bg-slate-100 transition-colors cursor-pointer"
                        title="删除绑定"
                      >
                        <Trash2 className="w-3.5 h-3.5" />
                      </button>
                    </td>
                  </tr>
                ))
              )}
            </tbody>
          </table>
        </div>
      </div>

      {/* 全量遥测字典与实时探查表 */}
      <div className="flex flex-col gap-3 pt-2 border-t border-slate-200">
        <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-2">
          <div>
            <h4 className="font-bold text-slate-900 text-sm flex items-center gap-2">
              <Radio className="w-4 h-4 text-slate-900" />
              CS2 全量官方数据管道字典与实时探查
            </h4>
            <p className="text-xs text-slate-500 mt-0.5">
              直接捕获 Valve 官方上报的所有类别遥测数据，支持按分类检索与实时数值映射。
            </p>
          </div>

          <div className="flex items-center gap-2">
            <div className="flex items-center gap-1.5 px-2.5 py-1 bg-white border border-slate-300 text-xs">
              <Search className="w-3.5 h-3.5 text-slate-400" />
              <input
                type="text"
                placeholder="搜索字段、含义或代号..."
                value={searchTerm}
                onChange={(e) => setSearchTerm(e.target.value)}
                className="w-44 text-xs font-medium text-slate-900 bg-transparent outline-none"
              />
            </div>
          </div>
        </div>

        {/* 观战 / GOTV 专用限制重要声明 */}
        <div className="p-3 bg-amber-50 border border-amber-300 rounded-none flex items-start gap-2.5 text-xs text-amber-900">
          <AlertTriangle className="w-4 h-4 text-amber-700 shrink-0 mt-0.5" />
          <div className="leading-relaxed">
            <span className="font-bold">⚠️ 观战 / GOTV 模式专用字段说明：</span>
            <span>
              根据 Valve 官方反作弊与竞技公平性限制，正常竞技交火中不向客户端开放敌方位置及全场装备信息。
              <code className="bg-amber-100 px-1 py-0.5 mx-1 font-mono">phase_countdowns.*</code>、
              <code className="bg-amber-100 px-1 py-0.5 mx-1 font-mono">allplayers_*</code>、
              <code className="bg-amber-100 px-1 py-0.5 mx-1 font-mono">allgrenades.*</code>、
              <code className="bg-amber-100 px-1 py-0.5 mx-1 font-mono">player_position.*</code> 
              等字段仅在<strong>观战模式 (GOTV) 或观看回放 Demo</strong>时下发数据，正常对局中为空值属官方设计，绝非程序 Bug。
            </span>
          </div>
        </div>

        {/* 分类切换按钮组 */}
        <div className="flex flex-wrap gap-1.5">
          {categories.map((cat) => (
            <button
              key={cat}
              onClick={() => setSelectedCategory(cat)}
              className={`px-2.5 py-1 text-xs font-bold rounded-none border transition-all cursor-pointer ${
                selectedCategory === cat
                  ? 'bg-slate-900 text-white border-slate-900'
                  : 'bg-white text-slate-700 border-slate-300 hover:border-slate-500'
              }`}
            >
              {cat}
            </button>
          ))}
        </div>

        {/* 全量字段表格 */}
        <div className="overflow-x-auto border border-slate-300 rounded-none bg-white max-h-96 overflow-y-auto">
          <table className="w-full text-left text-xs border-collapse">
            <thead className="sticky top-0 bg-slate-100 border-b border-slate-300 z-10">
              <tr className="text-slate-700 font-bold">
                <th className="py-2 px-3">字段路径 (GSI Key)</th>
                <th className="py-2 px-3">语义说明</th>
                <th className="py-2 px-3">取值范围 / 类型</th>
                <th className="py-2 px-3">当前上报实时值</th>
                <th className="py-2 px-3 text-right">快捷操作</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-200">
              {filteredDefinitions.length === 0 ? (
                <tr>
                  <td colSpan={5} className="py-8 text-center text-slate-400 font-medium">
                    未找到匹配的字段定义
                  </td>
                </tr>
              ) : (
                filteredDefinitions.map((item) => {
                  const liveVal = rawData[item.field];
                  const hasLiveVal = liveVal !== undefined;
                  return (
                    <tr key={item.field} className="hover:bg-slate-50 transition-colors">
                      <td className="py-2 px-3 font-mono font-bold text-slate-900">
                        <div className="flex items-center gap-1.5">
                          <span>{item.field}</span>
                          {item.spectator && (
                            <span className="text-[9px] bg-amber-100 text-amber-800 border border-amber-300 px-1 py-0.5 rounded-none">
                              GOTV
                            </span>
                          )}
                        </div>
                      </td>
                      <td className="py-2 px-3">
                        <div className="flex flex-col">
                          <span className="font-bold text-slate-900">{item.label}</span>
                          <span className="text-[10px] text-slate-500">{item.desc}</span>
                        </div>
                      </td>
                      <td className="py-2 px-3 text-slate-600 font-mono text-[11px]">
                        {item.range}
                      </td>
                      <td className="py-2 px-3">
                        {hasLiveVal ? (
                          <span className="font-mono font-bold px-1.5 py-0.5 bg-emerald-100 text-emerald-900 border border-emerald-300 rounded-none text-xs">
                            {typeof liveVal === 'object' ? JSON.stringify(liveVal) : String(liveVal)}
                          </span>
                        ) : (
                          <span className="font-mono text-slate-400 text-xs">
                            {item.spectator ? '(需 GOTV 模式)' : '-- (等待游戏上报)'}
                          </span>
                        )}
                      </td>
                      <td className="py-2 px-3 text-right">
                        <button
                          onClick={() => {
                            handleAddBinding({
                              field: item.field,
                              operator: typeof liveVal === 'boolean' ? '==' : typeof liveVal === 'number' ? '<' : '==',
                              value: hasLiveVal ? liveVal : (item.range.includes('true') ? true : 20),
                              profile: currentProfileName
                            });
                          }}
                          className="px-2 py-1 bg-white hover:bg-slate-100 text-slate-800 border border-slate-300 text-[11px] font-bold rounded-none active:scale-95 transition-all cursor-pointer"
                          title="将此字段加入 GSI 联动规则"
                        >
                          + 绑定规则
                        </button>
                      </td>
                    </tr>
                  );
                })
              )}
            </tbody>
          </table>
        </div>
      </div>
    </div>
  );
}
