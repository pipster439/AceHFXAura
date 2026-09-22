import React from 'react';
import { Gamepad2, Radio } from 'lucide-react';
import { motion, AnimatePresence } from 'framer-motion';

export default function GameModeModal({ isOpen }) {
  if (!isOpen) return null;

  return (
    <AnimatePresence>
      <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 backdrop-blur-xs p-4">
        <motion.div
          initial={{ opacity: 0, scale: 0.92, y: 16 }}
          animate={{ opacity: 1, scale: 1, y: 0 }}
          exit={{ opacity: 0, scale: 0.92, y: 16 }}
          transition={{ type: 'spring', stiffness: 380, damping: 25 }}
          className="bg-md-surface-container-high border border-md-outline-variant rounded-md-xl p-8 max-w-md w-full shadow-md-level3 flex flex-col items-center text-center gap-5"
          role="dialog"
          aria-modal="true"
          aria-labelledby="game-mode-modal-title"
        >
          <div className="w-16 h-16 rounded-md-lg bg-md-primary-container text-md-on-primary-container flex items-center justify-center shadow-md-level1">
            <Gamepad2 className="w-8 h-8" />
          </div>

          <div className="flex flex-col gap-2">
            <h3 id="game-mode-modal-title" className="text-base font-bold text-md-on-surface">
              竞技游戏 / 硬件免打扰模式
            </h3>
            <p className="text-xs text-md-on-surface-variant leading-relaxed">
              检测到前台正在运行全屏竞技游戏（如 CS2）。为保障键盘硬件极速响应（10ms / 100 FPS）与系统微秒级零占用，网页配置服务已自动暂停。
            </p>
          </div>

          <div className="flex items-center gap-2.5 px-4 py-2 rounded-md-full bg-md-surface-container-highest border border-md-outline-variant text-md-on-surface text-xs font-semibold mt-1">
            <Radio className="w-4 h-4 text-md-primary animate-pulse" />
            <span>切回桌面后界面将自动恢复...</span>
          </div>
        </motion.div>
      </div>
    </AnimatePresence>
  );
}
