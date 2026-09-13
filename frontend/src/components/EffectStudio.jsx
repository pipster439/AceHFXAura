import { canonicalConfig } from '../utils/orchestration.js';
import { stageEffect, effectConfig } from '../utils/applyEffect.js';
import React, { useState, useEffect, useRef, useCallback } from 'react';
import Blockly, { loadSafeWorkspaceJson } from '../blockly/index.js';
import { registerCustomBlocks } from '../blockly/customBlocks';
import { DEFAULT_INJECT_OPTIONS } from '../blockly/theme';
import { EFFECT_STUDIO_TOOLBOX } from '../blockly/toolboxes';
import { CppTranspiler } from '../blockly/cppTranspiler';
import { JsTranspiler, PREVIEW_68_KEYS } from '../blockly/jsTranspiler';
import { EFFECT_PRESETS } from '../blockly/presets';
import EffectLibraryModal from './EffectLibraryModal';
import { 
  Play, 
  Square, 
  Code, 
  Cpu, 
  Download, 
  Copy, 
  Check, 
  Sparkles, 
  RefreshCw, 
  Sliders, 
  Terminal, 
  AlertCircle,
  Save,
  CheckCircle2,
  FolderOpen
} from 'lucide-react';

export default function EffectStudio({
  config,
  onSaveConfig,
  showToast,
  onPreviewFrameUpdate
}) {
  const blocklyDivRef = useRef(null);
  const workspaceRef = useRef(null);
  const [effectName, setEffectName] = useState('custom_rainbow');
  const effectNameRef = useRef(effectName);
  effectNameRef.current = effectName;
  const [isPlaying, setIsPlaying] = useState(true);
  const [isLibraryOpen, setIsLibraryOpen] = useState(false);
  const [cppCode, setCppCode] = useState('');
  const [isCodeModalOpen, setIsCodeModalOpen] = useState(false);
  const [isCompiling, setIsCompiling] = useState(false);
  const [compilerLog, setCompilerLog] = useState(null);
  const [compilerSuccess, setCompilerSuccess] = useState(null);
  const [isCopied, setIsCopied] = useState(false);
  const [editError, setEditError] = useState(null);
  const previewClockRef = useRef({ elapsed: 0, last: null });

  // 模拟游戏遥测状态 (供用户调试 GSI 积木)
  const [simHealth, setSimHealth] = useState(100);
  const [simBomb, setSimBomb] = useState('carried');
  const [simKills, setSimKills] = useState(0);

  // 实时推流与当前帧缓存
  const [currentFrame, setCurrentFrame] = useState(() => PREVIEW_68_KEYS.map(() => [0, 0, 0]));
  const animFrameIdRef = useRef(null);
  const compiledJsRef = useRef(null);
  const decaysRef = useRef({});

  // 初始化 Blockly 画布
  useEffect(() => {
    registerCustomBlocks();
    if (!blocklyDivRef.current) return;

    const ws = Blockly.inject(blocklyDivRef.current, {
      ...DEFAULT_INJECT_OPTIONS,
      toolbox: EFFECT_STUDIO_TOOLBOX
    });
    workspaceRef.current = ws;

    let localDraft;
    try { localDraft = JSON.parse(sessionStorage.getItem('aura-effect-draft')); } catch {}
    // 默认加载第一个样例模板
    const defaultPreset = EFFECT_PRESETS[0];
    if (localDraft?.json) {
      loadSafeWorkspaceJson(localDraft.json, ws); setEffectName(localDraft.name); effectNameRef.current = localDraft.name;
    } else if (defaultPreset?.blocklyJson) {
      loadSafeWorkspaceJson(defaultPreset.blocklyJson, ws);
    }

    const onWorkspaceChange = (event) => {
      if (event?.isUiEvent) return;
      try {
        compiledJsRef.current = JsTranspiler.compile(ws);
        previewClockRef.current = { elapsed: 0, last: null };
        setCppCode(CppTranspiler.transpile(effectNameRef.current, ws));
        setEditError(null);
      } catch (err) {
        compiledJsRef.current = null;
        setEditError(err.message);
      }
    };

    ws.addChangeListener(onWorkspaceChange);
    onWorkspaceChange();

    const handleResize = () => Blockly.svgResize(ws);
    window.addEventListener('resize', handleResize);

    return () => {
      window.removeEventListener('resize', handleResize);
      try { sessionStorage.setItem('aura-effect-draft', JSON.stringify({ name: effectNameRef.current, json: Blockly.serialization.workspaces.save(ws) })); } catch {}
      ws.dispose();
      workspaceRef.current = null;
    };
  }, []);

  // 当 effectName 改变时更新 C++ 源码
  useEffect(() => {
    if (workspaceRef.current) {
      try { setCppCode(CppTranspiler.transpile(effectName, workspaceRef.current)); setEditError(null); }
      catch (err) { setEditError(err.message); }
    }
  }, [effectName]);

  // 25~60 FPS 实时渲染循环
  useEffect(() => {
    previewClockRef.current.last = null;
    if (!isPlaying) return;

    let lastTime = 0;
    const renderLoop = (time) => {
      if (time - lastTime >= 35) { // ~28 FPS
        lastTime = time;
        if (compiledJsRef.current) {
          const gsiMock = {
            player: {
              state: {
                health: simHealth,
                armor: 100,
                helmet: true,
                money: 4800,
                round_kills: simKills,
                flashed: 0,
                burning: 0
              }
            },
            round: {
              bomb: simBomb,
              phase: 'live'
            },
            map: {
              name: 'de_dust2',
              mode: 'competitive',
              round: 5
            }
          };

          const clock = previewClockRef.current;
          if (clock.last !== null) clock.elapsed += time - clock.last;
          clock.last = time;
          let frame;
          try { frame = compiledJsRef.current(clock.elapsed, gsiMock, decaysRef.current); }
          catch (err) { setEditError(err.message); setIsPlaying(false); return; }
          setCurrentFrame(frame);

          if (onPreviewFrameUpdate) {
            onPreviewFrameUpdate(frame);
          }
        }
      }
      animFrameIdRef.current = requestAnimationFrame(renderLoop);
    };

    animFrameIdRef.current = requestAnimationFrame(renderLoop);
    return () => {
      if (animFrameIdRef.current) {
        cancelAnimationFrame(animFrameIdRef.current);
      }
    };
  }, [isPlaying, simHealth, simBomb, simKills, onPreviewFrameUpdate]);

  // 加载预设模板
  const handleLoadPreset = (preset) => {
    if (!workspaceRef.current || !preset.blocklyJson) return;
    workspaceRef.current.clear();
    loadSafeWorkspaceJson(preset.blocklyJson, workspaceRef.current);
    setEffectName(preset.id);
    showToast?.(`已加载预设: ${preset.name}`, 'info');
  };

  // 复制 C++ 源码
  const handleCopyCode = async () => {
    try {
      await navigator.clipboard.writeText(cppCode);
      setIsCopied(true);
      setTimeout(() => setIsCopied(false), 2000);
      showToast?.('C++17 源码已复制到剪贴板', 'success');
    } catch (e) {
      showToast?.('复制失败，请手动选取代码', 'error');
    }
  };

  // 下载 .cpp 文件
  const handleDownloadCpp = () => {
    const blob = new Blob([cppCode], { type: 'text/x-c++src;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `effect_${effectName}.cpp`;
    a.click();
    URL.revokeObjectURL(url);
    showToast?.(`已导出 effect_${effectName}.cpp`, 'success');
  };

  const saveWorkspace = async (apply) => {
    if (!workspaceRef.current || !onSaveConfig || isCompiling) return;
    const name = effectName.trim();
    if (!/^[a-zA-Z_][a-zA-Z0-9_]{0,47}$/.test(name)) {
      showToast?.('名称需以字母或下划线开头，最多 48 个字符', 'error'); return;
    }
    setIsCompiling(true);
    try {
      const json = Blockly.serialization.workspaces.save(workspaceRef.current);
      let build;
      if (apply) {
        setCompilerLog('正在准备光效…');
        build = await stageEffect(name, workspaceRef.current, CppTranspiler, setCompilerLog);
      }
      const next = effectConfig(config, name, json, build);
      const ok = await onSaveConfig(next);
      if (!ok) throw new Error('配置保存失败，请重试；原有应用版本保留');
      setCompilerSuccess(true);
      if (apply) setIsPlaying(false);
      showToast?.(apply ? '光效已保存并应用，联动将使用这一版本' : '草稿已保存，正在运行的版本保持不变', 'success');
    } catch (err) {
      setCompilerSuccess(false); setCompilerLog(err.message); showToast?.(err.message, 'error');
    } finally { setIsCompiling(false); }
  };
  const handleCompileAndReload = () => saveWorkspace(true);
  const handleSaveToConfig = () => saveWorkspace(false);

  // 光效管理中心操作逻辑
  const handleSelectEffect = (name, blocklyJson) => {
    if (!workspaceRef.current) return;
    workspaceRef.current.clear();
    if (blocklyJson) {
      loadSafeWorkspaceJson(blocklyJson, workspaceRef.current);
    }
    setEffectName(name);
    showToast?.(`已载入光效: ${name}`, 'info');
  };

  const handleCreateEffect = async (name, templateKey) => {
    if (!workspaceRef.current) return;
    const clean = name.trim().toLowerCase().replace(/[^a-z0-9_]/g, '_');
    if (!clean) return;

    workspaceRef.current.clear();
    let templateJson = null;
    if (templateKey !== 'blank') {
      const preset = EFFECT_PRESETS.find(p => p.id === templateKey);
      if (preset?.blocklyJson) {
        templateJson = preset.blocklyJson;
        loadSafeWorkspaceJson(templateJson, workspaceRef.current);
      }
    }

    setEffectName(clean);

    const wsJson = templateJson || Blockly.serialization.workspaces.save(workspaceRef.current);
    const nextConfig = effectConfig(config, clean, wsJson);
    if (onSaveConfig && !await onSaveConfig(nextConfig)) return;
    showToast?.(`已新建光效草稿「${clean}」，应用后可用于联动`, 'success');
  };

  const handleCloneEffect = async (sourceName, cloneName) => {
    const srcData = config?.blockly_effects?.[sourceName];
    if (!srcData) return;

    const nextConfig = effectConfig(config, cloneName, srcData.blockly_json);
    if (onSaveConfig && !await onSaveConfig(nextConfig)) return;
    setEffectName(cloneName);
    if (workspaceRef.current && srcData.blockly_json) {
      workspaceRef.current.clear();
      loadSafeWorkspaceJson(srcData.blockly_json, workspaceRef.current);
    }
    showToast?.(`已克隆生成光效副本: ${cloneName}`, 'success');
  };

  const handleRenameEffect = async (oldName, newName) => {
    if (oldName === newName) return;
    const effects = { ...(config?.blockly_effects || {}) };
    const profiles = { ...(config?.profiles || {}) };
    if (!effects[oldName]) return;

    effects[newName] = {
      ...effects[oldName],
      name: newName,
      updated_at: Date.now()
    };
    delete effects[oldName];

    if (profiles[oldName]) {
      profiles[newName] = { ...profiles[oldName], title: newName };
      delete profiles[oldName];
    }

    const canonical = canonicalConfig(config);
    const nextConfig = { ...canonical, blockly_effects: effects, profiles,
      default_profile: canonical.default_profile === oldName ? newName : canonical.default_profile,
      blockly_orchestrator: undefined,
      orchestration: { ...canonical.orchestration,
        fallback_profile: canonical.orchestration.fallback_profile === oldName ? newName : canonical.orchestration.fallback_profile,
        rules: canonical.orchestration.rules.map(r => ({ ...r, target_profile: r.target_profile === oldName ? newName : r.target_profile })),
        event_overlays: canonical.orchestration.event_overlays.map(r => ({ ...r, effect: r.effect === oldName ? newName : r.effect }))
      }
    };
    if (onSaveConfig && !await onSaveConfig(nextConfig)) return;
    if (effectName === oldName) {
      setEffectName(newName);
    }
    showToast?.(`已将光效重命名为: ${newName}`, 'success');
  };

  const handleDeleteEffect = async (name) => {
    const c = canonicalConfig(config);
    if (c.default_profile === name || c.orchestration.fallback_profile === name || c.orchestration.rules.some(r => r.target_profile === name) || c.orchestration.event_overlays.some(r => r.effect === name)) {
      showToast?.('此光效正在被方案或联动引用，请先更换引用再删除', 'error'); return;
    }
    const effects = { ...(config?.blockly_effects || {}) };
    const profiles = { ...(config?.profiles || {}) };
    delete effects[name];
    delete profiles[name];

    const nextConfig = { ...config, blockly_effects: effects, profiles };
    if (onSaveConfig && !await onSaveConfig(nextConfig)) return;

    if (effectName === name) {
      const remaining = Object.keys(effects);
      if (remaining.length > 0) {
        handleSelectEffect(remaining[0], effects[remaining[0]]?.blockly_json);
      } else {
        setEffectName('custom_rainbow');
        workspaceRef.current?.clear();
      }
    }
    showToast?.(`已删除光效: ${name}`, 'info');
  };

  const handleExportEffect = (name) => {
    const data = config?.blockly_effects?.[name];
    if (!data) return;
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `effect_${name}.json`;
    a.click();
    URL.revokeObjectURL(url);
    showToast?.(`已导出光效配置文件: effect_${name}.json`, 'success');
  };

  const handleImportEffect = async (parsed) => {
    if (!parsed || !parsed.name) {
      showToast?.('导入的文件不是合法的光效配置文件', 'error');
      return;
    }
    const clean = parsed.name.replace(/[^a-zA-Z0-9_]/g, '_');
    const nextConfig = effectConfig(config, clean, parsed.blockly_json || parsed);
    if (onSaveConfig && !await onSaveConfig(nextConfig)) return;
    handleSelectEffect(clean, parsed.blockly_json || parsed);
    showToast?.(`已导入并切换至光效: ${clean}`, 'success');
  };

  const customEffectKeys = Object.keys(config?.blockly_effects || {});

  return (
    <div className="flex flex-col gap-4 p-1 h-[calc(100vh-280px)] min-h-[600px]">
      {/* 顶部控制栏 (MD3E Top App Bar) */}
      <div className="flex flex-wrap items-center justify-between gap-3 p-3 bg-md-surface-container-low border border-md-outline-variant rounded-md-lg shadow-md-level1">
        <div className="flex items-center gap-3">
          <div className="flex items-center justify-center w-9 h-9 rounded-md-full bg-md-primary/10 text-md-primary font-bold">
            <Sparkles className="w-5 h-5" />
          </div>
          <div className="flex flex-col">
            <div className="flex items-center gap-2">
              <span className="text-xs font-semibold text-md-on-surface-variant">当前光效:</span>
              <input
                type="text"
                value={effectName}
                onChange={(e) => setEffectName(e.target.value.trim().toLowerCase().replace(/[^a-z0-9_]/g, ''))}
                placeholder="effect_name"
                className="w-32 h-8 px-2.5 bg-md-surface-container border border-md-outline rounded-md-sm text-xs font-mono font-bold text-md-primary outline-none focus:border-md-primary"
              />

              {customEffectKeys.length > 0 && (
                <select
                  value={effectName}
                  onChange={(e) => {
                    const selected = e.target.value;
                    const blocklyJson = config?.blockly_effects?.[selected]?.blockly_json;
                    handleSelectEffect(selected, blocklyJson);
                  }}
                  className="h-8 px-2 bg-md-surface-container border border-md-outline rounded-md-sm text-xs text-md-on-surface outline-none cursor-pointer"
                  title="在已保存的工坊光效间快速切换"
                >
                  <option value={effectName}>{effectName} (当前编辑)</option>
                  {customEffectKeys.filter(k => k !== effectName).map(k => (
                    <option key={k} value={k}>{k}</option>
                  ))}
                </select>
              )}

              <button
                onClick={() => setIsLibraryOpen(true)}
                className="h-8 px-3 flex items-center gap-1.5 rounded-md-full bg-md-primary/15 text-md-primary hover:bg-md-primary/25 border border-md-primary/30 text-xs font-bold transition-all cursor-pointer shadow-xs"
                title="打开光效管理中心，自由新建、克隆与管理自定义光效"
              >
                <FolderOpen className="w-3.5 h-3.5" />
                <span>光效库 ({customEffectKeys.length})</span>
              </button>
            </div>
            <span className="text-[11px] text-md-on-surface-variant">
              等待会保留当前灯光；积木执行完毕后，下一帧从头开始
            </span>
          </div>
        </div>

        {/* 预设模板切换 */}
        <div className="flex items-center gap-2">
          <span className="text-xs font-medium text-md-on-surface-variant">模版样例:</span>
          {EFFECT_PRESETS.map((p) => (
            <button
              key={p.id}
              onClick={() => handleLoadPreset(p)}
              className="h-8 px-3 text-xs font-medium bg-md-surface-container border border-md-outline-variant rounded-md-full hover:bg-md-surface-container-high active:scale-95 transition-all cursor-pointer text-md-on-surface"
            >
              {p.name}
            </button>
          ))}
        </div>

        {/* 操作动作按钮群 */}
        <div className="flex items-center gap-2">
          <button
            onClick={() => setIsPlaying(!isPlaying)}
            className={`h-9 px-3.5 flex items-center gap-1.5 rounded-md-full text-xs font-bold transition-all cursor-pointer ${
              isPlaying
                ? 'bg-md-primary-container text-md-on-primary-container'
                : 'bg-md-surface-container text-md-on-surface'
            }`}
            title={isPlaying ? '暂停实时计算' : '恢复实时计算'}
          >
            {isPlaying ? <Square className="w-3.5 h-3.5 fill-current" /> : <Play className="w-3.5 h-3.5 fill-current" />}
            <span>{isPlaying ? '预览中' : '预览已暂停'}</span>
          </button>

          <button
            onClick={() => setIsCodeModalOpen(true)}
            className="h-9 px-3.5 flex items-center gap-1.5 rounded-md-full bg-md-surface-container border border-md-outline-variant text-md-on-surface hover:bg-md-surface-container-high active:scale-95 transition-all text-xs font-semibold cursor-pointer"
          >
            <Code className="w-4 h-4 text-md-tertiary" />
            <span>开发详情</span>
          </button>

          <button
            onClick={handleSaveToConfig}
            className="h-9 px-3.5 flex items-center gap-1.5 rounded-md-full bg-md-surface-container border border-md-outline-variant text-md-on-surface hover:bg-md-surface-container-high active:scale-95 transition-all text-xs font-semibold cursor-pointer"
          >
            <Save className="w-4 h-4 text-md-primary" />
            <span>保存草稿</span>
          </button>

          <button
            onClick={handleCompileAndReload}
            disabled={isCompiling || !!editError}
            className="h-9 px-4 flex items-center gap-2 rounded-md-full bg-md-primary text-md-on-primary hover:bg-md-primary/90 active:scale-95 transition-all text-xs font-bold shadow-md-level1 cursor-pointer disabled:opacity-50"
          >
            {isCompiling ? <RefreshCw className="w-4 h-4 animate-spin" /> : <Cpu className="w-4 h-4" />}
            <span>{isCompiling ? '正在应用…' : '保存并应用'}</span>
          </button>
        </div>
      </div>

      {/* 实时模拟与 GSI 传感器控制条 (供无游戏启动状态下调试积木) */}
      <div className="flex flex-wrap items-center justify-between gap-4 px-4 py-2 bg-md-surface-container-lowest border border-md-outline-variant rounded-md-md text-xs">
        <div className="flex items-center gap-2 text-md-on-surface-variant font-medium">
          <Sliders className="w-4 h-4 text-md-secondary" />
          <span>GSI 传感器模拟面板:</span>
        </div>

        <div className="flex items-center gap-3">
          <label className="flex items-center gap-2">
            <span className="text-md-on-surface-variant">血量 (Health):</span>
            <input
              type="range"
              min="0"
              max="100"
              value={simHealth}
              onChange={(e) => setSimHealth(Number(e.target.value))}
              className="w-28 accent-md-primary cursor-pointer"
            />
            <span className={`font-mono font-bold ${simHealth <= 20 ? 'text-md-error' : 'text-md-primary'}`}>
              {simHealth}
            </span>
          </label>

          <label className="flex items-center gap-2">
            <span className="text-md-on-surface-variant">炸弹状态:</span>
            <select
              value={simBomb}
              onChange={(e) => setSimBomb(e.target.value)}
              className="h-7 px-2 bg-md-surface-container border border-md-outline rounded-md-xs text-xs text-md-on-surface font-medium outline-none"
            >
              <option value="carried">随身携带 (carried)</option>
              <option value="planted">已安放 (planted)</option>
              <option value="defused">已拆除 (defused)</option>
            </select>
          </label>

          <label className="flex items-center gap-2">
            <span className="text-md-on-surface-variant">击杀数:</span>
            <button
              onClick={() => setSimKills((k) => (k + 1) % 6)}
              className="px-2 py-0.5 rounded-md-xs bg-md-surface-container border border-md-outline-variant font-mono font-bold text-md-primary hover:bg-md-surface-container-high"
            >
              {simKills} 杀 (点击递增)
            </button>
          </label>
        </div>

        {/* 68 键微型实时预览条 */}
        <div className="flex items-center gap-1">
          <span className="text-md-on-surface-variant mr-1">微型推流:</span>
          <div className="flex items-center gap-0.5 p-1 bg-black/40 rounded border border-md-outline-variant/40">
            {currentFrame.slice(0, 24).map((c, idx) => (
              <div
                key={idx}
                className="w-1.5 h-3 rounded-[1px] transition-colors duration-75"
                style={{ backgroundColor: `rgb(${c[0]}, ${c[1]}, ${c[2]})` }}
                title={`Key ${idx}: rgb(${c[0]}, ${c[1]}, ${c[2]})`}
              />
            ))}
            <span className="text-[10px] text-md-on-surface-variant ml-1 font-mono">...68K</span>
          </div>
        </div>
      </div>

      {editError && <p role="alert" className="text-sm text-md-error">{editError}</p>}
      {/* Google Blockly 主画布 */}
      <div className="flex-1 w-full h-full relative rounded-md-lg overflow-hidden border border-md-outline-variant shadow-md-level1 bg-md-surface-container-low">
        <div ref={blocklyDivRef} className="absolute inset-0 w-full h-full" />
      </div>

      {/* 编译器输出控制台抽屉 (如果有编译结果或正在编译) */}
      {compilerLog && (
        <div className="p-3 bg-md-surface-container-lowest border border-md-outline-variant rounded-md-md flex flex-col gap-2 max-h-40 overflow-y-auto">
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-2 text-xs font-bold">
              <Terminal className="w-4 h-4 text-md-primary" />
              <span>MSVC 编译诊断日志</span>
              {compilerSuccess === true && (
                <span className="flex items-center gap-1 text-emerald-400">
                  <CheckCircle2 className="w-3.5 h-3.5" /> 成功
                </span>
              )}
              {compilerSuccess === false && (
                <span className="flex items-center gap-1 text-md-error">
                  <AlertCircle className="w-3.5 h-3.5" /> 失败
                </span>
              )}
            </div>
            <button
              onClick={() => setCompilerLog(null)}
              className="text-[11px] text-md-on-surface-variant hover:text-md-on-surface cursor-pointer"
            >
              关闭
            </button>
          </div>
          <pre className="text-[11px] font-mono text-md-on-surface-variant whitespace-pre-wrap select-text leading-relaxed">
            {compilerLog}
          </pre>
        </div>
      )}

      {/* C++ 源码高亮查看模态框 */}
      {isCodeModalOpen && (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 backdrop-blur-xs p-4">
          <div className="w-full max-w-4xl max-h-[85vh] flex flex-col bg-md-surface-container border border-md-outline-variant rounded-md-xl shadow-md-level3 overflow-hidden animate-in fade-in zoom-in-95 duration-200">
            {/* 模态头部 */}
            <div className="flex items-center justify-between p-4 border-b border-md-outline-variant bg-md-surface-container-high">
              <div className="flex items-center gap-2">
                <Code className="w-5 h-5 text-md-primary" />
                <span className="font-bold text-sm text-md-on-surface">
                  生成的原生 C++17 源码 (ISO C++17 / 0 堆内存分配)
                </span>
              </div>
              <div className="flex items-center gap-2">
                <button
                  onClick={handleCopyCode}
                  className="h-8 px-3 flex items-center gap-1.5 rounded-md-full bg-md-surface-container border border-md-outline text-xs font-medium text-md-on-surface hover:bg-md-surface-container-highest cursor-pointer"
                >
                  {isCopied ? <Check className="w-3.5 h-3.5 text-emerald-400" /> : <Copy className="w-3.5 h-3.5" />}
                  <span>{isCopied ? '已复制' : '复制'}</span>
                </button>
                <button
                  onClick={handleDownloadCpp}
                  className="h-8 px-3 flex items-center gap-1.5 rounded-md-full bg-md-surface-container border border-md-outline text-xs font-medium text-md-on-surface hover:bg-md-surface-container-highest cursor-pointer"
                >
                  <Download className="w-3.5 h-3.5" />
                  <span>导出 .cpp</span>
                </button>
                <button
                  onClick={() => setIsCodeModalOpen(false)}
                  className="h-8 px-3 rounded-md-full hover:bg-md-surface-container-highest text-xs text-md-on-surface cursor-pointer"
                >
                  关闭
                </button>
              </div>
            </div>

            {/* 代码查看区 */}
            <div className="flex-1 overflow-auto p-4 bg-[#0d1117] text-slate-200 font-mono text-xs select-text leading-relaxed">
              <pre>{cppCode}</pre>
            </div>
          </div>
        </div>
      )}

      {/* 独立模态弹窗: 光效管理中心 */}
      <EffectLibraryModal
        isOpen={isLibraryOpen}
        onClose={() => setIsLibraryOpen(false)}
        effects={config?.blockly_effects || {}}
        currentEffectName={effectName}
        onSelectEffect={handleSelectEffect}
        onCreateEffect={handleCreateEffect}
        onCloneEffect={handleCloneEffect}
        onRenameEffect={handleRenameEffect}
        onDeleteEffect={handleDeleteEffect}
        onExportEffect={handleExportEffect}
        onImportEffect={handleImportEffect}
      />
    </div>
  );
}
