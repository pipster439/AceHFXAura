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
  bgColor,
  fpsVal = 25,
  blocklyFrame
}) {
  const containerRef = useRef(null);
  const blocklyFrameRef = useRef(blocklyFrame);
  blocklyFrameRef.current = blocklyFrame;
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

    animRef.current.reactiveDecays[keyName] = 1.0;
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
        startTime: performance.now()
      });
      if (animRef.current.activeRipples.length > 8) {
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

    let lastRenderTime = 0;
    const renderFrame = (timestamp) => {
      if (!isVisible) return;

      const t = timestamp || 0;
      const targetFps = fpsVal || 25;
      const frameInterval = 1000 / targetFps;

      if (t - lastRenderTime < frameInterval - 1.0) {
        animId = requestAnimationFrame(renderFrame);
        return;
      }
      lastRenderTime = t;

      const period = getPeriodMs();
      const dirVec = DIR_VECTORS[currentDirection] || DIR_VECTORS.right;
      const keysOverride = currentProfile?.keys || {};
      const rippleSpeed = 0.014 * (2500.0 / period);

      let globalLedIndex = 0;
      KEYBOARD_LAYOUT.forEach((row, rowIdx) => {
        let colOffset = 0.0;

        row.forEach((k) => {
          const el = keysDomRef.current[k.name];
          const currLedId = globalLedIndex++;
          if (!el) {
            colOffset += k.width;
            return;
          }

          let r = 0, g = 0, b = 0; // 默认深色未点亮暗态

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

          if (activeTab === 'blockly_effect' && blocklyFrameRef.current && blocklyFrameRef.current[currLedId]) {
            const bCol = blocklyFrameRef.current[currLedId];
            r = bCol[0]; g = bCol[1]; b = bCol[2];
          } else if (activeTab === 'perkey' || currentEffect === 'custom_keymap') {
            if (hasOverride && ovColor) {
              r = ovColor[0]; g = ovColor[1]; b = ovColor[2];
            } else {
              const base = hexToRgb(bgColor || '#000000');
              r = base[0]; g = base[1]; b = base[2];
            }
          } else if (hasOverride && ovColor) {
            r = ovColor[0]; g = ovColor[1]; b = ovColor[2];
          } else if (currentEffect === 'static') {
            const col = hexToRgb(gradientStops[0]?.color || '#0050C8');
            r = col[0]; g = col[1]; b = col[2];
            if (isAnalogEnabled) {
              const extra = animRef.current.analogDecays[k.name] || 0;
              if (extra > 0) {
                animRef.current.analogDecays[k.name] = Math.max(0, extra - 0.035);
                r = Math.min(255, Math.floor(r + extra * (255 - r)));
                g = Math.min(255, Math.floor(g + extra * (255 - g)));
                b = Math.min(255, Math.floor(b + extra * (255 - b)));
              }
            }
          } else if (currentEffect === 'breathing') {
            const phase = (t % period) / period;
            const factor = 0.5 - 0.5 * Math.cos(2.0 * Math.PI * phase);
            const col1 = hexToRgb(gradientStops[0]?.color || '#0064FF');
            const col2 = hexToRgb(gradientStops[1]?.color || '#000A32');
            r = Math.floor(col1[0] * (1.0 - factor) + col2[0] * factor);
            g = Math.floor(col1[1] * (1.0 - factor) + col2[1] * factor);
            b = Math.floor(col1[2] * (1.0 - factor) + col2[2] * factor);
          } else if (currentEffect === 'color_cycle') {
            const hue = ((t % period) / period) * 360.0;
            [r, g, b] = hsvToRgb(hue, 1.0, 1.0);
          } else if (currentEffect === 'wave') {
            let proj = 0.0;
            if (currentDirection === 'spread') {
              const dx = colOffset - 8.0;
              const dy = (rowIdx - 2.0) * 2.0;
              proj = (Math.sqrt(dx * dx + dy * dy) / 10.0) * thicknessVal;
            } else {
              const normX = colOffset / 16.0;
              const normY = rowIdx / 4.0;
              proj = (normX * dirVec.x + normY * dirVec.y) * thicknessVal;
            }
            const phase = (t % period) / period;
            let ratio = (proj - phase + 100.0) % 1.0;
            if (ratio < 0) ratio += 1.0;
            const col = hexToRgb(sampleGradientColor(gradientStops, ratio));
            r = col[0]; g = col[1]; b = col[2];
          } else if (currentEffect === 'reactive') {
            const baseCol = hexToRgb(bgColor || '#00050F');
            const trigCol = hexToRgb(gradientStops[0]?.color || '#FF1929');
            const decay = animRef.current.reactiveDecays[k.name] || 0;
            if (decay > 0.01) {
              animRef.current.reactiveDecays[k.name] = Math.max(0, decay - 0.035);
              r = Math.min(255, Math.floor(baseCol[0] + decay * (trigCol[0] - baseCol[0])));
              g = Math.min(255, Math.floor(baseCol[1] + decay * (trigCol[1] - baseCol[1])));
              b = Math.min(255, Math.floor(baseCol[2] + decay * (trigCol[2] - baseCol[2])));
            } else {
              r = baseCol[0];
              g = baseCol[1];
              b = baseCol[2];
            }
          } else if (currentEffect === 'ripple') {
            const halfThick = Math.max(0.1, 1.8 * thicknessVal);
            let totalGlow = 0.0;
            const centerCol = colOffset + k.width / 2.0;

            for (const rip of animRef.current.activeRipples) {
              const radius = (t - rip.startTime) * rippleSpeed;
              if (radius <= 20.0) {
                const dx = centerCol - rip.col;
                const dy = (rowIdx - rip.row) * 1.0;
                const dist = Math.sqrt(dx * dx + dy * dy);
                const diff = Math.abs(dist - radius);
                if (diff < halfThick) {
                  const wf = (1.0 - diff / halfThick) * (1.0 - radius / 20.0);
                  totalGlow += wf;
                }
              }
            }

            const baseCol = hexToRgb(bgColor || '#00050F');
            const trigCol = hexToRgb(gradientStops[0]?.color || '#00F0FF');
            if (totalGlow > 0.01) {
              const factor = Math.min(1.0, Math.max(0.0, totalGlow));
              r = Math.min(255, Math.floor(baseCol[0] + factor * (trigCol[0] - baseCol[0])));
              g = Math.min(255, Math.floor(baseCol[1] + factor * (trigCol[1] - baseCol[1])));
              b = Math.min(255, Math.floor(baseCol[2] + factor * (trigCol[2] - baseCol[2])));
            } else {
              r = baseCol[0];
              g = baseCol[1];
              b = baseCol[2];
            }
          } else if (currentEffect === 'starry_night') {
            const seed = (Math.round(colOffset) * 17 + rowIdx * 31) % 100;
            const starPhase = ((t + seed * 45) % period) / period;
            const glow = Math.max(0, Math.sin(starPhase * Math.PI) * 1.5 - 0.5);
            if (isStarryRandom) {
              const hue = seed * 3.6;
              [r, g, b] = hsvToRgb(hue, 1.0, glow);
            } else {
              const col = hexToRgb(gradientStops[0]?.color || '#00F0FF');
              r = Math.floor(col[0] * glow);
              g = Math.floor(col[1] * glow);
              b = Math.floor(col[2] * glow);
            }
          } else if (currentEffect === 'quicksand') {
            const phase = (t % period) / period;
            let proj = 0.0;
            if (currentDirection === 'spread') {
              const cx = colOffset - 8.0;
              const cy = (rowIdx - 2.5) * 2.0;
              proj = Math.sqrt(cx * cx + cy * cy) * 0.4 * thicknessVal;
            } else {
              proj = (colOffset * 0.4 * dirVec.x + rowIdx * 0.8 * dirVec.y) * thicknessVal;
            }
            const wave = Math.sin(proj + phase * 2.0 * Math.PI) * 0.5 + 0.5;
            const col1 = hexToRgb(gradientStops[0]?.color || '#FF1929');
            const col2 = hexToRgb(gradientStops[1]?.color || '#148AC4');
            r = Math.floor(col1[0] * wave + col2[0] * (1.0 - wave));
            g = Math.floor(col1[1] * wave + col2[1] * (1.0 - wave));
            b = Math.floor(col1[2] * wave + col2[2] * (1.0 - wave));
          } else if (currentEffect === 'current') {
            const half = Math.max(1, period / 2);
            const phase = (t % half) / half;
            const pulseCol = phase * 16.0;
            const pulseWidth = Math.max(0.1, 1.6 * thicknessVal);
            const diff = Math.abs(colOffset - pulseCol);
            if (diff < pulseWidth) {
              r = 255; g = 255; b = 255;
            } else {
              const col = hexToRgb(gradientStops[0]?.color || '#00F0FF');
              r = Math.floor(col[0] * 0.15);
              g = Math.floor(col[1] * 0.15);
              b = Math.floor(col[2] * 0.15);
            }
          } else if (currentEffect === 'raindrop') {
            const seed = (Math.round(colOffset) * 23 + rowIdx * 47) % 80;
            const dropPhase = ((t + seed * 80) % period) / period;
            const dropGlow = Math.pow(Math.max(0, 1.0 - dropPhase * 4.0), 2.0);
            const col = hexToRgb(gradientStops[0]?.color || '#00F0FF');
            const intensity = 0.15 + 0.85 * dropGlow;
            r = Math.floor(col[0] * intensity);
            g = Math.floor(col[1] * intensity);
            b = Math.floor(col[2] * intensity);
          }

          // 亮度调整：真实光学乘法缩放，关闭时降至纯黑 [0, 0, 0]
          const effectiveBrightness = isMasterLightOn ? Math.max(0, Math.min(1.0, brightnessVal)) : 0;
          r = Math.min(255, Math.max(0, Math.round(r * effectiveBrightness)));
          g = Math.min(255, Math.max(0, Math.round(g * effectiveBrightness)));
          b = Math.min(255, Math.max(0, Math.round(b * effectiveBrightness)));

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
        (rip) => (t - rip.startTime) * rippleSpeed <= 20.0
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
    bgColor,
    fpsVal
  ]);

  return (
    <div 
      ref={containerRef}
      className="w-full bg-transparent flex flex-col items-center justify-center shrink-0 overflow-hidden pt-6 pb-2"
    >
      {/* 键盘主体缩放容器：自适应居中与缩放 */}
      <div 
        className="origin-top flex justify-center mt-1"
        style={{
          width: baseWidth,
          transform: `scale(${scale})`,
          marginBottom: `-${Math.round((1 - scale) * 310)}px`,
          transition: 'transform var(--md-sys-motion-default-spatial)'
        }}
      >
        <div className="relative w-[980px] p-4 bg-md-surface-container border border-md-outline-variant rounded-md-xl shadow-md-level2 select-none">
          <div className="flex flex-col gap-2 p-2 bg-md-surface-container-highest/50 border border-md-outline-variant/40 rounded-md-lg">
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
                      type="button"
                      aria-label={`${k.name} 按键`}
                      ref={(el) => {
                        if (el) keysDomRef.current[k.name] = el;
                      }}
                      onClick={() => triggerKeyAction(k.name)}
                      style={{ width: `calc(${widthPct}% - 8px)` }}
                      className={`relative h-full flex flex-col items-center justify-center rounded-md-xs border transition-transform text-xs font-bold select-none cursor-pointer outline-none shadow-xs ${
                        isPressed ? 'scale-95 shadow-inner' : 'hover:scale-[1.02]'
                      } ${
                        isSelected
                          ? 'ring-2 ring-md-primary border-md-primary z-10'
                          : 'border-md-outline-variant/70'
                      }`}
                    >
                      {hasCustom && (
                        <span className="absolute top-1 right-1 w-2 h-2 bg-md-primary rounded-md-full shadow-xs" />
                      )}
                      {k.sub && (
                        <span className="text-[11px] opacity-75 pointer-events-none -mb-0.5 leading-none">
                          {k.sub}
                        </span>
                      )}
                      <span className="pointer-events-none text-xs leading-tight font-medium">
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
