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
    <div className="flex flex-col gap-4 p-2">
      {/* 头部 */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-2 pb-2 border-b border-slate-200">
        <div>
          <h3 className="font-bold text-slate-900 text-sm">前台进程联动与游戏抑制规则</h3>
          <p className="text-xs text-slate-500 mt-0.5">
            根据前台窗口进程自动切换灯效，检测到游戏时自动挂起服务保障性能。
          </p>
        </div>

        <button
          onClick={onAddRule}
          className="w-8 h-8 aspect-square flex items-center justify-center rounded-none bg-slate-900 text-white hover:bg-slate-800 active:scale-95 transition-all shadow-xs cursor-pointer"
          title="添加新规则"
        >
          <Plus className="w-4 h-4" />
        </button>
      </div>

      {/* 快捷推荐：紧凑按钮 */}
      <div className="flex flex-wrap items-center gap-2">
        <span className="text-xs font-semibold text-slate-700">快速添加:</span>
        {quickPresets.map((preset) => {
          const PresetIcon = preset.icon;
          return (
            <button
              key={preset.process}
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
              className="h-8 px-2.5 flex items-center gap-1.5 rounded-none border border-slate-300 bg-white text-slate-800 hover:border-slate-500 hover:bg-slate-50 active:scale-95 transition-all text-xs font-semibold"
            >
              <PresetIcon className="w-3.5 h-3.5 text-slate-600" />
              <span>{preset.label}</span>
            </button>
          );
        })}
      </div>

      {/* 规则数据表格 */}
      <div className="overflow-x-auto border border-slate-300 rounded-none bg-white">
        <table className="w-full text-left text-xs border-collapse">
          <thead>
            <tr className="bg-slate-100 border-b border-slate-300 text-slate-700 font-bold">
              <th className="py-2.5 px-3.5">程序文件名 (EXE)</th>
              <th className="py-2.5 px-3.5">关联灯效方案</th>
              <th className="py-2.5 px-3.5">游戏免打扰与零占用</th>
              <th className="py-2.5 px-3.5 text-right">操作</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-slate-200">
            {rules.length === 0 ? (
              <tr>
                <td colSpan={4} className="py-6 text-center text-slate-400 font-medium">
                  尚未配置任何联动规则，点击右上方按钮添加
                </td>
              </tr>
            ) : (
              rules.map((rule, idx) => (
                <tr key={idx} className="hover:bg-slate-50 transition-colors">
                  <td className="py-2.5 px-3.5">
                    <input
                      type="text"
                      value={rule.process || ''}
                      onChange={(e) => onUpdateRule(idx, { process: e.target.value.trim().toLowerCase() })}
                      placeholder="例如: cs2.exe"
                      className="w-48 px-2.5 py-1 bg-white border border-slate-300 rounded-none text-slate-900 text-xs font-mono font-medium outline-none focus:border-slate-900"
                    />
                  </td>
                  <td className="py-2.5 px-3.5">
                    <select
                      value={rule.profile}
                      onChange={(e) => onUpdateRule(idx, { profile: e.target.value })}
                      className="px-2.5 py-1 bg-white border border-slate-300 rounded-none text-slate-900 text-xs font-bold outline-none focus:border-slate-900 cursor-pointer"
                    >
                      {profileList.map((p) => (
                        <option key={p} value={p}>
                          {p}
                        </option>
                      ))}
                    </select>
                  </td>
                  <td className="py-2.5 px-3.5">
                    <label className="inline-flex items-center gap-1.5 cursor-pointer">
                      <input
                        type="checkbox"
                        checked={!!rule.suppress_web_ui}
                        onChange={(e) => onUpdateRule(idx, { suppress_web_ui: e.target.checked })}
                        className="w-3.5 h-3.5 accent-slate-900 rounded-none cursor-pointer"
                      />
                      <span className="text-slate-700 font-medium flex items-center gap-1 text-[11px]">
                        <ShieldAlert className="w-3.5 h-3.5 text-slate-500" />
                        竞技游戏免打扰与零占用
                      </span>
                    </label>
                  </td>
                  <td className="py-2.5 px-3.5 text-right">
                    <button
                      onClick={() => onDeleteRule(idx)}
                      className="w-7 h-7 aspect-square inline-flex items-center justify-center text-slate-400 hover:text-slate-900 rounded-none border border-transparent hover:border-slate-300 hover:bg-slate-100 transition-colors cursor-pointer"
                      title="删除规则"
                    >
                      <Trash2 className="w-3.5 h-3.5" />
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
