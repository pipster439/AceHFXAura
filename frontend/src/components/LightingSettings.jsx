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
  Gauge
} from 'lucide-react';
import { GRADIENT_PRESETS } from '../constants/keyboardLayout';

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
  bgColor = '#000000',
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
    const nextStops = [...gradientStops, { id: newId, pos: 0.5, color: '#0284C7' }];
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
    <div className="grid grid-cols-1 lg:grid-cols-3 gap-6 p-2">
      {/* 左栏：10 款效果选择 (精致紧凑正方形按钮) */}
      <div className="lg:col-span-2 flex flex-col gap-4">
        <div className="flex items-center justify-between pb-2 border-b border-slate-200">
          <h3 className="font-bold text-slate-900 text-sm">预设模式</h3>
          <div className="flex items-center gap-2">
            <span className="text-xs font-semibold text-slate-500">主灯</span>
            <button
              onClick={() => setIsMasterLightOn(!isMasterLightOn)}
              className={`w-7 h-7 aspect-square flex items-center justify-center rounded-none border transition-all cursor-pointer ${
                isMasterLightOn
                  ? 'bg-slate-900 text-white border-slate-900'
                  : 'bg-white text-slate-400 border-slate-300 hover:text-slate-700'
              }`}
              title={isMasterLightOn ? '主灯效: 开启' : '主灯效: 关闭'}
            >
              <Power className="w-3.5 h-3.5" />
            </button>
          </div>
        </div>

        {/* 效果选择网格：紧凑精巧正方形尺寸 */}
        <div className="grid grid-cols-4 sm:grid-cols-6 lg:grid-cols-11 gap-1.5">
          {effects.map((eff) => {
            const Icon = eff.icon;
            const isSelected = currentEffect === eff.id;
            return (
              <button
                key={eff.id}
                type="button"
                onClick={() => setCurrentEffect(eff.id)}
                className={`aspect-square flex flex-col items-center justify-center p-1.5 rounded-none border transition-all cursor-pointer select-none ${
                  isSelected
                    ? 'bg-slate-900 border-slate-900 text-white shadow-xs'
                    : 'bg-white border-slate-300 text-slate-700 hover:border-slate-500 hover:bg-slate-50 active:scale-98'
                }`}
                title={eff.id === 'custom_keymap' ? '自定义逐键 (custom_keymap)' : eff.label}
              >
                <Icon className={`w-4 h-4 mb-1 ${isSelected ? 'text-white' : 'text-slate-700'}`} />
                <span className="text-[10px] font-bold tracking-tight leading-none">{eff.label}</span>
              </button>
            );
          })}
        </div>

        {/* 响应/涟漪提示 */}
        {(currentEffect === 'reactive' || currentEffect === 'ripple') && (
          <div className="p-3 rounded-none border border-emerald-300 bg-emerald-50/80 flex flex-col gap-1 text-emerald-900 text-xs">
            <span className="font-bold flex items-center gap-1.5">
              <Zap className="w-3.5 h-3.5 text-emerald-600" />
              响应式光效已就绪：
            </span>
            <p className="text-[11px] leading-relaxed text-emerald-800">
              敲击键盘任意实体按键（或点击上方虚拟按键），被按下的键位将瞬间触发高光并渐进式余晖衰减，所有参数修改均已自动实时生效至物理键盘！
            </p>
          </div>
        )}

        {/* 自定义逐键提示 */}
        {currentEffect === 'custom_keymap' && (
          <div className="p-3 rounded-none border border-sky-300 bg-sky-50/80 flex flex-col gap-1.5 text-sky-900 text-xs">
            <div className="flex items-center justify-between">
              <span className="font-bold flex items-center gap-1.5">
                <Keyboard className="w-3.5 h-3.5 text-sky-600" />
                自定义逐键模式已激活：
              </span>
              {setActiveTab && (
                <button
                  type="button"
                  onClick={() => setActiveTab('perkey')}
                  className="px-2 py-0.5 text-[10px] font-bold rounded-none bg-sky-900 text-white hover:bg-sky-800 cursor-pointer"
                >
                  前往逐键涂装
                </button>
              )}
            </div>
            <p className="text-[11px] leading-relaxed text-sky-800">
              当前方案支持 68 个物理按键独立 RGB 色彩覆写。您可以在右侧配置未涂装按键的全局底色，或进入“逐键涂装”面板进行自定义涂色。
            </p>
          </div>
        )}

        {/* 磁轴模拟灯效 */}
        {currentEffect === 'static' && (
          <div className="p-3 rounded-none border border-slate-300 bg-white flex flex-col gap-1.5">
            <div className="flex items-center justify-between">
              <span className="text-xs font-bold text-slate-900">磁轴模拟行程 (Analog)</span>
              <button
                type="button"
                onClick={() => setIsAnalogEnabled(!isAnalogEnabled)}
                className={`w-7 h-7 aspect-square flex items-center justify-center rounded-none border text-[10px] font-bold transition-all cursor-pointer ${
                  isAnalogEnabled
                    ? 'bg-slate-900 text-white border-slate-900'
                    : 'bg-white border-slate-300 text-slate-600 hover:border-slate-500'
                }`}
              >
                {isAnalogEnabled ? 'ON' : 'OFF'}
              </button>
            </div>
            <p className="text-[11px] text-slate-500 leading-relaxed">
              实时映射实体磁轴行程下压深度，呈现白炽高亮及渐进式余晖衰减。
            </p>
          </div>
        )}
      </div>

      {/* 右栏：参数微调 */}
      <div className="flex flex-col gap-4">
        <h3 className="font-bold text-slate-900 text-sm pb-2 border-b border-slate-200">
          参数调节
        </h3>

        {/* 亮度 */}
        <div className="flex flex-col gap-1.5">
          <div className="flex items-center justify-between text-xs font-semibold text-slate-900">
            <span>亮度</span>
            <span className="font-mono text-slate-600 text-[11px]">{Math.round(brightnessVal * 100)}%</span>
          </div>
          <input
            type="range"
            min="0.05"
            max="1.0"
            step="0.01"
            value={brightnessVal}
            onChange={(e) => setBrightnessVal(parseFloat(e.target.value))}
            className="w-full h-1.5 bg-slate-200 rounded-none appearance-none cursor-pointer accent-slate-900"
          />
        </div>

        {/* 推流帧率 (FPS) */}
        <div className="flex flex-col gap-1.5">
          <div className="flex items-center justify-between text-xs font-semibold text-slate-900">
            <span className="flex items-center gap-1">
              <Activity className="w-3.5 h-3.5 text-slate-500" />
              推流帧率 (FPS)
            </span>
            <div className="flex items-center gap-1.5">
              <span className={`text-[10px] px-1.5 py-0.2 rounded-none border font-medium ${
                fpsVal >= 100 
                  ? 'border-amber-400 bg-amber-50 text-amber-700' 
                  : fpsVal >= 60 
                  ? 'border-emerald-400 bg-emerald-50 text-emerald-700' 
                  : 'border-slate-300 bg-slate-50 text-slate-600'
              }`}>
                {fpsVal >= 100 ? '硬件上限' : fpsVal >= 60 ? '高刷流畅' : '省电低耗'}
              </span>
              <span className="font-mono text-slate-900 font-bold text-xs">{fpsVal} FPS</span>
            </div>
          </div>
          <input
            type="range"
            min="10"
            max="100"
            step="5"
            value={fpsVal}
            onChange={(e) => setFpsVal && setFpsVal(parseInt(e.target.value, 10))}
            className="w-full h-1.5 bg-slate-200 rounded-none appearance-none cursor-pointer accent-slate-900"
          />
          <div className="flex items-center justify-between">
            <div className="flex gap-1">
              {[
                { label: '25 默认', val: 25 },
                { label: '60 流畅', val: 60 },
                { label: '100 极速', val: 100 }
              ].map((p) => (
                <button
                  key={p.val}
                  type="button"
                  onClick={() => setFpsVal && setFpsVal(p.val)}
                  className={`px-1.5 py-0.5 text-[10px] font-mono rounded-none border transition-all cursor-pointer ${
                    fpsVal === p.val
                      ? 'bg-slate-900 text-white border-slate-900'
                      : 'bg-white border-slate-300 text-slate-600 hover:border-slate-400'
                  }`}
                >
                  {p.label}
                </button>
              ))}
            </div>
            <span className="text-[10px] text-slate-600 font-medium">硬件上限 100 FPS (10ms)</span>
          </div>
        </div>

        {/* 速度：紧凑正方形按钮 (仅具备周期动画的光效显示，静态与逐键隐藏) */}
        {currentEffect !== 'static' && currentEffect !== 'custom_keymap' && (
          <div className="flex flex-col gap-1.5">
            <div className="flex items-center justify-between text-xs font-semibold text-slate-900">
              <span className="flex items-center gap-1">
                <Gauge className="w-3.5 h-3.5 text-slate-500" />
                速率
              </span>
              <span className="text-slate-500 font-mono text-[10px]">
                {speedIndex === 0 ? '5.5s' : speedIndex === 1 ? '3.2s' : '1.6s'}
              </span>
            </div>
            <div className="flex gap-1.5">
              {['慢', '中', '快'].map((label, idx) => (
                <button
                  key={label}
                  type="button"
                  onClick={() => setSpeedIndex(idx)}
                  className={`w-8 h-8 aspect-square flex items-center justify-center text-xs font-bold rounded-none border transition-all cursor-pointer ${
                    speedIndex === idx
                      ? 'bg-slate-900 text-white border-slate-900'
                      : 'bg-white border-slate-300 text-slate-700 hover:border-slate-500'
                  }`}
                >
                  {label}
                </button>
              ))}
            </div>
          </div>
        )}

        {/* 方向罗盘：紧凑正方形网格 */}
        {(currentEffect === 'wave' || currentEffect === 'quicksand') && (
          <div className="flex flex-col gap-1.5">
            <span className="text-xs font-semibold text-slate-900">行进方向</span>
            <div className="flex flex-wrap gap-1.5 max-w-[200px]">
              {directions.map((d) => {
                const DirIcon = d.icon;
                const isCurrent = currentDirection === d.id;
                return (
                  <button
                    key={d.id}
                    type="button"
                    onClick={() => setCurrentDirection(d.id)}
                    title={d.label}
                    className={`w-7 h-7 aspect-square flex items-center justify-center rounded-none border transition-all cursor-pointer ${
                      isCurrent
                        ? 'bg-slate-900 text-white border-slate-900'
                        : 'bg-white border-slate-300 text-slate-700 hover:border-slate-500'
                    }`}
                  >
                    <DirIcon className="w-3.5 h-3.5" />
                  </button>
                );
              })}
            </div>
          </div>
        )}

        {/* 波束/波纹厚度 (wave, current, ripple, quicksand) */}
        {['wave', 'current', 'ripple', 'quicksand'].includes(currentEffect) && (
          <div className="flex flex-col gap-1.5">
            <div className="flex items-center justify-between text-xs font-semibold text-slate-900">
              <span>{currentEffect === 'ripple' ? '波纹厚度' : '波束粗细'}</span>
              <span className="font-mono text-slate-600 text-[11px]">{thicknessVal.toFixed(1)}x</span>
            </div>
            <input
              type="range"
              min="0.2"
              max="3.0"
              step="0.1"
              value={thicknessVal}
              onChange={(e) => setThicknessVal(parseFloat(e.target.value))}
              className="w-full h-1.5 bg-slate-200 rounded-none appearance-none cursor-pointer accent-slate-900"
            />
          </div>
        )}

        {/* 星空随机 */}
        {currentEffect === 'starry_night' && (
          <div className="flex items-center justify-between p-2 rounded-none border border-slate-300 bg-white">
            <span className="text-xs font-semibold text-slate-800">全光谱闪烁</span>
            <input
              type="checkbox"
              checked={isStarryRandom}
              onChange={(e) => setIsStarryRandom(e.target.checked)}
              className="w-4 h-4 accent-slate-900 rounded-none cursor-pointer"
            />
          </div>
        )}

        {/* 背景底色 (reactive / ripple) */}
        {(currentEffect === 'reactive' || currentEffect === 'ripple') && (
          <div className="flex flex-col gap-1.5 p-2 rounded-none border border-slate-300 bg-white">
            <div className="flex items-center justify-between text-xs font-semibold text-slate-900">
              <span>背景底色 (Base Color)</span>
              <span className="font-mono text-slate-600 text-[11px] uppercase">{bgColor}</span>
            </div>
            <div className="flex items-center justify-between gap-2">
              <div className="flex items-center gap-2">
                <input
                  type="color"
                  value={bgColor}
                  onChange={(e) => setBgColor && setBgColor(e.target.value)}
                  className="w-6 h-6 rounded-none border border-slate-300 p-0 cursor-pointer bg-transparent"
                />
                <span className="text-[11px] text-slate-500">未触发按键常驻底色</span>
              </div>
              <div className="flex gap-1">
                {[
                  { label: '纯黑', val: '#000000' },
                  { label: '深蓝', val: '#00050F' },
                  { label: '深灰', val: '#111827' }
                ].map((p) => (
                  <button
                    key={p.val}
                    type="button"
                    onClick={() => setBgColor && setBgColor(p.val)}
                    className="px-1.5 py-0.5 text-[10px] rounded-none border border-slate-200 bg-slate-50 hover:bg-slate-100 text-slate-700 cursor-pointer"
                  >
                    {p.label}
                  </button>
                ))}
              </div>
            </div>
          </div>
        )}

        {/* 未涂装底色设置 (custom_keymap) */}
        {currentEffect === 'custom_keymap' && (
          <div className="flex flex-col gap-1.5 p-2 rounded-none border border-slate-300 bg-white">
            <div className="flex items-center justify-between text-xs font-semibold text-slate-900">
              <span>未覆写按键底色</span>
              <span className="font-mono text-slate-600 text-[11px] uppercase">{bgColor}</span>
            </div>
            <div className="flex items-center justify-between gap-2">
              <div className="flex items-center gap-2">
                <input
                  type="color"
                  value={bgColor}
                  onChange={(e) => setBgColor && setBgColor(e.target.value)}
                  className="w-6 h-6 rounded-none border border-slate-300 p-0 cursor-pointer bg-transparent"
                />
                <span className="text-[11px] text-slate-500">全局基底色彩</span>
              </div>
              <div className="flex gap-1">
                {[
                  { label: '纯黑', val: '#000000' },
                  { label: '深蓝', val: '#00050F' },
                  { label: '深灰', val: '#111827' }
                ].map((p) => (
                  <button
                    key={p.val}
                    type="button"
                    onClick={() => setBgColor && setBgColor(p.val)}
                    className="px-1.5 py-0.5 text-[10px] rounded-none border border-slate-200 bg-slate-50 hover:bg-slate-100 text-slate-700 cursor-pointer"
                  >
                    {p.label}
                  </button>
                ))}
              </div>
            </div>
          </div>
        )}

        {/* 色彩搭配 / 渐变轨分流 */}
        {currentEffect === 'color_cycle' ? (
          <div className="flex flex-col gap-1 p-3 rounded-none border border-slate-200 bg-slate-50 text-slate-700 text-xs">
            <span className="font-bold text-slate-900">全光谱自动彩虹循环</span>
            <p className="text-[11px] text-slate-500 leading-relaxed">
              循环模式由 C++ 引擎实时推流连续彩虹色相，无需手动配置色彩。
            </p>
          </div>
        ) : currentEffect === 'custom_keymap' ? (
          <div className="flex flex-col gap-2 p-3 rounded-none border border-slate-200 bg-slate-50 text-slate-700 text-xs">
            <span className="font-bold text-slate-900">逐键独立色彩配置</span>
            <p className="text-[11px] text-slate-500 leading-relaxed">
              已选定自定义逐键模式。进入“逐键涂装”面板可为 68 个物理按键分别指定独立色彩。
            </p>
            {setActiveTab && (
              <button
                type="button"
                onClick={() => setActiveTab('perkey')}
                className="self-start px-2.5 py-1 text-xs font-bold rounded-none bg-slate-900 text-white hover:bg-slate-800 transition-all cursor-pointer flex items-center gap-1.5"
              >
                <Keyboard className="w-3.5 h-3.5" />
                进入逐键涂装面板
              </button>
            )}
          </div>
        ) : (currentEffect === 'starry_night' && isStarryRandom) ? (
          <div className="flex flex-col gap-1 p-3 rounded-none border border-slate-200 bg-slate-50 text-slate-700 text-xs">
            <span className="font-bold text-slate-900">全光谱随机闪烁已开启</span>
            <p className="text-[11px] text-slate-500 leading-relaxed">
              繁星以随机光谱在夜空中绽放。若需指定单一色彩，请取消勾选上方“全光谱闪烁”。
            </p>
          </div>
        ) : (
          <div className="flex flex-col gap-2 pt-2 border-t border-slate-200">
            <div className="flex items-center justify-between">
              <span className="text-xs font-semibold text-slate-900">
                {currentEffect === 'reactive' || currentEffect === 'ripple' ? '触发高光色' : '色彩搭配'}
              </span>
              <select
                onChange={(e) => handlePresetSelect(e.target.value)}
                className="text-xs bg-white border border-slate-300 rounded-none px-2 py-1 outline-none text-slate-800 cursor-pointer"
              >
                <option value="default">深邃黑白</option>
                <option value="mono">经典黑白</option>
                <option value="rainbow">彩虹色系</option>
                <option value="aurora">极光之境</option>
                <option value="ocean">蔚蓝深海</option>
              </select>
            </div>

            <div className="relative flex items-center h-6 my-0.5">
              <div
                className="w-full h-3 rounded-none border border-slate-400"
                style={{ background: gradientCss }}
              />
              {gradientStops.map((stop) => (
                <button
                  key={stop.id}
                  type="button"
                  onClick={() => setSelectedStopId(stop.id)}
                  style={{ left: `${Math.round(stop.pos * 100)}%` }}
                  className={`absolute -translate-x-1/2 w-3.5 h-4.5 rounded-none border-2 transition-transform cursor-pointer ${
                    stop.id === selectedStopId
                      ? 'border-slate-900 scale-110 z-10'
                      : 'border-white'
                  }`}
                >
                  <span
                    className="block w-full h-full rounded-none"
                    style={{ backgroundColor: stop.color }}
                  />
                </button>
              ))}
            </div>

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
                  className="w-6 h-6 rounded-none border border-slate-300 p-0 cursor-pointer bg-transparent"
                />
                <span className="text-[11px] font-mono font-bold text-slate-800 uppercase">
                  {currentStop.color}
                </span>
              </div>

              <div className="flex items-center gap-1">
                <button
                  type="button"
                  onClick={removeStop}
                  disabled={gradientStops.length <= 1}
                  className="w-6 h-6 aspect-square flex items-center justify-center rounded-none bg-white border border-slate-300 text-slate-700 hover:border-slate-500 disabled:opacity-30 cursor-pointer"
                  title="删除色标"
                >
                  <Minus className="w-3 h-3" />
                </button>
                <button
                  type="button"
                  onClick={addStop}
                  className="w-6 h-6 aspect-square flex items-center justify-center rounded-none bg-white border border-slate-300 text-slate-700 hover:border-slate-500 cursor-pointer"
                  title="新增色标"
                >
                  <Plus className="w-3 h-3" />
                </button>
              </div>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
