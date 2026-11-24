import React, { useState, useEffect, useCallback } from "react";
import { Volume2, Monitor, ShieldCheck, RefreshCw, Power } from "lucide-react";

export default function InputSourcesControl({ embedded = false }) {
  const [inputs, setInputs] = useState(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [expanded, setExpanded] = useState(false);

  const refresh = useCallback(async () => {
    try {
      setError("");
      const res = await fetch("/api/inputs", { cache: "no-store" });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data = await res.json();
      setInputs(data);
    } catch (e) {
      setError(e.message || "获取输入状态失败");
    }
  }, []);

  useEffect(() => {
    if (embedded) refresh();
  }, [embedded, refresh]);

  const toggle = async (type) => {
    if (busy || !inputs) return;
    const current = inputs[type]?.enabled ?? false;
    const target = !current;
    if (target && globalThis.confirm?.(`开启${type === "audio" ? "系统音频特征采集 (WASAPI)" : "屏幕环境色彩采集 (GDI)"}？\n\n采集数据仅在本地内存运行，绝不上传，绝不注入外部进程；可随时一键关闭。`) === false) {
      return;
    }
    setBusy(true);
    setError("");
    try {
      const res = await fetch("/api/inputs", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ [type]: target })
      });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data = await res.json();
      if (data.status !== "ok") throw new Error(data.error || "操作未确认");
      setInputs(data);
    } catch (e) {
      setError(e.message || "更新采集状态失败");
    } finally {
      setBusy(false);
    }
  };

  const disableAll = async () => {
    if (busy) return;
    setBusy(true);
    setError("");
    try {
      const res = await fetch("/api/inputs", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ audio: false, screen: false })
      });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data = await res.json();
      setInputs(data);
    } catch (e) {
      setError(e.message || "停用全部采集失败");
    } finally {
      setBusy(false);
    }
  };

  const audio = inputs?.audio;
  const screen = inputs?.screen;

  return (
    <details
      data-input-capture-controls
      className="shrink-0 rounded-md-lg border border-md-outline-variant bg-md-surface-container-low p-3 text-xs"
      open={expanded}
      onToggle={e => setExpanded(e.currentTarget.open)}
    >
      <summary className="flex cursor-pointer items-center justify-between font-semibold">
        <span>
          动态输入采集 · 音频与屏幕
          {inputs ? ` (${(audio?.enabled ? 1 : 0) + (screen?.enabled ? 1 : 0)} 项激活)` : " (读取中)"}
        </span>
        <span className="text-md-on-surface-variant text-[10px]">展开设置</span>
      </summary>
      <div className="mt-2 space-y-3">
        <p className="text-md-on-surface-variant">
          为保证隐私与运行安全，系统音频 (WASAPI Loopback) 与屏幕环境颜色 (GDI) 采样默认保持关闭。仅在用户明确授权后开启，且可随时一键关闭。
        </p>
        {error && <p role="alert" className="text-md-error break-words">{error}</p>}
        <div className="grid grid-cols-1 md:grid-cols-2 gap-2">
          {/* Audio capture */}
          <div className="rounded border border-md-outline-variant p-2 flex flex-col justify-between">
            <div>
              <div className="flex items-center justify-between">
                <span className="font-semibold flex items-center gap-1">
                  <Volume2 className="h-3.5 w-3.5" /> 系统音频采集
                </span>
                <span className={`px-1.5 py-0.5 rounded text-[10px] font-medium ${audio?.enabled ? "bg-emerald-500/20 text-emerald-400 border border-emerald-500/30" : "bg-zinc-700/30 text-zinc-400"}`}>
                  {audio?.enabled ? "已开启" : "已停用"}
                </span>
              </div>
              <p className="mt-1 text-md-on-surface-variant text-[11px]">
                WASAPI 回环捕获实时计算 RMS、峰值与频谱质心。
                {audio?.capture && (
                  <span className="block text-[10px] text-zinc-400 mt-0.5">
                    捕获帧: {audio.capture.frames_captured || 0} · 错误: {audio.capture.capture_errors || 0} · 状态: {audio.capture.availability}
                  </span>
                )}
              </p>
            </div>
            <div className="mt-2 flex justify-end">
              <button
                type="button"
                disabled={busy}
                onClick={() => toggle("audio")}
                className={`rounded px-2.5 py-1 font-semibold text-xs transition-colors ${
                  audio?.enabled
                    ? "bg-rose-900/40 hover:bg-rose-900/60 text-rose-200 border border-rose-700/50"
                    : "bg-md-primary-container hover:bg-md-primary-container/80 text-md-on-primary-container"
                }`}
              >
                {audio?.enabled ? "停用音频" : "启用音频"}
              </button>
            </div>
          </div>

          {/* Screen capture */}
          <div className="rounded border border-md-outline-variant p-2 flex flex-col justify-between">
            <div>
              <div className="flex items-center justify-between">
                <span className="font-semibold flex items-center gap-1">
                  <Monitor className="h-3.5 w-3.5" /> 屏幕环境色彩
                </span>
                <span className={`px-1.5 py-0.5 rounded text-[10px] font-medium ${screen?.enabled ? "bg-emerald-500/20 text-emerald-400 border border-emerald-500/30" : "bg-zinc-700/30 text-zinc-400"}`}>
                  {screen?.enabled ? "已开启" : "已停用"}
                </span>
              </div>
              <p className="mt-1 text-md-on-surface-variant text-[11px]">
                GDI 桌面环境色降采样 (16x16) 平均 RGB 提取。
                {screen?.capture && (
                  <span className="block text-[10px] text-zinc-400 mt-0.5">
                    采样帧: {screen.capture.frames_captured || 0} · 错误: {screen.capture.capture_errors || 0} · 状态: {screen.capture.availability}
                  </span>
                )}
              </p>
            </div>
            <div className="mt-2 flex justify-end">
              <button
                type="button"
                disabled={busy}
                onClick={() => toggle("screen")}
                className={`rounded px-2.5 py-1 font-semibold text-xs transition-colors ${
                  screen?.enabled
                    ? "bg-rose-900/40 hover:bg-rose-900/60 text-rose-200 border border-rose-700/50"
                    : "bg-md-primary-container hover:bg-md-primary-container/80 text-md-on-primary-container"
                }`}
              >
                {screen?.enabled ? "停用屏幕" : "启用屏幕"}
              </button>
            </div>
          </div>
        </div>

        <div className="flex items-center justify-between pt-1 border-t border-md-outline-variant/40">
          <button
            type="button"
            onClick={refresh}
            disabled={busy}
            className="flex items-center gap-1 rounded border px-2 py-1 text-xs hover:bg-md-surface-container"
          >
            <RefreshCw className="h-3 w-3" /> 刷新状态
          </button>
          {(audio?.enabled || screen?.enabled) && (
            <button
              type="button"
              onClick={disableAll}
              disabled={busy}
              className="flex items-center gap-1 rounded border border-rose-700/60 bg-rose-950/40 px-2.5 py-1 text-xs font-semibold text-rose-300 hover:bg-rose-900/60"
            >
              <Power className="h-3 w-3" /> 一键停用全部采集
            </button>
          )}
        </div>
        <p className="text-[11px] text-md-on-surface-variant flex items-center gap-1">
          <ShieldCheck className="h-3.5 w-3.5 shrink-0 text-emerald-400" />
          <span>本地内存闭环，采集仅在后台守护进程存活期间生效，进程退出自动释放。</span>
        </p>
      </div>
    </details>
  );
}
