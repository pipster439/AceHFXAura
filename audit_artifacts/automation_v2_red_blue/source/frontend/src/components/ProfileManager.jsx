import React, { useState } from 'react';
import { Plus, Copy, Trash2, Star, ArrowRight, Layers, X } from 'lucide-react';
import { rgbToHex } from '../utils/color';
import { DEFAULT_STUDIO_COLOR } from '../tokens/keyboardPresets.tokens';

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
    <div className="flex flex-col gap-6 p-1">
      {/* 头部标题与新建方案按钮 */}
      <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-3 pb-3 border-b border-md-outline-variant">
        <div>
          <h3 className="font-bold text-md-on-surface text-sm">灯效方案管理器 (Profiles)</h3>
          <p className="text-xs text-md-on-surface-variant mt-0.5">
            切换、克隆与管理专属灯效方案，设置系统开机默认激活配置。
          </p>
        </div>

        <button
          type="button"
          onClick={handleOpenCreate}
          className="min-h-[48px] px-4 flex items-center justify-center gap-2 rounded-md-full bg-md-primary text-md-on-primary hover:bg-md-primary/90 active:scale-95 transition-transform shadow-md-level1 cursor-pointer text-xs font-bold"
          title="新建方案"
          aria-label="新建方案"
        >
          <Plus className="w-4 h-4" />
          <span>新建方案</span>
        </button>
      </div>

      {/* 方案卡片网格 (MD3E Cards) */}
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
        {profileKeys.map((pname) => {
          const prof = profiles[pname];
          const isCurrent = pname === currentProfileName;
          const isDefault = pname === defaultProfileName;
          const colorHex = prof.color ? rgbToHex(prof.color) : prof.color1 ? rgbToHex(prof.color1) : DEFAULT_STUDIO_COLOR;

          return (
            <div
              key={pname}
              className={`p-4 rounded-md-lg border transition-all flex flex-col justify-between gap-4 bg-md-surface-container-low shadow-sm ${
                isCurrent
                  ? 'border-md-primary shadow-md-level1 ring-1 ring-md-primary'
                  : 'border-md-outline-variant hover:border-md-outline'
              }`}
            >
              <div className="flex items-start justify-between">
                <div className="flex items-center gap-3">
                  <div
                    className="w-5 h-5 rounded-md-xs border border-md-outline-variant shrink-0 shadow-xs"
                    style={{ backgroundColor: colorHex }}
                  />
                  <div>
                    <h4 className="font-bold text-md-on-surface text-sm leading-tight">{pname}</h4>
                    <span className="text-xs text-md-on-surface-variant font-mono mt-0.5 block">
                      模式: {prof.type || 'static'}
                    </span>
                  </div>
                </div>

                <div className="flex items-center gap-1.5">
                  {isDefault && (
                    <span className="text-xs font-bold bg-md-secondary-container text-md-on-secondary-container border border-md-secondary px-2 py-0.5 rounded-md-full">
                      默认
                    </span>
                  )}
                  {isCurrent && (
                    <span className="text-xs font-bold bg-md-primary text-md-on-primary px-2 py-0.5 rounded-md-full">
                      当前
                    </span>
                  )}
                </div>
              </div>

              {/* 卡片底部操作条 */}
              <div className="flex items-center justify-between pt-3 border-t border-md-outline-variant/60">
                {!isCurrent ? (
                  <button
                    type="button"
                    onClick={() => onSelectProfile(pname)}
                    className="min-h-[48px] flex items-center gap-1.5 text-xs font-bold text-md-primary hover:underline cursor-pointer"
                    aria-label={`切换到方案 ${pname}`}
                  >
                    <span>切换使用</span>
                    <ArrowRight className="w-4 h-4" />
                  </button>
                ) : (
                  <span className="min-h-[48px] flex items-center text-xs font-medium text-md-on-surface-variant">
                    正在硬件生效中
                  </span>
                )}

                <div className="flex items-center gap-1">
                  {!isDefault && (
                    <button
                      type="button"
                      onClick={() => onSetDefaultProfile(pname)}
                      className="w-10 h-10 min-w-[40px] min-h-[40px] flex items-center justify-center rounded-md-full text-md-on-surface-variant hover:text-md-primary hover:bg-md-surface-container-high transition-colors cursor-pointer"
                      title="设为默认方案"
                      aria-label={`设为默认方案: ${pname}`}
                    >
                      <Star className="w-4 h-4" />
                    </button>
                  )}
                  <button
                    type="button"
                    onClick={() => handleOpenClone(pname)}
                    className="w-10 h-10 min-w-[40px] min-h-[40px] flex items-center justify-center rounded-md-full text-md-on-surface-variant hover:text-md-on-surface hover:bg-md-surface-container-high transition-colors cursor-pointer"
                    title="克隆此方案"
                    aria-label={`克隆此方案: ${pname}`}
                  >
                    <Copy className="w-4 h-4" />
                  </button>
                  {profileKeys.length > 1 && (
                    <button
                      type="button"
                      onClick={() => onDeleteProfile(pname)}
                      className="w-10 h-10 min-w-[40px] min-h-[40px] flex items-center justify-center rounded-md-full text-md-on-surface-variant hover:text-md-error hover:bg-md-error-container/30 transition-colors cursor-pointer"
                      title="删除方案"
                      aria-label={`删除方案: ${pname}`}
                    >
                      <Trash2 className="w-4 h-4" />
                    </button>
                  )}
                </div>
              </div>
            </div>
          );
        })}
      </div>

      {/* MD3E 标准模态对话框 (Modal Dialog: corner-extra-large 28px) */}
      {isModalOpen && (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 backdrop-blur-xs p-4">
          <div className="bg-md-surface-container-high border border-md-outline-variant rounded-md-xl p-6 max-w-md w-full shadow-md-level3 flex flex-col gap-4">
            <div className="flex items-center justify-between">
              <h3 className="text-base font-bold text-md-on-surface flex items-center gap-2">
                <Layers className="w-5 h-5 text-md-primary" />
                {modalMode === 'create' ? '新建灯效方案' : `克隆方案 [${cloneSource}]`}
              </h3>
              <button
                type="button"
                onClick={() => setIsModalOpen(false)}
                className="w-10 h-10 min-w-[40px] min-h-[40px] flex items-center justify-center rounded-md-full text-md-on-surface-variant hover:bg-md-surface-container-highest transition-colors cursor-pointer"
                aria-label="关闭对话框"
              >
                <X className="w-5 h-5" />
              </button>
            </div>

            <div className="flex flex-col gap-1.5">
              <label className="text-xs font-semibold text-md-on-surface" htmlFor="profile-name-input">
                新方案名称 (仅限英文、数字与下划线)
              </label>
              <input
                id="profile-name-input"
                type="text"
                value={inputName}
                onChange={(e) => setInputName(e.target.value)}
                placeholder="例如: cs2_practice 或 night_cyber"
                className="min-h-[48px] px-3.5 bg-md-surface-container border border-md-outline rounded-md-sm text-md-on-surface text-xs font-mono outline-none focus:border-md-primary focus:ring-1 focus:ring-md-primary"
                onKeyDown={(e) => e.key === 'Enter' && handleModalSubmit()}
                autoFocus
              />
            </div>

            <div className="flex items-center justify-end gap-2 pt-2">
              <button
                type="button"
                onClick={() => setIsModalOpen(false)}
                className="min-h-[40px] px-4 text-xs font-semibold text-md-on-surface-variant hover:text-md-on-surface hover:bg-md-surface-container-highest rounded-md-full transition-colors cursor-pointer"
                aria-label="取消操作"
              >
                取消
              </button>
              <button
                type="button"
                onClick={handleModalSubmit}
                className="min-h-[40px] px-5 text-xs font-bold bg-md-primary text-md-on-primary hover:bg-md-primary/90 rounded-md-full shadow-sm transition-colors cursor-pointer"
                aria-label="确认提交"
              >
                {modalMode === 'create' ? '立即创建' : '立即克隆'}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
