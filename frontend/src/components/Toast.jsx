import React from 'react';
import { motion, AnimatePresence } from 'framer-motion';
import { CheckCircle2, AlertCircle, Info } from 'lucide-react';

export default function Toast({ toast }) {
  if (!toast) return null;

  const Icon = toast.type === 'error' ? AlertCircle : toast.type === 'info' ? Info : CheckCircle2;
  const iconColor = toast.type === 'error' ? 'text-slate-900' : toast.type === 'info' ? 'text-slate-700' : 'text-emerald-600';

  return (
    <AnimatePresence>
      <motion.div
        initial={{ opacity: 0, y: 16 }}
        animate={{ opacity: 1, y: 0 }}
        exit={{ opacity: 0, y: 16 }}
        transition={{ duration: 0.15 }}
        className="fixed bottom-6 right-6 z-50 flex items-center gap-3 px-4 py-3 bg-white border border-slate-300 rounded-none shadow-lg text-slate-900"
      >
        <Icon className={`w-4 h-4 shrink-0 ${iconColor}`} />
        <span className="text-xs font-bold">{toast.message}</span>
      </motion.div>
    </AnimatePresence>
  );
}
