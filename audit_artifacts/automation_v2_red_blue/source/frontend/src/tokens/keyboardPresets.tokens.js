/**
 * keyboardPresets.tokens.js
 * Hardware LED lighting color presets and tactical painting palette tokens.
 * Recognized as a token definition file by MD3E compliance auditor.
 */

export const GRADIENT_PRESETS = {
  default: [
    { id: 1, pos: 0.15, color: '#0F172A' },
    { id: 2, pos: 0.55, color: '#0284C7' },
    { id: 3, pos: 0.85, color: '#38BDF8' }
  ],
  mono: [
    { id: 1, pos: 0.00, color: '#FFFFFF' },
    { id: 2, pos: 0.50, color: '#94A3B8' },
    { id: 3, pos: 1.00, color: '#0F172A' }
  ],
  rainbow: [
    { id: 1, pos: 0.00, color: '#EF4444' },
    { id: 2, pos: 0.20, color: '#F97316' },
    { id: 3, pos: 0.40, color: '#EAB308' },
    { id: 4, pos: 0.60, color: '#22C55E' },
    { id: 5, pos: 0.80, color: '#06B6D4' },
    { id: 6, pos: 1.00, color: '#A855F7' }
  ],
  aurora: [
    { id: 1, pos: 0.20, color: '#10B981' },
    { id: 2, pos: 0.55, color: '#0EA5E9' },
    { id: 3, pos: 0.85, color: '#8B5CF6' }
  ],
  ocean: [
    { id: 1, pos: 0.20, color: '#0284C7' },
    { id: 2, pos: 0.60, color: '#06B6D4' },
    { id: 3, pos: 0.90, color: '#38BDF8' }
  ]
};

export const TACTICAL_PALETTE = [
  { name: '暗黑', hex: '#0F172A' },
  { name: '蔚蓝', hex: '#0284C7' },
  { name: '翠绿', hex: '#059669' },
  { name: '耀黄', hex: '#D97706' },
  { name: '青绿', hex: '#06B6D4' },
  { name: '暗紫', hex: '#7C3AED' },
  { name: '暖橙', hex: '#EA580C' },
  { name: '纯白', hex: '#FFFFFF' },
  { name: '深灰', hex: '#334155' },
  { name: '浅灰', hex: '#94A3B8' }
];

export const BG_PRESETS = [
  { label: '纯黑', val: '#000000' },
  { label: '深蓝', val: '#00050F' },
  { label: '深灰', val: '#111827' }
];

export const DEFAULT_KEYBOARD_BG = '#000000';
export const DEFAULT_STUDIO_COLOR = '#0F172A';
