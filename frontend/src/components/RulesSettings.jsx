import React from 'react';
import { Plus, Trash2, Gamepad2, FileCode, Globe, FileText, ShieldAlert } from 'lucide-react';

export default function RulesSettings({
  rules,
  onAddRule,
  onDeleteRule,
  onUpdateRule,
  profiles,
  currentProfileName
}) {
  const profileList = Object.keys(profiles || {});

  const quickPresets = [
    { label: 'CS2', process: 'cs2.exe', profile: 'cs2_gamer', suppress: true, icon: Gamepad2 },
    { label: 'VS Code', process: 'code.exe', profile: 'coding', suppress: false, icon: FileCode },
    { label: 'Steam', process: 'steam.exe', profile: 'cs2_gamer', suppress: false, icon: Globe },
    { label: '记事本', process: 'notepad.exe', profile: 'office', suppress: false, icon: FileText }
  ];

  return (
    <div className="flex flex-col gap-4 p-1">
      {/* 头部标题与添加规则 */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-3 pb-3 border-b border-md-outline-variant">
        <div>
          <h3 className="font-bold text-md-on-surface text-sm">前台进程联动与游戏抑制规则</h3>
          <p className="text-xs text-md-on-surface-variant mt-0.5">
            根据前台窗口进程自动切换对应灯效方案，检测到全屏游戏时可自动挂起网页服务保障微秒级性能。
          </p>
        </div>

        <button
          type="button"
          onClick={onAddRule}
          className="min-h-[48px] px-4 flex items-center justify-center gap-2 rounded-md-full bg-md-primary text-md-on-primary hover:bg-md-primary/90 active:scale-95 transition-transform shadow-md-level1 cursor-pointer text-xs font-bold"
          title="添加新规则"
          aria-label="添加新规则"
        >
          <Plus className="w-4 h-4" />
          <span>添加新规则</span>
        </button>
      </div>

      {/* 快捷推荐 (MD3E Assist Chips) */}
      <div className="flex flex-wrap items-center gap-2">
        <span className="text-xs font-semibold text-md-on-surface">常用预设:</span>
        {quickPresets.map((preset) => {
          const PresetIcon = preset.icon;
          return (
            <button
              key={preset.process}
              type="button"
              onClick={() => {
                const exists = rules.some((r) => r.process.toLowerCase() === preset.process.toLowerCase());
                if (!exists) {
                  onAddRule({
                    process: preset.process,
                    profile: profileList.includes(preset.profile) ? preset.profile : currentProfileName,
                    suppress_web_ui: preset.suppress
                  });
                }
              }}
              className="min-h-[40px] px-3.5 flex items-center gap-2 rounded-md-full border border-md-outline-variant bg-md-surface-container text-md-on-surface hover:bg-md-surface-container-high active:scale-95 transition-all text-xs font-medium cursor-pointer"
              aria-label={`添加预设规则: ${preset.label}`}
            >
              <PresetIcon className="w-4 h-4 text-md-primary" />
              <span>{preset.label}</span>
            </button>
          );
        })}
      </div>

      {/* 规则数据表格 (MD3E Surface Container) */}
      <div className="overflow-x-auto border border-md-outline-variant rounded-md-lg bg-md-surface-container-low shadow-md-level1">
        <table className="w-full text-left text-xs border-collapse">
          <thead>
            <tr className="bg-md-surface-container-high border-b border-md-outline-variant text-md-on-surface font-bold">
              <th className="py-3 px-4">程序文件名 (EXE)</th>
              <th className="py-3 px-4">关联灯效方案</th>
              <th className="py-3 px-4">竞技游戏免打扰与零占用</th>
              <th className="py-3 px-4 text-right">操作</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-md-outline-variant/60">
            {rules.length === 0 ? (
              <tr>
                <td colSpan={4} className="py-8 text-center text-md-on-surface-variant font-medium">
                  尚未配置任何联动规则，请点击右上角按钮添加
                </td>
              </tr>
            ) : (
              rules.map((rule, idx) => (
                <tr key={idx} className="hover:bg-md-surface-container/60 transition-colors">
                  <td className="py-3 px-4">
                    <input
                      type="text"
                      value={rule.process || ''}
                      onChange={(e) => onUpdateRule(idx, { process: e.target.value.trim().toLowerCase() })}
                      placeholder="例如: cs2.exe"
                      className="w-56 min-h-[40px] px-3 bg-md-surface-container border border-md-outline rounded-md-sm text-md-on-surface text-xs font-mono font-medium outline-none focus:border-md-primary focus:ring-1 focus:ring-md-primary"
                      aria-label="程序文件名"
                    />
                  </td>
                  <td className="py-3 px-4">
                    <select
                      value={rule.profile}
                      onChange={(e) => onUpdateRule(idx, { profile: e.target.value })}
                      className="min-h-[40px] px-3 bg-md-surface-container border border-md-outline rounded-md-sm text-md-on-surface text-xs font-semibold outline-none focus:border-md-primary focus:ring-1 focus:ring-md-primary cursor-pointer"
                      aria-label="关联灯效方案"
                    >
                      {profileList.map((p) => (
                        <option key={p} value={p} className="bg-md-surface text-md-on-surface">
                          {p}
                        </option>
                      ))}
                    </select>
                  </td>
                  <td className="py-3 px-4">
                    <label className="inline-flex items-center gap-2 cursor-pointer select-none">
                      <input
                        type="checkbox"
                        checked={!!rule.suppress_web_ui}
                        onChange={(e) => onUpdateRule(idx, { suppress_web_ui: e.target.checked })}
                        className="w-5 h-5 accent-[var(--md-sys-color-primary)] rounded-md-xs cursor-pointer"
                        aria-label="竞技游戏免打扰与零占用选项"
                      />
                      <span className="text-md-on-surface font-medium flex items-center gap-1.5 text-xs">
                        <ShieldAlert className="w-4 h-4 text-md-primary" />
                        竞技游戏免打扰与零占用
                      </span>
                    </label>
                  </td>
                  <td className="py-3 px-4 text-right">
                    <button
                      type="button"
                      onClick={() => onDeleteRule(idx)}
                      className="w-10 h-10 min-w-[40px] min-h-[40px] inline-flex items-center justify-center text-md-on-surface-variant hover:text-md-error hover:bg-md-error-container/30 rounded-md-full transition-colors cursor-pointer"
                      title="删除规则"
                      aria-label="删除规则"
                    >
                      <Trash2 className="w-4 h-4" />
                    </button>
                  </td>
                </tr>
              ))
            )}
          </tbody>
        </table>
      </div>
    </div>
  );
}
