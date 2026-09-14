import React, { useState, useEffect, useMemo } from 'react';
import {
  Activity,
  Crosshair,
  Shield,
  Heart,
  Bomb,
  Layers,
  Sliders,
  Clock
} from 'lucide-react';

import { evalCondition, evaluateOverlayStatus } from '../utils/orchestration';

export { evalCondition, evaluateOverlayStatus };

export default function OrchestrationInspector({
  config,
  currentProfileName
}) {
  const [liveGsi, setLiveGsi] = useState(null);
  const [isDaemonOnline, setIsDaemonOnline] = useState(true);
  const [isSimMode, setIsSimMode] = useState(false);

  // 模拟状态
  const [simProc, setSimProc] = useState('cs2.exe');
  const [simHealth, setSimHealth] = useState(100);
  const [simArmor, setSimArmor] = useState(100);
  const [simBomb, setSimBomb] = useState('carried');
  const [simPhase, setSimPhase] = useState('live');
  const [simKills, setSimKills] = useState(0);
  const [recentSimEvent, setRecentSimEvent] = useState(null);

  // 轮询 GSI 遥测
  useEffect(() => {
    let unmounted = false;
    const poll = async () => {
      try {
        const res = await fetch('/api/gsi/current', { cache: 'no-store' });
        if (res.ok) {
          const data = await res.json();
          if (!unmounted) {
            setLiveGsi(data);
            setIsDaemonOnline(true);
          }
        } else {
          if (!unmounted) setIsDaemonOnline(false);
        }
      } catch (err) {
        if (!unmounted) setIsDaemonOnline(false);
      }
    };

    poll();
    const interval = setInterval(poll, 1200);
    return () => {
      unmounted = true;
      clearInterval(interval);
    };
  }, []);

  // 当前有效的前台进程与 GSI 键值字典
  const effectiveProcess = isSimMode ? simProc : (liveGsi?.foreground_process || 'explorer.exe');
  const isGsiConnected = isSimMode ? true : Boolean(liveGsi?.connected);

  const effectiveGsi = useMemo(() => {
    if (isSimMode) {
      return {
        'player.state.health': simHealth,
        'player.state.armor': simArmor,
        'round.bomb': simBomb,
        'round.phase': simPhase,
        'player.state.round_kills': simKills
      };
    }
    return liveGsi?.data || {};
  }, [isSimMode, simHealth, simArmor, simBomb, simPhase, simKills, liveGsi]);

  // 计算当前规则匹配
  const { matchedRule, activeProfile } = useMemo(() => {
    const rules = config?.orchestration?.rules || [];
    const fallback = config?.orchestration?.fallback_profile || config?.default_profile || 'desktop';

    for (let i = 0; i < rules.length; i++) {
      const r = rules[i];
      if (r.enabled === false) continue;

      let procMatches = true;
      if (r.process && r.process.trim() !== '') {
        procMatches = r.process.toLowerCase() === effectiveProcess.toLowerCase();
      }

      let condMatches = true;
      if (r.condition && Object.keys(r.condition).length > 0) {
        condMatches = evalCondition(r.condition, effectiveProcess, effectiveGsi);
      }

      if (procMatches && condMatches) {
        return {
          matchedRule: { ...r, index: i },
          activeProfile: r.target_profile || fallback
        };
      }
    }

    return { matchedRule: null, activeProfile: fallback };
  }, [config, effectiveProcess, effectiveGsi]);

  // 计算当前叠加层状态
  const overlayStatuses = useMemo(() => {
    const overlays = config?.orchestration?.event_overlays || [];
    return overlays.map((ov, idx) => {
      const { isActive, reason } = evaluateOverlayStatus(ov, {
        isSimMode,
        recentSimEvent,
        liveGsi,
        effectiveProcess,
        effectiveGsi
      });

      return {
        ...ov,
        index: idx,
        isActive,
        reason
      };
    });
  }, [config, effectiveProcess, effectiveGsi, isSimMode, recentSimEvent, liveGsi]);

  // 模拟触发突发事件
  const handleTriggerSimEvent = (eventName) => {
    setRecentSimEvent(eventName);
    setTimeout(() => {
      setRecentSimEvent(null);
    }, 1200);
  };

  return (
    <div className="flex flex-col h-full gap-3 text-xs select-none">
      {/* 顶部标题与状态指示 */}
      <div className="flex items-center justify-between pb-2 border-b border-md-outline-variant">
        <div className="flex items-center gap-2">
          <Activity className="w-4 h-4 text-md-primary animate-pulse" />
          <span className="font-bold text-xs text-md-on-surface">自动化遥测与状态监视</span>
        </div>
        <div className="flex items-center gap-1.5">
          <span className={`text-[10px] px-2 py-0.5 rounded-full font-bold border ${
            isDaemonOnline
              ? 'bg-emerald-500/15 border-emerald-500/40 text-emerald-400'
              : 'bg-md-error-container/40 border-md-error text-md-error'
          }`}>
            {isDaemonOnline ? 'Daemon 在线' : 'Daemon 离线'}
          </span>
          <span className={`text-[10px] px-2 py-0.5 rounded-full font-bold border ${
            isGsiConnected
              ? 'bg-cyan-500/15 border-cyan-500/40 text-cyan-400'
              : 'bg-md-surface-container border-md-outline-variant text-md-on-surface-variant'
          }`}>
            {isGsiConnected ? 'GSI 活跃' : 'GSI 离线'}
          </span>
        </div>
      </div>

      {/* 模式切换栏: 实机遥测 vs 模拟调试 */}
      <div className="flex items-center gap-1 p-0.5 bg-md-surface-container rounded-md-full text-[11px] font-semibold border border-md-outline-variant/60">
        <button
          type="button"
          onClick={() => setIsSimMode(false)}
          className={`flex-1 py-1 text-center rounded-md-full transition-all cursor-pointer ${
            !isSimMode
              ? 'bg-md-primary text-md-on-primary font-bold shadow-xs'
              : 'text-md-on-surface-variant hover:text-md-on-surface'
          }`}
        >
          实机遥测模式
        </button>
        <button
          type="button"
          onClick={() => setIsSimMode(true)}
          className={`flex-1 py-1 text-center rounded-md-full transition-all cursor-pointer ${
            isSimMode
              ? 'bg-md-primary text-md-on-primary font-bold shadow-xs'
              : 'text-md-on-surface-variant hover:text-md-on-surface'
          }`}
        >
          模拟调试模式
        </button>
      </div>

      {/* 卡片 1: 前台进程与基础方案匹配 */}
      <div className="p-3 bg-md-surface-container rounded-md-lg border border-md-outline-variant flex flex-col gap-2 shadow-xs">
        <div className="flex items-center justify-between text-md-on-surface font-bold">
          <div className="flex items-center gap-1.5">
            <Crosshair className="w-3.5 h-3.5 text-md-primary" />
            <span>前台进程与基础方案</span>
          </div>
          <span className="font-mono text-[11px] px-2 py-0.5 rounded bg-md-surface-container-high border border-md-outline-variant text-md-primary font-bold">
            {effectiveProcess}
          </span>
        </div>

        {isSimMode && (
          <div className="flex items-center gap-1.5 pt-1">
            <span className="text-[11px] text-md-on-surface-variant shrink-0">模拟进程:</span>
            <input
              type="text"
              value={simProc}
              onChange={(e) => setSimProc(e.target.value.trim())}
              placeholder="如 cs2.exe"
              className="flex-1 h-6 px-2 bg-md-surface-container-lowest border border-md-outline rounded text-[11px] font-mono text-md-on-surface outline-none"
            />
          </div>
        )}

        <div className="pt-2 border-t border-md-outline-variant/60 flex flex-col gap-1 text-[11px]">
          <div className="flex items-center justify-between">
            <span className="text-md-on-surface-variant">评估结果方案:</span>
            <span className="font-bold text-emerald-400 font-mono">
              {activeProfile}
            </span>
          </div>
          <div className="flex items-center justify-between">
            <span className="text-md-on-surface-variant">规则命中:</span>
            <span className="text-md-on-surface truncate">
              {matchedRule ? `规则 #${matchedRule.index + 1} (${matchedRule.id || matchedRule.process || '条件匹配'})` : '无规则命中 (使用兜底方案)'}
            </span>
          </div>
          <div className="flex items-center justify-between text-md-on-surface-variant/80">
            <span>兜底方案 (Fallback):</span>
            <span className="font-mono">{config?.orchestration?.fallback_profile || config?.default_profile || 'desktop'}</span>
          </div>
        </div>
      </div>

      {/* 卡片 2: 激活中的叠加层 (Overlays) */}
      <div className="p-3 bg-md-surface-container rounded-md-lg border border-md-outline-variant flex flex-col gap-2 shadow-xs">
        <div className="flex items-center justify-between text-md-on-surface font-bold">
          <div className="flex items-center gap-1.5">
            <Layers className="w-3.5 h-3.5 text-md-secondary" />
            <span>叠加层监视 (Overlays)</span>
          </div>
          <span className="text-[10px] px-1.5 py-0.5 rounded bg-md-secondary/15 text-md-secondary font-bold">
            {overlayStatuses.filter(o => o.isActive).length} / {overlayStatuses.length} 激活
          </span>
        </div>

        <div className="space-y-1.5 max-h-36 overflow-y-auto">
          {overlayStatuses.length === 0 ? (
            <div className="p-2 text-center text-md-on-surface-variant text-[11px]">
              尚未配置任何事件或条件叠加层
            </div>
          ) : (
            overlayStatuses.map((ov) => (
              <div
                key={ov.index}
                className={`p-2 rounded border flex items-center justify-between gap-2 transition-all ${
                  ov.isActive
                    ? 'bg-emerald-500/10 border-emerald-500/50 text-md-on-surface shadow-xs'
                    : 'bg-md-surface-container-high/40 border-md-outline-variant/40 text-md-on-surface-variant'
                }`}
              >
                <div className="flex flex-col min-w-0 flex-1">
                  <div className="flex items-center gap-1.5">
                    <span className="font-bold text-[11px] truncate">{ov.effect}</span>
                    <span className={`text-[9px] px-1 rounded font-mono ${
                      ov.trigger === 'state' ? 'bg-indigo-500/20 text-indigo-300' : 'bg-amber-500/20 text-amber-300'
                    }`}>
                      {ov.trigger === 'state' ? '持续条件' : '瞬态事件'}
                    </span>
                  </div>
                  <span className="text-[10px] text-md-on-surface-variant mt-0.5 truncate">
                    {ov.reason} (优先级 {ov.priority || 10})
                  </span>
                </div>

                {ov.trigger !== 'state' && isSimMode && (
                  <button
                    type="button"
                    onClick={() => handleTriggerSimEvent(ov.event)}
                    className="h-6 px-2 rounded-md-full bg-md-primary/15 text-md-primary hover:bg-md-primary/25 border border-md-primary/30 text-[10px] font-bold cursor-pointer shrink-0 active:scale-95 transition-all"
                  >
                    测试触发
                  </button>
                )}
              </div>
            ))
          )}
        </div>
      </div>

      {/* 卡片 3: GSI 遥测调试面板 (Health, Armor, Bomb, Phase, Kills) */}
      <div className="p-3 bg-md-surface-container rounded-md-lg border border-md-outline-variant flex flex-col gap-2.5 shadow-xs">
        <div className="flex items-center justify-between text-md-on-surface font-bold">
          <div className="flex items-center gap-1.5">
            <Sliders className="w-3.5 h-3.5 text-md-tertiary" />
            <span>{isSimMode ? 'GSI 模拟调试滑块' : '当前 GSI 遥测数值'}</span>
          </div>
          {isSimMode && (
            <span className="text-[10px] text-amber-400 font-bold">● 实时交互中</span>
          )}
        </div>

        {/* 血量 */}
        <div className="flex flex-col gap-1">
          <div className="flex items-center justify-between text-[11px]">
            <span className="flex items-center gap-1 text-md-on-surface-variant">
              <Heart className="w-3 h-3 text-red-400" />
              <span>玩家血量 (health):</span>
            </span>
            <span className={`font-mono font-bold ${
              (effectiveGsi['player.state.health'] ?? 100) < 25 ? 'text-red-400' : 'text-emerald-400'
            }`}>
              {effectiveGsi['player.state.health'] ?? 100} HP
            </span>
          </div>
          {isSimMode && (
            <input
              type="range"
              min="0"
              max="100"
              value={simHealth}
              onChange={(e) => setSimHealth(Number(e.target.value))}
              className="w-full h-1.5 accent-md-primary cursor-pointer"
            />
          )}
        </div>

        {/* 护甲 */}
        <div className="flex flex-col gap-1">
          <div className="flex items-center justify-between text-[11px]">
            <span className="flex items-center gap-1 text-md-on-surface-variant">
              <Shield className="w-3 h-3 text-blue-400" />
              <span>护甲值 (armor):</span>
            </span>
            <span className="font-mono font-bold text-blue-400">
              {effectiveGsi['player.state.armor'] ?? 100}
            </span>
          </div>
          {isSimMode && (
            <input
              type="range"
              min="0"
              max="100"
              value={simArmor}
              onChange={(e) => setSimArmor(Number(e.target.value))}
              className="w-full h-1.5 accent-md-primary cursor-pointer"
            />
          )}
        </div>

        {/* C4 炸弹状态 */}
        <div className="flex items-center justify-between text-[11px]">
          <span className="flex items-center gap-1 text-md-on-surface-variant">
            <Bomb className="w-3 h-3 text-amber-400" />
            <span>C4 状态 (round.bomb):</span>
          </span>
          {isSimMode ? (
            <select
              value={simBomb}
              onChange={(e) => setSimBomb(e.target.value)}
              className="h-6 px-1.5 bg-md-surface-container-lowest border border-md-outline rounded text-[10px] text-md-on-surface font-medium outline-none cursor-pointer"
            >
              <option value="carried">carried (随身)</option>
              <option value="planted">planted (已安放)</option>
              <option value="dropped">dropped (掉落)</option>
              <option value="defused">defused (已拆除)</option>
              <option value="exploded">exploded (已引爆)</option>
            </select>
          ) : (
            <span className="font-mono font-bold text-amber-400">
              {effectiveGsi['round.bomb'] || '无'}
            </span>
          )}
        </div>

        {/* 回合阶段与击杀 */}
        <div className="flex items-center justify-between text-[11px]">
          <span className="flex items-center gap-1 text-md-on-surface-variant">
            <Clock className="w-3 h-3 text-md-primary" />
            <span>阶段 (phase):</span>
          </span>
          {isSimMode ? (
            <select
              value={simPhase}
              onChange={(e) => setSimPhase(e.target.value)}
              className="h-6 px-1.5 bg-md-surface-container-lowest border border-md-outline rounded text-[10px] text-md-on-surface font-medium outline-none cursor-pointer"
            >
              <option value="live">live (进行中)</option>
              <option value="freezetime">freezetime (冻结)</option>
              <option value="warmup">warmup (热身)</option>
              <option value="over">over (结束)</option>
            </select>
          ) : (
            <span className="font-mono font-semibold text-md-on-surface">
              {effectiveGsi['round.phase'] || 'live'}
            </span>
          )}
        </div>

        {isSimMode && (
          <div className="pt-2 border-t border-md-outline-variant/60 flex items-center justify-between">
            <span className="text-[11px] text-md-on-surface-variant">击杀数测试:</span>
            <div className="flex items-center gap-2">
              <span className="font-mono font-bold text-md-primary">{simKills} 杀</span>
              <button
                type="button"
                onClick={() => {
                  setSimKills(k => k + 1);
                  handleTriggerSimEvent('event.kill');
                }}
                className="h-6 px-2.5 rounded bg-md-primary text-md-on-primary font-bold text-[10px] cursor-pointer active:scale-95 shadow-xs"
              >
                +1 击杀并触发
              </button>
            </div>
          </div>
        )}
      </div>

      {/* 底部信息与运行时协议 */}
      <div className="p-2.5 bg-md-surface-container-lowest border border-md-outline-variant rounded-md-md flex flex-col gap-1 text-[10px] text-md-on-surface-variant">
        <div className="flex items-center justify-between">
          <span>规则引擎架构:</span>
          <span className="font-mono font-bold text-md-primary">orchestration.version = 2</span>
        </div>
        <div className="flex items-center justify-between">
          <span>模式说明:</span>
          <span>自动化模式仅展示规则评估，不复用独立光效推流帧</span>
        </div>
      </div>
    </div>
  );
}
