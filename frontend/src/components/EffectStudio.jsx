import { StudioConversationTools, projectFingerprint } from '../utils/studioConversation.js';
import { assistantContext, parseProposal, StudioProposalSession, loadAiSnapshot, validateCandidate } from '../utils/studioAssistant.js';
import { listenStudioCommands, postStudioState } from '../utils/studioHost.js';
import { normalizePublication } from '../blockly/publication.js';
import { stageEffect, buildEffect, effectConfig, getEffectLifecycleStatus, fetchPublishReadiness } from '../utils/applyEffect.js';
import React, { useState, useEffect, useLayoutEffect, useRef, useCallback } from 'react';
import { createPortal } from 'react-dom';
import Blockly, { loadSafeWorkspaceJson, assertNoLegacyEventPulse } from '../blockly/index.js';
import { dismissForOverlay } from '../blockly/dismissForOverlay.js';
import { registerCustomBlocks } from '../blockly/customBlocks';
import { DEFAULT_INJECT_OPTIONS } from '../blockly/theme';
import { EFFECT_STUDIO_TOOLBOX } from '../blockly/toolboxes';
import { CppTranspiler } from '../blockly/cppTranspiler';
import { JsTranspiler, PREVIEW_68_KEYS } from '../blockly/jsTranspiler';
import TestBench from './StudioTestBench';
import ProgramCatalog from './ProgramCatalog';
import InputSourcesControl from './InputSourcesControl';
import { createBundleBridge, createBundlePayload, validateBundlePayload, importedDraft, nextImportedName } from '../utils/studioBundle.js';
import { createStudioStorage, durableStudioDraft } from '../utils/studioStorage.js';
import { CAPABILITY_LABELS, CAPABILITY_DIAGNOSTICS, deriveEffectCapabilities } from '../utils/effectCapabilities.js';
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

export const WORKSPACE_STATUS = Object.freeze({
  UNREADY: 'unready',
  READY: 'ready',
  DESTROYED: 'destroyed'
});

