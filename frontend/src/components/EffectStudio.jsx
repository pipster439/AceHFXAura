import React, { useState, useEffect, useRef, useCallback } from 'react';
import * as Blockly from 'blockly/core';
import 'blockly/blocks';
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
  onPreviewFrameUpdate
}) {
  const blocklyDivRef = useRef(null);
  const workspaceRef = useRef(null);
  const [effectName, setEffectName] = useState('custom_rainbow');
  const [isPlaying, setIsPlaying] = useState(true);
  const [cppCode, setCppCode] = useState('');
  const [isCodeModalOpen, setIsCodeModalOpen] = useState(false);
  const [isCompiling, setIsCompiling] = useState(false);
  const [compilerLog, setCompilerLog] = useState(null);
  const [compilerSuccess, setCompilerSuccess] = useState(null);
  const [isCopied, setIsCopied] = useState(false);

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

    // 默认加载第一个样例模板
    const defaultPreset = EFFECT_PRESETS[0];
    if (defaultPreset?.blocklyJson) {
      try {
        Blockly.serialization.workspaces.load(defaultPreset.blocklyJson, ws);
      } catch (err) {
        console.warn('Failed to load default effect preset:', err);
      }
    }

    const onWorkspaceChange = () => {
      // 重新编译 JS 闭包供实时预览
      compiledJsRef.current = JsTranspiler.compile(ws);
      // 同步生成 C++ 源码
      const code = CppTranspiler.transpile(effectName, ws);
      setCppCode(code);
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

  // 当 effectName 改变时更新 C++ 源码
  useEffect(() => {
    if (workspaceRef.current) {
      const code = CppTranspiler.transpile(effectName, workspaceRef.current);
      setCppCode(code);
    }
  }, [effectName]);

  // 25~60 FPS 实时渲染循环
  useEffect(() => {
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

          const frame = compiledJsRef.current(time, gsiMock, decaysRef.current);
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
    Blockly.serialization.workspaces.load(preset.blocklyJson, workspaceRef.current);
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

  // 一键后台动态编译 DLL 并热重载至守护进程
  const handleCompileAndReload = async () => {
    if (!effectName.trim()) {
      showToast?.('请输入合法的插件名称', 'error');
      return;
    }
    const cleanName = effectName.replace(/[^a-zA-Z0-9_]/g, '_');

    setIsCompiling(true);
    setCompilerLog('正在调用 MSVC cl.exe 编译独立动态链接库...\n');
    setCompilerSuccess(null);

    try {
      // 1. 调用 POST /api/compile_effect
      const compileRes = await fetch('/api/compile_effect', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          name: cleanName,
          code: cppCode
        })
      });

      const compileData = await compileRes.json();
      setCompilerLog(compileData.compiler_output || compileData.log || compileData.message);

      if (!compileRes.ok || !compileData.success) {
        setCompilerSuccess(false);
        showToast?.('编译失败，请查看编译器诊断日志', 'error');
        return;
      }

      setCompilerSuccess(true);
      showToast?.(`编译成功: ${compileData.dll_path} (${compileData.duration_ms}ms)`, 'success');

      // 2. 调用 POST /api/reload_plugin 热加载至守护进程
      const reloadRes = await fetch('/api/reload_plugin', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          name: cleanName
        })
      });

      if (reloadRes.ok) {
        showToast?.(`守护进程已即时热加载: ${cleanName}，推流管线无缝切入！`, 'success');
      }
    } catch (err) {
      setCompilerLog(`网络或请求异常: ${err.message}`);
      setCompilerSuccess(false);
      showToast?.(`编译执行异常: ${err.message}`, 'error');
    } finally {
      setIsCompiling(false);
    }
  };

  // 保存当前积木工作区至 config.json
  const handleSaveToConfig = () => {
    if (!workspaceRef.current || !onSaveConfig) return;
    const wsJson = Blockly.serialization.workspaces.save(workspaceRef.current);
    const cleanName = effectName.replace(/[^a-zA-Z0-9_]/g, '_');

    const nextConfig = {
      ...config,
      blockly_effects: {
        ...(config?.blockly_effects || {}),
        [cleanName]: {
          version: 1,
          name: cleanName,
          blockly_json: wsJson,
          updated_at: Date.now()
        }
      }
    };

    onSaveConfig(nextConfig);
    showToast?.(`已将光效工作区「${cleanName}」保存至配置中心`, 'success');
  };

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
              <span className="text-xs font-semibold text-md-on-surface-variant">插件标识:</span>
              <input
                type="text"
                value={effectName}
                onChange={(e) => setEffectName(e.target.value.trim().toLowerCase().replace(/[^a-z0-9_]/g, ''))}
                placeholder="effect_name"
                className="w-44 h-8 px-2.5 bg-md-surface-container border border-md-outline rounded-md-sm text-xs font-mono font-bold text-md-primary outline-none focus:border-md-primary"
              />
            </div>
            <span className="text-[11px] text-md-on-surface-variant">
              输出路径: plugins/effect_{effectName || 'custom'}.dll
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
            <span>{isPlaying ? '实时生效中' : '已暂停'}</span>
          </button>

          <button
            onClick={() => setIsCodeModalOpen(true)}
            className="h-9 px-3.5 flex items-center gap-1.5 rounded-md-full bg-md-surface-container border border-md-outline-variant text-md-on-surface hover:bg-md-surface-container-high active:scale-95 transition-all text-xs font-semibold cursor-pointer"
          >
            <Code className="w-4 h-4 text-md-tertiary" />
            <span>查看 C++ 源码</span>
          </button>

          <button
            onClick={handleSaveToConfig}
            className="h-9 px-3.5 flex items-center gap-1.5 rounded-md-full bg-md-surface-container border border-md-outline-variant text-md-on-surface hover:bg-md-surface-container-high active:scale-95 transition-all text-xs font-semibold cursor-pointer"
          >
            <Save className="w-4 h-4 text-md-primary" />
            <span>保存积木</span>
          </button>

          <button
            onClick={handleCompileAndReload}
            disabled={isCompiling}
            className="h-9 px-4 flex items-center gap-2 rounded-md-full bg-md-primary text-md-on-primary hover:bg-md-primary/90 active:scale-95 transition-all text-xs font-bold shadow-md-level1 cursor-pointer disabled:opacity-50"
          >
            {isCompiling ? <RefreshCw className="w-4 h-4 animate-spin" /> : <Cpu className="w-4 h-4" />}
            <span>{isCompiling ? '编译热插拔中...' : '编译为原生 DLL'}</span>
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
