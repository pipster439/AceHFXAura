import React from 'react';
import { 
  Palette, 
  Keyboard, 
  Workflow, 
  Crosshair, 
  SlidersHorizontal, 
  PanelLeftClose, 
  PanelLeftOpen, 
  Radio, 
  Layers,
  Cpu,
  Sun,
  Moon,
  Sparkles,
  GitBranch
} from 'lucide-react';

export default function Sidebar({
  isCollapsed,
  setIsCollapsed,
  activeTab,
  setActiveTab,
  currentProfileName,
  onProfileChange,
  profiles,
  defaultProfileName,
  isSaving,
  isServiceOnline,
  theme = 'dark',
  onToggleTheme
}) {
  const navItems = [
    { id: 'lighting', label: '预设灯效', icon: Palette },
    { id: 'perkey', label: '逐键涂装', icon: Keyboard },
    { id: 'blockly_effect', label: '工作室', icon: Sparkles },
    { id: 'gsi', label: 'CS2 连接与数据', icon: Crosshair },
    { id: 'profiles', label: '方案管理', icon: SlidersHorizontal }
  ];

  return (
    <aside 
      className={`relative flex flex-col bg-md-surface-container-low border-r border-md-outline-variant select-none z-30 transition-[width] duration-300 ease-out ${
        isCollapsed ? 'w-24' : 'w-64'
      }`}
      aria-label="主要导航"
    >
      {/* 导航轨头部 */}
      <div className="flex items-center justify-between h-16 px-4 border-b border-md-outline-variant">
        <div className="flex items-center gap-3 overflow-hidden">
          <div className="flex items-center justify-center w-10 h-10 aspect-square rounded-md-md bg-md-primary text-md-on-primary shrink-0 shadow-md-level1">
            <Cpu className="w-5 h-5" />
          </div>
          {!isCollapsed && (
            <div className="flex flex-col overflow-hidden">
              <span className="font-bold text-md-on-surface text-sm tracking-tight truncate">
                ROG FALCHION
              </span>
              <span className="text-xs text-md-on-surface-variant truncate">
                Ace HFX 65%
              </span>
            </div>
          )}
        </div>

        <button
          type="button"
          onClick={() => setIsCollapsed(!isCollapsed)}
          className="w-10 h-10 min-w-[40px] min-h-[40px] flex items-center justify-center text-md-on-surface-variant rounded-md-full hover:bg-md-surface-container-high hover:text-md-on-surface active:scale-95 transition-transform cursor-pointer focus-visible:outline-none"
          title={isCollapsed ? '展开导航轨' : '收起导航轨'}
          aria-label={isCollapsed ? '展开导航轨' : '收起导航轨'}
        >
          {isCollapsed ? <PanelLeftOpen className="w-5 h-5" /> : <PanelLeftClose className="w-5 h-5" />}
        </button>
      </div>

      {/* 方案快速选择区 */}
      <div className="p-3 border-b border-md-outline-variant">
        {!isCollapsed ? (
          <div className="flex flex-col gap-1.5">
            <div className="flex items-center justify-between text-xs font-medium text-md-on-surface-variant px-1">
              <span className="flex items-center gap-1.5">
                <Layers className="w-3.5 h-3.5 text-md-primary" />
                当前方案
              </span>
              {currentProfileName === defaultProfileName && (
                <span className="text-xs px-2 py-0.5 rounded-md-full bg-md-primary-container text-md-on-primary-container font-semibold">
                  默认
                </span>
              )}
            </div>
            <select
              value={currentProfileName}
              onChange={(e) => onProfileChange(e.target.value)}
              className="w-full h-10 px-3 text-xs font-semibold text-md-on-surface bg-md-surface-container border border-md-outline rounded-md-sm cursor-pointer outline-none focus:border-md-primary focus:ring-1 focus:ring-md-primary transition-colors"
              aria-label="选择灯效方案"
            >
              {Object.keys(profiles || {}).map((name) => (
                <option key={name} value={name} className="bg-md-surface text-md-on-surface">
                  {name} {name === defaultProfileName ? '(默认)' : ''}
                </option>
              ))}
            </select>
          </div>
        ) : (
          <div className="flex justify-center" title={`当前方案: ${currentProfileName}`}>
            <div className="flex items-center justify-center w-11 h-11 aspect-square rounded-md-md bg-md-surface-container-high border border-md-outline-variant text-md-on-surface text-xs font-bold shadow-md-level1">
              {currentProfileName.charAt(0).toUpperCase()}
            </div>
          </div>
        )}
      </div>

      {/* 核心导航条目列表 (MD3E 规范) */}
      <nav className="flex-1 p-2 space-y-1.5 overflow-y-auto">
        {navItems.map((item) => {
          const Icon = item.icon;
          const isActive = (activeTab === item.id || (item.id === 'blockly_effect' && ['blockly_orchestrator', 'rules'].includes(activeTab)));
          return (
            <button
              key={item.id}
              type="button"
              onClick={() => setActiveTab(item.id)}
              className={`w-full min-h-[48px] flex items-center gap-3 px-3 py-2.5 rounded-md-full text-xs font-medium transition-colors cursor-pointer group relative ${
                isActive
                  ? 'bg-md-secondary-container text-md-on-secondary-container font-bold shadow-md-level1'
                  : 'text-md-on-surface-variant hover:bg-md-surface-container hover:text-md-on-surface'
              } ${isCollapsed ? 'flex-col justify-center py-2 h-14' : ''}`}
              title={isCollapsed ? item.label : undefined}
              aria-label={item.label}
              aria-current={isActive ? 'page' : undefined}
            >
              <div className={`flex items-center justify-center w-6 h-6 shrink-0 transition-transform ${isActive ? 'scale-110 text-md-on-secondary-container' : 'text-md-on-surface-variant group-hover:text-md-on-surface'}`}>
                <Icon className="w-5 h-5" />
              </div>
              <span className={`truncate text-xs ${isCollapsed ? 'text-[11px] leading-tight mt-0.5' : ''}`}>
                {item.label}
              </span>
            </button>
          );
        })}
      </nav>

      {/* 底部功能区：明暗切换与服务状态 */}
      <div className="p-3 border-t border-md-outline-variant flex flex-col gap-2 bg-md-surface-container-lowest">
        {/* 明暗切换按钮 */}
        {onToggleTheme && (
          <button
            type="button"
            onClick={onToggleTheme}
            className={`w-full min-h-[48px] flex items-center justify-between px-3 py-2 rounded-md-full border border-md-outline-variant bg-md-surface-container hover:bg-md-surface-container-high text-md-on-surface text-xs font-medium cursor-pointer transition-colors ${
              isCollapsed ? 'justify-center px-0' : ''
            }`}
            title={theme === 'dark' ? '切换为浅色主题' : '切换为深色主题'}
            aria-label={theme === 'dark' ? '切换为浅色主题' : '切换为深色主题'}
          >
            <span className="flex items-center gap-2">
              {theme === 'dark' ? <Moon className="w-4 h-4 text-md-primary" /> : <Sun className="w-4 h-4 text-md-primary" />}
              {!isCollapsed && <span>{theme === 'dark' ? '暗黑模式' : '明亮模式'}</span>}
            </span>
            {!isCollapsed && (
              <span className="text-[11px] text-md-on-surface-variant">MD3E</span>
            )}
          </button>
        )}

        {/* 服务心跳与实时生效 */}
        <div className={`flex items-center gap-2 px-2 py-1.5 rounded-md-md bg-md-surface-container text-xs text-md-on-surface-variant ${
          isCollapsed ? 'justify-center px-0' : ''
        }`}>
          <Radio className={`w-4 h-4 shrink-0 ${isServiceOnline ? 'text-md-primary' : 'text-md-error'}`} />
          {!isCollapsed && (
            <div className="flex flex-col min-w-0">
              <span className="font-semibold truncate text-md-on-surface">
                {isServiceOnline ? '守护进程运行中' : '守护进程离线'}
              </span>
              <span className="text-[11px] text-md-on-surface-variant truncate">
                {isSaving ? '正在同步硬件...' : '修改实时硬件生效'}
              </span>
            </div>
          )}
        </div>
      </div>
    </aside>
  );
}
