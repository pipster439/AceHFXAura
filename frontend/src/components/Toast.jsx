import React from 'react';
import { motion, AnimatePresence } from 'framer-motion';
import { CheckCircle2, AlertCircle, Info } from 'lucide-react';

export default function Toast({ toast, onClose }) {
  if (!toast) return null;

  const Icon = toast.type === 'error' ? AlertCircle : toast.type === 'info' ? Info : CheckCircle2;
  const iconClass = toast.type === 'error' ? 'text-md-error' : toast.type === 'info' ? 'text-md-primary' : 'text-md-primary';

  return (
    <AnimatePresence>
      <motion.div
        initial={{ opacity: 0, y: 20, scale: 0.95 }}
        animate={{ opacity: 1, y: 0, scale: 1 }}
        exit={{ opacity: 0, y: 20, scale: 0.95 }}
        transition={{ type: 'spring', stiffness: 380, damping: 25 }}
        className="fixed bottom-6 right-6 z-50 flex items-center gap-3 px-4 py-3 bg-md-inverse-surface text-md-inverse-on-surface border border-md-outline-variant/30 rounded-md-sm shadow-md-level3 max-w-sm"
        role="status"
        aria-live="polite"
      >
        <Icon className={`w-5 h-5 shrink-0 ${iconClass}`} />
        <span className="text-xs font-medium leading-tight">{toast.message}</span>
      </motion.div>
    </AnimatePresence>
  );
}
