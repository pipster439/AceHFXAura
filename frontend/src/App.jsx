import React, { useState, useEffect, useCallback, useRef } from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import Sidebar from './components/Sidebar';
import KeyboardVisualizer from './components/KeyboardVisualizer';
import LightingSettings from './components/LightingSettings';
import PerKeyStudio from './components/PerKeyStudio';
import RulesSettings from './components/RulesSettings';
import GsiSettings from './components/GsiSettings';
import ProfileManager from './components/ProfileManager';
import Toast from './components/Toast';
import GameModeModal from './components/GameModeModal';
import EffectStudio from './components/EffectStudio';
import OrchestratorStudio from './components/OrchestratorStudio';
import { GRADIENT_PRESETS } from './constants/keyboardLayout';
import { DEFAULT_KEYBOARD_BG, TACTICAL_PALETTE } from './tokens/keyboardPresets.tokens';
import { rgbToHex, hexToRgb } from './utils/color';

export default function App() {
  const [isCollapsed, setIsCollapsed] = useState(false);
  const [activeTab, setActiveTab] = useState('lighting');
  const [config, setConfig] = useState(null);
  const [currentProfileName, setCurrentProfileName] = useState('desktop');

  // 当前激活方案的核心状态
  const [currentEffect, setCurrentEffect] = useState('static');
  const [isMasterLightOn, setIsMasterLightOn] = useState(true);
  const [isAnalogEnabled, setIsAnalogEnabled] = useState(false);
  const [brightnessVal, setBrightnessVal] = useState(1.0);
  const [speedIndex, setSpeedIndex] = useState(2);
  const [currentDirection, setCurrentDirection] = useState('diag_dl');
  const [thicknessVal, setThicknessVal] = useState(1.0);
  const [gradientStops, setGradientStops] = useState(GRADIENT_PRESETS.default);
  const [selectedStopId, setSelectedStopId] = useState(1);
  const [isStarryRandom, setIsStarryRandom] = useState(false);
  const [bgColor, setBgColor] = useState(DEFAULT_KEYBOARD_BG);
  const [fpsVal, setFpsVal] = useState(25);

  // MD3E 主题模式 (默认暗黑)
  const [theme, setTheme] = useState(() => {
    return localStorage.getItem('aura-theme') || 'dark';
  });

  const toggleTheme = () => {
    const nextTheme = theme === 'dark' ? 'light' : 'dark';
    setTheme(nextTheme);
    localStorage.setItem('aura-theme', nextTheme);
    document.documentElement.setAttribute('data-theme', nextTheme);
  };

  useEffect(() => {
    document.documentElement.setAttribute('data-theme', theme);
  }, [theme]);

  // 逐键涂装选中集合
  const [selectedKeyNames, setSelectedKeyNames] = useState(new Set());

  // Blockly 光效工坊实时推流帧 (68 颗按键 RGB)
  const [blocklyFrame, setBlocklyFrame] = useState(null);
  const lastPreviewPushRef = useRef(0);
  const handleBlocklyPreviewFrame = useCallback((frame) => {
    setBlocklyFrame(frame);
    const now = performance.now();
    if (now - lastPreviewPushRef.current > 45) { // 限制 ~22 FPS 推流给 daemon
      lastPreviewPushRef.current = now;
      if (frame && frame.length === 68) {
        fetch('/api/preview', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ colors: frame })
        }).catch(() => {});
      }
    }
  }, []);

  // 状态反馈与服务存活
  const [toast, setToast] = useState(null);
  const [isSaving, setIsSaving] = useState(false);
  const [isServiceOnline, setIsServiceOnline] = useState(true);
  const [isGameModalOpen, setIsGameModalOpen] = useState(false);

  const isInitializedRef = useRef(false);
  const isSwitchingProfileRef = useRef(false);
  const saveTimeoutRef = useRef(null);
  const configRef = useRef(null);
  configRef.current = config;

  const showToast = (message, type = 'success') => {
    setToast({ message, type });
    setTimeout(() => setToast(null), 3000);
  };

  // 方案数据加载到 UI 控件
  const loadProfileToState = useCallback((profileData) => {
    if (!profileData) return;

    setCurrentEffect(profileData.type || 'static');
    setIsAnalogEnabled(profileData.analog === true);

    if (profileData.direction) {
      setCurrentDirection(profileData.direction);
    } else {
      setCurrentDirection('diag_dl');
    }

    if (typeof profileData.thickness === 'number' && Number.isFinite(profileData.thickness)) {
      setThicknessVal(Math.max(0.1, Math.min(5.0, profileData.thickness)));
    } else {
      setThicknessVal(1.0);
    }

    setIsStarryRandom(Boolean(profileData.random_colors));

    if (profileData.bg) {
      setBgColor(rgbToHex(profileData.bg));
    } else if (profileData.type === 'reactive' || profileData.type === 'ripple') {
      setBgColor(rgbToHex([0, 5, 15]));
    } else {
      setBgColor(DEFAULT_KEYBOARD_BG);
    }

    if (typeof profileData.brightness === 'number' && Number.isFinite(profileData.brightness)) {
      const b = profileData.brightness > 1.0 ? profileData.brightness / 255.0 : profileData.brightness;
      setBrightnessVal(Math.max(0, Math.min(1.0, b)));
    } else {
      setBrightnessVal(1.0);
    }

    if (typeof profileData.speed_index === 'number' && Number.isFinite(profileData.speed_index)) {
      const idx = Math.round(profileData.speed_index);
      setSpeedIndex(Math.max(0, Math.min(2, idx)));
    } else if (profileData.period_ms && Number.isFinite(profileData.period_ms)) {
      if (profileData.period_ms > 4500) setSpeedIndex(0);
      else if (profileData.period_ms > 2400) setSpeedIndex(1);
      else setSpeedIndex(2);
    } else {
      setSpeedIndex(1);
    }

    if (profileData.color || profileData.color1) {
      const c1 = profileData.color || profileData.color1;
      const c2 = profileData.color2;
      const nextStops = [
        { id: 1, pos: 0.15, color: rgbToHex(c1) },
        { id: 2, pos: 0.85, color: c2 ? rgbToHex(c2) : TACTICAL_PALETTE[1].hex }
      ];
      setGradientStops(nextStops);
      setSelectedStopId(1);
    }

    if (typeof profileData.fps === 'number' && Number.isFinite(profileData.fps)) {
      setFpsVal(Math.max(10, Math.min(100, Math.round(profileData.fps))));
    } else {
      setFpsVal(25);
    }
  }, []);

  // 读取配置
  const fetchConfig = useCallback(async () => {
    try {
      const res = await fetch('/api/config');
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data = await res.json();
      setConfig(data);

      if (typeof data.fps === 'number' && Number.isFinite(data.fps)) {
        setFpsVal(Math.max(10, Math.min(100, Math.round(data.fps))));
      }

      const defaultProf = data.default_profile || Object.keys(data.profiles || {})[0] || 'desktop';
      setCurrentProfileName(defaultProf);
      loadProfileToState(data.profiles?.[defaultProf]);

      setTimeout(() => {
        isInitializedRef.current = true;
      }, 150);
    } catch (err) {
      console.warn('获取配置异常:', err);
    }
  }, [loadProfileToState]);

  useEffect(() => {
    fetchConfig();
  }, [fetchConfig]);

  // 心跳与游戏模式检测
  useEffect(() => {
    let consecutiveFails = 0;
    const interval = setInterval(async () => {
      try {
        const res = await fetch('/api/status', { cache: 'no-store' });
        if (res.ok) {
          consecutiveFails = 0;
          setIsServiceOnline(true);
          if (isGameModalOpen) {
            setIsGameModalOpen(false);
            fetchConfig();
          }
        } else {
          consecutiveFails++;
        }
      } catch (e) {
        consecutiveFails++;
      }

      if (consecutiveFails >= 2) {
        setIsServiceOnline(false);
        setIsGameModalOpen(true);
      }
    }, 1500);

    return () => clearInterval(interval);
  }, [isGameModalOpen, fetchConfig]);

  // 直接保存配置到后端（无阻断静默提交）
  const saveConfigDirectly = useCallback(async (newConfig) => {
    if (!newConfig) return;
    try {
      setIsSaving(true);
      const res = await fetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(newConfig)
      });
      if (!res.ok) {
        const data = await res.json().catch(() => ({}));
        console.warn('实时配置保存失败:', data.message);
      }
    } catch (err) {
      console.warn('实时配置保存网络异常:', err.message);
    } finally {
      setIsSaving(false);
    }
  }, []);

  // 方案切换 (实时自动激活至硬件)
  const handleProfileChange = (pname) => {
    if (!configRef.current?.profiles?.[pname]) return;
    isSwitchingProfileRef.current = true;
    setCurrentProfileName(pname);
    loadProfileToState(configRef.current.profiles[pname]);
    setSelectedKeyNames(new Set());

    const nextConfig = {
      ...configRef.current,
      default_profile: pname
    };
    setConfig(nextConfig);
    saveConfigDirectly(nextConfig);

    setTimeout(() => {
      isSwitchingProfileRef.current = false;
    }, 150);
  };

  // 防抖自动同步当前 UI 状态到后端与内存
  const queueAutoSync = useCallback(() => {
    if (!isInitializedRef.current || isSwitchingProfileRef.current) return;
    if (saveTimeoutRef.current) clearTimeout(saveTimeoutRef.current);

    saveTimeoutRef.current = setTimeout(() => {
      const baseConfig = configRef.current;
      if (!baseConfig || !baseConfig.profiles) return;
      const pName = currentProfileName;
      if (!baseConfig.profiles[pName]) return;

      const cloned = JSON.parse(JSON.stringify(baseConfig));
      const prof = cloned.profiles[pName];

      if (activeTab === 'perkey') {
        prof.type = 'custom_keymap';
        prof.bg = hexToRgb(bgColor);
        prof.brightness = isMasterLightOn ? brightnessVal : 0;
      } else {
        prof.type = currentEffect;
        prof.analog = currentEffect === 'static' ? isAnalogEnabled : false;
        prof.brightness = isMasterLightOn ? brightnessVal : 0;
        prof.speed_index = speedIndex;
        prof.direction = currentDirection;
        prof.thickness = thicknessVal;
        prof.period_ms = speedIndex === 0 ? 5500 : speedIndex === 1 ? 3200 : 1600;

        if (gradientStops && gradientStops.length > 0) {
          prof.color = hexToRgb(gradientStops[0].color);
          prof.color1 = hexToRgb(gradientStops[0].color);
        }
        if (gradientStops && gradientStops.length > 1) {
          prof.color2 = hexToRgb(gradientStops[1].color);
        }

        if (currentEffect === 'starry_night') {
          prof.random_colors = Boolean(isStarryRandom);
        } else {
          delete prof.random_colors;
        }

        if (['reactive', 'ripple', 'custom_keymap', 'static'].includes(currentEffect)) {
          prof.bg = hexToRgb(bgColor);
        } else {
          delete prof.bg;
        }
        prof.fps = fpsVal;
      }

      cloned.fps = fpsVal;
      cloned.default_profile = pName;

      setConfig(cloned);
      saveConfigDirectly(cloned);
    }, 120);
  }, [
    currentProfileName,
    activeTab,
    currentEffect,
    isMasterLightOn,
    isAnalogEnabled,
    brightnessVal,
    currentDirection,
    thicknessVal,
    isStarryRandom,
    speedIndex,
    gradientStops,
    bgColor,
    fpsVal,
    saveConfigDirectly
  ]);

  // 监听所有调光与硬件参数，自动实时推流生效
  useEffect(() => {
    if (!isInitializedRef.current || isSwitchingProfileRef.current) return;
    queueAutoSync();
  }, [
    currentEffect,
    isMasterLightOn,
    isAnalogEnabled,
    brightnessVal,
    fpsVal,
    speedIndex,
    currentDirection,
    thicknessVal,
    isStarryRandom,
    gradientStops,
    bgColor,
    queueAutoSync
  ]);

  // 逐键点选交互
  const handleToggleKeySelection = (keyName) => {
    setSelectedKeyNames((prev) => {
      const next = new Set(prev);
      if (next.has(keyName)) next.delete(keyName);
      else next.add(keyName);
      return next;
    });
  };

  const handleSelectGroup = (group) => {
    const next = new Set(selectedKeyNames);
    if (group === 'WASD') {
      ['W', 'A', 'S', 'D'].forEach((k) => next.add(k));
    } else if (group === 'ARROWS') {
      ['UP', 'DOWN', 'LEFT', 'RIGHT'].forEach((k) => next.add(k));
    } else if (group === 'NUMBERS') {
      ['1','2','3','4','5','6','7','8','9','0','-','='].forEach((k) => next.add(k));
    } else if (group === 'MODIFIERS') {
      ['L_CTRL','L_WIN','L_ALT','SPACE','R_ALT','FN','COPILOT','L_SHIFT','R_SHIFT','CAPS','TAB','ENTER'].forEach((k) => next.add(k));
    } else if (group === 'ALL') {
      ['ESC','1','2','3','4','5','6','7','8','9','0','-','=','BACKSPACE','INS',
       'TAB','Q','W','E','R','T','Y','U','I','O','P','[',']','\\','DEL',
       'CAPS','A','S','D','F','G','H','J','K','L',';','\'','ENTER','PGUP',
       'L_SHIFT','Z','X','C','V','B','N','M',',','.','/','R_SHIFT','UP','PGDN',
       'L_CTRL','L_WIN','L_ALT','SPACE','R_ALT','FN','COPILOT','LEFT','DOWN','RIGHT'].forEach((k) => next.add(k));
    }
    setSelectedKeyNames(next);
  };

  const handleClearSelection = () => {
    setSelectedKeyNames(new Set());
  };

  const handleApplyColorToSelected = (hex) => {
    if (!selectedKeyNames || selectedKeyNames.size === 0) {
      showToast('请先在虚拟键盘上选定按键', 'info');
      return;
    }
    const baseConfig = configRef.current;
    if (!baseConfig || !baseConfig.profiles) return;
    const cloned = JSON.parse(JSON.stringify(baseConfig));
    const prof = cloned.profiles[currentProfileName];
    if (!prof) return;
    if (!prof.keys) prof.keys = {};

    prof.type = 'custom_keymap';
    prof.bg = hexToRgb(bgColor);

    const rgb = hexToRgb(hex);
    selectedKeyNames.forEach((kname) => {
      prof.keys[kname] = rgb;
    });

    cloned.default_profile = currentProfileName;
    setConfig(cloned);
    saveConfigDirectly(cloned);
    showToast(`已涂装 ${selectedKeyNames.size} 个按键 (已实时生效)`);
  };

  const handleRemoveOverridesFromSelected = () => {
    if (!selectedKeyNames || selectedKeyNames.size === 0) {
      showToast('请先选择要清除自定义覆写的按键', 'info');
      return;
    }
    const baseConfig = configRef.current;
    if (!baseConfig || !baseConfig.profiles) return;
    const cloned = JSON.parse(JSON.stringify(baseConfig));
    const prof = cloned.profiles[currentProfileName];
    if (!prof?.keys) return;

    selectedKeyNames.forEach((kname) => {
      delete prof.keys[kname];
    });

    cloned.default_profile = currentProfileName;
    setConfig(cloned);
    saveConfigDirectly(cloned);
    showToast(`已清除 ${selectedKeyNames.size} 个按键的独立覆写 (已实时生效)`);
  };

  // 规则设置操作 (实时同步)
  const handleAddRule = (newRule = { process: '', profile: currentProfileName, suppress_web_ui: false }) => {
    const updated = {
      ...configRef.current,
      rules: [...(configRef.current?.rules || []), newRule]
    };
    setConfig(updated);
    saveConfigDirectly(updated);
  };

  const handleDeleteRule = (idx) => {
    const updatedRules = (configRef.current?.rules || []).filter((_, i) => i !== idx);
    const updated = { ...configRef.current, rules: updatedRules };
    setConfig(updated);
    saveConfigDirectly(updated);
  };

  const handleUpdateRule = (idx, patch) => {
    const updatedRules = (configRef.current?.rules || []).map((r, i) => (i === idx ? { ...r, ...patch } : r));
    const updated = { ...configRef.current, rules: updatedRules };
    setConfig(updated);
    saveConfigDirectly(updated);
  };

  // GSI 规则操作 (过滤空字段并实时同步)
  const handleUpdateGsiBindings = (updatedBindings) => {
    const cleaned = updatedBindings.filter(b => b && b.field && b.field.trim() !== '');
    const updated = { ...configRef.current, gsi_bindings: cleaned };
    setConfig(updated);
    saveConfigDirectly(updated);
  };

  // 方案管理操作 (设为默认立即同步至硬件)
  const handleSetDefaultProfile = (name) => {
    const nextConfig = { ...configRef.current, default_profile: name };
    setConfig(nextConfig);
    saveConfigDirectly(nextConfig);
    showToast(`已将 [${name}] 设为默认方案并实时生效！`);
  };

  const handleCloneProfile = (sourceName, newName) => {
    if (!configRef.current?.profiles?.[sourceName]) return;
    const clonedProf = JSON.parse(JSON.stringify(configRef.current.profiles[sourceName]));
    const nextConfig = {
      ...configRef.current,
      profiles: {
        ...configRef.current.profiles,
        [newName]: clonedProf
      }
    };
    setConfig(nextConfig);
    saveConfigDirectly(nextConfig);
    setCurrentProfileName(newName);
    loadProfileToState(clonedProf);
    showToast(`已克隆方案: ${newName}`);
  };

  const handleCreateProfile = (newName) => {
    const fresh = {
      type: 'static',
      color: [15, 23, 42],
      brightness: 1.0,
      speed_index: 1,
      period_ms: 3200,
      keys: {}
    };
    const nextConfig = {
      ...configRef.current,
      profiles: {
        ...configRef.current.profiles,
        [newName]: fresh
      }
    };
    setConfig(nextConfig);
    saveConfigDirectly(nextConfig);
    setCurrentProfileName(newName);
    loadProfileToState(fresh);
    showToast(`已新建方案: ${newName}`);
  };

  const handleDeleteProfile = (name) => {
    if (Object.keys(configRef.current?.profiles || {}).length <= 1) {
      showToast('至少需保留一个配置文件', 'error');
      return;
    }
    const nextProfiles = { ...configRef.current.profiles };
    delete nextProfiles[name];

    let nextDefault = configRef.current.default_profile;
    if (nextDefault === name) {
      nextDefault = Object.keys(nextProfiles)[0];
    }
    const nextCurrent = Object.keys(nextProfiles)[0];

    const nextConfig = {
      ...configRef.current,
      default_profile: nextDefault,
      profiles: nextProfiles
    };
    setConfig(nextConfig);
    saveConfigDirectly(nextConfig);
    setCurrentProfileName(nextCurrent);
    loadProfileToState(nextProfiles[nextCurrent]);
    showToast(`已删除方案: ${name}`);
  };

  return (
    <div className="flex h-screen w-screen bg-md-surface text-md-on-surface overflow-hidden font-sans select-none">
      {/* 左侧 MD3E Navigation Rail / Drawer */}
      <Sidebar
        isCollapsed={isCollapsed}
        setIsCollapsed={setIsCollapsed}
        activeTab={activeTab}
        setActiveTab={setActiveTab}
        currentProfileName={currentProfileName}
        onProfileChange={handleProfileChange}
        profiles={config?.profiles}
        defaultProfileName={config?.default_profile}
        isSaving={isSaving}
        isServiceOnline={isServiceOnline}
        theme={theme}
        onToggleTheme={toggleTheme}
      />

      {/* 右侧主工作区：上部常驻键盘 + 下部功能设置 */}
      <div className="flex-1 flex flex-col h-full overflow-y-auto overflow-x-hidden p-6 gap-6">
        {/* 常驻键盘可视化舞台 */}
        <KeyboardVisualizer
          activeTab={activeTab}
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
          onToggleKeySelection={handleToggleKeySelection}
          bgColor={bgColor}
          fpsVal={fpsVal}
          blocklyFrame={blocklyFrame}
        />

        {/* 下方功能设置面板 (随侧边栏 Tab 切换平滑物理弹簧过渡) */}
        <div className="flex-1">
          <AnimatePresence mode="wait">
            <motion.div
              key={activeTab}
              initial={{ opacity: 0, y: 12 }}
              animate={{ opacity: 1, y: 0 }}
              exit={{ opacity: 0, y: -12 }}
              transition={{ type: 'spring', stiffness: 380, damping: 28 }}
            >
              {activeTab === 'lighting' && (
                <LightingSettings
                  currentEffect={currentEffect}
                  setCurrentEffect={setCurrentEffect}
                  isMasterLightOn={isMasterLightOn}
                  setIsMasterLightOn={setIsMasterLightOn}
                  isAnalogEnabled={isAnalogEnabled}
                  setIsAnalogEnabled={setIsAnalogEnabled}
                  brightnessVal={brightnessVal}
                  setBrightnessVal={setBrightnessVal}
                  speedIndex={speedIndex}
                  setSpeedIndex={setSpeedIndex}
                  currentDirection={currentDirection}
                  setCurrentDirection={setCurrentDirection}
                  thicknessVal={thicknessVal}
                  setThicknessVal={setThicknessVal}
                  gradientStops={gradientStops}
                  setGradientStops={setGradientStops}
                  selectedStopId={selectedStopId}
                  setSelectedStopId={setSelectedStopId}
                  isStarryRandom={isStarryRandom}
                  setIsStarryRandom={setIsStarryRandom}
                  fpsVal={fpsVal}
                  setFpsVal={setFpsVal}
                  bgColor={bgColor}
                  setBgColor={setBgColor}
                  setActiveTab={setActiveTab}
                />
              )}

              {activeTab === 'perkey' && (
                <PerKeyStudio
                  selectedKeyNames={selectedKeyNames}
                  onSelectGroup={handleSelectGroup}
                  onClearSelection={handleClearSelection}
                  currentProfile={config?.profiles?.[currentProfileName]}
                  onApplyColorToSelected={handleApplyColorToSelected}
                  onRemoveOverridesFromSelected={handleRemoveOverridesFromSelected}
                  bgColor={bgColor}
                  setBgColor={setBgColor}
                />
              )}

              {activeTab === 'blockly_effect' && (
                <EffectStudio
                  config={config}
                  onSaveConfig={saveConfigDirectly}
                  showToast={showToast}
                  onPreviewFrameUpdate={handleBlocklyPreviewFrame}
                />
              )}

              {activeTab === 'blockly_orchestrator' && (
                <OrchestratorStudio
                  config={config}
                  onSaveConfig={saveConfigDirectly}
                  profiles={config?.profiles}
                  currentProfileName={currentProfileName}
                  showToast={showToast}
                  onSwitchToLegacyRules={() => setActiveTab('rules')}
                />
              )}

              {activeTab === 'rules' && (
                <RulesSettings
                  rules={config?.rules || []}
                  onAddRule={handleAddRule}
                  onDeleteRule={handleDeleteRule}
                  onUpdateRule={handleUpdateRule}
                  profiles={config?.profiles}
                  currentProfileName={currentProfileName}
                />
              )}

              {activeTab === 'gsi' && (
                <GsiSettings
                  config={config}
                  onUpdateGsiBindings={handleUpdateGsiBindings}
                  profiles={config?.profiles}
                  currentProfileName={currentProfileName}
                  showToast={showToast}
                />
              )}

              {activeTab === 'profiles' && (
                <ProfileManager
                  profiles={config?.profiles || {}}
                  currentProfileName={currentProfileName}
                  defaultProfileName={config?.default_profile}
                  onSelectProfile={(pname) => handleProfileChange(pname, true)}
                  onSetDefaultProfile={handleSetDefaultProfile}
                  onCloneProfile={handleCloneProfile}
                  onDeleteProfile={handleDeleteProfile}
                  onCreateProfile={handleCreateProfile}
                />
              )}
            </motion.div>
          </AnimatePresence>
        </div>
      </div>

      {/* 轻量 Toast 通知 */}
      <Toast toast={toast} onClose={() => setToast(null)} />

      {/* 竞技游戏模式降级对话框 */}
      <GameModeModal isOpen={isGameModalOpen} />
    </div>
  );
}