export default function EffectStudio({
  config,
  onSaveConfig,
  showToast,
  onPreviewFrameUpdate,
  activeEffectName,
  onEffectNameChange,
  embedded = false,
  compact = false,
  overlayOpen = false,
  onShellState,
  onImportDraft
}) {
  const aiRequestRef = useRef(null);
  const aiSessionRef = useRef(new StudioProposalSession());
  const chatToolsRef = useRef(new StudioConversationTools(aiSessionRef.current));
  const chatTurnRef = useRef(null);
  const aiOperationRef = useRef(null);
  const editorIdRef = useRef(globalThis.crypto.randomUUID().replaceAll('-', ''));
  const revisionSequence = useRef(0);
  const postRevision = async () => {
    if (!embedded || !workspaceRef.current || workspaceStatusRef.current !== WORKSPACE_STATUS.READY) return;
    const seq = ++revisionSequence.current, snapshot = currentDraft();
    if (!snapshot) return;
    aiSessionRef.current.activateProject(snapshot.name);
    const fingerprint = await projectFingerprint(snapshot);
    if (seq === revisionSequence.current) window.chrome?.webview?.postMessage({
      type: 'studio_ai_state', editor_id: editorIdRef.current, sequence: seq, name: snapshot.name, fingerprint,
      undo_requests: aiSessionRef.current.getUndoableRequestIds(snapshot.name), can_undo: aiSessionRef.current.canUndo(snapshot),
      operation_id: aiOperationRef.current?.id || null
    });
  };
  const [benchOpen, setBenchOpen] = useState(false);
  const [runToken, setRunToken] = useState(0);
  const bundleBridge = useRef(null);
  const [bundleDialog, setBundleDialog] = useState(null);
  const [bundleName, setBundleName] = useState('');
  const [bundleDescription, setBundleDescription] = useState('');
  const [bundleAuthor, setBundleAuthor] = useState('');
  const [bundleTags, setBundleTags] = useState('');
  const [bundleError, setBundleError] = useState('');
  useEffect(() => { bundleBridge.current = createBundleBridge(window.chrome?.webview); return () => bundleBridge.current?.dispose(); }, []);
  const storageRef = useRef(null); const observedRef = useRef(null); const observeSequence = useRef(0);
  const configLoadedRef = useRef(!!config);
  const [storageStatus, setStorageStatus] = useState(''); const [recovery, setRecovery] = useState(null);
  const [snapshots, setSnapshots] = useState([]); const [snapshotId, setSnapshotId] = useState('');
  const [capabilityManifest, setCapabilityManifest] = useState(null);
  const [workspaceStatus, setWorkspaceStatus] = useState(WORKSPACE_STATUS.UNREADY);
  const workspaceStatusRef = useRef(WORKSPACE_STATUS.UNREADY);
  const [programSnapshot, setProgramSnapshot] = useState(null);
  const currentDraft = () => {
    if (!workspaceRef.current || workspaceStatusRef.current !== WORKSPACE_STATUS.READY) return null;
    return {
      name: effectNameRef.current,
      publication: publicationRef.current,
      json: Blockly.serialization.workspaces.save(workspaceRef.current)
    };
  };
  const linkProgramToLighting = async entry => {
    const profileName = `ir_${entry.name}`.replace(/[^a-zA-Z0-9_]/g, '_').slice(0, 48);
    if (!/^[a-zA-Z_][a-zA-Z0-9_]{0,47}$/.test(profileName)) { showToast?.('程序名称无法创建 Lighting 方案', 'error'); return; }
    if (config?.profiles?.[profileName] || config?.blockly_effects?.[profileName]) { showToast?.(`Lighting 方案「${profileName}」已存在，未覆盖。`, 'error'); return; }
    if (globalThis.confirm?.(`创建 Lighting 方案「${profileName}」并指向 ${entry.plugin_name}？\n这只写入配置，不会自动激活。`) === false) return;
    const next = { ...config, profiles: { ...(config?.profiles || {}), [profileName]: { type: 'plugin', plugin_name: entry.plugin_name, title: profileName } } };
    if (await onSaveConfig?.(next)) showToast?.(`已创建 Lighting 方案「${profileName}」；尚未自动激活。`, 'success');
  };
  const openBench = () => { if (!isCompiling && workspaceRef.current) { setIsPlaying(false); setBenchOpen(true); } };
  const queueDraft = () => {
    if (legacyWorkspaceBlockedRef.current) return;
    if (embedded && observedRef.current === effectNameRef.current && workspaceRef.current && workspaceStatusRef.current === WORKSPACE_STATUS.READY) {
      const draft = currentDraft();
      if (draft) storageRef.current?.request('autosave', draft).then(r => setStorageStatus(r.text)).catch(e => setStorageStatus(e.message));
    }
  };
  const observeDraft = () => {
    if (!embedded || !storageRef.current || !config) return;
    const name = effectNameRef.current, seq = ++observeSequence.current; observedRef.current = null;
    setRecovery(null); setSnapshots([]); setSnapshotId('');
    try {
      storageRef.current.request('observe', durableStudioDraft(config, name)).then(r => {
        if (seq !== observeSequence.current) return;
        observedRef.current = name; setRecovery(r.recovery); setSnapshots(r.snapshots || []);
        if (!r.recovery) queueDraft(); else { setIsPlaying(false); setBenchOpen(false); setStorageStatus('可恢复草稿：由你决定恢复或放弃。'); }
      }).catch(e => setStorageStatus(e.message));
    } catch (e) { setStorageStatus(e.message); }
  };
  const checkpointDraft = async (reason, draft = currentDraft(), requestId = null) => {
    if (legacyWorkspaceBlockedRef.current) throw new Error('当前工程无法加载，请先打开有效工程。');
    if (!embedded) throw new Error('发布前快照需要 Aura 桌面宿主。');
    const result = await storageRef.current.request('snapshot', draft, { reason, ...(requestId ? { ai_request_id: requestId } : {}) });
    if (draft.name === effectNameRef.current) setSnapshots(result.snapshots || []);
    return result;
  };
  const restoreDraft = async (operation, extra = {}) => {
    if (isCompiling || aiOperationRef.current) return;
    setIsCompiling(true);
    try {
      const result = await storageRef.current.request(operation, currentDraft(), extra);
      const draft = result.draft; if (draft.name !== effectNameRef.current) throw new Error('恢复工程与当前工程不一致');
      if (aiOperationRef.current) throw new Error('AI 工程操作正在进行；请完成后再恢复草稿。');
      // Validate before replacing the current draft; no config/publish call.
      validateCandidate(draft);
      setIsPlaying(false); setBenchOpen(false); loadAiSnapshot(workspaceRef.current, draft.json);
      retainedWorkspaceValidRef.current = true;
      legacyWorkspaceBlockedRef.current = false; setLegacyWorkspaceError(null);
      publicationRef.current = normalizePublication(draft.publication); setPublication(publicationRef.current);
      compiledJsRef.current = JsTranspiler.compile(workspaceRef.current, publicationRef.current);
      setRecovery(null); aiSessionRef.current.invalidateProject(draft.name);
      chatToolsRef.current = new StudioConversationTools(aiSessionRef.current); chatTurnRef.current = null; aiRequestRef.current = null;
      postRevision(); queueDraft(); setStorageStatus('已恢复至草稿；发布状态保持不变，旧 AI 撤销记录已失效。');
    } catch (e) { setStorageStatus(e.message); }
    finally { setIsCompiling(false); }
  };
  const discardRecovery = async () => {
    try { await storageRef.current.request('discard', currentDraft()); setRecovery(null); setStorageStatus('可恢复草稿已放弃。'); }
    catch (e) { setStorageStatus(e.message); }
  };
  const blocklyDivRef = useRef(null);
  const workspaceRef = useRef(null);
  const retainedWorkspaceValidRef = useRef(false);
  useLayoutEffect(() => { if (overlayOpen || bundleDialog) return dismissForOverlay(workspaceRef.current); }, [overlayOpen, bundleDialog]);
  const [effectName, setEffectName] = useState(activeEffectName || 'custom_rainbow');
  const effectNameRef = useRef(effectName);
  effectNameRef.current = effectName;
  const [publication, setPublication] = useState(() => normalizePublication(config?.blockly_effects?.[activeEffectName]?.publication));
  useEffect(() => { postRevision(); }, [effectName, publication]);
  const publicationRef = useRef(publication);
  publicationRef.current = publication;
  const [isPlaying, setIsPlaying] = useState(true);
  const [cppCode, setCppCode] = useState('');
  const [isCodeModalOpen, setIsCodeModalOpen] = useState(false);
  const [showCompactControls, setShowCompactControls] = useState(false);
  const [showLifecycleControls, setShowLifecycleControls] = useState(false);
  const [isCompiling, setIsCompiling] = useState(false);
  const [compilerLog, setCompilerLog] = useState(null);
  const [buildResult, setBuildResult] = useState('尚未构建');
  const [pluginLoadStatus, setPluginLoadStatus] = useState('未发布');
  const [compilerFailureStage, setCompilerFailureStage] = useState('build');
  const [compilerSuccess, setCompilerSuccess] = useState(null);
  const [isCopied, setIsCopied] = useState(false);
  const [editError, setEditError] = useState(null);
  const [legacyWorkspaceError, setLegacyWorkspaceError] = useState(null);
  const legacyWorkspaceBlockedRef = useRef(false);
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
    if (embedded) storageRef.current = createStudioStorage(window.chrome?.webview, m => { if (m.name === effectNameRef.current) setStorageStatus(m.text); });
    if (!blocklyDivRef.current) return;

    const ws = Blockly.inject(blocklyDivRef.current, {
      ...DEFAULT_INJECT_OPTIONS,
      toolbox: EFFECT_STUDIO_TOOLBOX
    });
    workspaceRef.current = ws;

    let localDraft;
    try { localDraft = JSON.parse(sessionStorage.getItem('aura-effect-draft')); }
    catch { legacyWorkspaceBlockedRef.current = true; setLegacyWorkspaceError('恢复草稿 JSON 已损坏；原记录已保留，请打开有效工程。'); }
    // 默认加载第一个样例模板
    const defaultPreset = EFFECT_PRESETS[0];
    if (localDraft?.json) {
      try {
        assertNoLegacyEventPulse(localDraft.json);
        loadSafeWorkspaceJson(localDraft.json, ws); setEffectName(localDraft.name); effectNameRef.current = localDraft.name;
        publicationRef.current = normalizePublication(localDraft.publication); setPublication(publicationRef.current);
      } catch (err) { legacyWorkspaceBlockedRef.current = true; setLegacyWorkspaceError(err.message); }
    } else if (config?.blockly_effects?.[activeEffectName]?.blockly_json) {
      try { loadSafeWorkspaceJson(config.blockly_effects[activeEffectName].blockly_json, ws, true); }
      catch (err) { legacyWorkspaceBlockedRef.current = true; setLegacyWorkspaceError(err.message); }
    } else if (defaultPreset?.blocklyJson) {
      loadSafeWorkspaceJson(defaultPreset.blocklyJson, ws);
    }
    retainedWorkspaceValidRef.current = !legacyWorkspaceBlockedRef.current;

    const onWorkspaceChange = (event) => {
      if (event?.isUiEvent) return;
      if (!workspaceRef.current || workspaceStatusRef.current === WORKSPACE_STATUS.DESTROYED) return;
      const draft = currentDraft();
      if (draft) {
        setProgramSnapshot(draft);
        setCapabilityManifest(deriveEffectCapabilities(draft));
      }
      postRevision();
      try {
        compiledJsRef.current = JsTranspiler.compile(ws, publicationRef.current);
        previewClockRef.current = { elapsed: 0, last: null };
        setCppCode(CppTranspiler.transpile(effectNameRef.current, ws, publicationRef.current));
        setEditError(null);
      } catch (err) {
        compiledJsRef.current = null;
        setEditError(err.message);
      }
      if (event?.recordUndo !== false && event) queueDraft();
    };

    ws.addChangeListener(onWorkspaceChange);
    workspaceStatusRef.current = WORKSPACE_STATUS.READY;
    setWorkspaceStatus(WORKSPACE_STATUS.READY);
    onWorkspaceChange();
    observeDraft();

    const handleResize = () => Blockly.svgResize(ws);
    window.addEventListener('resize', handleResize);
    const observer = new ResizeObserver(handleResize);
    observer.observe(blocklyDivRef.current);

    return () => {
      window.removeEventListener('resize', handleResize);
      observer.disconnect();
      workspaceStatusRef.current = WORKSPACE_STATUS.DESTROYED;
      setWorkspaceStatus(WORKSPACE_STATUS.DESTROYED);
      setProgramSnapshot(null);
      try { if (!legacyWorkspaceBlockedRef.current && ws) sessionStorage.setItem('aura-effect-draft', JSON.stringify({ name: effectNameRef.current, publication: publicationRef.current, json: Blockly.serialization.workspaces.save(ws) })); } catch {}
      ws.dispose();
      workspaceRef.current = null;
      chatToolsRef.current.cancel();
      storageRef.current?.dispose(); storageRef.current = null;
    };
  }, []);

  // 当 activeEffectName 从外部改变时载入
  useEffect(() => {
    if (activeEffectName && workspaceRef.current && (activeEffectName !== effectNameRef.current || !configLoadedRef.current && config || legacyWorkspaceBlockedRef.current)) {
      if (legacyWorkspaceBlockedRef.current && retainedWorkspaceValidRef.current && activeEffectName === effectNameRef.current) {
        legacyWorkspaceBlockedRef.current = false; setLegacyWorkspaceError(null);
        return; // Returning to the retained project preserves unsaved edits.
      }
      configLoadedRef.current = !!config;
      setBenchOpen(false);
      const effectData = config?.blockly_effects?.[activeEffectName];
      Blockly.Events.disable();
      try {
        loadSafeWorkspaceJson(effectData?.blockly_json || { blocks: [] }, workspaceRef.current);
        retainedWorkspaceValidRef.current = true;
        legacyWorkspaceBlockedRef.current = false;
        setLegacyWorkspaceError(null);
      } catch (err) {
        legacyWorkspaceBlockedRef.current = true;
        setLegacyWorkspaceError(err.message);
        showToast?.(err.message, 'error');
        return; // Keep the retained draft and its identity; never autosave under the rejected name.
      } finally { Blockly.Events.enable(); }
      publicationRef.current = normalizePublication(effectData?.publication); setPublication(publicationRef.current);
      setBuildResult('尚未构建'); setPluginLoadStatus(effectData?.applied_plugin_name ? '已有发布记录' : '未发布');
      setEffectName(activeEffectName);
      effectNameRef.current = activeEffectName;
      const draft = currentDraft();
      if (draft) {
        setProgramSnapshot(draft);
        setCapabilityManifest(deriveEffectCapabilities(draft));
      }
      postRevision();
      observeDraft();
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
    queueDraft();
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
    try { loadSafeWorkspaceJson(preset.blocklyJson, workspaceRef.current); }
    catch (err) { showToast?.(err.message, 'error'); return; }
    legacyWorkspaceBlockedRef.current = false;
    retainedWorkspaceValidRef.current = true;
    setLegacyWorkspaceError(null);
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
    if (legacyWorkspaceError) { showToast?.(legacyWorkspaceError, 'error'); return; }
    if (recovery) { showToast?.('请先恢复或放弃可恢复草稿。', 'error'); return; }
    const name = effectName.trim();
    if (!/^[a-zA-Z_][a-zA-Z0-9_]{0,47}$/.test(name)) {
      showToast?.('名称需以字母或下划线开头，最多 48 个字符', 'error'); return;
    }
    setIsCompiling(true);
    try {
      const json = Blockly.serialization.workspaces.save(workspaceRef.current);
      let build;
      if (apply) {
        if (deriveEffectCapabilities(currentDraft()).diagnostics.length) throw new Error('请先解决工程能力验证错误');
        validateCandidate(currentDraft());
        if (embedded) await checkpointDraft('before_publish');
        setBuildResult('正在构建并发布…'); setPluginLoadStatus('等待发布确认'); setCompilerLog('正在准备发布光效…');
        build = await stageEffect(name, workspaceRef.current, CppTranspiler, setCompilerLog, publication);
      }
      const next = effectConfig(config, name, json, build, publication);
      const ok = await onSaveConfig(next, apply ? config : undefined);
      if (!ok) throw new Error('配置保存失败，请重试；原有运行版本保留');
      if (embedded) { const result = await storageRef.current.request('saved', currentDraft()); setStorageStatus(result.text); setSnapshots(result.snapshots || []); }
      setCompilerSuccess(true);
      if (apply) { setBuildResult('构建通过'); setPluginLoadStatus('daemon 已确认加载'); }
      if (apply) setIsPlaying(false);
      showToast?.(apply ? '光效已发布，配置已写入；daemon 将在下次热重载后应用' : '草稿已保存，正在运行的版本保持不变', 'success');
    } catch (err) {
      if (apply) { setBuildResult('发布失败'); setPluginLoadStatus('原发布记录保留'); }
      setCompilerSuccess(false); setCompilerFailureStage(err.stage === 'daemon_reload' ? 'plugin_load' : 'build'); setCompilerLog([err.message, err.detail].filter(Boolean).join('\n\n')); showToast?.(err.message, 'error');
    } finally { setIsCompiling(false); }
  };
  const handleBuild = async () => {
    if (isCompiling || !workspaceRef.current || legacyWorkspaceError || editError) return;
    setIsCompiling(true); setBuildResult('正在构建…'); setCompilerLog('正在构建（仅编译）…'); setCompilerSuccess(null);
    try {
      if (deriveEffectCapabilities(currentDraft()).diagnostics.length) throw new Error('请先解决工程能力验证错误');
      validateCandidate(currentDraft());
      const result = await buildEffect(effectName, workspaceRef.current, CppTranspiler, publication);
      setCompilerLog(result.compiler_output || '构建成功；尚未发布'); setCompilerSuccess(true); setBuildResult('构建通过');
    } catch (err) { setBuildResult('构建失败'); setCompilerSuccess(false); setCompilerFailureStage(err.stage === 'daemon_reload' ? 'plugin_load' : 'build'); setCompilerLog([err.message, err.detail].filter(Boolean).join('\n')); }
    finally { setIsCompiling(false); }
  };
  const bundleAction = async operation => {
    if (isCompiling || recovery || bundleDialog) return;
    setIsPlaying(false); setBundleError('');
    try {
      if (operation === 'export') {
        const payload = createBundlePayload(currentDraft());
        setBundleDescription(''); setBundleAuthor(''); setBundleTags(''); setBundleDialog({ operation, payload });
      } else {
        setIsCompiling(true);
        const result = await bundleBridge.current.request('import');
        if (result?.cancelled) return;
        const payload = validateBundlePayload(result.payload);
        const catalog = config?.blockly_effects || {};
        setBundleName(Object.hasOwn(catalog, payload.project.name) ? nextImportedName(payload.project.name, catalog) : payload.project.name);
        setBundleDialog({ operation, payload });
      }
    } catch (e) { setBundleError(e.message); showToast?.(e.message, 'error'); }
    finally { setIsCompiling(false); }
  };
  const confirmBundle = async () => {
    if (isCompiling || !bundleDialog) return;
    setIsCompiling(true); setBundleError('');
    try {
      if (bundleDialog.operation === 'import') {
        const draft = importedDraft(bundleDialog.payload, config?.blockly_effects, bundleName);
        await onImportDraft(draft); setStorageStatus('工程包已导入为新草稿；尚未发布。');
      } else {
        const payload = createBundlePayload(bundleDialog.payload.project, { description: bundleDescription, author: bundleAuthor, tags: bundleTags.split(',').map(t => t.trim()).filter(Boolean) });
        const result = await bundleBridge.current.request('export', payload);
        if (result?.cancelled) return;
        setStorageStatus('工程包已导出；仅包含源工程与元数据。');
      }
      setBundleDialog(null);
    } catch (e) { setBundleError(e.message); }
    finally { setIsCompiling(false); }
  };
  const commandState = () => ({ busy: isCompiling || !!bundleDialog, recovery: !!recovery, hasSnapshots: snapshots.length > 0, embedded,
    workType: 'effect', validation: legacyWorkspaceError || editError || capabilityManifest?.diagnostics.map(c => CAPABILITY_DIAGNOSTICS[c]).join(' ') || '验证通过' });
  useEffect(() => listenStudioCommands(window.chrome?.webview, {
    save: () => saveWorkspace(false), publish: () => { if (!editError) saveWorkspace(true); },
    build: handleBuild, bench: openBench, preview: () => { if (!isCompiling && !benchOpen) setIsPlaying(v => !v); },
    details: () => setIsCodeModalOpen(true),
    run_scenario: () => { openBench(); setRunToken(v => v + 1); },
    restore_snapshot: () => { if (snapshotId) restoreDraft('restore_snapshot', { snapshot_id: snapshotId }); else document.querySelector('[aria-label="草稿快照"]')?.focus(); },
    export_bundle: () => bundleAction('export'), import_bundle: () => bundleAction('import'),
    validate: () => {
      try { CppTranspiler.transpile(effectName, workspaceRef.current, publication); JsTranspiler.compile(workspaceRef.current, publication); setEditError(null); }
      catch (err) { setEditError(err.message); }
    }
  }, commandState), [embedded, effectName, publication, config, isCompiling, editError, legacyWorkspaceError, benchOpen, recovery, snapshots, snapshotId, bundleDialog, capabilityManifest]);
  useEffect(() => {
    onShellState?.({ name: effectName, playing: isPlaying, busy: isCompiling || !!bundleDialog, recovery: !!recovery, hasSnapshots: snapshots.length > 0, embedded,
      validation: legacyWorkspaceError || editError || capabilityManifest?.diagnostics.map(c => CAPABILITY_DIAGNOSTICS[c]).join(' ') || '验证通过',
      build: buildResult, plugin: pluginLoadStatus,
      lifecycle: getEffectLifecycleStatus(config?.blockly_effects?.[effectName]).label,
      diagnostics: (compilerLog || '').slice(0, 16384) });
  }, [effectName, isPlaying, isCompiling, buildResult, pluginLoadStatus, compilerSuccess, compilerLog, editError, legacyWorkspaceError, capabilityManifest, config, onShellState, recovery, snapshots, embedded, bundleDialog]);

  const handleCompileAndReload = () => saveWorkspace(true);
  const handleSaveToConfig = () => saveWorkspace(false);

  const currentEffectData = config?.blockly_effects?.[effectName];
  const lifecycle = getEffectLifecycleStatus(currentEffectData);

  return (
    <div data-workspace-status={workspaceStatus} className={`flex min-w-0 flex-col gap-3 p-1 h-full ${embedded ? 'min-h-[320px]' : 'min-h-[560px]'}`}>
      {embedded && <div className="shrink-0 flex flex-wrap items-center gap-2 text-xs" data-studio-storage>
        <span data-autosave-status role="status">{storageStatus || '草稿自动保存就绪'}</span>
        {recovery && <div role="alert" data-recovery-offer className="flex flex-wrap gap-2"><strong>可恢复草稿 · {new Date(recovery.updated_utc).toLocaleString()}</strong>
          <button data-recovery-restore onClick={() => restoreDraft('restore_recovery')}>恢复草稿</button><button data-recovery-discard onClick={discardRecovery}>放弃恢复</button></div>}
        <select className="min-w-0 max-w-full rounded bg-md-surface-container" aria-label="草稿快照" data-studio-snapshots value={snapshotId} onChange={e => setSnapshotId(e.target.value)}><option value="">选择快照（{snapshots.length}）</option>
          {snapshots.map(s => <option key={s.id} value={s.id}>{s.reason === 'before_ai_apply' ? 'AI 应用前' : '发布前'} · {new Date(s.created_utc).toLocaleString()}</option>)}</select>
        <button data-snapshot-restore disabled={!snapshotId || !!recovery || isCompiling} onClick={() => restoreDraft('restore_snapshot', { snapshot_id: snapshotId })}>恢复快照至草稿</button>
      </div>}
      {capabilityManifest && <div data-effect-capabilities className="shrink-0 text-xs text-md-on-surface-variant">
        工程输入：{capabilityManifest.inputs.map(c => CAPABILITY_LABELS[c]).join('、') || '无需外部输入'} · 输出：{capabilityManifest.outputs.map(c => CAPABILITY_LABELS[c]).join('、') || '无'}
        {capabilityManifest.diagnostics.map(code => <p role="alert" key={code}>{CAPABILITY_DIAGNOSTICS[code]}</p>)}
      </div>}
      {embedded && <details data-effect-extensions className="shrink-0 rounded-md-lg border border-md-outline-variant bg-md-surface-container-low p-3 text-xs">
        <summary className="cursor-pointer font-semibold">灯效扩展 · {Object.values(config?.blockly_effects || {}).filter(x => x?.applied_plugin_name).length} 个已发布 / {Object.keys(config?.blockly_effects || {}).length} 个工程</summary>
        <p className="mt-2 text-md-on-surface-variant">导入 .auraeffect 源工程包后可先预览、验证，再由你手动发布为独立插件 DLL；后续添加新灯效无需重新打包 Aura。导入不会直接执行第三方代码或改写正在运行的灯效。</p>
        <div className="mt-2 flex flex-wrap gap-2">
          <button type="button" disabled={isCompiling || !!recovery || !!bundleDialog} onClick={() => bundleAction('import')}
            className="rounded-md-full bg-md-primary-container px-3 py-1.5 font-semibold text-md-on-primary-container disabled:opacity-50">导入灯效包</button>
          <span className="text-md-on-surface-variant">需要发布时使用工作室上方的「发布」；原生 DLL 的运行仍需要本机 MSVC 编译环境。</span>
        </div>
        <ul className="mt-2 max-h-28 overflow-y-auto">
          {Object.entries(config?.blockly_effects || {}).map(([name, value]) =>
            <li key={name} className="py-1"><span className="font-mono">{name}</span> · {value?.applied_plugin_name ? '已发布插件' : '未发布草稿'}</li>)}
        </ul>
      </details>}
      <ProgramCatalog embedded={embedded} snapshot={workspaceStatus === WORKSPACE_STATUS.READY ? programSnapshot : null} onStatus={setStorageStatus} onLinkLighting={linkProgramToLighting} />
      <InputSourcesControl embedded={embedded} />
      <div className="flex shrink-0 flex-wrap items-center gap-2 rounded-md-lg border border-md-primary/40 bg-md-primary-container/30 px-3 py-2 text-xs text-md-on-surface" aria-label="当前光效生命周期">
        {!embedded && <button onClick={openBench} disabled={isCompiling}>测试台</button>}
        <strong className="font-mono min-w-0 max-w-full break-all" title={effectName}>{effectName}</strong>
        <span className={`rounded-md-full border px-2 py-0.5 font-semibold ${lifecycle.badgeClass}`}>{lifecycle.label}</span>
        <button type="button" aria-expanded={showLifecycleControls} onClick={() => setShowLifecycleControls(!showLifecycleControls)} className="rounded-md-full border border-md-primary bg-md-primary-container px-2 py-1 font-bold text-md-on-primary-container">
          {publication.mode === 'one_shot' ? `单次光效 · 淡出 ${publication.fade_out_ms} ms` : '持续光效'} · 编辑
        </button>
        <span className="text-md-on-surface-variant">{publication.mode === 'one_shot' ? 'Blockly 序列结束后淡出并结束' : '持续运行'}</span>
      </div>
      {showLifecycleControls && <div className="flex shrink-0 flex-wrap items-center gap-3 rounded-md-lg bg-md-surface-container px-3 py-2 text-xs text-md-on-surface">
        <label>播放方式 <select aria-label="播放方式" disabled={isCompiling} value={publication.mode} onChange={e => setPublication(normalizePublication({ ...publication, mode: e.target.value }))}>
          <option value="continuous">持续光效</option><option value="one_shot">单次光效</option>
        </select></label>
        {publication.mode === 'one_shot' && <label>序列结束后淡出 <input type="number" min="0" max="60000" disabled={isCompiling} value={publication.fade_out_ms} onChange={e => setPublication(normalizePublication({ ...publication, fade_out_ms: Math.min(60000, Math.max(0, Math.trunc(Number(e.target.value) || 0))) }))} className="w-20" /> ms</label>}
      </div>}
      {!embedded && compact && <button type="button" aria-expanded={showCompactControls} onClick={() => setShowCompactControls(!showCompactControls)}
        className="shrink-0 rounded-md-sm bg-md-surface-container px-3 py-1.5 text-left text-xs font-semibold">
        {showCompactControls ? '收起作品操作' : '作品操作 · 保存与发布'}
      </button>}
      <div className={embedded ? 'hidden' : compact ? (showCompactControls ? 'flex max-h-[45%] shrink-0 flex-col gap-3 overflow-y-auto' : 'hidden') : 'contents'}>
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
          </div>
        </div>

        {/* 预设模板切换 */}
        <div className="flex items-center gap-2">
          <span className="text-xs font-medium text-md-on-surface-variant">模版样例:</span>
          {EFFECT_PRESETS.map((p) => (
            <button
              key={p.id}
              title={`${p.description} · 输入：${p.manifest.inputs.map(c => CAPABILITY_LABELS[c]).join('、') || '无'}`}
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
            disabled={isCompiling || !!legacyWorkspaceError}
            className="h-9 px-3.5 flex items-center gap-1.5 rounded-md-full bg-md-surface-container border border-md-outline-variant text-md-on-surface hover:bg-md-surface-container-high active:scale-95 transition-all text-xs font-semibold cursor-pointer disabled:opacity-50"
            title="保存当前积木草稿，保持正在运行的硬件版本不变"
          >
            <Save className="w-4 h-4 text-md-primary" />
            <span>保存草稿</span>
          </button>

          <button
            onClick={handleCompileAndReload}
            disabled={isCompiling || !!editError || !!legacyWorkspaceError}
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
      <details className="shrink-0 rounded-md-md bg-md-surface-container-lowest border border-md-outline-variant text-xs">
        <summary className="cursor-pointer px-3 py-2 font-medium">光效预览 · 本地模拟输入</summary>
        <div className="flex flex-wrap items-center justify-between gap-4 px-4 py-2">

        <div className="flex items-center gap-3">
          <label className="flex items-center gap-2">
            <span className="text-md-on-surface-variant">血量：</span>
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
              <option value="carried">随身携带</option>
              <option value="planted">已安放</option>
              <option value="defused">已拆除</option>
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
                title={`按键 ${idx}: RGB(${c[0]}, ${c[1]}, ${c[2]})`}
              />
            ))}
            <span className="text-[10px] text-md-on-surface-variant ml-1 font-mono">...68K</span>
          </div>
        </div>
      </div>

      </details>
      {(legacyWorkspaceError || editError) && <div role="alert" className="text-sm text-md-error">{legacyWorkspaceError || editError}</div>}
      </div>
      {/* Google Blockly 主画布 */}
      {bundleDialog && createPortal(<div data-bundle-dialog role="dialog" aria-modal="true" aria-label="工程包确认" className="fixed inset-0 z-[100] flex items-center justify-center bg-black/40 p-3" onKeyDown={e => { if (e.key === 'Escape' && !isCompiling) setBundleDialog(null); }}>
        <div className="w-full max-w-lg max-h-[85vh] overflow-y-auto rounded-md-xl border border-md-outline-variant bg-md-surface-container p-4 space-y-3">
          <h2 className="font-bold">{bundleDialog.operation === 'import' ? '导入工程包 · 创建新草稿' : '导出工程包'}</h2>
          <p className="break-words text-sm">{bundleDialog.payload.manifest.name} · {bundleDialog.payload.manifest.capabilities.inputs.map(i => CAPABILITY_LABELS[i] || i).join('、') || '无外部输入'}</p>
          {bundleDialog.operation === 'import' ? <>
            <p className="whitespace-pre-wrap break-words text-sm">{bundleDialog.payload.manifest.description || '无描述'}<br/>作者：{bundleDialog.payload.manifest.author || '未提供'}<br/>标签：{bundleDialog.payload.manifest.tags.join('、') || '无'}<br/>创建版本：{bundleDialog.payload.manifest.created_with_version}</p>
            <label className="block">新草稿名称<input autoFocus data-bundle-name value={bundleName} maxLength={48} onChange={e => setBundleName(e.target.value)} className="block w-full bg-md-surface p-2 rounded"/></label>
            <p className="text-sm">确认后保存为独立草稿。不会覆盖现有工程，也不会构建或发布。</p>
          </> : <>
            <label className="block">描述<textarea autoFocus value={bundleDescription} maxLength={1024} onChange={e => setBundleDescription(e.target.value)} className="block w-full bg-md-surface p-2 rounded"/></label>
            <label className="block">作者（可选）<input value={bundleAuthor} maxLength={80} onChange={e => setBundleAuthor(e.target.value)} className="block w-full bg-md-surface p-2 rounded"/></label>
            <label className="block">标签（逗号分隔，最多 12 个）<input value={bundleTags} maxLength={492} onChange={e => setBundleTags(e.target.value)} className="block w-full bg-md-surface p-2 rounded"/></label>
            <p className="text-sm">仅包含当前源工程、播放方式与以上元数据。</p>
          </>}
          {bundleError && <p role="alert" className="break-words text-md-error">{bundleError}</p>}
          <div className="flex flex-wrap gap-3"><button data-bundle-confirm disabled={isCompiling} onClick={confirmBundle} className="rounded bg-md-primary text-md-on-primary px-3 py-2">{bundleDialog.operation === 'import' ? '确认创建草稿' : '选择导出位置'}</button><button disabled={isCompiling} onClick={() => setBundleDialog(null)}>取消</button></div>
        </div>
      </div>, document.body)}
      {benchOpen && <TestBench getProject={currentDraft} runToken={runToken} onClose={() => setBenchOpen(false)}/>}
      <div inert={recovery ? '' : undefined} className={`${benchOpen ? 'hidden' : 'flex-1'} min-h-[240px] w-full relative rounded-md-lg overflow-hidden border border-md-outline-variant shadow-md-level1 bg-md-surface-container-low`}>
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
