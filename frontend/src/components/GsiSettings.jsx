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
  Search,
  AlertTriangle,
  Sparkles,
  Gamepad2
} from 'lucide-react';
import { GSI_FIELD_DEFINITIONS } from '../constants/gsiDefinitions';

export default function GsiSettings({
  showToast
}) {
  const [gsiCurrent, setGsiCurrent] = useState(null);
  const [cfgInfo, setCfgInfo] = useState(null);
  const [customPath, setCustomPath] = useState('');
  const [isInstalling, setIsInstalling] = useState(false);
  const [searchTerm, setSearchTerm] = useState('');
  const [selectedCategory, setSelectedCategory] = useState('全部');


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
    <div className="flex flex-col gap-6 p-1">
      {/* 兼容模式提示横幅 */}
      <div className="p-3 bg-md-surface-container-high border border-md-outline-variant rounded-md-lg flex items-center justify-between gap-3 text-xs">
        <div className="flex items-center gap-2">
          <span className="px-2.5 py-0.5 rounded-full bg-amber-500/15 text-amber-400 font-bold text-[11px] border border-amber-500/30 shrink-0">
            高级 / 兼容模式
          </span>
          <span className="text-md-on-surface">
            新版推荐在<strong>「工作室」</strong>中通过传感器积木统一编排 CS2 联动。此处提供 Valve 官方全量数据字典探查与 CFG 配置文件一键部署。
          </span>
        </div>
      </div>

      {/* 头部区域 */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-3 pb-3 border-b border-md-outline-variant">
        <div>
          <h3 className="font-bold text-md-on-surface text-base flex items-center gap-2">
            <Crosshair className="w-5 h-5 text-md-primary" />
            CS2 GSI 游戏联动
          </h3>
          <p className="text-xs text-md-on-surface-variant mt-0.5">
            基于 Valve 官方 GSI 规范，实时捕获游戏内血量、护甲、C4 炸弹与击杀事件，并与键盘灯效硬件联动。
          </p>
        </div>

        <div className="flex items-center gap-2">
          <span className={`px-3 py-1.5 text-xs font-bold rounded-md-full border ${
            isConnected
              ? 'bg-md-primary-container text-md-on-primary-container border-md-primary shadow-xs'
              : 'bg-md-surface-container-high text-md-on-surface-variant border-md-outline-variant'
          }`}>
            {isConnected ? `GSI 遥测在线 (包: ${gsiCurrent?.packet_count || 0})` : '等待 CS2 游戏连接'}
          </span>
        </div>
      </div>

      {/* 前台窗口聚焦判定指示器 */}
      <div className={`p-4 rounded-md-lg border flex flex-col sm:flex-row sm:items-center justify-between gap-3 ${
        isCs2Foreground
          ? 'bg-md-primary-container border-md-primary text-md-on-primary-container'
          : 'bg-md-surface-container-high border-md-outline-variant text-md-on-surface'
      }`}>
        <div className="flex items-center gap-2.5 text-xs font-bold">
          <Gamepad2 className="w-4 h-4 shrink-0 text-md-primary" />
          <span>前台聚焦判定:</span>
          {isCs2Foreground ? (
            <span className="bg-md-primary text-md-on-primary px-2.5 py-1 rounded-md-full font-mono text-xs">
              cs2.exe（真实前台）
            </span>
          ) : (
            <span className="bg-md-surface-container-highest text-md-on-surface-variant border border-md-outline-variant px-2.5 py-1 rounded-md-full font-mono text-xs">
              {foregroundProc} (前台非 CS2，GSI 联动已隔离挂起)
            </span>
          )}
        </div>
        <span className="text-xs opacity-90 font-medium">
          {isCs2Foreground
            ? '当前遥测可供自动化规则使用；规则在工作室的自动化页面编辑'
            : '保护非游戏体验：切回 CS2 窗口时自动瞬间恢复光效'}
        </span>
      </div>

      {/* 遥测实况数据方块 (MD3E Cards) */}
      <div className="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-6 gap-3">
        <div className="p-3.5 bg-md-surface-container-low border border-md-outline-variant rounded-md-lg flex flex-col gap-1 shadow-sm">
          <span className="text-xs font-semibold text-md-on-surface-variant flex items-center gap-1.5">
            <Heart className="w-4 h-4 text-md-error" />
            生命值 (HP)
          </span>
          <span className="text-2xl font-mono font-black text-md-on-surface">
            {rawData['player_state.health'] !== undefined ? rawData['player_state.health'] : (rawData['player.state.health'] !== undefined ? rawData['player.state.health'] : '--')}
          </span>
        </div>

        <div className="p-3.5 bg-md-surface-container-low border border-md-outline-variant rounded-md-lg flex flex-col gap-1 shadow-sm">
          <span className="text-xs font-semibold text-md-on-surface-variant flex items-center gap-1.5">
            <Shield className="w-4 h-4 text-md-primary" />
            护甲值 (AP)
          </span>
          <span className="text-2xl font-mono font-black text-md-on-surface">
            {rawData['player_state.armor'] !== undefined ? rawData['player_state.armor'] : (rawData['player.state.armor'] !== undefined ? rawData['player.state.armor'] : '--')}
          </span>
        </div>

        <div className="p-3.5 bg-md-surface-container-low border border-md-outline-variant rounded-md-lg flex flex-col gap-1 shadow-sm">
          <span className="text-xs font-semibold text-md-on-surface-variant flex items-center gap-1.5">
            <Activity className="w-4 h-4 text-md-tertiary" />
            C4 炸弹状态
          </span>
          <span className="text-sm font-bold text-md-on-surface truncate mt-1">
            {rawData['round.bomb'] || '无'}
          </span>
        </div>

        <div className="p-3.5 bg-md-surface-container-low border border-md-outline-variant rounded-md-lg flex flex-col gap-1 shadow-sm">
          <span className="text-xs font-semibold text-md-on-surface-variant flex items-center gap-1.5">
            <Zap className="w-4 h-4 text-md-primary" />
            回合击杀
          </span>
          <span className="text-2xl font-mono font-black text-md-on-surface">
            {rawData['player_state.round_kills'] !== undefined ? rawData['player_state.round_kills'] : 0}
          </span>
        </div>

        <div className="p-3.5 bg-md-surface-container-low border border-md-outline-variant rounded-md-lg flex flex-col gap-1 shadow-sm">
          <span className="text-xs font-semibold text-md-on-surface-variant">地图 / 回合</span>
          <span className="text-xs font-bold text-md-on-surface truncate mt-1">
            {rawData['map.name'] || '未开始'} (R{rawData['map.round'] || 0})
          </span>
        </div>

        <div className="p-3.5 bg-md-surface-container-low border border-md-outline-variant rounded-md-lg flex flex-col gap-1 shadow-sm">
          <span className="text-xs font-semibold text-md-on-surface-variant">监听端口</span>
          <span className="text-xs font-mono font-bold text-md-on-surface truncate mt-1">
            127.0.0.1:19897
          </span>
        </div>
      </div>

      {/* 实时游戏事件流 */}
      {recentEvents.length > 0 && (
        <div className="p-4 bg-md-surface-container-low border border-md-outline-variant rounded-md-lg flex flex-col gap-2 shadow-sm">
          <div className="flex items-center justify-between">
            <span className="text-xs font-bold text-md-on-surface flex items-center gap-2">
              <Sparkles className="w-4 h-4 text-md-primary" />
              实时捕获事件流 (Recent Game Events)
            </span>
            <span className="text-xs text-md-on-surface-variant">自动由游戏状态机推导并驱动瞬时脉冲</span>
          </div>
          <div className="flex flex-wrap gap-2">
            {recentEvents.map((ev, i) => (
              <div key={i} className="flex items-center gap-2 px-3 py-1 bg-md-surface-container border border-md-outline-variant text-xs rounded-md-full">
                <span className="w-2 h-2 bg-md-primary rounded-md-full animate-ping" />
                <span className="font-bold text-md-on-surface">{ev.label}</span>
                <span className="text-xs text-md-on-surface-variant font-mono">{ev.time_str}</span>
                <span className="text-xs text-md-on-surface">{ev.desc}</span>
              </div>
            ))}
          </div>
        </div>
      )}

      {/* CFG 部署区 */}
      <div className="p-4 bg-md-surface-container-low border border-md-outline-variant rounded-md-lg flex flex-col gap-3 shadow-sm">
        <div className="flex items-center justify-between">
          <span className="text-xs font-bold text-md-on-surface flex items-center gap-2">
            <FolderCheck className="w-4 h-4 text-md-primary" />
            CS2 CFG 配置文件部署
          </span>
          <span className={`text-xs font-semibold px-2.5 py-1 rounded-md-full border ${
            cfgInfo?.installed
              ? 'bg-md-primary-container text-md-on-primary-container border-md-primary'
              : 'bg-md-surface-container-high text-md-on-surface-variant border-md-outline-variant'
          }`}>
            {cfgInfo?.installed ? '已部署 gamestate_integration_aura.cfg' : '未检测到配置文件'}
          </span>
        </div>

        <div className="flex flex-col sm:flex-row items-center gap-3">
          <div className="flex-1 flex items-center gap-2.5 px-3.5 py-2 bg-md-surface-container border border-md-outline rounded-md-sm w-full">
            <FolderOpen className="w-4 h-4 text-md-on-surface-variant shrink-0" />
            <input
              type="text"
              value={customPath}
              onChange={(e) => setCustomPath(e.target.value)}
              placeholder="CS2 cfg 目录路径..."
              className="w-full text-xs font-mono text-md-on-surface bg-transparent outline-none"
              aria-label="CS2 cfg 目录路径"
            />
          </div>

          <button
            type="button"
            onClick={handleInstallCfg}
            disabled={isInstalling}
            className="w-full sm:w-auto min-h-[48px] px-5 flex items-center justify-center gap-2 bg-md-primary hover:bg-md-primary/90 text-md-on-primary rounded-md-full border border-md-primary text-xs font-bold cursor-pointer active:scale-95 disabled:opacity-50 transition-all shrink-0 shadow-md-level1"
            aria-label="一键部署到 CS2 目录"
          >
            <Download className="w-4 h-4" />
            <span>{isInstalling ? '部署中...' : '一键部署到 CS2 目录'}</span>
          </button>
        </div>
      </div>

      {/* 全量遥测字典与实时探查表 */}
      <div className="flex flex-col gap-3 pt-3 border-t border-md-outline-variant">
        <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-3">
          <div>
            <h4 className="font-bold text-md-on-surface text-sm flex items-center gap-2">
              <Radio className="w-4 h-4 text-md-primary" />
              CS2 全量官方数据管道字典与实时探查
            </h4>
            <p className="text-xs text-md-on-surface-variant mt-0.5">
              直接捕获 Valve 官方上报的所有类别遥测数据，支持按分类检索与实时数值映射。
            </p>
          </div>

          <div className="flex items-center gap-2">
            <div className="flex items-center gap-2 min-h-[40px] px-3 bg-md-surface-container border border-md-outline rounded-md-full text-xs">
              <Search className="w-4 h-4 text-md-on-surface-variant" />
              <input
                type="text"
                placeholder="搜索字段、含义或代号..."
                value={searchTerm}
                onChange={(e) => setSearchTerm(e.target.value)}
                className="w-52 text-xs font-medium text-md-on-surface bg-transparent outline-none"
                aria-label="搜索字段"
              />
            </div>
          </div>
        </div>

        {/* 观战模式声明 */}
        <div className="p-4 bg-md-surface-container-high border border-md-outline-variant rounded-md-lg flex items-start gap-3 text-xs text-md-on-surface">
          <AlertTriangle className="w-5 h-5 text-md-primary shrink-0 mt-0.5" />
          <div className="leading-relaxed">
            <span className="font-bold">观战 / GOTV 模式专用字段说明：</span>
            <span>
              根据 Valve 官方反作弊与竞技公平性限制，正常竞技交火中不向客户端开放敌方位置及全场装备信息。
              部分字段仅在<strong>观战模式 (GOTV) 或观看回放 Demo</strong>时下发数据，正常对局中为空值属官方设计，绝非程序 Bug。
            </span>
          </div>
        </div>

        {/* 分类切换 Chips */}
        <div className="flex flex-wrap gap-2">
          {categories.map((cat) => (
            <button
              key={cat}
              type="button"
              onClick={() => setSelectedCategory(cat)}
              className={`min-h-[36px] px-3.5 py-1 text-xs font-semibold rounded-md-full border transition-colors cursor-pointer ${
                selectedCategory === cat
                  ? 'bg-md-secondary-container text-md-on-secondary-container border-md-secondary shadow-xs'
                  : 'bg-md-surface-container text-md-on-surface-variant border-md-outline-variant hover:bg-md-surface-container-high hover:text-md-on-surface'
              }`}
              aria-label={`分类: ${cat}`}
            >
              {cat}
            </button>
          ))}
        </div>

        {/* 全量字段表格 */}
        <div className="overflow-x-auto border border-md-outline-variant rounded-md-lg bg-md-surface-container-low max-h-96 overflow-y-auto shadow-md-level1">
          <table className="w-full text-left text-xs border-collapse">
            <thead className="sticky top-0 bg-md-surface-container-high border-b border-md-outline-variant z-10">
              <tr className="text-md-on-surface font-bold">
                <th className="py-3 px-4">字段路径 (GSI Key)</th>
                <th className="py-3 px-4">语义说明</th>
                <th className="py-3 px-4">取值范围 / 类型</th>
                <th className="py-3 px-4">当前上报实时值</th>
                <th className="py-3 px-4 text-right">快捷操作</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-md-outline-variant/60">
              {filteredDefinitions.length === 0 ? (
                <tr>
                  <td colSpan={5} className="py-8 text-center text-md-on-surface-variant font-medium">
                    未找到匹配的字段定义
                  </td>
                </tr>
              ) : (
                filteredDefinitions.map((item) => {
                  const liveVal = rawData[item.field];
                  const hasLiveVal = liveVal !== undefined;
                  return (
                    <tr key={item.field} className="hover:bg-md-surface-container/60 transition-colors">
                      <td className="py-3 px-4 font-mono font-bold text-md-on-surface">
                        <div className="flex items-center gap-2">
                          <span>{item.field}</span>
                          {item.spectator && (
                            <span className="text-[11px] bg-md-primary-container text-md-on-primary-container border border-md-primary px-2 py-0.5 rounded-md-full">
                              GOTV
                            </span>
                          )}
                        </div>
                      </td>
                      <td className="py-3 px-4">
                        <div className="flex flex-col">
                          <span className="font-bold text-md-on-surface">{item.label}</span>
                          <span className="text-xs text-md-on-surface-variant">{item.desc}</span>
                        </div>
                      </td>
                      <td className="py-3 px-4 text-md-on-surface-variant font-mono text-xs">
                        {item.range}
                      </td>
                      <td className="py-3 px-4">
                        {hasLiveVal ? (
                          <span className="font-mono font-bold px-2 py-1 bg-md-primary-container text-md-on-primary-container border border-md-primary rounded-md-sm text-xs">
                            {typeof liveVal === 'object' ? JSON.stringify(liveVal) : String(liveVal)}
                          </span>
                        ) : (
                          <span className="font-mono text-md-on-surface-variant text-xs">
                            {item.spectator ? '(需 GOTV 模式)' : '-- (等待游戏上报)'}
                          </span>
                        )}
                      </td>
                      <td className="py-3 px-4 text-right">

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
