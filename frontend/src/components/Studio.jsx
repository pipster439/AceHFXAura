import React, { useState, useMemo } from 'react';
import { 
  Sparkles, 
  GitBranch, 
  Plus, 
  Layers, 
  Play, 
  Square, 
  Copy, 
  Trash2, 
  Sliders, 
  Settings2, 
  Cpu, 
  FolderOpen, 
  CheckCircle2, 
  Radio, 
  ExternalLink,
  Search,
  Filter
} from 'lucide-react';
import EffectStudio from './EffectStudio';
import OrchestratorStudio from './OrchestratorStudio';
import KeyboardVisualizer from './KeyboardVisualizer';
import { getEffectLifecycleStatus, effectConfig } from '../utils/applyEffect';
import { EFFECT_PRESETS } from '../blockly/presets';
import { canonicalConfig } from '../utils/orchestration';

export default function Studio({
  config,
  onSaveConfig,
  currentProfileName,
  showToast,
  blocklyFrame,
  onPreviewFrameUpdate,
  currentEffect,
  isMasterLightOn,
  isAnalogEnabled,
  brightnessVal,
  speedIndex,
  currentDirection,
  thicknessVal,
  gradientStops,
  isStarryRandom,
  selectedKeyNames,
  onToggleKeySelection,
  bgColor,
  fpsVal,
  onSwitchToLegacyRules,
  onSwitchToLegacyGsi
}) {
  // 当前激活的作品类型：'effect' (光效) 或 'orchestration' (自动化)
  const [activeWorkType, setActiveWorkType] = useState('effect');
  const [activeEffectName, setActiveEffectName] = useState(() => {
    const keys = Object.keys(config?.blockly_effects || {});
    return keys[0] || 'custom_rainbow';
  });

  // 作品列表过滤器：'all' | 'effect' | 'orchestration'
  const [filterType, setFilterType] = useState('all');
  const [searchQuery, setSearchQuery] = useState('');

  // 新建作品模态框状态
  const [isNewModalOpen, setIsNewModalOpen] = useState(false);
  const [newEffectName, setNewEffectName] = useState('');
  const [newEffectTemplate, setNewEffectTemplate] = useState('blank');

  // 获取所有光效列表
  const effectKeys = useMemo(() => {
    return Object.keys(config?.blockly_effects || {});
  }, [config?.blockly_effects]);

  // 处理新建光效
  const handleCreateNewEffect = async (e) => {
    e.preventDefault();
    const clean = newEffectName.trim().toLowerCase().replace(/[^a-z0-9_]/g, '_');
    if (!clean) {
      showToast?.('请输入合法的光效名称', 'error');
      return;
    }
    if (config?.blockly_effects?.[clean]) {
      showToast?.('该光效名称已存在', 'error');
      return;
    }

    let initialJson = null;
    if (newEffectTemplate !== 'blank') {
      const preset = EFFECT_PRESETS.find(p => p.id === newEffectTemplate);
      if (preset?.blocklyJson) {
        initialJson = preset.blocklyJson;
      }
    }

    const nextConfig = effectConfig(config, clean, initialJson || { blocks: { languageVersion: 0, blocks: [] } });
    const ok = await onSaveConfig(nextConfig);
    if (ok) {
      showToast?.(`已新建光效草稿「${clean}」`, 'success');
      setActiveEffectName(clean);
      setActiveWorkType('effect');
      setIsNewModalOpen(false);
      setNewEffectName('');
    }
  };

  // 处理删除光效
  const handleDeleteEffect = async (name, e) => {
    e.stopPropagation();
    const c = canonicalConfig(config);
    if (
      c.default_profile === name || 
      c.orchestration?.fallback_profile === name || 
      c.orchestration?.rules?.some(r => r.target_profile === name) || 
      c.orchestration?.event_overlays?.some(r => r.effect === name)
    ) {
      showToast?.('此光效正在被方案或联动引用，请先更换引用后再删除', 'error');
      return;
    }

    if (!confirm(`确定要删除光效「${name}」吗？`)) return;

    const effects = { ...(config?.blockly_effects || {}) };
    const profiles = { ...(config?.profiles || {}) };
    delete effects[name];
    delete profiles[name];

    const nextConfig = { ...config, blockly_effects: effects, profiles };
    const ok = await onSaveConfig(nextConfig);
    if (ok) {
      showToast?.(`已删除光效: ${name}`, 'info');
      const remaining = Object.keys(effects);
      if (remaining.length > 0) {
        setActiveEffectName(remaining[0]);
      } else {
        setActiveEffectName('custom_rainbow');
      }
    }
  };

  // 处理克隆光效
  const handleCloneEffect = async (srcName, e) => {
    e.stopPropagation();
    const src = config?.blockly_effects?.[srcName];
    if (!src) return;
    const cloneName = `${srcName}_copy`;
    const nextConfig = effectConfig(config, cloneName, src.blockly_json);
    const ok = await onSaveConfig(nextConfig);
    if (ok) {
      showToast?.(`已克隆光效: ${cloneName}`, 'success');
      setActiveEffectName(cloneName);
      setActiveWorkType('effect');
    }
  };

  // 过滤后的列表项
  const filteredEffects = effectKeys.filter(k => 
    (!searchQuery || k.toLowerCase().includes(searchQuery.toLowerCase()))
  );

  return (
    <div className="flex h-[calc(100vh-100px)] w-full overflow-hidden gap-4 select-none">
      {/* 1. 左侧：作品列表导航轨 (Works List) */}
      <div className="w-72 shrink-0 flex flex-col bg-md-surface-container-low border border-md-outline-variant rounded-md-xl shadow-md-level1 overflow-hidden">
        {/* 头部标题与新建按钮 */}
        <div className="p-3 border-b border-md-outline-variant bg-md-surface-container/50 flex flex-col gap-2.5">
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-2">
              <div className="w-7 h-7 rounded-md-full bg-md-primary/15 text-md-primary flex items-center justify-center font-bold">
                <Layers className="w-4 h-4" />
              </div>
              <span className="font-bold text-xs text-md-on-surface">作品列表</span>
            </div>
            <button
              onClick={() => setIsNewModalOpen(true)}
              className="h-7 px-2.5 flex items-center gap-1 rounded-md-full bg-md-primary text-md-on-primary hover:bg-md-primary/90 text-xs font-bold transition-transform active:scale-95 cursor-pointer shadow-xs"
              title="新建光效草稿"
            >
              <Plus className="w-3.5 h-3.5" />
              <span>新建光效</span>
            </button>
          </div>

          {/* 搜索与类型过滤 */}
          <div className="flex flex-col gap-2">
            <div className="flex items-center gap-1.5 px-2.5 py-1 bg-md-surface-container border border-md-outline-variant rounded-md-md text-xs">
              <Search className="w-3.5 h-3.5 text-md-on-surface-variant shrink-0" />
              <input
                type="text"
                placeholder="搜索作品..."
                value={searchQuery}
                onChange={(e) => setSearchQuery(e.target.value)}
                className="w-full bg-transparent text-xs text-md-on-surface outline-none"
              />
            </div>

            <div className="flex items-center gap-1 p-0.5 bg-md-surface-container rounded-md-full text-xs font-semibold">
              {[
                { id: 'all', label: '全部' },
                { id: 'effect', label: '光效' },
                { id: 'orchestration', label: '自动化' }
              ].map(tab => (
                <button
                  key={tab.id}
                  onClick={() => setFilterType(tab.id)}
                  className={`flex-1 py-1 text-center rounded-md-full transition-all cursor-pointer text-[11px] ${
                    filterType === tab.id
                      ? 'bg-md-secondary-container text-md-on-secondary-container font-bold shadow-xs'
                      : 'text-md-on-surface-variant hover:text-md-on-surface'
                  }`}
                >
                  {tab.label}
                </button>
              ))}
            </div>
          </div>
        </div>

        {/* 作品项目列表 */}
        <div className="flex-1 overflow-y-auto p-2 space-y-1">
          {/* 自动化作品项 (Orchestration) */}
          {(filterType === 'all' || filterType === 'orchestration') && (
            <div
              onClick={() => setActiveWorkType('orchestration')}
              className={`group flex items-center justify-between p-2.5 rounded-md-lg border transition-all cursor-pointer ${
                activeWorkType === 'orchestration'
                  ? 'bg-md-primary/10 border-md-primary text-md-on-surface shadow-xs'
                  : 'bg-md-surface-container border-transparent hover:border-md-outline-variant/60 text-md-on-surface-variant hover:text-md-on-surface'
              }`}
            >
              <div className="flex items-center gap-2.5 min-w-0">
                <div className={`w-7 h-7 rounded-md-md flex items-center justify-center shrink-0 ${
                  activeWorkType === 'orchestration' ? 'bg-md-primary text-md-on-primary' : 'bg-md-surface-container-high text-md-on-surface-variant'
                }`}>
                  <GitBranch className="w-4 h-4" />
                </div>
                <div className="flex flex-col min-w-0">
                  <span className="font-bold text-xs truncate">主联动规则编排</span>
                  <div className="flex items-center gap-1.5 mt-0.5">
                    <span className="text-[10px] px-1.5 py-0.2 rounded bg-md-secondary/15 text-md-secondary font-semibold">
                      自动化
                    </span>
                    <span className="text-[10px] text-emerald-400 font-semibold">
                      ● 运行中
                    </span>
                  </div>
                </div>
              </div>
            </div>
          )}

          {/* 光效作品列表 (Effects) */}
          {(filterType === 'all' || filterType === 'effect') && (
            <>
              {filteredEffects.length === 0 ? (
                <div className="p-4 text-center text-xs text-md-on-surface-variant">
                  暂无匹配的光效作品
                </div>
              ) : (
                filteredEffects.map((name) => {
                  const effectData = config?.blockly_effects?.[name];
                  const lifecycle = getEffectLifecycleStatus(effectData);
                  const isSelected = activeWorkType === 'effect' && activeEffectName === name;

                  return (
                    <div
                      key={name}
                      onClick={() => {
                        setActiveEffectName(name);
                        setActiveWorkType('effect');
                      }}
                      className={`group flex items-center justify-between p-2.5 rounded-md-lg border transition-all cursor-pointer ${
                        isSelected
                          ? 'bg-md-primary/10 border-md-primary text-md-on-surface shadow-xs'
                          : 'bg-md-surface-container border-transparent hover:border-md-outline-variant/60 text-md-on-surface-variant hover:text-md-on-surface'
                      }`}
                    >
                      <div className="flex items-center gap-2.5 min-w-0 flex-1">
                        <div className={`w-7 h-7 rounded-md-md flex items-center justify-center shrink-0 ${
                          isSelected ? 'bg-md-primary text-md-on-primary' : 'bg-md-surface-container-high text-md-on-surface-variant'
                        }`}>
                          <Sparkles className="w-4 h-4" />
                        </div>
                        <div className="flex flex-col min-w-0 flex-1">
                          <span className="font-bold text-xs truncate" title={name}>{name}</span>
                          <div className="flex items-center gap-1.5 mt-0.5">
                            <span className="text-[10px] px-1.5 py-0.2 rounded bg-md-surface-container-highest text-md-on-surface-variant font-semibold">
                              光效
                            </span>
                            <span className={`text-[10px] px-1.5 py-0.2 rounded border font-semibold ${lifecycle.badgeClass}`}>
                              {lifecycle.label}
                            </span>
                          </div>
                        </div>
                      </div>

                      {/* 悬停快捷按钮 */}
                      <div className="flex items-center gap-1 opacity-0 group-hover:opacity-100 transition-opacity">
                        <button
                          type="button"
                          onClick={(e) => handleCloneEffect(name, e)}
                          className="w-6 h-6 rounded flex items-center justify-center hover:bg-md-surface-container-high text-md-on-surface-variant hover:text-md-on-surface"
                          title="克隆此光效"
                        >
                          <Copy className="w-3 h-3" />
                        </button>
                        <button
                          type="button"
                          onClick={(e) => handleDeleteEffect(name, e)}
                          className="w-6 h-6 rounded flex items-center justify-center hover:bg-md-error-container/40 text-md-on-surface-variant hover:text-md-error"
                          title="删除此光效"
                        >
                          <Trash2 className="w-3 h-3" />
                        </button>
                      </div>
                    </div>
                  );
                })
              )}
            </>
          )}
        </div>

        {/* 底部兼容设置快捷入口 */}
        <div className="p-2 border-t border-md-outline-variant bg-md-surface-container/30 flex flex-col gap-1 text-xs">
          <div className="flex items-center justify-between text-xs text-md-on-surface-variant px-1 font-semibold">
            <span>高级与兼容功能</span>
          </div>
          <div className="flex items-center gap-1.5">
            {onSwitchToLegacyRules && (
              <button
                type="button"
                onClick={onSwitchToLegacyRules}
                className="flex-1 py-1 px-2 rounded-md-sm bg-md-surface-container hover:bg-md-surface-container-high text-[11px] text-md-on-surface border border-md-outline-variant text-center transition-colors cursor-pointer truncate"
                title="打开传统进程规则表格"
              >
                传统规则表
              </button>
            )}
            {onSwitchToLegacyGsi && (
              <button
                type="button"
                onClick={onSwitchToLegacyGsi}
                className="flex-1 py-1 px-2 rounded-md-sm bg-md-surface-container hover:bg-md-surface-container-high text-[11px] text-md-on-surface border border-md-outline-variant text-center transition-colors cursor-pointer truncate"
                title="打开 CS2 官方 GSI 数据诊断与 CFG 部署"
              >
                CS2 遥测诊断
              </button>
            )}
          </div>
        </div>
      </div>

      {/* 2. 中间：Blockly 核心编辑区 (Center Canvas) */}
      <div className="flex-1 flex flex-col h-full min-w-0 overflow-hidden">
        {activeWorkType === 'effect' ? (
          <EffectStudio
            config={config}
            onSaveConfig={onSaveConfig}
            showToast={showToast}
            onPreviewFrameUpdate={onPreviewFrameUpdate}
            activeEffectName={activeEffectName}
            onEffectNameChange={(name) => setActiveEffectName(name)}
          />
        ) : (
          <OrchestratorStudio
            config={config}
            onSaveConfig={onSaveConfig}
            profiles={config?.profiles}
            currentProfileName={currentProfileName}
            showToast={showToast}
            onSwitchToLegacyRules={onSwitchToLegacyRules}
          />
        )}
      </div>

      {/* 3. 右侧：键盘预览与调试状态工作台 (Right Preview & Debug Station) */}
      <div className="w-84 shrink-0 flex flex-col bg-md-surface-container-low border border-md-outline-variant rounded-md-xl shadow-md-level1 p-3 gap-3 overflow-y-auto">
        {/* 顶部标题与状态指示 */}
        <div className="flex items-center justify-between pb-2 border-b border-md-outline-variant">
          <div className="flex items-center gap-2">
            <Radio className="w-4 h-4 text-md-primary" />
            <span className="font-bold text-xs text-md-on-surface">键盘实时推流预览</span>
          </div>
          <span className="text-[11px] font-mono px-2 py-0.5 rounded-full bg-md-primary/10 text-md-primary font-bold">
            {fpsVal || 25} FPS
          </span>
        </div>

        {/* 键盘实时微型舞台 (自适应容器宽度) */}
        <div className="flex flex-col items-center justify-center p-1 bg-black/20 rounded-md-lg border border-md-outline-variant/60 overflow-hidden">
          <KeyboardVisualizer
            activeTab="blockly_effect"
            currentEffect={currentEffect}
            isMasterLightOn={isMasterLightOn}
            isAnalogEnabled={isAnalogEnabled}
            brightnessVal={brightnessVal}
            speedIndex={speedIndex}
            currentDirection={currentDirection}
            thicknessVal={thicknessVal}
            gradientStops={gradientStops}
            isStarryRandom={isStarryRandom}
            currentProfile={config?.profiles?.[currentProfileName]}
            selectedKeyNames={selectedKeyNames}
            onToggleKeySelection={onToggleKeySelection}
            bgColor={bgColor}
            fpsVal={fpsVal}
            blocklyFrame={blocklyFrame}
          />
        </div>

        {/* 当前作品信息小结 */}
        <div className="p-2.5 bg-md-surface-container rounded-md-md border border-md-outline-variant flex flex-col gap-1.5 text-xs">
          <div className="flex items-center justify-between">
            <span className="text-md-on-surface-variant font-medium">当前作品:</span>
            <span className="font-mono font-bold text-md-on-surface truncate">
              {activeWorkType === 'effect' ? activeEffectName : '主联动规则'}
            </span>
          </div>
          <div className="flex items-center justify-between">
            <span className="text-md-on-surface-variant font-medium">作品类型:</span>
            <span className="font-semibold text-md-primary">
              {activeWorkType === 'effect' ? '独立光效 (Effect)' : '状态联动 (Orchestration)'}
            </span>
          </div>
          {activeWorkType === 'effect' && (
            <div className="flex items-center justify-between">
              <span className="text-md-on-surface-variant font-medium">当前状态:</span>
              {(() => {
                const lc = getEffectLifecycleStatus(config?.blockly_effects?.[activeEffectName]);
                return (
                  <span className={`px-2 py-0.5 rounded border text-[11px] font-bold ${lc.badgeClass}`}>
                    {lc.label}
                  </span>
                );
              })()}
            </div>
          )}
        </div>

        {/* 调试说明与提示 */}
        <div className="p-3 bg-md-surface-container-lowest border border-md-outline-variant rounded-md-md flex flex-col gap-1.5 text-[11px] text-md-on-surface-variant leading-relaxed">
          <div className="flex items-center gap-1.5 font-bold text-md-on-surface">
            <CheckCircle2 className="w-3.5 h-3.5 text-md-primary" />
            <span>实时调试提示</span>
          </div>
          <p>
            • 在中间 Blockly 中拼装积木，右侧虚拟键盘将以 ~25 FPS 保持帧同步。
          </p>
          <p>
            • 点击「保存草稿」记录源码；点击「发布」转译为原生动态链接库，直接推送到硬件。
          </p>
        </div>
      </div>

      {/* 新建光效模态弹窗 */}
      {isNewModalOpen && (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 backdrop-blur-xs p-4">
          <div className="w-full max-w-md bg-md-surface-container border border-md-outline-variant rounded-md-xl shadow-md-level3 overflow-hidden animate-in fade-in zoom-in-95 duration-150">
            <div className="p-4 border-b border-md-outline-variant bg-md-surface-container-high flex items-center justify-between">
              <div className="flex items-center gap-2">
                <Sparkles className="w-4 h-4 text-md-primary" />
                <span className="font-bold text-sm text-md-on-surface">新建光效草稿</span>
              </div>
              <button
                onClick={() => setIsNewModalOpen(false)}
                className="text-xs text-md-on-surface-variant hover:text-md-on-surface cursor-pointer"
              >
                取消
              </button>
            </div>

            <form onSubmit={handleCreateNewEffect} className="p-4 flex flex-col gap-4">
              <div className="flex flex-col gap-1.5">
                <label className="text-xs font-semibold text-md-on-surface">光效标识名称 (ID):</label>
                <input
                  type="text"
                  placeholder="如: my_wave_effect"
                  value={newEffectName}
                  onChange={(e) => setNewEffectName(e.target.value)}
                  className="h-9 px-3 bg-md-surface-container-lowest border border-md-outline rounded-md-sm text-xs text-md-on-surface font-mono outline-none focus:border-md-primary focus:ring-1 focus:ring-md-primary"
                  autoFocus
                />
                <span className="text-[11px] text-md-on-surface-variant">仅限英文字母、数字和下划线</span>
              </div>

              <div className="flex flex-col gap-1.5">
                <label className="text-xs font-semibold text-md-on-surface">初始模板样例:</label>
                <select
                  value={newEffectTemplate}
                  onChange={(e) => setNewEffectTemplate(e.target.value)}
                  className="h-9 px-2 bg-md-surface-container-lowest border border-md-outline rounded-md-sm text-xs text-md-on-surface cursor-pointer outline-none"
                >
                  <option value="blank">空白画布 (从零开始拼装)</option>
                  {EFFECT_PRESETS.map(p => (
                    <option key={p.id} value={p.id}>{p.name}</option>
                  ))}
                </select>
              </div>

              <div className="flex justify-end gap-2 pt-2 border-t border-md-outline-variant">
                <button
                  type="button"
                  onClick={() => setIsNewModalOpen(false)}
                  className="h-8 px-4 rounded-md-full border border-md-outline text-xs text-md-on-surface hover:bg-md-surface-container-high cursor-pointer"
                >
                  取消
                </button>
                <button
                  type="submit"
                  disabled={!newEffectName.trim()}
                  className="h-8 px-5 rounded-md-full bg-md-primary text-md-on-primary hover:bg-md-primary/90 text-xs font-bold transition-all disabled:opacity-50 cursor-pointer shadow-xs"
                >
                  确认新建
                </button>
              </div>
            </form>
          </div>
        </div>
      )}
    </div>
  );
}
