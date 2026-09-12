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
import { TACTICAL_PALETTE, DEFAULT_STUDIO_COLOR, DEFAULT_KEYBOARD_BG, BG_PRESETS } from '../tokens/keyboardPresets.tokens';

export default function PerKeyStudio({
  selectedKeyNames,
  onSelectGroup,
  onClearSelection,
  currentProfile,
  onApplyColorToSelected,
  onRemoveOverridesFromSelected,
  bgColor = DEFAULT_KEYBOARD_BG,
  setBgColor
}) {
  const [studioColor, setStudioColor] = useState(DEFAULT_STUDIO_COLOR);

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
    <div className="grid grid-cols-1 lg:grid-cols-3 gap-6 p-1">
      {/* 左栏：选区控制与战术调色板 */}
      <div className="lg:col-span-2 flex flex-col gap-4">
        <div className="flex items-center justify-between pb-3 border-b border-md-outline-variant">
          <div>
            <h3 className="font-bold text-md-on-surface text-sm">逐键定制工坊</h3>
            <p className="text-xs text-md-on-surface-variant mt-0.5">在上方键盘中点选或使用快捷分组指定按键色彩</p>
          </div>
          <span className="text-xs font-bold text-md-on-primary-container bg-md-primary-container px-3 py-1.5 rounded-md-full shadow-xs">
            已选 {selectedKeyNames?.size || 0} 键
          </span>
        </div>

        {/* 快捷分组按钮 (MD3E Tonal Button Group) */}
        <div className="flex flex-col gap-2">
          <span className="text-xs font-semibold text-md-on-surface">快捷选键分组</span>
          <div className="flex flex-wrap gap-2">
            {groupButtons.map((btn) => {
              const Icon = btn.icon;
              return (
                <button
                  key={btn.id}
                  type="button"
                  onClick={() => (btn.isClear ? onClearSelection() : onSelectGroup(btn.id))}
                  className="min-h-[48px] min-w-[56px] px-3.5 flex items-center justify-center gap-2 rounded-md-full border border-md-outline-variant bg-md-surface-container text-md-on-surface hover:bg-md-surface-container-high active:scale-95 transition-all select-none cursor-pointer"
                  title={btn.label}
                  aria-label={`选键分组: ${btn.label}`}
                >
                  <Icon className="w-4 h-4 text-md-primary" />
                  <span className="text-xs font-semibold">{btn.label}</span>
                </button>
              );
            })}
          </div>
        </div>

        {/* 战术调色板 */}
        <div className="flex flex-col gap-2 pt-3 border-t border-md-outline-variant">
          <span className="text-xs font-semibold text-md-on-surface">战术调色板 (点击立即赋予所选键位)</span>
          <div className="flex flex-wrap gap-2.5">
            {TACTICAL_PALETTE.map((c) => (
              <button
                key={c.hex}
                type="button"
                onClick={() => {
                  setStudioColor(c.hex);
                  if (selectedKeyNames && selectedKeyNames.size > 0) {
                    onApplyColorToSelected(c.hex);
                  }
                }}
                title={c.name}
                aria-label={`调色板颜色: ${c.name}`}
                style={{ backgroundColor: c.hex }}
                className={`w-12 h-12 min-w-[48px] min-h-[48px] rounded-md-md border-2 transition-transform cursor-pointer shadow-sm ${
                  studioColor.toUpperCase() === c.hex.toUpperCase()
                    ? 'ring-2 ring-md-primary border-md-on-surface scale-110 z-10'
                    : 'border-md-outline-variant/80 hover:scale-105'
                }`}
              />
            ))}
          </div>
        </div>
      </div>

      {/* 右栏：色彩赋予与独立操作 (MD3E Card) */}
      <div className="flex flex-col gap-4 bg-md-surface-container-low p-4 rounded-md-xl border border-md-outline-variant shadow-md-level1">
        <h3 className="font-bold text-md-on-surface text-sm pb-2 border-b border-md-outline-variant">
          色彩赋予与操作
        </h3>

        {/* 任意自选色彩 */}
        <div className="flex flex-col gap-2">
          <span className="text-xs font-semibold text-md-on-surface">指定独立 RGB 色彩</span>
          <div className="flex items-center gap-3">
            <input
              type="color"
              value={studioColor}
              onChange={(e) => setStudioColor(e.target.value)}
              className="w-12 h-12 min-w-[48px] min-h-[48px] rounded-md-md border border-md-outline p-0 cursor-pointer bg-transparent shadow-sm"
              aria-label="选择自定义涂装颜色"
            />
            <div className="flex-1 min-h-[48px] flex items-center gap-2 px-3 bg-md-surface-container border border-md-outline rounded-md-sm focus-within:border-md-primary focus-within:ring-1 focus-within:ring-md-primary">
              <Pipette className="w-4 h-4 text-md-on-surface-variant" />
              <input
                type="text"
                value={studioColor}
                onChange={(e) => setStudioColor(e.target.value)}
                className="w-full text-xs font-mono font-bold text-md-on-surface bg-transparent outline-none uppercase"
                aria-label="十六进制色彩代码"
                placeholder="#RRGGBB"
              />
            </div>
          </div>
        </div>

        {/* 核心操作按钮 (MD3E 按钮层级：1 Filled 主按钮 + 1 Outlined 次按钮) */}
        <div className="flex flex-col gap-2 pt-2">
          <button
            type="button"
            onClick={handleApplyClick}
            className="w-full min-h-[48px] flex items-center justify-center gap-2 px-4 bg-md-primary hover:bg-md-primary/90 text-md-on-primary rounded-md-full border border-md-primary shadow-md-level1 cursor-pointer active:scale-98 transition-transform text-xs font-bold"
            title="涂装选定按键"
            aria-label="涂装选定按键"
          >
            <Paintbrush className="w-4 h-4" />
            <span>涂装选定按键</span>
          </button>

          <button
            type="button"
            onClick={onRemoveOverridesFromSelected}
            className="w-full min-h-[48px] flex items-center justify-center gap-2 px-4 bg-md-surface-container hover:bg-md-surface-container-high text-md-on-surface rounded-md-full border border-md-outline-variant cursor-pointer active:scale-98 transition-transform text-xs font-semibold"
            title="清除选中按键的独立覆写"
            aria-label="清除选中按键的独立覆写"
          >
            <Eraser className="w-4 h-4" />
            <span>清除覆写 (恢复底色)</span>
          </button>
        </div>

        {/* 未覆写按键全局底色 */}
        <div className="flex flex-col gap-2 pt-3 border-t border-md-outline-variant">
          <div className="flex items-center justify-between">
            <span className="text-xs font-semibold text-md-on-surface">未分配按键底色</span>
            <input
              type="color"
              value={bgColor}
              onChange={(e) => setBgColor(e.target.value)}
              className="w-8 h-8 rounded-md-xs border border-md-outline p-0 cursor-pointer bg-transparent"
              aria-label="设置未分配按键底色"
            />
          </div>
          <div className="flex items-center gap-2">
            {BG_PRESETS.map((p) => (
              <button
                key={p.val}
                type="button"
                onClick={() => setBgColor && setBgColor(p.val)}
                className="min-h-[32px] px-3 py-1 text-xs rounded-md-full border border-md-outline-variant bg-md-surface-container-high hover:bg-md-surface-container-highest text-md-on-surface cursor-pointer"
                aria-label={`底色设为 ${p.label}`}
              >
                {p.label}
              </button>
            ))}
          </div>
          <p className="text-xs text-md-on-surface-variant leading-relaxed">
            用于渲染未单独指定自定义色彩的 68 键基底背景。
          </p>
        </div>
      </div>
    </div>
  );
}
