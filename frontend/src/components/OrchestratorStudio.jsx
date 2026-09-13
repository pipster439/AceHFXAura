import { studioExample } from '../blockly/studioExample.js';
import { stageEffect, effectConfig } from '../utils/applyEffect.js';
import { canonicalConfig } from '../utils/orchestration.js';
import React, { useState, useEffect, useRef } from 'react';
import Blockly, { loadSafeWorkspaceJson } from '../blockly/index.js';
import { registerCustomBlocks } from '../blockly/customBlocks';
import { DEFAULT_INJECT_OPTIONS } from '../blockly/theme';
import { ORCHESTRATOR_STUDIO_TOOLBOX } from '../blockly/toolboxes';
import { OrchestratorSerializer } from '../blockly/orchestratorSerializer';
import { ORCHESTRATOR_PRESETS } from '../blockly/presets';
import { CppTranspiler } from '../blockly/cppTranspiler';
import { 
  GitBranch, 
  Layers, 
  Zap, 
  Save, 
  Eye, 
  FileJson, 
  Check, 
  Plus, 
  RotateCcw,
  Workflow,
  AlertTriangle,
  Cpu,
  RefreshCw,
  Sparkles
} from 'lucide-react';

export default function OrchestratorStudio({
  config,
  onSaveConfig,
  profiles,
  currentProfileName,
  showToast,
  onSwitchToLegacyRules
}) {
  const blocklyDivRef = useRef(null);
  const workspaceRef = useRef(null);
  const [orchestrationData, setOrchestrationData] = useState(null);
  const [isJsonModalOpen, setIsJsonModalOpen] = useState(false);
  const [isCopied, setIsCopied] = useState(false);
  const [isBatchCompiling, setIsBatchCompiling] = useState(false);
  const [editError, setEditError] = useState(null);
  const [exampleAssets, setExampleAssets] = useState(null);
  const configRef = useRef(config);
  configRef.current = config;
  const assetsRef = useRef(exampleAssets);
  assetsRef.current = exampleAssets;
  const draftBaseRef = useRef(JSON.stringify(config?.orchestration || { rules: config?.rules, gsi_bindings: config?.gsi_bindings }));

  // 初始化 Blockly 画布
  useEffect(() => {
    registerCustomBlocks();
    if (!blocklyDivRef.current) return;

    const ws = Blockly.inject(blocklyDivRef.current, {
      ...DEFAULT_INJECT_OPTIONS,
      toolbox: ORCHESTRATOR_STUDIO_TOOLBOX
    });
    workspaceRef.current = ws;

    // 恢复积木状态或从现有 config 自动迁移
    let draft;
    try { draft = JSON.parse(sessionStorage.getItem('aura-orchestration-draft')); } catch {}
    if (draft?.base === draftBaseRef.current && draft?.json) {
      loadSafeWorkspaceJson(draft.json, ws); setExampleAssets(draft.assets);
    } else OrchestratorSerializer.restoreWorkspace(ws, canonicalConfig(config));

    const onWorkspaceChange = (event) => {
      if (event?.isUiEvent) return;
      try {
        setOrchestrationData(OrchestratorSerializer.serializeWorkspace(ws, configRef.current?.orchestration?.fallback_profile || configRef.current?.default_profile || 'desktop'));
        setEditError(null);
      } catch (err) { setEditError(err.message); }
    };

    ws.addChangeListener(onWorkspaceChange);
    onWorkspaceChange();

    const handleResize = () => Blockly.svgResize(ws);
    window.addEventListener('resize', handleResize);

    return () => {
      window.removeEventListener('resize', handleResize);
      try { sessionStorage.setItem('aura-orchestration-draft', JSON.stringify({ base: draftBaseRef.current, json: Blockly.serialization.workspaces.save(ws), assets: assetsRef.current })); } catch {}
      ws.dispose();
      workspaceRef.current = null;
    };
  }, []);

  // 加载预设模板
  const handleLoadPreset = (preset) => {
    if (!workspaceRef.current || !preset.blocklyJson) return;
    setExampleAssets(null);
    workspaceRef.current.clear();
    loadSafeWorkspaceJson(preset.blocklyJson, workspaceRef.current);
    showToast?.(`已加载预设编排: ${preset.name}`, 'info');
  };

  // Compile all referenced drafts before committing the authoritative rule set.
  const handleSaveOrchestration = async () => {
    if (!workspaceRef.current || isBatchCompiling) return;
    setIsBatchCompiling(true);
    try {
      const serialized = OrchestratorSerializer.serializeWorkspace(workspaceRef.current, config?.orchestration?.fallback_profile || config?.default_profile || 'desktop');
      let next = canonicalConfig(config);
      if (exampleAssets) next = { ...next, profiles: { ...next.profiles, ...exampleAssets.profiles }, blockly_effects: { ...next.blockly_effects, ...exampleAssets.effects } };
      const references = new Set([serialized.orchestration.fallback_profile, ...serialized.orchestration.rules.map(r => r.target_profile), ...serialized.orchestration.event_overlays.map(r => r.effect)]);
      for (const name of references) {
        const draft = next.blockly_effects?.[name];
        if (draft?.blockly_json) {
          const ws = new Blockly.Workspace();
          try {
            loadSafeWorkspaceJson(draft.blockly_json, ws, true);
            const build = await stageEffect(name, ws, CppTranspiler);
            next = effectConfig(next, name, draft.blockly_json, build);
          } finally { ws.dispose(); }
        } else if (!next.profiles?.[name]) throw new Error(`光效“${name}”不存在，请先制作或选择已有光效`);
      }
      next = { ...next, ...serialized };
      if (!await onSaveConfig(next)) throw new Error('联动配置保存失败，原有配置仍保留');
      draftBaseRef.current = JSON.stringify(next.orchestration);
      showToast?.('联动已保存并应用，所引用的光效已一并更新', 'success');
    } catch (err) { showToast?.(err.message, 'error'); setEditError(err.message); }
    finally { setIsBatchCompiling(false); }
  };

  // 复制 JSON 规则树
  const handleCopyJson = async () => {
    try {
      await navigator.clipboard.writeText(JSON.stringify(orchestrationData, null, 2));
      setIsCopied(true);
      setTimeout(() => setIsCopied(false), 2000);
      showToast?.('规则树 JSON 已复制到剪贴板', 'success');
    } catch (e) {
      showToast?.('复制失败', 'error');
    }
  };

  const rulesCount = orchestrationData?.orchestration?.rules?.length || 0;
  const overlaysCount = orchestrationData?.orchestration?.event_overlays?.length || 0;
  const fallbackProfile = orchestrationData?.orchestration?.fallback_profile || 'desktop';

  return (
    <div className="flex flex-col gap-4 p-1 h-[calc(100vh-280px)] min-h-[600px]">
      {/* 顶部控制栏 (MD3E Top App Bar) */}
      <div className="flex flex-wrap items-center justify-between gap-3 p-3 bg-md-surface-container-low border border-md-outline-variant rounded-md-lg shadow-md-level1">
        <div className="flex items-center gap-3">
          <div className="flex items-center justify-center w-9 h-9 rounded-md-full bg-md-secondary/10 text-md-secondary font-bold">
            <GitBranch className="w-5 h-5" />
          </div>
          <div className="flex flex-col">
            <span className="text-xs font-bold text-md-on-surface">
              设置联动：何时播放光效
            </span>
            <span className="text-[11px] text-md-on-surface-variant">
              事件发生时播放一次；条件成立期间持续叠加。优先级数值越大越靠上；切回桌面结束游戏叠加。
            </span>
          </div>
        </div>

        {/* 快捷指标 */}
        <div className="flex items-center gap-2 text-xs">
          <span className="px-2.5 py-1 rounded-md-full bg-md-surface-container border border-md-outline-variant text-md-on-surface font-medium flex items-center gap-1.5">
            <Workflow className="w-3.5 h-3.5 text-md-primary" />
            进程规则: <strong className="text-md-primary">{rulesCount}</strong>
          </span>
          <span className="px-2.5 py-1 rounded-md-full bg-md-surface-container border border-md-outline-variant text-md-on-surface font-medium flex items-center gap-1.5">
            <Zap className="w-3.5 h-3.5 text-md-tertiary" />
            事件覆盖: <strong className="text-md-tertiary">{overlaysCount}</strong>
          </span>
          <span className="px-2.5 py-1 rounded-md-full bg-md-surface-container border border-md-outline-variant text-md-on-surface font-medium flex items-center gap-1.5">
            <Layers className="w-3.5 h-3.5 text-md-secondary" />
            兜底方案: <strong className="text-md-secondary">{fallbackProfile}</strong>
          </span>
        </div>

        {/* 预设与操作 */}
        <div className="flex items-center gap-2">
          <button className="h-8 px-3 text-xs rounded-full bg-md-secondary-container text-md-on-secondary-container" onClick={() => {
            const example = studioExample(config?.default_profile || 'desktop');
            setExampleAssets(example);
            loadSafeWorkspaceJson(example.blocklyJson, workspaceRef.current);
            showToast?.('已载入示例草稿：血量底色、击杀扩散、低血量呼吸；保存后应用', 'info');
          }}>载入 CS2 完整示例</button>
          {ORCHESTRATOR_PRESETS.map((p) => (
            <button
              key={p.id}
              onClick={() => handleLoadPreset(p)}
              className="h-8 px-3 text-xs font-medium bg-md-surface-container border border-md-outline-variant rounded-md-full hover:bg-md-surface-container-high active:scale-95 transition-all cursor-pointer text-md-on-surface"
            >
              {p.name}
            </button>
          ))}

          <button
            onClick={() => setIsJsonModalOpen(true)}
            className="h-9 px-3.5 flex items-center gap-1.5 rounded-md-full bg-md-surface-container border border-md-outline-variant text-md-on-surface hover:bg-md-surface-container-high active:scale-95 transition-all text-xs font-semibold cursor-pointer"
            title="查看生成的声明式规则树 JSON"
          >
            <FileJson className="w-4 h-4 text-md-secondary" />
            <span>规则树 JSON</span>
          </button>

          {onSwitchToLegacyRules && (
            <button
              onClick={onSwitchToLegacyRules}
              className="h-9 px-3 flex items-center gap-1.5 rounded-md-full bg-md-surface-container border border-md-outline-variant text-md-on-surface hover:bg-md-surface-container-high active:scale-95 transition-all text-xs font-medium cursor-pointer"
              title="切换至传统表格管理视图"
            >
              <span>表格视图</span>
            </button>
          )}

          <button
            onClick={handleSaveOrchestration}
            disabled={isBatchCompiling}
            className="h-9 px-4 flex items-center gap-2 rounded-md-full bg-md-primary text-md-on-primary hover:bg-md-primary/90 active:scale-95 transition-all text-xs font-bold shadow-md-level1 cursor-pointer"
          >
            <Save className="w-4 h-4" />
            <span>{isBatchCompiling ? '正在应用…' : '保存并应用'}</span>
          </button>
        </div>
      </div>

      {editError && <p role="alert" className="text-sm text-md-error">{editError}</p>}
      <p className="text-xs text-md-on-surface-variant">等待、循环播放在“制作光效”内设置。这里的基础方案按从上到下首个命中生效；不同叠加层可以同时运行。</p>

      {/* Google Blockly 主编排画布 */}
      <div className="flex-1 w-full h-full relative rounded-md-lg overflow-hidden border border-md-outline-variant shadow-md-level1 bg-md-surface-container-low">
        <div ref={blocklyDivRef} className="absolute inset-0 w-full h-full" />
      </div>

      {/* 规则树 JSON 结构查看模态框 */}
      {isJsonModalOpen && (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 backdrop-blur-xs p-4">
          <div className="w-full max-w-3xl max-h-[85vh] flex flex-col bg-md-surface-container border border-md-outline-variant rounded-md-xl shadow-md-level3 overflow-hidden animate-in fade-in zoom-in-95 duration-200">
            <div className="flex items-center justify-between p-4 border-b border-md-outline-variant bg-md-surface-container-high">
              <div className="flex items-center gap-2">
                <FileJson className="w-5 h-5 text-md-secondary" />
                <span className="font-bold text-sm text-md-on-surface">
                  声明式规则树结构 (AST / config.json)
                </span>
              </div>
              <div className="flex items-center gap-2">
                <button
                  onClick={handleCopyJson}
                  className="h-8 px-3 flex items-center gap-1.5 rounded-md-full bg-md-surface-container border border-md-outline text-xs font-medium text-md-on-surface hover:bg-md-surface-container-highest cursor-pointer"
                >
                  {isCopied ? <Check className="w-3.5 h-3.5 text-emerald-400" /> : <Save className="w-3.5 h-3.5" />}
                  <span>{isCopied ? '已复制' : '复制 JSON'}</span>
                </button>
                <button
                  onClick={() => setIsJsonModalOpen(false)}
                  className="h-8 px-3 rounded-md-full hover:bg-md-surface-container-highest text-xs text-md-on-surface cursor-pointer"
                >
                  关闭
                </button>
              </div>
            </div>

            <div className="flex-1 overflow-auto p-4 bg-[#0d1117] text-slate-200 font-mono text-xs select-text leading-relaxed">
              <pre>{JSON.stringify(orchestrationData, null, 2)}</pre>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
