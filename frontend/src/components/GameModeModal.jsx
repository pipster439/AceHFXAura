import React from 'react';
import { Gamepad2, Radio } from 'lucide-react';
import { motion } from 'framer-motion';

export default function GameModeModal({ isOpen }) {
  if (!isOpen) return null;

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-slate-900/50 backdrop-blur-2xs p-4">
      <motion.div
        initial={{ opacity: 0, scale: 0.95 }}
        animate={{ opacity: 1, scale: 1 }}
        exit={{ opacity: 0, scale: 0.95 }}
        className="bg-white border border-slate-300 rounded-none p-8 max-w-md w-full shadow-2xl flex flex-col items-center text-center gap-4"
      >
        <div className="w-14 h-14 rounded-none bg-slate-100 text-slate-900 border border-slate-300 flex items-center justify-center">
          <Gamepad2 className="w-7 h-7" />
        </div>

        <div className="flex flex-col gap-1.5">
          <h3 className="text-base font-bold text-slate-900">竞技游戏 / 性能免打扰模式</h3>
          <p className="text-xs text-slate-500 leading-relaxed">
            检测到前台正在运行全屏竞技游戏。为保障键盘硬件极速响应与系统零占用，网页配置服务已自动暂停。
          </p>
        </div>

        <div className="flex items-center gap-2 px-4 py-2 rounded-none bg-slate-100 border border-slate-300 text-slate-700 text-xs font-bold mt-2">
          <Radio className="w-4 h-4 text-emerald-600 animate-pulse" />
          <span>切回桌面后界面将自动恢复...</span>
        </div>
      </motion.div>
    </div>
  );
}
