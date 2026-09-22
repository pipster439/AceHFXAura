// 68 键 ANSI 65% 物理矩阵布局
export const KEYBOARD_LAYOUT = [
  // Row 1 (15 keys, 16u)
  [
    { name: 'ESC', label: 'Esc', width: 1 },
    { name: '1', label: '1', sub: '!', width: 1 },
    { name: '2', label: '2', sub: '@', width: 1 },
    { name: '3', label: '3', sub: '#', width: 1 },
    { name: '4', label: '4', sub: '$', width: 1 },
    { name: '5', label: '5', sub: '%', width: 1 },
    { name: '6', label: '6', sub: '^', width: 1 },
    { name: '7', label: '7', sub: '&', width: 1 },
    { name: '8', label: '8', sub: '*', width: 1 },
    { name: '9', label: '9', sub: '(', width: 1 },
    { name: '0', label: '0', sub: ')', width: 1 },
    { name: '-', label: '-', sub: '_', width: 1 },
    { name: '=', label: '=', sub: '+', width: 1 },
    { name: 'BACKSPACE', label: 'Backspace', width: 2 },
    { name: 'INS', label: 'Ins', width: 1 }
  ],
  // Row 2 (15 keys, 16u)
  [
    { name: 'TAB', label: 'Tab', width: 1.5 },
    { name: 'Q', label: 'Q', width: 1 },
    { name: 'W', label: 'W', width: 1 },
    { name: 'E', label: 'E', width: 1 },
    { name: 'R', label: 'R', width: 1 },
    { name: 'T', label: 'T', width: 1 },
    { name: 'Y', label: 'Y', width: 1 },
    { name: 'U', label: 'U', width: 1 },
    { name: 'I', label: 'I', width: 1 },
    { name: 'O', label: 'O', width: 1 },
    { name: 'P', label: 'P', width: 1 },
    { name: '[', label: '[', sub: '{', width: 1 },
    { name: ']', label: ']', sub: '}', width: 1 },
    { name: '\\', label: '\\', sub: '|', width: 1.5 },
    { name: 'DEL', label: 'Del', width: 1 }
  ],
  // Row 3 (14 keys, 16u)
  [
    { name: 'CAPS', label: 'Caps', width: 1.75 },
    { name: 'A', label: 'A', width: 1 },
    { name: 'S', label: 'S', width: 1 },
    { name: 'D', label: 'D', width: 1 },
    { name: 'F', label: 'F', width: 1 },
    { name: 'G', label: 'G', width: 1 },
    { name: 'H', label: 'H', width: 1 },
    { name: 'J', label: 'J', width: 1 },
    { name: 'K', label: 'K', width: 1 },
    { name: 'L', label: 'L', width: 1 },
    { name: ';', label: ';', sub: ':', width: 1 },
    { name: '\'', label: '\'', sub: '"', width: 1 },
    { name: 'ENTER', label: 'Enter', width: 2.25 },
    { name: 'PGUP', label: 'PgUp', width: 1 }
  ],
  // Row 4 (14 keys, 16u)
  [
    { name: 'L_SHIFT', label: 'Shift', width: 2.25 },
    { name: 'Z', label: 'Z', width: 1 },
    { name: 'X', label: 'X', width: 1 },
    { name: 'C', label: 'C', width: 1 },
    { name: 'V', label: 'V', width: 1 },
    { name: 'B', label: 'B', width: 1 },
    { name: 'N', label: 'N', width: 1 },
    { name: 'M', label: 'M', width: 1 },
    { name: ',', label: ',', sub: '<', width: 1 },
    { name: '.', label: '.', sub: '>', width: 1 },
    { name: '/', label: '/', sub: '?', width: 1 },
    { name: 'R_SHIFT', label: 'Shift', width: 1.75 },
    { name: 'UP', label: '▲', width: 1 },
    { name: 'PGDN', label: 'PgDn', width: 1 }
  ],
  // Row 5 (10 keys, 16u)
  [
    { name: 'L_CTRL', label: 'Ctrl', width: 1.25 },
    { name: 'L_WIN', label: 'Win', width: 1.25 },
    { name: 'L_ALT', label: 'Alt', width: 1.25 },
    { name: 'SPACE', label: 'Space', width: 6.25 },
    { name: 'R_ALT', label: 'Alt', width: 1 },
    { name: 'FN', label: 'Fn', width: 1 },
    { name: 'COPILOT', label: 'Co', width: 1 },
    { name: 'LEFT', label: '◀', width: 1 },
    { name: 'DOWN', label: '▼', width: 1 },
    { name: 'RIGHT', label: '▶', width: 1 }
  ]
];

export const CODE_TO_KEY_MAP = {
  Escape: 'ESC', Digit1: '1', Digit2: '2', Digit3: '3', Digit4: '4', Digit5: '5',
  Digit6: '6', Digit7: '7', Digit8: '8', Digit9: '9', Digit0: '0',
  Minus: '-', Equal: '=', Backspace: 'BACKSPACE', Insert: 'INS',
  Tab: 'TAB', KeyQ: 'Q', KeyW: 'W', KeyE: 'E', KeyR: 'R', KeyT: 'T',
  KeyY: 'Y', KeyU: 'U', KeyI: 'I', KeyO: 'O', KeyP: 'P',
  BracketLeft: '[', BracketRight: ']', Backslash: '\\', Delete: 'DEL',
  CapsLock: 'CAPS', KeyA: 'A', KeyS: 'S', KeyD: 'D', KeyF: 'F', KeyG: 'G',
  KeyH: 'H', KeyJ: 'J', KeyK: 'K', KeyL: 'L', Semicolon: ';', Quote: '\'',
  Enter: 'ENTER', PageUp: 'PGUP', ShiftLeft: 'L_SHIFT',
  KeyZ: 'Z', KeyX: 'X', KeyC: 'C', KeyV: 'V', KeyB: 'B', KeyN: 'N',
  KeyM: 'M', Comma: ',', Period: '.', Slash: '/', ShiftRight: 'R_SHIFT',
  ArrowUp: 'UP', PageDown: 'PGDN', ControlLeft: 'L_CTRL', MetaLeft: 'L_WIN',
  AltLeft: 'L_ALT', Space: 'SPACE', AltRight: 'R_ALT', ControlRight: 'R_CTRL',
  ArrowLeft: 'LEFT', ArrowDown: 'DOWN', ArrowRight: 'RIGHT',
  ContextMenu: 'COPILOT', F23: 'COPILOT', App: 'COPILOT'
};

export const DIR_VECTORS = {
  up: { x: 0.0, y: -1.0 },
  down: { x: 0.0, y: 1.0 },
  left: { x: -1.0, y: 0.0 },
  right: { x: 1.0, y: 0.0 },
  diag_ur: { x: 0.707, y: -0.707 },
  diag_dl: { x: -0.707, y: 0.707 },
  diag_ul: { x: -0.707, y: -0.707 },
  diag_dr: { x: 0.707, y: 0.707 },
  spread: { x: 0.0, y: 0.0 }
};

export { GRADIENT_PRESETS } from '../tokens/keyboardPresets.tokens.js';
