import React, { useEffect, useRef, useState } from 'react';
import { KEYBOARD_LAYOUT, CODE_TO_KEY_MAP, DIR_VECTORS } from '../constants/keyboardLayout';
import { hexToRgb, rgbToHex, hsvToRgb, sampleGradientColor } from '../utils/color';

export default function KeyboardVisualizer({
  activeTab,
  currentEffect,
  isMasterLightOn,
  isAnalogEnabled,
  brightnessVal,
  speedIndex,
  currentDirection,
  thicknessVal,
  gradientStops,
  isStarryRandom,
  currentProfile,
  selectedKeyNames,
  onToggleKeySelection,
  bgColor
}) {
  const containerRef = useRef(null);
  const [scale, setScale] = useState(1);
  const [pressedKeys, setPressedKeys] = useState(new Set());

  // 动态光效运行数据缓存 (60 FPS 无 React state 重绘开销)
  const animRef = useRef({
    reactiveDecays: {},
    analogDecays: {},
    activeRipples: []
  });

  const keysDomRef = useRef({});

  // 键盘放大设计基准：980px 宽度
  const baseWidth = 980;

  // 自适应缩放监听，绝无横向溢出
  useEffect(() => {
    if (!containerRef.current) return;
    const observer = new ResizeObserver((entries) => {
      for (const entry of entries) {
        const width = entry.contentRect.width;
        const newScale = Math.min(1.08, (width - 24) / baseWidth);
        setScale(Math.max(0.48, newScale));
      }
    });
    observer.observe(containerRef.current);
    return () => observer.disconnect();
  }, []);

  const triggerKeyAction = (keyName) => {
    if (activeTab === 'perkey') {
      onToggleKeySelection(keyName);
      return;
    }

    animRef.current.reactiveDecays[keyName] = 1.4;
    animRef.current.analogDecays[keyName] = 1.0;

    // 涟漪发射
    let targetKeyData = null;
    KEYBOARD_LAYOUT.forEach((row, rIdx) => {
      let cOffset = 0;
      row.forEach((k) => {
        if (k.name === keyName) {
          targetKeyData = { col: cOffset + k.width / 2, row: rIdx };
        }
        cOffset += k.width;
      });
    });

    if (targetKeyData) {
      animRef.current.activeRipples.push({
        col: targetKeyData.col,
        row: targetKeyData.row,
        startTime: performance.now(),
        maxRadius: 20.0,
        speed: 0.018
      });
      if (animRef.current.activeRipples.length > 12) {
        animRef.current.activeRipples.shift();
      }
    }
  };

  // 全局物理按键捕获
  useEffect(() => {
    const handleKeyDown = (e) => {
      if (['INPUT', 'SELECT', 'TEXTAREA'].includes(e.target.tagName)) return;
      const keyName = CODE_TO_KEY_MAP[e.code] || (e.key ? e.key.toUpperCase() : null);
      if (keyName) {
        setPressedKeys((prev) => new Set(prev).add(keyName));
        triggerKeyAction(keyName);
      }
    };

    const handleKeyUp = (e) => {
      if (['INPUT', 'SELECT', 'TEXTAREA'].includes(e.target.tagName)) return;
      const keyName = CODE_TO_KEY_MAP[e.code] || (e.key ? e.key.toUpperCase() : null);
      if (keyName) {
        setPressedKeys((prev) => {
          const next = new Set(prev);
          next.delete(keyName);
          return next;
        });
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    window.addEventListener('keyup', handleKeyUp);
    return () => {
      window.removeEventListener('keydown', handleKeyDown);
      window.removeEventListener('keyup', handleKeyUp);
    };
  }, [activeTab]);

  // 60 FPS 动态帧仿真与节能挂起
  useEffect(() => {
    let animId = null;
    let isVisible = !document.hidden;

    const handleVisibility = () => {
      isVisible = !document.hidden;
      if (isVisible) {
        animId = requestAnimationFrame(renderFrame);
      }
    };
    document.addEventListener('visibilitychange', handleVisibility);

    const getPeriodMs = () => {
      if (speedIndex === 0) return 5500;
      if (speedIndex === 1) return 3200;
      return 1600;
    };

    const renderFrame = (timestamp) => {
      if (!isVisible) return;

      const t = timestamp || 0;
      const period = getPeriodMs();
      const dirVec = DIR_VECTORS[currentDirection] || DIR_VECTORS.right;
      const keysOverride = currentProfile?.keys || {};

      KEYBOARD_LAYOUT.forEach((row, rowIdx) => {
        let colOffset = 0.0;

        row.forEach((k) => {
          const el = keysDomRef.current[k.name];
          if (!el) {
            colOffset += k.width;
            return;
          }

          let r = 244, g = 246, b = 249; // 浅色底

          let hasOverride = false;
          let ovColor = null;
          if (keysOverride[k.name]) {
            ovColor = keysOverride[k.name];
            hasOverride = true;
          } else if (keysOverride['WASD'] && ['W', 'A', 'S', 'D'].includes(k.name)) {
            ovColor = keysOverride['WASD'];
            hasOverride = true;
          } else if (keysOverride['ARROWS'] && ['UP', 'DOWN', 'LEFT', 'RIGHT'].includes(k.name)) {
            ovColor = keysOverride['ARROWS'];
            hasOverride = true;
          }

          if (activeTab === 'perkey') {
            if (hasOverride && ovColor) {
              r = ovColor[0]; g = ovColor[1]; b = ovColor[2];
            } else {
              const base = hexToRgb(bgColor || '#F1F5F9');
              r = base[0]; g = base[1]; b = base[2];
            }
          } else if (!isMasterLightOn) {
            r = 230; g = 233; b = 237;
          } else if (hasOverride && ovColor) {
            r = ovColor[0]; g = ovColor[1]; b = ovColor[2];
          } else if (currentEffect === 'static') {
            const col = hexToRgb(gradientStops[0]?.color || '#0F172A');
            r = col[0]; g = col[1]; b = col[2];
            if (isAnalogEnabled) {
              const extra = animRef.current.analogDecays[k.name] || 0;
              if (extra > 0) {
                animRef.current.analogDecays[k.name] = Math.max(0, extra - 0.035);
                r = Math.min(255, r + extra * (255 - r));
                g = Math.min(255, g + extra * (255 - g));
                b = Math.min(255, b + extra * (255 - b));
              }
            }
          } else if (currentEffect === 'breathing') {
            const phase = (t % period) / period;
            const factor = 0.5 - 0.5 * Math.cos(2.0 * Math.PI * phase);
            const col1 = hexToRgb(gradientStops[0]?.color || '#0F172A');
            const col2 = hexToRgb(gradientStops[1]?.color || '#F8FAFC');
            r = col1[0] * factor + col2[0] * (1.0 - factor);
            g = col1[1] * factor + col2[1] * (1.0 - factor);
            b = col1[2] * factor + col2[2] * (1.0 - factor);
          } else if (currentEffect === 'color_cycle') {
            const hue = ((t % period) / period) * 360.0;
            [r, g, b] = hsvToRgb(hue, 0.85, 1.0);
          } else if (currentEffect === 'wave') {
            let proj = 0.0;
            if (currentDirection === 'spread') {
              const dx = colOffset - 8.0;
              const dy = (rowIdx - 2.0) * 2.0;
              proj = Math.sqrt(dx * dx + dy * dy) / 10.0;
            } else {
              const normX = colOffset / 16.0;
              const normY = rowIdx / 4.0;
              proj = (normX * dirVec.x + normY * dirVec.y) * thicknessVal;
            }
            const phase = (t % period) / period;
            const ratio = (proj - phase + 100.0) % 1.0;
            const col = hexToRgb(sampleGradientColor(gradientStops, ratio));
            r = col[0]; g = col[1]; b = col[2];
          } else if (currentEffect === 'starry_night') {
            if (isStarryRandom) {
              const seed = (colOffset * 19 + rowIdx * 37) % 360;
              const starPhase = ((t + seed * 40) % 2200) / 2200;
              const starGlow = Math.max(0, Math.sin(starPhase * Math.PI) * 1.5 - 0.5);
              [r, g, b] = hsvToRgb(seed, 0.8, starGlow);
            } else {
              const seed = (colOffset * 17 + rowIdx * 31) % 100;
              const starPhase = ((t + seed * 50) % 2000) / 2000;
              const starGlow = Math.max(0, Math.sin(starPhase * Math.PI) * 1.5 - 0.5);
              const col = hexToRgb(sampleGradientColor(gradientStops, seed / 100.0));
              r = col[0] * starGlow + 244 * (1 - starGlow);
              g = col[1] * starGlow + 246 * (1 - starGlow);
              b = col[2] * starGlow + 249 * (1 - starGlow);
            }
          } else if (currentEffect === 'quicksand') {
            const phase = (t % period) / period;
            const wave = Math.sin(colOffset * 0.4 * dirVec.x + rowIdx * 0.8 * dirVec.y + phase * 2 * Math.PI);
            const factor = wave * 0.5 + 0.5;
            const col = hexToRgb(sampleGradientColor(gradientStops, factor));
            r = col[0]; g = col[1]; b = col[2];
          } else if (currentEffect === 'current') {
            const phase = (t % (period / 2)) / (period / 2);
            const isPulse = Math.abs(colOffset - phase * 16.0) < 1.8 * thicknessVal;
            if (isPulse) {
              r = 255; g = 255; b = 255;
            } else {
              const col = hexToRgb(gradientStops[0]?.color || '#0284C7');
              r = col[0] * 0.2 + 200;
              g = col[1] * 0.2 + 205;
              b = col[2] * 0.2 + 210;
            }
          } else if (currentEffect === 'raindrop') {
            const seed = (colOffset * 23 + rowIdx * 47) % 80;
            const dropPhase = ((t + seed * 80) % 2800) / 2800;
            const dropGlow = Math.pow(Math.max(0, 1.0 - dropPhase * 4), 2);
            const col = hexToRgb(gradientStops[0]?.color || '#0284C7');
            r = col[0] * dropGlow + 244 * (1 - dropGlow);
            g = col[1] * dropGlow + 246 * (1 - dropGlow);
            b = col[2] * dropGlow + 249 * (1 - dropGlow);
          } else if (currentEffect === 'reactive' || currentEffect === 'ripple') {
            let totalGlow = 0.0;
            const decay = animRef.current.reactiveDecays[k.name] || 0;
            if (decay > 0) {
              totalGlow += decay;
              animRef.current.reactiveDecays[k.name] = Math.max(0, decay - 0.025);
            }

            const centerCol = colOffset + k.width / 2.0;
            for (const rip of animRef.current.activeRipples) {
              const radius = (t - rip.startTime) * rip.speed;
              if (radius <= rip.maxRadius) {
                const dx = centerCol - rip.col;
                const dy = (rowIdx - rip.row) * 2.2;
                const dist = Math.sqrt(dx * dx + dy * dy);
                const diff = Math.abs(dist - radius);
                if (diff < 1.8) {
                  const wf = (1.0 - diff / 1.8) * (1.0 - radius / rip.maxRadius);
                  totalGlow += wf * 0.95;
                }
              }
            }

            let baseCol = hexToRgb(gradientStops[0]?.color || '#FF1929');
            if (baseCol[0] < 35 && baseCol[1] < 35 && baseCol[2] < 35) {
              if (gradientStops[1]?.color) {
                baseCol = hexToRgb(gradientStops[1].color);
              } else {
                baseCol = [255, 25, 41]; // ROG 红色
              }
            }
            if (totalGlow > 0.01) {
              const factor = Math.min(1.0, totalGlow);
              r = Math.min(255, Math.floor(baseCol[0] * factor + 241 * (1 - factor)));
              g = Math.min(255, Math.floor(baseCol[1] * factor + 245 * (1 - factor)));
              b = Math.min(255, Math.floor(baseCol[2] * factor + 249 * (1 - factor)));
            } else {
              r = 241; g = 245; b = 249;
            }
          }

          // 亮度调整
          r = Math.min(255, Math.floor(r * brightnessVal + 244 * (1 - brightnessVal)));
          g = Math.min(255, Math.floor(g * brightnessVal + 246 * (1 - brightnessVal)));
          b = Math.min(255, Math.floor(b * brightnessVal + 249 * (1 - brightnessVal)));

          el.style.backgroundColor = `rgb(${r}, ${g}, ${b})`;

          const lum = 0.299 * r + 0.587 * g + 0.114 * b;
          if (lum < 140) {
            el.style.color = '#ffffff';
          } else {
            el.style.color = '#0f172a';
          }

          colOffset += k.width;
        });
      });

      animRef.current.activeRipples = animRef.current.activeRipples.filter(
        (rip) => (t - rip.startTime) * rip.speed <= rip.maxRadius
      );

      animId = requestAnimationFrame(renderFrame);
    };

    animId = requestAnimationFrame(renderFrame);
    return () => {
      cancelAnimationFrame(animId);
      document.removeEventListener('visibilitychange', handleVisibility);
    };
  }, [
    activeTab,
    currentEffect,
    isMasterLightOn,
    isAnalogEnabled,
    brightnessVal,
    speedIndex,
    currentDirection,
    thicknessVal,
    gradientStops,
    isStarryRandom,
    currentProfile,
    bgColor
  ]);

  return (
    <div 
      ref={containerRef}
      className="w-full bg-transparent flex flex-col items-center justify-center shrink-0 overflow-hidden pt-8 pb-4"
    >
      {/* 键盘主体缩放容器：往下移动并放大 */}
      <div 
        className="transition-transform duration-200 ease-out origin-top flex justify-center mt-2"
        style={{
          width: baseWidth,
          transform: `scale(${scale})`,
          marginBottom: `-${Math.round((1 - scale) * 310)}px`
        }}
      >
        <div className="relative w-[980px] p-3.5 bg-slate-100 border border-slate-300 rounded-none shadow-2xs select-none">
          <div className="flex flex-col gap-2 p-1.5 bg-slate-200/80 border border-slate-300 rounded-none">
            {KEYBOARD_LAYOUT.map((row, rIdx) => (
              <div key={rIdx} className="flex gap-2 h-[50px]">
                {row.map((k) => {
                  const isSelected = selectedKeyNames?.has(k.name);
                  const isPressed = pressedKeys.has(k.name);
                  const widthPct = (k.width / 16.0) * 100;
                  const hasCustom = !!(
                    currentProfile?.keys?.[k.name] ||
                    (currentProfile?.keys?.WASD && ['W', 'A', 'S', 'D'].includes(k.name)) ||
                    (currentProfile?.keys?.ARROWS && ['UP', 'DOWN', 'LEFT', 'RIGHT'].includes(k.name))
                  );

                  return (
                    <button
                      key={k.name}
                      ref={(el) => {
                        if (el) keysDomRef.current[k.name] = el;
                      }}
                      onClick={() => triggerKeyAction(k.name)}
                      style={{ width: `calc(${widthPct}% - 8px)` }}
                      className={`relative h-full flex flex-col items-center justify-center rounded-none border transition-all duration-75 text-xs font-bold select-none cursor-pointer outline-none ${
                        isPressed ? 'translate-y-0.5 shadow-inner' : 'hover:border-slate-500'
                      } ${
                        isSelected
                          ? 'ring-2 ring-slate-900 border-slate-900 z-10'
                          : 'border-slate-300'
                      }`}
                    >
                      {hasCustom && (
                        <span className="absolute top-1 right-1 w-1.5 h-1.5 bg-slate-900 rounded-none" />
                      )}
                      {k.sub && (
                        <span className="text-[10px] opacity-60 pointer-events-none -mb-0.5 leading-none">
                          {k.sub}
                        </span>
                      )}
                      <span className="pointer-events-none text-xs leading-tight">
                        {k.label}
                      </span>
                    </button>
                  );
                })}
              </div>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
}
