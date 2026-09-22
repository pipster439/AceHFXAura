import React, { useState } from 'react';
import { 
  Sparkles, 
  Plus, 
  Trash2, 
  Edit3, 
  Copy, 
  Check, 
  X, 
  Search, 
  Download, 
  Upload, 
  Clock,
  Layers,
  Play
} from 'lucide-react';
import { EFFECT_PRESETS } from '../blockly/presets';

export default function EffectLibraryModal({
  isOpen,
  onClose,
  effects = {},
  currentEffectName,
  onSelectEffect,
  onCreateEffect,
  onCloneEffect,
  onRenameEffect,
  onDeleteEffect,
  onExportEffect,
  onImportEffect
}) {
  const [searchTerm, setSearchTerm] = useState('');
  const [isCreating, setIsCreating] = useState(false);
  const [newEffectName, setNewEffectName] = useState('');
  const [selectedTemplate, setSelectedTemplate] = useState('blank');
  const [editingName, setEditingName] = useState(null);
  const [renameValue, setRenameValue] = useState('');
  const [deleteConfirm, setDeleteConfirm] = useState(null);

  if (!isOpen) return null;

  // 聚合所有工坊光效条目
  const effectEntries = Object.entries(effects || {}).map(([name, data]) => ({
    name,
    title: data?.title || name,
    description: data?.description || '自定义 Scratch 积木光效',
    updatedAt: data?.updated_at || Date.now(),
    blockCount: data?.blockly_json?.blocks?.blocks?.length || data?.blockly_json?.blocks?.length || 0,
    isCurrent: name === currentEffectName,
    data
  }));

  const filteredEffects = effectEntries.filter(e => 
    e.name.toLowerCase().includes(searchTerm.toLowerCase()) ||
    e.title.toLowerCase().includes(searchTerm.toLowerCase())
  );

  const handleStartCreate = () => {
    setIsCreating(true);
    setNewEffectName('');
    setSelectedTemplate('blank');
  };

  const handleConfirmCreate = () => {
    const clean = newEffectName.trim().toLowerCase().replace(/[^a-z0-9_]/g, '_');
    if (!clean) return;
    onCreateEffect?.(clean, selectedTemplate);
    setIsCreating(false);
    setNewEffectName('');
  };

  const handleStartRename = (name) => {
    setEditingName(name);
    setRenameValue(name);
  };

  const handleConfirmRename = (oldName) => {
    const clean = renameValue.trim().toLowerCase().replace(/[^a-z0-9_]/g, '_');
    if (clean && clean !== oldName) {
      onRenameEffect?.(oldName, clean);
    }
    setEditingName(null);
  };

  const handleImportFile = (e) => {
    const file = e.target.files?.[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = (event) => {
      try {
        const parsed = JSON.parse(event.target.result);
        onImportEffect?.(parsed);
      } catch (err) {
        console.error('Import failed:', err);
      }
    };
    reader.readAsText(file);
    e.target.value = '';
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 backdrop-blur-sm p-4 animate-in fade-in duration-200">
      <div 
        className="relative w-full max-w-4xl max-h-[85vh] bg-md-surface-container-low border border-md-outline-variant rounded-md-xl shadow-md-level3 flex flex-col overflow-hidden text-md-on-surface"
        onClick={(e) => e.stopPropagation()}
      >
        {/* 顶部标题栏 */}
        <div className="flex items-center justify-between px-6 py-4 border-b border-md-outline-variant bg-md-surface-container">
          <div className="flex items-center gap-3">
            <div className="flex items-center justify-center w-10 h-10 rounded-md-full bg-md-primary/10 text-md-primary">
              <Sparkles className="w-5 h-5" />
            </div>
            <div>
              <h2 className="text-base font-bold text-md-on-surface flex items-center gap-2">
                光效管理中心
                <span className="text-xs font-normal text-md-on-surface-variant">
                  (共 {effectEntries.length} 款光效)
                </span>
              </h2>
              <p className="text-xs text-md-on-surface-variant">
                独立设计、管理与导出 Scratch 风格自定义键盘光效，保存后即时在方案编排中联动
              </p>
            </div>
          </div>
          <button
            onClick={onClose}
            className="w-8 h-8 flex items-center justify-center rounded-md-full hover:bg-md-surface-container-highest transition-colors text-md-on-surface-variant cursor-pointer"
          >
            <X className="w-5 h-5" />
          </button>
        </div>

        {/* 搜索与工具栏 */}
        <div className="flex flex-wrap items-center justify-between gap-3 px-6 py-3 border-b border-md-outline-variant/60 bg-md-surface-container-lowest">
          <div className="relative flex-1 max-w-sm">
            <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-md-on-surface-variant/60" />
            <input
              type="text"
              value={searchTerm}
              onChange={(e) => setSearchTerm(e.target.value)}
              placeholder="搜索光效名称或标识..."
              className="w-full h-9 pl-9 pr-4 bg-md-surface-container border border-md-outline-variant rounded-md-full text-xs text-md-on-surface outline-none focus:border-md-primary transition-colors"
            />
          </div>

          <div className="flex items-center gap-2">
            <label className="h-9 px-3.5 flex items-center gap-1.5 rounded-md-full bg-md-surface-container border border-md-outline-variant text-xs font-medium text-md-on-surface hover:bg-md-surface-container-high transition-all cursor-pointer">
              <Upload className="w-3.5 h-3.5" />
              <span>导入 JSON</span>
              <input type="file" accept=".json" onChange={handleImportFile} className="hidden" />
            </label>

            <button
              onClick={handleStartCreate}
              className="h-9 px-4 flex items-center gap-1.5 rounded-md-full bg-md-primary text-md-on-primary text-xs font-bold hover:bg-md-primary/90 active:scale-95 transition-all shadow-md-level1 cursor-pointer"
            >
              <Plus className="w-4 h-4" />
              <span>新建光效</span>
            </button>
          </div>
        </div>

        {/* 新建光效内嵌卡片 */}
        {isCreating && (
          <div className="mx-6 my-3 p-4 bg-md-primary-container/20 border border-md-primary/40 rounded-md-lg flex flex-col gap-3 animate-in slide-in-from-top duration-200">
            <div className="flex items-center justify-between">
              <span className="text-xs font-bold text-md-primary flex items-center gap-1.5">
                <Sparkles className="w-4 h-4" /> 创建新自定义光效
              </span>
              <button onClick={() => setIsCreating(false)} className="text-md-on-surface-variant hover:text-md-on-surface">
                <X className="w-4 h-4" />
              </button>
            </div>

            <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
              <div>
                <label className="text-[11px] font-medium text-md-on-surface-variant block mb-1">
                  光效标识 (英文标识，如 custom_fire, matrix_wave):
                </label>
                <input
                  type="text"
                  value={newEffectName}
                  onChange={(e) => setNewEffectName(e.target.value.toLowerCase().replace(/[^a-z0-9_]/g, ''))}
                  placeholder="my_custom_effect"
                  className="w-full h-8 px-3 bg-md-surface-container border border-md-outline rounded-md-sm text-xs font-mono font-bold text-md-primary outline-none focus:border-md-primary"
                  autoFocus
                />
              </div>

              <div>
                <label className="text-[11px] font-medium text-md-on-surface-variant block mb-1">
                  初始化起点:
                </label>
                <select
                  value={selectedTemplate}
                  onChange={(e) => setSelectedTemplate(e.target.value)}
                  className="w-full h-8 px-3 bg-md-surface-container border border-md-outline rounded-md-sm text-xs text-md-on-surface outline-none focus:border-md-primary"
                >
                  <option value="blank">空白画布 (自由拼装)</option>
                  {EFFECT_PRESETS.map((p) => (
                    <option key={p.id} value={p.id}>模板: {p.name}</option>
                  ))}
                </select>
              </div>
            </div>

            <div className="flex justify-end gap-2 mt-1">
              <button
                onClick={() => setIsCreating(false)}
                className="h-7 px-3 text-xs text-md-on-surface-variant hover:bg-md-surface-container rounded-md-full transition-colors cursor-pointer"
              >
                取消
              </button>
              <button
                onClick={handleConfirmCreate}
                disabled={!newEffectName.trim()}
                className="h-7 px-4 bg-md-primary text-md-on-primary text-xs font-bold rounded-md-full hover:bg-md-primary/90 disabled:opacity-50 transition-all cursor-pointer"
              >
                确认创建
              </button>
            </div>
          </div>
        )}

        {/* 光效卡片网格列表 */}
        <div className="flex-1 overflow-y-auto p-6 grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
          {filteredEffects.length === 0 ? (
            <div className="col-span-full py-16 flex flex-col items-center justify-center text-center text-md-on-surface-variant">
              <div className="w-16 h-16 rounded-md-full bg-md-surface-container-highest flex items-center justify-center mb-3">
                <Layers className="w-8 h-8 opacity-40" />
              </div>
              <p className="text-sm font-semibold">暂无匹配的自定义光效</p>
              <p className="text-xs text-md-on-surface-variant/80 mt-1">
                点击右上角「新建光效」开始打造你的第一款 Scratch 积木键盘光效
              </p>
            </div>
          ) : (
            filteredEffects.map((item) => (
              <div
                key={item.name}
                className={`relative flex flex-col justify-between p-4 rounded-md-lg border transition-all ${
                  item.isCurrent
                    ? 'bg-md-primary-container/15 border-md-primary shadow-md-level1'
                    : 'bg-md-surface-container border-md-outline-variant hover:border-md-primary/50 hover:shadow-md-level1'
                }`}
              >
                {/* 卡片头部 */}
                <div>
                  <div className="flex items-start justify-between gap-2 mb-1.5">
                    {editingName === item.name ? (
                      <div className="flex items-center gap-1 w-full">
                        <input
                          type="text"
                          value={renameValue}
                          onChange={(e) => setRenameValue(e.target.value.toLowerCase().replace(/[^a-z0-9_]/g, ''))}
                          className="flex-1 h-7 px-2 bg-md-surface-container-highest border border-md-primary rounded text-xs font-mono font-bold text-md-primary outline-none"
                          autoFocus
                          onKeyDown={(e) => {
                            if (e.key === 'Enter') handleConfirmRename(item.name);
                            if (e.key === 'Escape') setEditingName(null);
                          }}
                        />
                        <button
                          onClick={() => handleConfirmRename(item.name)}
                          className="p-1 hover:text-md-primary cursor-pointer"
                        >
                          <Check className="w-3.5 h-3.5 text-md-primary" />
                        </button>
                      </div>
                    ) : (
                      <div className="flex items-center gap-1.5 min-w-0">
                        <h3 className="text-xs font-bold font-mono text-md-on-surface truncate">
                          {item.name}
                        </h3>
                        {item.isCurrent && (
                          <span className="shrink-0 px-2 py-0.5 text-[10px] font-bold bg-md-primary text-md-on-primary rounded-md-full">
                            编辑中
                          </span>
                        )}
                      </div>
                    )}

                    <div className="flex items-center gap-1 shrink-0">
                      <button
                        onClick={() => handleStartRename(item.name)}
                        title="重命名标识"
                        className="p-1 text-md-on-surface-variant hover:text-md-on-surface hover:bg-md-surface-container-high rounded cursor-pointer transition-colors"
                      >
                        <Edit3 className="w-3.5 h-3.5" />
                      </button>
                      <button
                        onClick={() => onCloneEffect?.(item.name)}
                        title="克隆副本"
                        className="p-1 text-md-on-surface-variant hover:text-md-on-surface hover:bg-md-surface-container-high rounded cursor-pointer transition-colors"
                      >
                        <Copy className="w-3.5 h-3.5" />
                      </button>
                      <button
                        onClick={() => onExportEffect?.(item.name)}
                        title="导出 JSON"
                        className="p-1 text-md-on-surface-variant hover:text-md-on-surface hover:bg-md-surface-container-high rounded cursor-pointer transition-colors"
                      >
                        <Download className="w-3.5 h-3.5" />
                      </button>
                      <button
                        onClick={() => setDeleteConfirm(item.name)}
                        title="删除光效"
                        className="p-1 text-md-on-surface-variant hover:text-md-error hover:bg-md-error-container/30 rounded cursor-pointer transition-colors"
                      >
                        <Trash2 className="w-3.5 h-3.5" />
                      </button>
                    </div>
                  </div>

                  <p className="text-[11px] text-md-on-surface-variant line-clamp-2 mb-3">
                    {item.description}
                  </p>
                </div>

                {/* 卡片底部信息与主动作 */}
                <div className="pt-3 border-t border-md-outline-variant/40 flex items-center justify-between text-[11px] text-md-on-surface-variant">
                  <div className="flex items-center gap-1 text-[10px]">
                    <Clock className="w-3 h-3 opacity-60" />
                    <span>{new Date(item.updatedAt).toLocaleDateString()}</span>
                  </div>

                  {deleteConfirm === item.name ? (
                    <div className="flex items-center gap-1.5 animate-in fade-in duration-150">
                      <span className="text-[10px] text-md-error font-bold">确定删除?</span>
                      <button
                        onClick={() => {
                          onDeleteEffect?.(item.name);
                          setDeleteConfirm(null);
                        }}
                        className="px-2 py-0.5 bg-md-error text-md-on-error text-[10px] font-bold rounded hover:bg-md-error/90 cursor-pointer"
                      >
                        是
                      </button>
                      <button
                        onClick={() => setDeleteConfirm(null)}
                        className="px-2 py-0.5 bg-md-surface-container-highest text-md-on-surface text-[10px] rounded hover:bg-md-outline-variant cursor-pointer"
                      >
                        否
                      </button>
                    </div>
                  ) : (
                    <button
                      onClick={() => {
                        onSelectEffect?.(item.name, item.data?.blockly_json);
                        onClose();
                      }}
                      className={`h-7 px-3 flex items-center gap-1 rounded-md-full text-xs font-bold transition-all cursor-pointer ${
                        item.isCurrent
                          ? 'bg-md-primary/15 text-md-primary hover:bg-md-primary/25'
                          : 'bg-md-surface-container-highest text-md-on-surface hover:bg-md-primary hover:text-md-on-primary'
                      }`}
                    >
                      <Play className="w-3 h-3 fill-current" />
                      <span>{item.isCurrent ? '当前画布' : '载入编辑'}</span>
                    </button>
                  )}
                </div>
              </div>
            ))
          )}
        </div>

        {/* 底部关闭栏 */}
        <div className="flex items-center justify-between px-6 py-3 border-t border-md-outline-variant bg-md-surface-container">
          <span className="text-xs text-md-on-surface-variant">
            提示: 所有光效自动保存至 <code className="font-mono text-md-primary">config.json</code>，随时可在「方案编排」工作台中调度。
          </span>
          <button
            onClick={onClose}
            className="h-8 px-4 bg-md-surface-container-highest hover:bg-md-outline-variant text-md-on-surface text-xs font-semibold rounded-md-full transition-colors cursor-pointer"
          >
            关闭窗口
          </button>
        </div>
      </div>
    </div>
  );
}
