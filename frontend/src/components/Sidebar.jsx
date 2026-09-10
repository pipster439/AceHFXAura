import React from 'react';
import { 
  Palette, 
  Keyboard, 
  Workflow, 
  Crosshair,
  SlidersHorizontal, 
  PanelLeftClose, 
  PanelLeftOpen, 
  Save, 
  Radio, 
  Layers,
  Cpu
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
  onSave,
  isSaving,
  isServiceOnline
}) {
  const navItems = [
    { id: 'lighting', label: '预设灯效', icon: Palette },
    { id: 'perkey', label: '逐键涂装', icon: Keyboard },
    { id: 'rules', label: '进程联动', icon: Workflow },
    { id: 'gsi', label: 'CS2 GSI', icon: Crosshair },
    { id: 'profiles', label: '方案管理', icon: SlidersHorizontal }
  ];

  return (
    <aside 
      className={`relative flex flex-col bg-white border-r border-slate-200 transition-all duration-200 select-none z-30 ${
        isCollapsed ? 'w-16' : 'w-56'
      }`}
    >
      {/* 侧边栏头部 */}
      <div className="flex items-center justify-between h-14 px-3.5 border-b border-slate-200">
        <div className="flex items-center gap-2.5 overflow-hidden">
          <div className="flex items-center justify-center w-8 h-8 aspect-square rounded-none bg-slate-900 text-white shrink-0">
            <Cpu className="w-4 h-4" />
          </div>
          {!isCollapsed && (
            <div className="flex flex-col overflow-hidden">
              <span className="font-bold text-slate-900 text-xs tracking-tight truncate">ROG FALCHION</span>
              <span className="text-[10px] font-semibold text-slate-500 truncate">Ace HFX 65%</span>
            </div>
          )}
        </div>

        <button
          onClick={() => setIsCollapsed(!isCollapsed)}
          className="w-7 h-7 aspect-square flex items-center justify-center text-slate-500 rounded-none border border-transparent hover:border-slate-300 hover:text-slate-900 hover:bg-slate-100 active:scale-95 transition-all cursor-pointer"
          title={isCollapsed ? '展开' : '收起'}
          aria-label={isCollapsed ? '展开' : '收起'}
        >
          {isCollapsed ? <PanelLeftOpen className="w-3.5 h-3.5" /> : <PanelLeftClose className="w-3.5 h-3.5" />}
        </button>
      </div>

      {/* 方案快速选择 */}
      <div className="p-2.5 border-b border-slate-200">
        {!isCollapsed ? (
          <div className="flex flex-col gap-1">
            <div className="flex items-center justify-between text-[11px] font-bold text-slate-700">
              <span className="flex items-center gap-1">
                <Layers className="w-3 h-3" />
                方案
              </span>
              {currentProfileName === defaultProfileName && (
                <span className="text-[9px] bg-slate-100 text-slate-700 border border-slate-300 px-1 py-0.5 rounded-none font-bold">默认</span>
              )}
            </div>
            <select
              value={currentProfileName}
              onChange={(e) => onProfileChange(e.target.value)}
              className="w-full h-7 px-2 text-[11px] font-bold text-slate-900 bg-white border border-slate-300 rounded-none cursor-pointer outline-none focus:border-slate-900"
            >
              {Object.keys(profiles || {}).map((name) => (
                <option key={name} value={name}>
                  {name} {name === defaultProfileName ? '(默认)' : ''}
                </option>
              ))}
            </select>
          </div>
        ) : (
          <div className="flex justify-center" title={`当前方案: ${currentProfileName}`}>
            <div className="flex items-center justify-center w-8 h-8 aspect-square rounded-none bg-slate-100 border border-slate-300 text-slate-900 text-[11px] font-bold">
              {currentProfileName.charAt(0).toUpperCase()}
            </div>
          </div>
        )}
      </div>

      {/* 导航菜单列表 */}
      <nav className="flex-1 p-1.5 space-y-1 overflow-y-auto">
        {navItems.map((item) => {
          const Icon = item.icon;
          const isActive = activeTab === item.id;
          return (
            <button
              key={item.id}
              onClick={() => setActiveTab(item.id)}
              className={`w-full flex items-center gap-2.5 px-2.5 py-2 rounded-none text-xs font-bold transition-all cursor-pointer ${
                isActive
                  ? 'bg-slate-900 text-white shadow-xs'
                  : 'text-slate-700 hover:bg-slate-100 hover:text-slate-900 active:scale-98'
              } ${isCollapsed ? 'justify-center px-0 h-10 aspect-square' : ''}`}
              title={isCollapsed ? item.label : undefined}
            >
              <Icon className={`w-4 h-4 shrink-0 ${isActive ? 'text-white' : 'text-slate-700'}`} />
              {!isCollapsed && <span className="truncate text-xs">{item.label}</span>}
            </button>
          );
        })}
      </nav>

      {/* 底部保存与服务心跳 */}
      <div className="p-2.5 border-t border-slate-200 flex flex-col gap-2 bg-slate-50">
        <div className={`flex items-center gap-1.5 px-1 py-0.5 text-[11px] font-medium text-slate-600 ${isCollapsed ? 'justify-center px-0' : ''}`}>
          <Radio className={`w-3.5 h-3.5 ${isServiceOnline ? 'text-emerald-600' : 'text-amber-500'}`} />
          {!isCollapsed && (
            <span className="truncate">
              {isServiceOnline ? '服务运行中' : '离线'}
            </span>
          )}
        </div>

        {/* 保存按键 */}
        <button
          onClick={onSave}
          disabled={isSaving}
          className={`w-full flex items-center justify-center gap-1.5 h-8 rounded-none text-xs font-bold text-white bg-slate-900 hover:bg-slate-800 active:scale-98 shadow-xs transition-all border border-slate-900 disabled:opacity-50 cursor-pointer ${
            isCollapsed ? 'px-0 aspect-square' : 'px-2'
          }`}
          title="保存并应用"
        >
          <Save className="w-3.5 h-3.5 shrink-0" />
          {!isCollapsed && <span>{isSaving ? '保存中...' : '保存并应用'}</span>}
        </button>
      </div>
    </aside>
  );
}
