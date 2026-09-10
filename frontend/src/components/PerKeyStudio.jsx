import React, { useState } from 'react';
import {
  Gamepad2,
  Move,
  Binary,
  Command,
  CheckSquare,
  XSquare,
  Paintbrush,
  Eraser,
  Pipette
} from 'lucide-react';
import { hexToRgb } from '../utils/color';

export default function PerKeyStudio({
  selectedKeyNames,
  onSelectGroup,
  onClearSelection,
  currentProfile,
  onApplyColorToSelected,
  onRemoveOverridesFromSelected,
  bgColor,
  setBgColor
}) {
  const [studioColor, setStudioColor] = useState('#0F172A');

  const paletteColors = [
    { name: '暗黑', hex: '#0F172A' },
    { name: '蔚蓝', hex: '#0284C7' },
    { name: '翠绿', hex: '#059669' },
    { name: '耀黄', hex: '#D97706' },
    { name: '青绿', hex: '#06B6D4' },
    { name: '暗紫', hex: '#7C3AED' },
    { name: '暖橙', hex: '#EA580C' },
    { name: '纯白', hex: '#FFFFFF' },
    { name: '深灰', hex: '#334155' },
    { name: '浅灰', hex: '#94A3B8' }
  ];

  const handleApplyClick = () => {
    onApplyColorToSelected(studioColor);
  };

  const groupButtons = [
    { id: 'WASD', label: 'WASD', icon: Gamepad2 },
    { id: 'ARROWS', label: '方向', icon: Move },
    { id: 'NUMBERS', label: '数字', icon: Binary },
    { id: 'MODIFIERS', label: '修饰', icon: Command },
    { id: 'ALL', label: '全选', icon: CheckSquare },
    { id: 'CLEAR', label: '清空', icon: XSquare, isClear: true }
  ];

  return (
    <div className="grid grid-cols-1 lg:grid-cols-3 gap-6 p-2">
      {/* 左栏 */}
      <div className="lg:col-span-2 flex flex-col gap-4">
        <div className="flex items-center justify-between pb-2 border-b border-slate-200">
          <h3 className="font-bold text-slate-900 text-sm">逐键定制</h3>
          <span className="text-[11px] font-bold text-slate-700 bg-slate-100 px-2.5 py-1 border border-slate-300 rounded-none">
            已选 {selectedKeyNames?.size || 0} 键
          </span>
        </div>

        {/* 快捷分组：紧凑正方形按钮 */}
        <div className="flex flex-col gap-2">
          <span className="text-xs font-semibold text-slate-700">快捷选择</span>
          <div className="flex flex-wrap gap-2">
            {groupButtons.map((btn) => {
              const Icon = btn.icon;
              return (
                <button
                  key={btn.id}
                  onClick={() => (btn.isClear ? onClearSelection() : onSelectGroup(btn.id))}
                  className="w-11 h-11 aspect-square flex flex-col items-center justify-center rounded-none border border-slate-300 bg-white text-slate-800 hover:border-slate-500 hover:bg-slate-50 active:scale-95 transition-all select-none cursor-pointer"
                  title={btn.label}
                >
                  <Icon className="w-4 h-4 mb-0.5 text-slate-700" />
                  <span className="text-[10px] font-bold leading-none">{btn.label}</span>
                </button>
              );
            })}
          </div>
        </div>

        {/* 调色板：紧凑尺寸 */}
        <div className="flex flex-col gap-2 pt-2 border-t border-slate-200">
          <span className="text-xs font-semibold text-slate-700">战术调色板</span>
          <div className="flex flex-wrap gap-2">
            {paletteColors.map((c) => (
              <button
                key={c.hex}
                onClick={() => {
                  setStudioColor(c.hex);
                  if (selectedKeyNames && selectedKeyNames.size > 0) {
                    onApplyColorToSelected(c.hex);
                  }
                }}
                title={c.name}
                style={{ backgroundColor: c.hex }}
                className={`w-7 h-7 aspect-square rounded-none border transition-all cursor-pointer ${
                  studioColor.toUpperCase() === c.hex.toUpperCase()
                    ? 'ring-2 ring-slate-900 border-slate-900 scale-105 z-10'
                    : 'border-slate-300 hover:border-slate-500'
                }`}
              />
            ))}
          </div>
        </div>
      </div>

      {/* 右栏 */}
      <div className="flex flex-col gap-4">
        <h3 className="font-bold text-slate-900 text-sm pb-2 border-b border-slate-200">
          色彩赋予
        </h3>

        {/* 颜色选择 */}
        <div className="flex flex-col gap-2">
          <span className="text-xs font-semibold text-slate-700">指定色彩</span>
          <div className="flex items-center gap-2">
            <input
              type="color"
              value={studioColor}
              onChange={(e) => setStudioColor(e.target.value)}
              className="w-8 h-8 rounded-none border border-slate-300 p-0 cursor-pointer bg-transparent"
            />
            <div className="flex-1 flex items-center gap-1.5 px-2.5 py-1.5 bg-white border border-slate-300 rounded-none">
              <Pipette className="w-3.5 h-3.5 text-slate-400" />
              <input
                type="text"
                value={studioColor}
                onChange={(e) => setStudioColor(e.target.value)}
                className="w-full text-xs font-mono font-bold text-slate-900 bg-transparent outline-none uppercase"
              />
            </div>
          </div>
        </div>

        {/* 涂装操作：紧凑尺寸按键 */}
        <div className="flex gap-2">
          <button
            onClick={handleApplyClick}
            className="flex-1 h-9 flex items-center justify-center gap-1.5 px-3 bg-slate-900 hover:bg-slate-800 text-white rounded-none border border-slate-900 shadow-xs cursor-pointer active:scale-95 transition-all text-xs font-bold"
            title="涂装选定"
          >
            <Paintbrush className="w-3.5 h-3.5" />
            <span>涂装选定</span>
          </button>

          <button
            onClick={onRemoveOverridesFromSelected}
            className="flex-1 h-9 flex items-center justify-center gap-1.5 px-3 bg-white hover:bg-slate-100 text-slate-700 rounded-none border border-slate-300 cursor-pointer active:scale-95 transition-all text-xs font-bold"
            title="清除覆写"
          >
            <Eraser className="w-3.5 h-3.5" />
            <span>清除覆写</span>
          </button>
        </div>

        {/* 背景底色 */}
        <div className="flex flex-col gap-1.5 pt-2 border-t border-slate-200">
          <div className="flex items-center justify-between">
            <span className="text-xs font-semibold text-slate-700">未分配按键底色</span>
            <input
              type="color"
              value={bgColor}
              onChange={(e) => setBgColor(e.target.value)}
              className="w-6 h-6 rounded-none border border-slate-300 p-0 cursor-pointer bg-transparent"
            />
          </div>
          <p className="text-[11px] text-slate-500 leading-relaxed">
            用于渲染未单独指定自定义颜色的按键基底。
          </p>
        </div>
      </div>
    </div>
  );
}
