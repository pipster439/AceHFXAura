import React, { useState, useEffect, useRef } from 'react';
import Blockly, { loadSafeWorkspaceJson } from '../blockly/index.js';
import { registerCustomBlocks } from '../blockly/customBlocks';
import { DEFAULT_INJECT_OPTIONS } from '../blockly/theme';
import { ORCHESTRATOR_STUDIO_TOOLBOX } from '../blockly/toolboxes';
import { OrchestratorSerializer } from '../blockly/orchestratorSerializer';
import { ORCHESTRATOR_PRESETS } from '../blockly/presets';
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
  Workflow
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
    OrchestratorSerializer.restoreWorkspace(ws, config);

    const onWorkspaceChange = () => {
      const serialized = OrchestratorSerializer.serializeWorkspace(
        ws, 
        config?.orchestration?.fallback_profile || config?.default_profile || 'desktop'
      );
      setOrchestrationData(serialized);
    };

    ws.addChangeListener(onWorkspaceChange);
    onWorkspaceChange();

    const handleResize = () => Blockly.svgResize(ws);
    window.addEventListener('resize', handleResize);

    return () => {
      window.removeEventListener('resize', handleResize);
      ws.dispose();
      workspaceRef.current = null;
    };
  }, []);

  // 加载预设模板
  const handleLoadPreset = (preset) => {
    if (!workspaceRef.current || !preset.blocklyJson) return;
    workspaceRef.current.clear();
    loadSafeWorkspaceJson(preset.blocklyJson, workspaceRef.current);
    showToast?.(`已加载预设编排: ${preset.name}`, 'info');
  };

  // 保存编排至 config.json
  const handleSaveOrchestration = () => {
    if (!workspaceRef.current || !onSaveConfig) return;

    const serialized = OrchestratorSerializer.serializeWorkspace(
      workspaceRef.current, 
      config?.orchestration?.fallback_profile || config?.default_profile || 'desktop'
    );

    const nextConfig = {
      ...config,
      orchestration: serialized.orchestration,
      rules: serialized.rules,
      blockly_orchestrator: serialized.blockly_orchestrator
    };

    onSaveConfig(nextConfig);
    showToast?.('方案与 GSI 事件编排已保存，守护进程秒级热生效！', 'success');
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
              方案与 GSI 事件编排工作台
            </span>
            <span className="text-[11px] text-md-on-surface-variant">
              支持持续进程匹配、多条件 GSI 状态分支判断与瞬态游戏事件脉冲覆盖
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
            className="h-9 px-4 flex items-center gap-2 rounded-md-full bg-md-primary text-md-on-primary hover:bg-md-primary/90 active:scale-95 transition-all text-xs font-bold shadow-md-level1 cursor-pointer"
          >
            <Save className="w-4 h-4" />
            <span>保存并生效</span>
          </button>
        </div>
      </div>

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
