import React, { useState } from 'react';
import { Plus, Copy, Trash2, Star, CheckCircle2, ArrowRight } from 'lucide-react';
import { rgbToHex } from '../utils/color';

export default function ProfileManager({
  profiles,
  currentProfileName,
  defaultProfileName,
  onSelectProfile,
  onSetDefaultProfile,
  onCloneProfile,
  onDeleteProfile,
  onCreateProfile
}) {
  const [isModalOpen, setIsModalOpen] = useState(false);
  const [modalMode, setModalMode] = useState('create');
  const [inputName, setInputName] = useState('');
  const [cloneSource, setCloneSource] = useState('');

  const profileKeys = Object.keys(profiles || {});

  const handleOpenCreate = () => {
    setModalMode('create');
    setInputName('');
    setIsModalOpen(true);
  };

  const handleOpenClone = (sourceName) => {
    setModalMode('clone');
    setCloneSource(sourceName);
    setInputName(`${sourceName}_copy`);
    setIsModalOpen(true);
  };

  const handleModalSubmit = () => {
    const trimmed = inputName.trim();
    if (!trimmed) return;
    if (profiles[trimmed]) {
      alert('方案名称已存在，请换一个名称');
      return;
    }

    if (modalMode === 'create') {
      onCreateProfile(trimmed);
    } else {
      onCloneProfile(cloneSource, trimmed);
    }
    setIsModalOpen(false);
  };

  return (
    <div className="flex flex-col gap-4 p-2">
      {/* 头部 */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-2 pb-2 border-b border-slate-200">
        <div>
          <h3 className="font-bold text-slate-900 text-sm">灯效配置文件 (Profiles)</h3>
          <p className="text-xs text-slate-500 mt-0.5">
            切换、克隆与管理专属灯效方案。
          </p>
        </div>

        <button
          onClick={handleOpenCreate}
          className="w-8 h-8 aspect-square flex items-center justify-center rounded-none bg-slate-900 text-white hover:bg-slate-800 active:scale-95 transition-all shadow-xs cursor-pointer"
          title="新建方案"
        >
          <Plus className="w-4 h-4" />
        </button>
      </div>

      {/* 方案卡片网格：紧凑直角卡片与正方形按钮 */}
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-3.5">
        {profileKeys.map((pname) => {
          const prof = profiles[pname];
          const isCurrent = pname === currentProfileName;
          const isDefault = pname === defaultProfileName;
          const colorHex = prof.color ? rgbToHex(prof.color) : prof.color1 ? rgbToHex(prof.color1) : '#0F172A';

          return (
            <div
              key={pname}
              className={`p-3.5 rounded-none border transition-all flex flex-col justify-between gap-3 bg-white ${
                isCurrent
                  ? 'border-slate-900 shadow-xs ring-1 ring-slate-900'
                  : 'border-slate-300 hover:border-slate-500'
              }`}
            >
              <div className="flex items-start justify-between">
                <div className="flex items-center gap-2.5">
                  <div
                    className="w-4 h-4 rounded-none border border-slate-400 shrink-0"
                    style={{ backgroundColor: colorHex }}
                  />
                  <div>
                    <h4 className="font-bold text-slate-900 text-xs leading-tight">{pname}</h4>
                    <span className="text-[10px] text-slate-500 font-mono mt-0.5 block">
                      模式: {prof.type || 'static'}
                    </span>
                  </div>
                </div>

                <div className="flex items-center gap-1">
                  {isDefault && (
                    <span className="text-[10px] font-bold bg-slate-100 text-slate-800 border border-slate-300 px-1.5 py-0.5 rounded-none">
                      默认
                    </span>
                  )}
                  {isCurrent && (
                    <span className="text-[10px] font-bold bg-slate-900 text-white px-1.5 py-0.5 rounded-none">
                      当前
                    </span>
                  )}
                </div>
              </div>

              {/* 操作按钮：紧凑正方形 */}
              <div className="flex items-center justify-between pt-2.5 border-t border-slate-200">
                {!isCurrent ? (
                  <button
                    onClick={() => onSelectProfile(pname)}
                    className="flex items-center gap-1 text-xs font-bold text-slate-900 hover:underline cursor-pointer"
                  >
                    <span>切换使用</span>
                    <ArrowRight className="w-3 h-3" />
                  </button>
                ) : (
                  <span className="text-[11px] font-medium text-slate-500">正在使用</span>
                )}

                <div className="flex items-center gap-1.5">
                  {!isDefault && (
                    <button
                      onClick={() => onSetDefaultProfile(pname)}
                      className="w-7 h-7 aspect-square flex items-center justify-center rounded-none border border-slate-300 text-slate-600 hover:border-slate-600 hover:bg-slate-50 transition-colors"
                      title="设为默认"
                    >
                      <Star className="w-3.5 h-3.5" />
                    </button>
                  )}
                  <button
                    onClick={() => handleOpenClone(pname)}
                    className="w-7 h-7 aspect-square flex items-center justify-center rounded-none border border-slate-300 text-slate-600 hover:border-slate-600 hover:bg-slate-50 transition-colors"
                    title="克隆此方案"
                  >
                    <Copy className="w-3.5 h-3.5" />
                  </button>
                  {profileKeys.length > 1 && (
                    <button
                      onClick={() => onDeleteProfile(pname)}
                      className="w-7 h-7 aspect-square flex items-center justify-center rounded-none border border-slate-300 text-slate-600 hover:text-slate-900 hover:border-slate-600 hover:bg-slate-50 transition-colors"
                      title="删除方案"
                    >
                      <Trash2 className="w-3.5 h-3.5" />
                    </button>
                  )}
                </div>
              </div>
            </div>
          );
        })}
      </div>

      {/* 弹窗 */}
      {isModalOpen && (
        <div className="fixed inset-0 bg-slate-900/40 backdrop-blur-2xs flex items-center justify-center z-50 p-4">
          <div className="bg-white rounded-none border border-slate-300 shadow-xl max-w-sm w-full p-5 flex flex-col gap-3.5">
            <h4 className="font-bold text-slate-900 text-sm">
              {modalMode === 'create' ? '新建方案' : `克隆方案: ${cloneSource}`}
            </h4>
            <div className="flex flex-col gap-1">
              <label className="text-xs font-bold text-slate-700">方案英文标识名称</label>
              <input
                type="text"
                value={inputName}
                onChange={(e) => setInputName(e.target.value)}
                placeholder="例如: custom_mode"
                className="w-full px-2.5 py-1.5 text-xs bg-white border border-slate-300 rounded-none outline-none focus:border-slate-900 font-mono"
                autoFocus
              />
            </div>

            <div className="flex justify-end gap-2 pt-1">
              <button
                onClick={() => setIsModalOpen(false)}
                className="px-3 py-1.5 rounded-none text-xs font-bold text-slate-700 bg-slate-100 hover:bg-slate-200 border border-slate-300 active:scale-95"
              >
                取消
              </button>
              <button
                onClick={handleModalSubmit}
                className="px-3.5 py-1.5 rounded-none text-xs font-bold text-white bg-slate-900 hover:bg-slate-800 active:scale-95"
              >
                确定
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
