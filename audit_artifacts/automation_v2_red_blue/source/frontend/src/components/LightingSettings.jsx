import React from 'react';
import {
  Sun,
  Wind,
  RotateCw,
  Waves,
  Zap,
  Disc,
  Star,
  Hourglass,
  Activity,
  CloudRain,
  Keyboard,
  Power,
  ArrowUp,
  ArrowDown,
  ArrowLeft,
  ArrowRight,
  ArrowUpRight,
  ArrowDownLeft,
  ArrowUpLeft,
  ArrowDownRight,
  Compass,
  Plus,
  Minus,
  Gauge,
  Sparkles
} from 'lucide-react';
import { GRADIENT_PRESETS, BG_PRESETS, TACTICAL_PALETTE, DEFAULT_KEYBOARD_BG } from '../tokens/keyboardPresets.tokens';

export default function LightingSettings({
  currentEffect,
  setCurrentEffect,
  isMasterLightOn,
  setIsMasterLightOn,
  isAnalogEnabled,
  setIsAnalogEnabled,
  brightnessVal,
  setBrightnessVal,
  speedIndex,
  setSpeedIndex,
  currentDirection,
  setCurrentDirection,
  thicknessVal,
  setThicknessVal,
  gradientStops,
  setGradientStops,
  selectedStopId,
  setSelectedStopId,
  isStarryRandom,
  setIsStarryRandom,
  fpsVal = 25,
  setFpsVal,
  bgColor = DEFAULT_KEYBOARD_BG,
  setBgColor,
  setActiveTab
}) {
  const effects = [
    { id: 'static', label: '静态', icon: Sun },
    { id: 'breathing', label: '呼吸', icon: Wind },
    { id: 'color_cycle', label: '循环', icon: RotateCw },
    { id: 'wave', label: '波浪', icon: Waves },
    { id: 'reactive', label: '响应', icon: Zap },
    { id: 'ripple', label: '涟漪', icon: Disc },
    { id: 'starry_night', label: '星空', icon: Star },
    { id: 'quicksand', label: '流沙', icon: Hourglass },
    { id: 'current', label: '电流', icon: Activity },
    { id: 'raindrop', label: '雨滴', icon: CloudRain },
    { id: 'custom_keymap', label: '逐键', icon: Keyboard }
  ];

  const directions = [
    { id: 'up', label: '向上', icon: ArrowUp },
    { id: 'down', label: '向下', icon: ArrowDown },
    { id: 'left', label: '向左', icon: ArrowLeft },
    { id: 'right', label: '向右', icon: ArrowRight },
    { id: 'diag_ur', label: '右上', icon: ArrowUpRight },
    { id: 'diag_dl', label: '左下', icon: ArrowDownLeft },
    { id: 'diag_ul', label: '左上', icon: ArrowUpLeft },
    { id: 'diag_dr', label: '右下', icon: ArrowDownRight },
    { id: 'spread', label: '扩散', icon: Compass }
  ];

  const addStop = () => {
    const newId = Date.now();
    const fallbackColor = TACTICAL_PALETTE[1]?.hex || (gradientStops[0]?.color || '#0284C7');
    const nextStops = [...gradientStops, { id: newId, pos: 0.5, color: fallbackColor }];
    setGradientStops(nextStops);
    setSelectedStopId(newId);
  };

  const removeStop = () => {
    if (gradientStops.length <= 1) return;
    const nextStops = gradientStops.filter((s) => s.id !== selectedStopId);
    setGradientStops(nextStops);
    setSelectedStopId(nextStops[0].id);
  };

  const handlePresetSelect = (presetKey) => {
    if (GRADIENT_PRESETS[presetKey]) {
      const cloned = JSON.parse(JSON.stringify(GRADIENT_PRESETS[presetKey]));
      setGradientStops(cloned);
      setSelectedStopId(cloned[0].id);
    }
  };

  const currentStop = gradientStops.find((s) => s.id === selectedStopId) || gradientStops[0];

  const sortedStops = [...gradientStops].sort((a, b) => a.pos - b.pos);
  const gradientCss = `linear-gradient(to right, ${sortedStops
    .map((s) => `${s.color} ${Math.round(s.pos * 100)}%`)
    .join(', ')})`;

  return (
    <div className="grid grid-cols-1 lg:grid-cols-3 gap-6 p-1">
      {/* 左栏：11 款效果选择 (MD3E Expressive Grid) */}
      <div className="lg:col-span-2 flex flex-col gap-4">
        {/* 标题栏与主灯开关 */}
        <div className="flex items-center justify-between pb-3 border-b border-md-outline-variant">
          <div>
            <h3 className="font-bold text-md-on-surface text-sm">预设光效模式</h3>
            <p className="text-xs text-md-on-surface-variant mt-0.5">选择并定制全局硬件推流光效矩阵</p>
          </div>
          <div className="flex items-center gap-3">
            <span className="text-xs font-semibold text-md-on-surface-variant">主灯效</span>
            <button
              type="button"
              onClick={() => setIsMasterLightOn(!isMasterLightOn)}
              className={`min-w-[48px] min-h-[48px] px-3.5 flex items-center justify-center gap-2 rounded-md-full border transition-colors cursor-pointer shadow-sm ${
                isMasterLightOn
                  ? 'bg-md-primary text-md-on-primary border-md-primary font-bold'
                  : 'bg-md-surface-container-high text-md-on-surface-variant border-md-outline hover:text-md-on-surface'
              }`}
              title={isMasterLightOn ? '主灯效: 开启' : '主灯效: 关闭'}
              aria-label={isMasterLightOn ? '主灯效: 开启' : '主灯效: 关闭'}
            >
              <Power className="w-4 h-4" />
              <span className="text-xs">{isMasterLightOn ? '已开启' : '已关闭'}</span>
            </button>
          </div>
        </div>

        {/* 效果选择网格：MD3E 卡片矩阵 */}
        <div className="grid grid-cols-4 sm:grid-cols-6 lg:grid-cols-11 gap-2">
          {effects.map((eff) => {
            const Icon = eff.icon;
            const isSelected = currentEffect === eff.id;
            return (
              <button
                key={eff.id}
                type="button"
                onClick={() => setCurrentEffect(eff.id)}
                className={`min-h-[64px] min-w-[48px] flex flex-col items-center justify-center p-2 rounded-md-md border transition-all cursor-pointer select-none ${
                  isSelected
                    ? 'bg-md-secondary-container border-md-secondary text-md-on-secondary-container shadow-md-level1 font-bold ring-1 ring-md-secondary'
                    : 'bg-md-surface-container border-md-outline-variant/60 text-md-on-surface-variant hover:border-md-outline hover:bg-md-surface-container-high hover:text-md-on-surface'
                }`}
                title={eff.id === 'custom_keymap' ? '自定义逐键 (custom_keymap)' : eff.label}
                aria-label={eff.label}
              >
                <Icon className={`w-5 h-5 mb-1.5 transition-transform ${isSelected ? 'scale-110 text-md-on-secondary-container' : 'text-md-on-surface-variant'}`} />
                <span className="text-xs tracking-tight leading-none">{eff.label}</span>
              </button>
            );
          })}
        </div>

        {/* 响应/涟漪提示卡片 */}
        {(currentEffect === 'reactive' || currentEffect === 'ripple') && (
          <div className="p-4 rounded-md-lg border border-md-outline-variant bg-md-surface-container-high flex flex-col gap-1.5 text-md-on-surface shadow-md-level1">
            <span className="font-bold flex items-center gap-2 text-xs text-md-primary">
              <Zap className="w-4 h-4 text-md-primary" />
              响应式光效已激活
            </span>
            <p className="text-xs leading-relaxed text-md-on-surface-variant">
              敲击键盘任意实体按键（或点击上方虚拟按键），被按下的键位将瞬间触发高光并渐进式余晖衰减，所有参数修改均已自动实时生效至物理键盘！
            </p>
          </div>
        )}

        {/* 自定义逐键提示卡片 */}
        {currentEffect === 'custom_keymap' && (
          <div className="p-4 rounded-md-lg border border-md-outline-variant bg-md-surface-container-high flex flex-col gap-2 text-md-on-surface shadow-md-level1">
            <div className="flex items-center justify-between">
              <span className="font-bold flex items-center gap-2 text-xs text-md-primary">
                <Keyboard className="w-4 h-4 text-md-primary" />
                自定义逐键模式已激活
              </span>
              {setActiveTab && (
                <button
                  type="button"
                  onClick={() => setActiveTab('perkey')}
                  className="min-h-[48px] px-4 text-xs font-semibold rounded-md-full bg-md-primary text-md-on-primary hover:bg-md-primary/90 transition-colors cursor-pointer flex items-center gap-1.5"
                  aria-label="前往逐键涂装工坊"
                >
                  前往逐键涂装
                </button>
              )}
            </div>
            <p className="text-xs leading-relaxed text-md-on-surface-variant">
              当前方案支持 68 个物理按键独立 RGB 色彩覆写。您可以在右侧配置未涂装按键的全局底色，或进入“逐键涂装”面板进行自定义涂色。
            </p>
          </div>
        )}

        {/* 磁轴模拟行程卡片 */}
        {currentEffect === 'static' && (
          <div className="p-4 rounded-md-lg border border-md-outline-variant bg-md-surface-container flex flex-col gap-2 shadow-sm">
            <div className="flex items-center justify-between">
              <div className="flex flex-col">
                <span className="text-xs font-bold text-md-on-surface">磁轴模拟行程 (Analog)</span>
                <span className="text-xs text-md-on-surface-variant">实时映射实体磁轴行程下压深度</span>
              </div>
              <button
                type="button"
                onClick={() => setIsAnalogEnabled(!isAnalogEnabled)}
                className={`min-h-[48px] min-w-[72px] px-3 flex items-center justify-center rounded-md-full border text-xs font-bold transition-colors cursor-pointer ${
                  isAnalogEnabled
                    ? 'bg-md-primary text-md-on-primary border-md-primary shadow-sm'
                    : 'bg-md-surface-container-high border-md-outline text-md-on-surface-variant hover:text-md-on-surface'
                }`}
                aria-label={isAnalogEnabled ? '关闭磁轴模拟' : '开启磁轴模拟'}
              >
                {isAnalogEnabled ? '已开启' : '已关闭'}
              </button>
            </div>
          </div>
        )}
      </div>

      {/* 右栏：参数微调面板 (MD3E Card) */}
      <div className="flex flex-col gap-4 bg-md-surface-container-low p-4 rounded-md-xl border border-md-outline-variant shadow-md-level1">
        <h3 className="font-bold text-md-on-surface text-sm pb-2 border-b border-md-outline-variant">
          参数微调
        </h3>

        {/* 亮度调节 */}
        <div className="flex flex-col gap-2">
          <div className="flex items-center justify-between text-xs font-semibold text-md-on-surface">
            <span>亮度</span>
            <span className="font-mono text-md-primary font-bold">{Math.round(brightnessVal * 100)}%</span>
          </div>
          <input
            type="range"
            min="0.05"
            max="1.0"
            step="0.01"
            value={brightnessVal}
            onChange={(e) => setBrightnessVal(parseFloat(e.target.value))}
            className="w-full h-2 bg-md-surface-container-highest rounded-md-full appearance-none cursor-pointer accent-[var(--md-sys-color-primary)]"
            aria-label="亮度调节"
          />
        </div>

        {/* 推流帧率 (FPS) */}
        <div className="flex flex-col gap-2">
          <div className="flex items-center justify-between text-xs font-semibold text-md-on-surface">
            <span className="flex items-center gap-1.5">
              <Activity className="w-4 h-4 text-md-primary" />
              推流帧率 (FPS)
            </span>
            <div className="flex items-center gap-2">
              <span className={`text-xs px-2 py-0.5 rounded-md-full border font-semibold ${
                fpsVal >= 100 
                  ? 'border-md-error bg-md-error-container text-md-on-error-container' 
                  : fpsVal >= 60 
                  ? 'border-md-primary bg-md-primary-container text-md-on-primary-container' 
                  : 'border-md-outline-variant bg-md-surface-container-high text-md-on-surface-variant'
              }`}>
                {fpsVal >= 100 ? '硬件上限' : fpsVal >= 60 ? '高刷流畅' : '省电低耗'}
              </span>
              <span className="font-mono text-md-on-surface font-bold text-xs">{fpsVal} FPS</span>
            </div>
          </div>
          <input
            type="range"
            min="10"
            max="100"
            step="5"
            value={fpsVal}
            onChange={(e) => setFpsVal && setFpsVal(parseInt(e.target.value, 10))}
            className="w-full h-2 bg-md-surface-container-highest rounded-md-full appearance-none cursor-pointer accent-[var(--md-sys-color-primary)]"
            aria-label="推流帧率调节"
          />
          {/* MD3E Connected Button Group */}
          <div className="flex items-center justify-between pt-1">
            <div className="inline-flex rounded-md-full p-1 bg-md-surface-container-highest border border-md-outline-variant/60">
              {[
                { label: '25 默认', val: 25 },
                { label: '60 流畅', val: 60 },
                { label: '100 极速', val: 100 }
              ].map((p) => (
                <button
                  key={p.val}
                  type="button"
                  onClick={() => setFpsVal && setFpsVal(p.val)}
                  className={`min-h-[32px] px-3 py-1 text-xs font-mono rounded-md-full transition-colors cursor-pointer ${
                    fpsVal === p.val
                      ? 'bg-md-primary text-md-on-primary font-bold shadow-xs'
                      : 'text-md-on-surface-variant hover:text-md-on-surface'
                  }`}
                  aria-label={`设为 ${p.label}`}
                >
                  {p.label}
                </button>
              ))}
            </div>
            <span className="text-xs text-md-on-surface-variant">硬件上限 100 FPS</span>
          </div>
        </div>

        {/* 速率微调 (仅周期动画光效显示) */}
        {currentEffect !== 'static' && currentEffect !== 'custom_keymap' && (
          <div className="flex flex-col gap-2">
            <div className="flex items-center justify-between text-xs font-semibold text-md-on-surface">
              <span className="flex items-center gap-1.5">
                <Gauge className="w-4 h-4 text-md-primary" />
                运动周期速率
              </span>
              <span className="text-md-on-surface-variant font-mono text-xs">
                {speedIndex === 0 ? '5.5s' : speedIndex === 1 ? '3.2s' : '1.6s'}
              </span>
            </div>
            <div className="inline-flex rounded-md-full p-1 bg-md-surface-container-highest border border-md-outline-variant/60 self-start">
              {['慢速', '中速', '极速'].map((label, idx) => (
                <button
                  key={label}
                  type="button"
                  onClick={() => setSpeedIndex(idx)}
                  className={`min-h-[36px] px-4 py-1 text-xs font-semibold rounded-md-full transition-colors cursor-pointer ${
                    speedIndex === idx
                      ? 'bg-md-primary text-md-on-primary font-bold shadow-xs'
                      : 'text-md-on-surface-variant hover:text-md-on-surface'
                  }`}
                  aria-label={`设为 ${label}`}
                >
                  {label}
                </button>
              ))}
            </div>
          </div>
        )}

        {/* 行进方向 (wave / quicksand) */}
        {(currentEffect === 'wave' || currentEffect === 'quicksand') && (
          <div className="flex flex-col gap-2">
            <span className="text-xs font-semibold text-md-on-surface">行进方向</span>
            <div className="grid grid-cols-3 gap-2 max-w-[180px]">
              {directions.map((d) => {
                const DirIcon = d.icon;
                const isCurrent = currentDirection === d.id;
                return (
                  <button
                    key={d.id}
                    type="button"
                    onClick={() => setCurrentDirection(d.id)}
                    title={d.label}
                    aria-label={`方向: ${d.label}`}
                    className={`min-w-[48px] min-h-[48px] flex items-center justify-center rounded-md-md border transition-colors cursor-pointer ${
                      isCurrent
                        ? 'bg-md-primary text-md-on-primary border-md-primary shadow-xs font-bold'
                        : 'bg-md-surface-container border-md-outline-variant text-md-on-surface-variant hover:bg-md-surface-container-high hover:text-md-on-surface'
                    }`}
                  >
                    <DirIcon className="w-5 h-5" />
                  </button>
                );
              })}
            </div>
          </div>
        )}

        {/* 波束粗细 (wave, current, ripple, quicksand) */}
        {['wave', 'current', 'ripple', 'quicksand'].includes(currentEffect) && (
          <div className="flex flex-col gap-2">
            <div className="flex items-center justify-between text-xs font-semibold text-md-on-surface">
              <span>{currentEffect === 'ripple' ? '波纹厚度' : '波束粗细'}</span>
              <span className="font-mono text-md-primary font-bold text-xs">{thicknessVal.toFixed(1)}x</span>
            </div>
            <input
              type="range"
              min="0.2"
              max="3.0"
              step="0.1"
              value={thicknessVal}
              onChange={(e) => setThicknessVal(parseFloat(e.target.value))}
              className="w-full h-2 bg-md-surface-container-highest rounded-md-full appearance-none cursor-pointer accent-[var(--md-sys-color-primary)]"
              aria-label="波束粗细调节"
            />
          </div>
        )}

        {/* 星空全光谱随机闪烁 */}
        {currentEffect === 'starry_night' && (
          <div className="flex items-center justify-between p-3 rounded-md-lg border border-md-outline-variant bg-md-surface-container">
            <span className="text-xs font-semibold text-md-on-surface">全光谱随机闪烁</span>
            <input
              type="checkbox"
              checked={isStarryRandom}
              onChange={(e) => setIsStarryRandom(e.target.checked)}
              className="w-5 h-5 accent-[var(--md-sys-color-primary)] rounded-md-xs cursor-pointer"
              aria-label="全光谱随机闪烁开关"
            />
          </div>
        )}

        {/* 背景底色配置 (reactive / ripple) */}
        {(currentEffect === 'reactive' || currentEffect === 'ripple') && (
          <div className="flex flex-col gap-2 p-3 rounded-md-lg border border-md-outline-variant bg-md-surface-container">
            <div className="flex items-center justify-between text-xs font-semibold text-md-on-surface">
              <span>常驻背景底色</span>
              <span className="font-mono text-md-primary font-bold uppercase">{bgColor}</span>
            </div>
            <div className="flex items-center justify-between gap-2">
              <div className="flex items-center gap-2">
                <input
                  type="color"
                  value={bgColor}
                  onChange={(e) => setBgColor && setBgColor(e.target.value)}
                  className="w-8 h-8 rounded-md-xs border border-md-outline p-0 cursor-pointer bg-transparent"
                  aria-label="选择背景颜色"
                />
                <span className="text-xs text-md-on-surface-variant">未击中键位常驻底色</span>
              </div>
              <div className="flex gap-1.5">
                {BG_PRESETS.map((p) => (
                  <button
                    key={p.val}
                    type="button"
                    onClick={() => setBgColor && setBgColor(p.val)}
                    className="min-h-[32px] px-2.5 py-1 text-xs rounded-md-full border border-md-outline-variant bg-md-surface-container-high hover:bg-md-surface-container-highest text-md-on-surface cursor-pointer"
                    aria-label={`底色设为 ${p.label}`}
                  >
                    {p.label}
                  </button>
                ))}
              </div>
            </div>
          </div>
        )}

        {/* 未覆写底色配置 (custom_keymap) */}
        {currentEffect === 'custom_keymap' && (
          <div className="flex flex-col gap-2 p-3 rounded-md-lg border border-md-outline-variant bg-md-surface-container">
            <div className="flex items-center justify-between text-xs font-semibold text-md-on-surface">
              <span>未覆写按键底色</span>
              <span className="font-mono text-md-primary font-bold uppercase">{bgColor}</span>
            </div>
            <div className="flex items-center justify-between gap-2">
              <div className="flex items-center gap-2">
                <input
                  type="color"
                  value={bgColor}
                  onChange={(e) => setBgColor && setBgColor(e.target.value)}
                  className="w-8 h-8 rounded-md-xs border border-md-outline p-0 cursor-pointer bg-transparent"
                  aria-label="选择未覆写按键底色"
                />
                <span className="text-xs text-md-on-surface-variant">全局基底色彩</span>
              </div>
              <div className="flex gap-1.5">
                {BG_PRESETS.map((p) => (
                  <button
                    key={p.val}
                    type="button"
                    onClick={() => setBgColor && setBgColor(p.val)}
                    className="min-h-[32px] px-2.5 py-1 text-xs rounded-md-full border border-md-outline-variant bg-md-surface-container-high hover:bg-md-surface-container-highest text-md-on-surface cursor-pointer"
                    aria-label={`底色设为 ${p.label}`}
                  >
                    {p.label}
                  </button>
                ))}
              </div>
            </div>
          </div>
        )}

        {/* 色彩渐变色标调节器 */}
        {currentEffect === 'color_cycle' ? (
          <div className="flex flex-col gap-1 p-3 rounded-md-lg border border-md-outline-variant bg-md-surface-container text-xs">
            <span className="font-bold text-md-on-surface">全光谱自动彩虹循环</span>
            <p className="text-xs text-md-on-surface-variant leading-relaxed">
              循环模式由硬件引擎实时推流全色域连续彩虹渐变，无需手动指定色标。
            </p>
          </div>
        ) : currentEffect === 'custom_keymap' ? (
          <div className="flex flex-col gap-2 p-3 rounded-md-lg border border-md-outline-variant bg-md-surface-container text-xs">
            <span className="font-bold text-md-on-surface">逐键独立色彩涂装</span>
            <p className="text-xs text-md-on-surface-variant leading-relaxed">
              当前为自定义逐键模式。进入“逐键涂装”面板可针对 68 个物理按键分别指定专属独立色彩。
            </p>
            {setActiveTab && (
              <button
                type="button"
                onClick={() => setActiveTab('perkey')}
                className="min-h-[48px] self-start px-4 py-2 text-xs font-semibold rounded-md-full bg-md-secondary-container text-md-on-secondary-container hover:bg-md-secondary-container/90 transition-colors cursor-pointer flex items-center gap-2"
                aria-label="前往逐键涂装工坊"
              >
                <Keyboard className="w-4 h-4" />
                进入逐键涂装工坊
              </button>
            )}
          </div>
        ) : (currentEffect === 'starry_night' && isStarryRandom) ? (
          <div className="flex flex-col gap-1 p-3 rounded-md-lg border border-md-outline-variant bg-md-surface-container text-xs">
            <span className="font-bold text-md-on-surface">全光谱随机星空已开启</span>
            <p className="text-xs text-md-on-surface-variant leading-relaxed">
              繁星以随机光谱在夜空中交替绽放。若需指定单一色彩，请取消上方“全光谱随机闪烁”。
            </p>
          </div>
        ) : (
          <div className="flex flex-col gap-3 pt-3 border-t border-md-outline-variant">
            <div className="flex items-center justify-between">
              <span className="text-xs font-semibold text-md-on-surface">
                {currentEffect === 'reactive' || currentEffect === 'ripple' ? '触发高光色' : '色彩搭配预设'}
              </span>
              <select
                onChange={(e) => handlePresetSelect(e.target.value)}
                className="text-xs bg-md-surface-container border border-md-outline rounded-md-sm px-3 py-1.5 outline-none text-md-on-surface cursor-pointer focus:border-md-primary"
                aria-label="选择色彩搭配预设"
              >
                <option value="default">深邃黑白</option>
                <option value="mono">经典黑白</option>
                <option value="rainbow">彩虹色系</option>
                <option value="aurora">极光之境</option>
                <option value="ocean">蔚蓝深海</option>
              </select>
            </div>

            {/* 渐变指示条 */}
            <div className="relative flex items-center h-8 my-1 px-1">
              <div
                className="w-full h-4 rounded-md-full border border-md-outline-variant shadow-inner"
                style={{ background: gradientCss }}
              />
              {gradientStops.map((stop) => (
                <button
                  key={stop.id}
                  type="button"
                  onClick={() => setSelectedStopId(stop.id)}
                  style={{ left: `${Math.round(stop.pos * 100)}%` }}
                  className={`absolute -translate-x-1/2 w-5 h-6 rounded-md-xs border-2 shadow-md-level1 transition-transform cursor-pointer ${
                    stop.id === selectedStopId
                      ? 'border-md-primary scale-125 z-10 ring-2 ring-md-primary'
                      : 'border-white hover:scale-110'
                  }`}
                  aria-label={`色标位置 ${Math.round(stop.pos * 100)}%`}
                >
                  <span
                    className="block w-full h-full rounded-md-xs"
                    style={{ backgroundColor: stop.color }}
                  />
                </button>
              ))}
            </div>

            {/* 色标编辑与增删 */}
            <div className="flex items-center justify-between">
              <div className="flex items-center gap-2">
                <input
                  type="color"
                  value={currentStop.color}
                  onChange={(e) => {
                    const updated = gradientStops.map((s) =>
                      s.id === selectedStopId ? { ...s, color: e.target.value } : s
                    );
                    setGradientStops(updated);
                  }}
                  className="w-8 h-8 rounded-md-xs border border-md-outline p-0 cursor-pointer bg-transparent"
                  aria-label="指定色标色彩"
                />
                <span className="text-xs font-mono font-bold text-md-on-surface uppercase">
                  {currentStop.color}
                </span>
              </div>

              <div className="flex items-center gap-2">
                <button
                  type="button"
                  onClick={removeStop}
                  disabled={gradientStops.length <= 1}
                  className="min-w-[48px] min-h-[48px] flex items-center justify-center rounded-md-full bg-md-surface-container border border-md-outline-variant text-md-on-surface-variant hover:text-md-on-surface hover:bg-md-surface-container-high disabled:opacity-30 cursor-pointer transition-colors"
                  title="删除当前色标"
                  aria-label="删除当前色标"
                >
                  <Minus className="w-4 h-4" />
                </button>
                <button
                  type="button"
                  onClick={addStop}
                  className="min-w-[48px] min-h-[48px] flex items-center justify-center rounded-md-full bg-md-surface-container border border-md-outline-variant text-md-on-surface-variant hover:text-md-on-surface hover:bg-md-surface-container-high cursor-pointer transition-colors"
                  title="新增色标"
                  aria-label="新增色标"
                >
                  <Plus className="w-4 h-4" />
                </button>
              </div>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
