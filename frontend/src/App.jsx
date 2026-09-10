import React, { useState, useEffect, useCallback } from 'react';
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
import { GRADIENT_PRESETS } from './constants/keyboardLayout';
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
  const [bgColor, setBgColor] = useState('#000000');

  // 逐键涂装选中集合
  const [selectedKeyNames, setSelectedKeyNames] = useState(new Set());

  // 状态反馈与服务存活
  const [toast, setToast] = useState(null);
  const [isSaving, setIsSaving] = useState(false);
  const [isServiceOnline, setIsServiceOnline] = useState(true);
  const [isGameModalOpen, setIsGameModalOpen] = useState(false);

  const showToast = (message, type = 'success') => {
    setToast({ message, type });
    setTimeout(() => setToast(null), 3000);
  };

  // 方案数据加载到 UI 控件
  const loadProfileToState = useCallback((profileData) => {
    if (!profileData) return;

    setCurrentEffect(profileData.type || 'static');
    setIsAnalogEnabled(profileData.analog === true);

    if (profileData.direction) setCurrentDirection(profileData.direction);

    if (typeof profileData.brightness === 'number') {
      const b = profileData.brightness > 1.0 ? profileData.brightness / 255.0 : profileData.brightness;
      setBrightnessVal(Math.max(0, Math.min(1.0, b)));
    } else {
      setBrightnessVal(1.0);
    }

    if (typeof profileData.speed_index === 'number') {
      const idx = Math.round(profileData.speed_index);
      setSpeedIndex(Math.max(0, Math.min(2, idx)));
    } else if (profileData.period_ms) {
      if (profileData.period_ms > 4500) setSpeedIndex(0);
      else if (profileData.period_ms > 2400) setSpeedIndex(1);
      else setSpeedIndex(2);
    }

    if (profileData.color || profileData.color1) {
      const c1 = profileData.color || profileData.color1;
      const c2 = profileData.color2;
      const nextStops = [
        { id: 1, pos: 0.15, color: rgbToHex(c1) },
        { id: 2, pos: 0.85, color: c2 ? rgbToHex(c2) : '#0284C7' }
      ];
      setGradientStops(nextStops);
      setSelectedStopId(1);
    }

    if (profileData.bg) {
      setBgColor(rgbToHex(profileData.bg));
    }
  }, []);

  // 读取配置
  const fetchConfig = useCallback(async () => {
    try {
      const res = await fetch('/api/config');
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data = await res.json();
      setConfig(data);

      const defaultProf = data.default_profile || Object.keys(data.profiles || {})[0] || 'desktop';
      setCurrentProfileName(defaultProf);
      loadProfileToState(data.profiles?.[defaultProf]);
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

  // 切换方案，可选择是否直接应用为硬件活跃方案
  const handleProfileChange = async (pname, applyToHardware = false) => {
    if (!config?.profiles?.[pname]) return;
    syncCurrentStateToConfig();
    setCurrentProfileName(pname);
    loadProfileToState(config.profiles[pname]);
    setSelectedKeyNames(new Set());

    if (applyToHardware) {
      const nextConfig = {
        ...config,
        default_profile: pname
      };
      setConfig(nextConfig);
      try {
        await fetch('/api/config', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(nextConfig)
        });
        showToast(`已切换方案 [${pname}] 并立即激活到物理键盘！`);
      } catch (e) {
        showToast('切换硬件方案失败: ' + e.message, 'error');
      }
    }
  };

  // 将当前 UI 状态同步进 config 内存树
  const syncCurrentStateToConfig = useCallback(() => {
    if (!config?.profiles?.[currentProfileName]) return;
    const prof = config.profiles[currentProfileName];

    if (activeTab === 'perkey') {
      prof.type = 'custom_keymap';
      prof.bg = hexToRgb(bgColor);
      prof.brightness = brightnessVal;
    } else {
      prof.type = currentEffect;
      prof.analog = currentEffect === 'static' ? isAnalogEnabled : false;
      prof.brightness = brightnessVal;
      prof.speed_index = speedIndex;
      prof.direction = currentDirection;
      prof.period_ms = speedIndex === 0 ? 5500 : speedIndex === 1 ? 3200 : 1600;

      if (gradientStops.length > 0) {
        prof.color = hexToRgb(gradientStops[0].color);
        prof.color1 = hexToRgb(gradientStops[0].color);
      }
      if (gradientStops.length > 1) {
        prof.color2 = hexToRgb(gradientStops[1].color);
      }

      if (currentEffect === 'reactive' || currentEffect === 'ripple' || currentEffect === 'static') {
        prof.bg = [0, 0, 0];
      } else {
        delete prof.bg;
      }
    }
  }, [
    config,
    currentProfileName,
    activeTab,
    currentEffect,
    isAnalogEnabled,
    brightnessVal,
    currentDirection,
    speedIndex,
    gradientStops,
    bgColor
  ]);

  // 保存当前方案配置到后端（沿用当前已生效的 default_profile，不覆盖默认方案）
  const handleSave = async () => {
    syncCurrentStateToConfig();
    const toSave = {
      ...config,
      default_profile: config?.default_profile || currentProfileName
    };
    setConfig(toSave);
    setIsSaving(true);
    try {
      const res = await fetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(toSave)
      });
      const data = await res.json();
      if (res.ok) {
        const isDefault = toSave.default_profile === currentProfileName;
        showToast(
          isDefault
            ? `方案 [${currentProfileName}] 已保存并立即生效到物理键盘！`
            : `方案 [${currentProfileName}] 已保存（当前默认方案仍为 [${toSave.default_profile}]）`
        );
      } else {
        showToast(data.message || '保存失败', 'error');
      }
    } catch (err) {
      showToast('请求失败: ' + err.message, 'error');
    } finally {
      setIsSaving(false);
    }
  };

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
    const prof = config?.profiles?.[currentProfileName];
    if (!prof) return;
    if (!prof.keys) prof.keys = {};

    prof.type = 'custom_keymap';
    prof.bg = hexToRgb(bgColor);

    const rgb = hexToRgb(hex);
    selectedKeyNames.forEach((kname) => {
      prof.keys[kname] = rgb;
    });

    setConfig({ ...config });
    showToast(`已涂装 ${selectedKeyNames.size} 个按键 (已自动切换为逐键定制模式)`);
  };

  const handleRemoveOverridesFromSelected = () => {
    if (!selectedKeyNames || selectedKeyNames.size === 0) {
      showToast('请先选择要清除自定义覆写的按键', 'info');
      return;
    }
    const prof = config?.profiles?.[currentProfileName];
    if (!prof?.keys) return;

    selectedKeyNames.forEach((kname) => {
      delete prof.keys[kname];
    });

    setConfig({ ...config });
    showToast(`已清除 ${selectedKeyNames.size} 个按键的独立覆写`);
  };

  // 规则设置操作
  const handleAddRule = (newRule = { process: '', profile: currentProfileName, suppress_web_ui: false }) => {
    const updated = {
      ...config,
      rules: [...(config.rules || []), newRule]
    };
    setConfig(updated);
  };

  const handleDeleteRule = (idx) => {
    const updatedRules = config.rules.filter((_, i) => i !== idx);
    setConfig({ ...config, rules: updatedRules });
  };

  const handleUpdateRule = (idx, patch) => {
    const updatedRules = config.rules.map((r, i) => (i === idx ? { ...r, ...patch } : r));
    setConfig({ ...config, rules: updatedRules });
  };

  // GSI 规则操作 (过滤空字段)
  const handleUpdateGsiBindings = (updatedBindings) => {
    const cleaned = updatedBindings.filter(b => b && b.field && b.field.trim() !== '');
    setConfig({ ...config, gsi_bindings: cleaned });
  };

  // 方案管理操作 (设为默认立即同步至硬件)
  const handleSetDefaultProfile = async (name) => {
    const nextConfig = { ...config, default_profile: name };
    setConfig(nextConfig);
    try {
      await fetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(nextConfig)
      });
      showToast(`已将 [${name}] 设为默认方案并已应用到键盘！`);
    } catch (e) {
      showToast('设置默认失败: ' + e.message, 'error');
    }
  };

  const handleCloneProfile = (sourceName, newName) => {
    syncCurrentStateToConfig();
    const cloned = JSON.parse(JSON.stringify(config.profiles[sourceName]));
    const nextConfig = {
      ...config,
      profiles: {
        ...config.profiles,
        [newName]: cloned
      }
    };
    setConfig(nextConfig);
    setCurrentProfileName(newName);
    loadProfileToState(cloned);
    showToast(`已克隆方案: ${newName}`);
  };

  const handleCreateProfile = (newName) => {
    const fresh = {
      type: 'static',
      color: [15, 23, 42],
      brightness: 1.0,
      speed_index: 1,
      period_ms: 2500,
      keys: {}
    };
    const nextConfig = {
      ...config,
      profiles: {
        ...config.profiles,
        [newName]: fresh
      }
    };
    setConfig(nextConfig);
    setCurrentProfileName(newName);
    loadProfileToState(fresh);
    showToast(`已新建方案: ${newName}`);
  };

  const handleDeleteProfile = (name) => {
    if (Object.keys(config.profiles).length <= 1) {
      showToast('至少需保留一个配置文件', 'error');
      return;
    }
    const nextProfiles = { ...config.profiles };
    delete nextProfiles[name];

    let nextDefault = config.default_profile;
    if (nextDefault === name) {
      nextDefault = Object.keys(nextProfiles)[0];
    }
    const nextCurrent = Object.keys(nextProfiles)[0];

    const nextConfig = {
      ...config,
      default_profile: nextDefault,
      profiles: nextProfiles
    };
    setConfig(nextConfig);
    setCurrentProfileName(nextCurrent);
    loadProfileToState(nextProfiles[nextCurrent]);
    showToast(`已删除方案: ${name}`);
  };

  return (
    <div className="flex h-screen w-screen bg-slate-50 overflow-hidden font-sans select-none">
      {/* 左侧可折叠伸缩栏 */}
      <Sidebar
        isCollapsed={isCollapsed}
        setIsCollapsed={setIsCollapsed}
        activeTab={activeTab}
        setActiveTab={setActiveTab}
        currentProfileName={currentProfileName}
        onProfileChange={handleProfileChange}
        profiles={config?.profiles}
        defaultProfileName={config?.default_profile}
        onSave={handleSave}
        isSaving={isSaving}
        isServiceOnline={isServiceOnline}
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
        />

        {/* 下方功能设置面板 (随侧边栏 Tab 切换平滑过渡) */}
        <div className="flex-1">
          <AnimatePresence mode="wait">
            <motion.div
              key={activeTab}
              initial={{ opacity: 0, y: 6 }}
              animate={{ opacity: 1, y: 0 }}
              exit={{ opacity: 0, y: -6 }}
              transition={{ duration: 0.12 }}
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
                  onSave={handleSave}
                  isSaving={isSaving}
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
