import { normalizePublication } from '../blockly/publication.js';
import { canonicalConfig } from '../utils/orchestration.js';
import { stageEffect, effectConfig, getEffectLifecycleStatus, fetchPublishReadiness } from '../utils/applyEffect.js';
import React, { useState, useEffect, useRef, useCallback } from 'react';
import Blockly, { loadSafeWorkspaceJson } from '../blockly/index.js';
import { registerCustomBlocks } from '../blockly/customBlocks';
import { DEFAULT_INJECT_OPTIONS } from '../blockly/theme';
import { EFFECT_STUDIO_TOOLBOX } from '../blockly/toolboxes';
import { CppTranspiler } from '../blockly/cppTranspiler';
import { JsTranspiler, PREVIEW_68_KEYS } from '../blockly/jsTranspiler';
import { EFFECT_PRESETS } from '../blockly/presets';
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
  CheckCircle2
} from 'lucide-react';

export default function EffectStudio({
  config,
  onSaveConfig,
  showToast,
  onPreviewFrameUpdate,
  activeEffectName,
  onEffectNameChange
}) {
  const blocklyDivRef = useRef(null);
  const workspaceRef = useRef(null);
  const [effectName, setEffectName] = useState(activeEffectName || 'custom_rainbow');
  const effectNameRef = useRef(effectName);
  effectNameRef.current = effectName;
  const [publication, setPublication] = useState(() => normalizePublication(config?.blockly_effects?.[activeEffectName]?.publication));
  const publicationRef = useRef(publication);
  publicationRef.current = publication;
  const [isPlaying, setIsPlaying] = useState(true);
  const [cppCode, setCppCode] = useState('');
  const [isCodeModalOpen, setIsCodeModalOpen] = useState(false);
  const [isCompiling, setIsCompiling] = useState(false);
  const [compilerLog, setCompilerLog] = useState(null);
  const [compilerSuccess, setCompilerSuccess] = useState(null);
  const [isCopied, setIsCopied] = useState(false);
  const [editError, setEditError] = useState(null);
  const [publishReadiness, setPublishReadiness] = useState({
    ready: true,
    sdkFound: true,
    msvcFound: true,
    sdkIncludeDir: '',
    msvcVcvarsPath: '',
    loaded: false
  });

  useEffect(() => {
    let active = true;
    const checkReadiness = () => {
      fetchPublishReadiness().then(r => {
        if (active) setPublishReadiness({ ...r, loaded: true });
      });
    };
    checkReadiness();
    const interval = setInterval(checkReadiness, 5000);
    window.addEventListener('focus', checkReadiness);
    return () => {
      active = false;
      clearInterval(interval);
      window.removeEventListener('focus', checkReadiness);
    };
  }, []);

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
      publicationRef.current = normalizePublication(localDraft.publication); setPublication(publicationRef.current);
      loadSafeWorkspaceJson(localDraft.json, ws); setEffectName(localDraft.name); effectNameRef.current = localDraft.name;
    } else if (defaultPreset?.blocklyJson) {
      loadSafeWorkspaceJson(defaultPreset.blocklyJson, ws);
    }

    const onWorkspaceChange = (event) => {
      if (event?.isUiEvent) return;
      try {
        compiledJsRef.current = JsTranspiler.compile(ws, publicationRef.current);
        previewClockRef.current = { elapsed: 0, last: null };
        setCppCode(CppTranspiler.transpile(effectNameRef.current, ws, publicationRef.current));
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
      try { sessionStorage.setItem('aura-effect-draft', JSON.stringify({ name: effectNameRef.current, publication: publicationRef.current, json: Blockly.serialization.workspaces.save(ws) })); } catch {}
      ws.dispose();
      workspaceRef.current = null;
    };
  }, []);

  // 当 activeEffectName 从外部改变时载入
  useEffect(() => {
    if (activeEffectName && activeEffectName !== effectNameRef.current && workspaceRef.current) {
      const effectData = config?.blockly_effects?.[activeEffectName];
      publicationRef.current = normalizePublication(effectData?.publication); setPublication(publicationRef.current);
      workspaceRef.current.clear();
      if (effectData?.blockly_json) {
        loadSafeWorkspaceJson(effectData.blockly_json, workspaceRef.current);
      }
      setEffectName(activeEffectName);
      effectNameRef.current = activeEffectName;
    }
  }, [activeEffectName, config]);

  const updateEffectName = (newName) => {
    setEffectName(newName);
    effectNameRef.current = newName;
    onEffectNameChange?.(newName);
  };

  // 当 effectName 改变时更新 C++ 源码
  useEffect(() => {
    if (workspaceRef.current) {
      try { setCppCode(CppTranspiler.transpile(effectName, workspaceRef.current, publication)); setEditError(null); }
      catch (err) { setEditError(err.message); }
    }
      if (workspaceRef.current) {
        compiledJsRef.current = JsTranspiler.compile(workspaceRef.current, publication);
        previewClockRef.current = { elapsed: 0, last: null };
      }
  }, [effectName, publication]);

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
    updateEffectName(preset.id);
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
        setCompilerLog('正在准备发布光效…');
        build = await stageEffect(name, workspaceRef.current, CppTranspiler, setCompilerLog, publication);
      }
      const next = effectConfig(config, name, json, build, publication);
      const ok = await onSaveConfig(next, apply ? config : undefined);
      if (!ok) throw new Error('配置保存失败，请重试；原有运行版本保留');
      setCompilerSuccess(true);
      if (apply) setIsPlaying(false);
      showToast?.(apply ? '光效已发布，配置已写入；daemon 将在下次热重载后应用' : '草稿已保存，正在运行的版本保持不变', 'success');
    } catch (err) {
      setCompilerSuccess(false); setCompilerLog([err.message, err.detail].filter(Boolean).join('\n\n')); showToast?.(err.message, 'error');
    } finally { setIsCompiling(false); }
  };
  const handleCompileAndReload = () => saveWorkspace(true);
  const handleSaveToConfig = () => saveWorkspace(false);

  const currentEffectData = config?.blockly_effects?.[effectName];
  const lifecycle = getEffectLifecycleStatus(currentEffectData);

  return (
    <div className="flex flex-col gap-4 p-1 h-full min-h-[560px]">
      <div className="flex items-center gap-3">
        <label>发布方式 <select aria-label="发布方式" disabled={isCompiling} value={publication.mode}
          onChange={e => setPublication(normalizePublication({ ...publication, mode: e.target.value }))}>
          <option value="continuous">持续循环</option><option value="one_shot">单次播放</option>
        </select></label>
        {publication.mode === 'one_shot' && <label>序列结束后淡出（毫秒） <input type="number" min="0" max="60000"
          disabled={isCompiling} value={publication.fade_out_ms}
          onChange={e => setPublication({ ...publication, fade_out_ms: Math.min(60000, Math.max(0, Math.trunc(Number(e.target.value) || 0))) })} /></label>}
      </div>
      {/* 顶部控制栏 (MD3E Top App Bar) */}
      <div className="flex flex-wrap items-center justify-between gap-3 p-3 bg-md-surface-container-low border border-md-outline-variant rounded-md-lg shadow-md-level1">
        <div className="flex items-center gap-3">
          <div className="flex items-center justify-center w-9 h-9 rounded-md-full bg-md-primary/10 text-md-primary font-bold">
            <Sparkles className="w-5 h-5" />
          </div>
          <div className="flex flex-col">
            <div className="flex items-center gap-2">
              <span className="text-xs font-semibold text-md-on-surface-variant">当前光效:</span>
              <span className="px-2.5 py-1 bg-md-surface-container border border-md-outline rounded-md-sm text-xs font-mono font-bold text-md-primary">
                {effectName}
              </span>

              <span
                className={`px-2.5 py-0.5 text-xs font-bold rounded-md-full border ${lifecycle.badgeClass}`}
                title={`生命周期状态: ${lifecycle.label}`}
              >
                {lifecycle.label}
              </span>

              {publishReadiness.loaded && !publishReadiness.ready && (
                <span
                  className={`inline-flex items-center gap-1 px-2.5 py-0.5 text-xs font-semibold rounded-md-full border ${
                    !publishReadiness.msvcFound
                      ? 'bg-amber-500/15 text-amber-400 border-amber-500/30'
                      : 'bg-rose-500/15 text-rose-400 border-rose-500/30'
                  }`}
                  title={
                    !publishReadiness.msvcFound
                      ? '未检测到 MSVC 编译环境。仍可编辑、预览并保存草稿；安装 Visual Studio / Build Tools 的 C++ 桌面工作负载后即可发布。'
                      : '未检测到 Plugin SDK 头文件。如果是单文件发行版，请确认运行时解压完整。'
                  }
                >
                  <AlertCircle className="w-3.5 h-3.5" />
                  {!publishReadiness.msvcFound ? '缺少 MSVC (可编辑/存草稿)' : '缺少 SDK 头文件'}
                </span>
              )}
            </div>
            <span className="text-[11px] text-md-on-surface-variant">
              保存草稿仅保存源码；发布后即转译并加载至硬件生效
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
            title="查看生成的原生 C++17 源码"
          >
            <Code className="w-4 h-4 text-md-tertiary" />
            <span>开发详情</span>
          </button>

          <button
            onClick={handleSaveToConfig}
            disabled={isCompiling}
            className="h-9 px-3.5 flex items-center gap-1.5 rounded-md-full bg-md-surface-container border border-md-outline-variant text-md-on-surface hover:bg-md-surface-container-high active:scale-95 transition-all text-xs font-semibold cursor-pointer disabled:opacity-50"
            title="保存当前积木草稿，保持正在运行的硬件版本不变"
          >
            <Save className="w-4 h-4 text-md-primary" />
            <span>保存草稿</span>
          </button>

          <button
            onClick={handleCompileAndReload}
            disabled={isCompiling || !!editError}
            className="h-9 px-4 flex items-center gap-2 rounded-md-full bg-md-primary text-md-on-primary hover:bg-md-primary/90 active:scale-95 transition-all text-xs font-bold shadow-md-level1 cursor-pointer disabled:opacity-50"
            title={
              publishReadiness.loaded && !publishReadiness.ready
                ? (!publishReadiness.msvcFound
                    ? '缺少 MSVC 编译环境：当前仍可保存草稿与预览；安装 C++ Desktop 工作负载后即可一键发布'
                    : '缺少 Plugin SDK 头文件：请检查安装或运行时目录')
                : '发布光效：编译原生插件并实时应用至键盘硬件'
            }
          >
            {isCompiling ? <RefreshCw className="w-4 h-4 animate-spin" /> : <Sparkles className="w-4 h-4" />}
            <span>{isCompiling ? '正在发布…' : '发布'}</span>
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
    </div>
  );
}
