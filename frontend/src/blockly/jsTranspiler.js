import { normalizePublication } from './publication.js';
import { compileSequence } from './sequenceCompiler.js';
/**
 * JavaScript Runtime & Transpiler for Real-Time Google Blockly Live Preview
 * Executes within browser animation loop (60 FPS) with sub-millisecond execution budget.
 */

import { KEYBOARD_LAYOUT } from '../constants/keyboardLayout.js';

// Build the standard 68-key topological map for the preview engine
export const PREVIEW_68_KEYS = [];
{
  let ledIdx = 0;
  KEYBOARD_LAYOUT.forEach((row, rowIdx) => {
    let colOffset = 0.5;
    row.forEach((k) => {
      PREVIEW_68_KEYS.push({
        name: k.name,
        label: k.label,
        row: rowIdx + 1,
        col: Math.round(colOffset),
        physical_x: colOffset,
        physical_y: (rowIdx + 1) * 1.0,
        led_id: ledIdx++,
        width: k.width
      });
      colOffset += k.width;
    });
  });
}

export class JsTranspiler {
  /**
   * Compiles the Blockly workspace into an executable JS function
   * @param {import('blockly').WorkspaceSvg} workspace 
   * @returns {(elapsed_ms: number, gsi: any, decays: Record<string, number>) => Array<[number, number, number]>}
   */
  static compile(workspace, options = {}) {
    if (!workspace) {
      return () => PREVIEW_68_KEYS.map(() => [0, 0, 0]);
    }

    const topBlocks = workspace.getTopBlocks(true);
    if (!topBlocks || topBlocks.length === 0) {
      return () => PREVIEW_68_KEYS.map(() => [0, 0, 0]);
    }

    const code = this.generateJsFunctionBody(topBlocks, options);
    const fn = new Function('elapsed_ms', 'keymap', 'gsi', 'decays', 'state', code);
    const state = {};
    return (elapsed_ms, gsi, decays) => fn(elapsed_ms, PREVIEW_68_KEYS, gsi || {}, decays || {}, state);
  }

  static generateJsFunctionBody(topBlocks, options = {}) {
    const publication = normalizePublication(options);
    const oneShot = publication.mode === "one_shot";
    const ws = topBlocks[0]?.workspace || { getTopBlocks: () => topBlocks };
    const program = compileSequence(ws, { language: 'js', oneShot, value: this.valueToJs.bind(this), statement: this.blockToJs.bind(this) });
    const variables = (ws.getAllVariables?.() || []).map(v => `var_${v.name.replace(/[^a-zA-Z0-9_]/g, '_')}`);
    return `
      if (!state.frame ${oneShot ? '' : '|| elapsed_ms < state.last'}) {
        state.frame = keymap.map(() => [0, 0, 0]);
        state.pc = ${program.entry}; state.wake = 0;
        state.indices = new Array(${program.slots}).fill(0);
        state.limits = new Array(${program.slots}).fill(0);
        state.vars = {};
      }
      state.last = elapsed_ms;
      ${oneShot ? `if (state.terminal) {
        const opacity = ${publication.fade_out_ms} ? Math.max(0, 1 - (elapsed_ms - state.terminalAt) / ${publication.fade_out_ms || 1}) : 1;
        return state.frame.map(rgb => rgb.map(channel => Math.floor(channel * opacity)));
      }` : ''}
      const frame = state.frame, info = null;
      let _pc = state.pc, _wake = state.wake;
      const _indices = state.indices, _limits = state.limits;
      ${variables.map(v => `let ${v} = state.vars.${v} ?? 0;`).join('\n')}
      try {
        if (elapsed_ms < _wake) return frame;
        for (let _budget = 0; _budget < 4096; _budget++) {
          switch (_pc) { ${program.code} }
        }
        return frame;
      } finally {
        state.pc = _pc; state.wake = _wake;
        ${variables.map(v => `state.vars.${v} = ${v};`).join('\n')}
      }
    `;
  }

  static blockToJs(block) {
    if (!block) return '';
    const type = block.type;

    switch (type) {
      case 'key_fill_all': {
        const color = this.valueToJs(block, 'COLOR', '[255, 128, 0]');
        const next = this.blockToJs(block.getNextBlock());
        return `(() => {
          const c = ${color};
          for (let i = 0; i < frame.length; i++) {
            frame[i] = [c[0], c[1], c[2]];
          }
        })();\n${next}`;
      }

      case 'key_set_color': {
        const ledId = this.valueToJs(block, 'LED_ID', 'info.led_id');
        const color = this.valueToJs(block, 'COLOR', '[255, 255, 255]');
        const next = this.blockToJs(block.getNextBlock());
        return `(() => {
          const id = Math.max(0, Math.min(frame.length - 1, Math.round(${ledId})));
          const c = ${color};
          frame[id] = [c[0], c[1], c[2]];
        })();\n${next}`;
      }

      case 'key_ripple_effect': {
        const centerKey = block.getFieldValue('KEY') || 'W';
        const color = this.valueToJs(block, 'COLOR', '[0, 200, 255]');
        const speed = parseFloat(block.getFieldValue('SPEED') || '2.5') || 2.5;
        const next = this.blockToJs(block.getNextBlock());
        return `(() => {
          const center = keymap.find(k => k.name === ${JSON.stringify(centerKey)}) || { physical_x: 7.5, physical_y: 3.0 };
          const c = ${color};
          const sp = ${speed};
          for (let i = 0; i < frame.length; i++) {
            const k = keymap[i];
            const dist = Math.hypot(k.physical_x - center.physical_x, k.physical_y - center.physical_y);
            const wave = Math.max(0, 1.0 - Math.abs(((dist - (elapsed_ms / 1000.0) * sp) % 4.0 + 4.0) % 4.0 - 1.0));
            frame[i] = [Math.round(c[0] * wave), Math.round(c[1] * wave), Math.round(c[2] * wave)];
          }
        })();\n${next}`;
      }

      case 'key_for_each': {
        const zone = block.getFieldValue('ZONE') || 'all';
        const doBlock = block.getInputTargetBlock('DO');
        const doCode = doBlock ? this.blockToJs(doBlock) : '';
        const next = this.blockToJs(block.getNextBlock());

        let filter = 'true';
        if (zone === 'wasd') {
          filter = '["W", "A", "S", "D"].includes(info.name)';
        } else if (zone === 'arrows') {
          filter = '["UP", "DOWN", "LEFT", "RIGHT"].includes(info.name)';
        } else if (zone === 'numeric') {
          filter = 'info.row === 1 && info.col >= 2 && info.col <= 13';
        } else if (zone === 'navigation') {
          filter = 'info.col >= 14 || ["UP", "DOWN", "LEFT", "RIGHT"].includes(info.name)';
        } else if (zone === 'modifier') {
          filter = '["LCTRL", "RCTRL", "L_CTRL", "R_CTRL", "LSHIFT", "RSHIFT", "L_SHIFT", "R_SHIFT", "LALT", "RALT", "L_ALT", "R_ALT", "TAB", "CAPS", "ENTER"].includes(info.name)';
        }

        return `for (let kIdx = 0; kIdx < keymap.length; kIdx++) {
          const info = keymap[kIdx];
          if (${filter}) {
            ${doCode}
          }
        }\n${next}`;
      }

      case 'controls_if': {
        let code = '';
        let i = 0;
        while (block.getInput(`IF${i}`)) {
          const cond = this.valueToJs(block, `IF${i}`, 'false');
          const doBlock = block.getInputTargetBlock(`DO${i}`);
          const doCode = doBlock ? this.blockToJs(doBlock) : '';
          if (i === 0) {
            code += `if (${cond}) {\n${doCode}\n}`;
          } else {
            code += ` else if (${cond}) {\n${doCode}\n}`;
          }
          i++;
        }
        const elseBlock = block.getInputTargetBlock('ELSE');
        if (elseBlock) {
          const elseCode = this.blockToJs(elseBlock);
          code += ` else {\n${elseCode}\n}`;
        }
        const next = this.blockToJs(block.getNextBlock());
        return `${code}\n${next}`;
      }

      case 'controls_repeat_ext': {
        const times = this.valueToJs(block, 'TIMES', '10');
        const doBlock = block.getInputTargetBlock('DO');
        const doCode = doBlock ? this.blockToJs(doBlock) : '';
        const next = this.blockToJs(block.getNextBlock());
        return `for (let _rpt = 0; _rpt < Math.min(500, Math.max(0, Math.round(${times}))); _rpt++) {\n${doCode}\n}\n${next}`;
      }

      case 'controls_whileUntil': {
        const mode = block.getFieldValue('MODE') || 'WHILE';
        let cond = this.valueToJs(block, 'BOOL', 'false');
        if (mode === 'UNTIL') cond = `!(${cond})`;
        const doBlock = block.getInputTargetBlock('DO');
        const doCode = doBlock ? this.blockToJs(doBlock) : '';
        const next = this.blockToJs(block.getNextBlock());
        return `(() => {
          let _guard = 0;
          while (${cond}) {
            if (++_guard > 500) break;
            ${doCode}
          }
        })();\n${next}`;
      }

      case 'controls_flow_statements': {
        const flow = block.getFieldValue('FLOW') || 'BREAK';
        return `${flow === 'BREAK' ? 'break;' : 'continue;'}\n`;
      }

      case 'effect_wait_ms': {
        const next = this.blockToJs(block.getNextBlock());
        return next;
      }

      case 'variables_set': {
        const varModel = block.getField('VAR')?.getVariable?.();
        const varName = varModel ? varModel.name : (block.getFieldValue('VAR') || 'x');
        const cleanName = varName.replace(/[^a-zA-Z0-9_]/g, '_');
        const val = this.valueToJs(block, 'VALUE', '0');
        const next = this.blockToJs(block.getNextBlock());
        return `var_${cleanName} = ${val};\n${next}`;
      }

      case 'math_change': {
        const varModel = block.getField('VAR')?.getVariable?.();
        const varName = varModel ? varModel.name : (block.getFieldValue('VAR') || 'x');
        const cleanName = varName.replace(/[^a-zA-Z0-9_]/g, '_');
        const delta = this.valueToJs(block, 'DELTA', '1');
        const next = this.blockToJs(block.getNextBlock());
        return `var_${cleanName} = (var_${cleanName} || 0) + (${delta});\n${next}`;
      }

      default: {
        return this.blockToJs(block.getNextBlock());
      }
    }
  }

  static valueToJs(block, inputName, fallback = '0') {
    const target = block.getInputTargetBlock(inputName);
    if (!target) return fallback;

    switch (target.type) {
      case 'math_number':
        return String(target.getFieldValue('NUM') || 0);

      case 'time_elapsed_ms':
        return 'elapsed_ms';

      case 'time_phase': {
        const period = this.valueToJs(target, 'PERIOD', '2000');
        return `((elapsed_ms % Math.max(33, ${period})) / Math.max(33, ${period}))`;
      }

      case 'math_waveform': {
        const wType = target.getFieldValue('TYPE') || 'SINE';
        const phase = this.valueToJs(target, 'PHASE', '0');
        const amp = this.valueToJs(target, 'AMPLITUDE', '1');
        const offset = this.valueToJs(target, 'OFFSET', '0');

        if (wType === 'SINE') {
          return `(Math.sin((${phase}) * 6.2831853) * (${amp}) + (${offset}))`;
        } else if (wType === 'TRIANGLE') {
          return `((Math.abs(((${phase}) % 1.0) * 2.0 - 1.0) * 2.0 - 1.0) * (${amp}) + (${offset}))`;
        } else if (wType === 'SQUARE') {
          return `((((${phase}) % 1.0) < 0.5 ? 1.0 : -1.0) * (${amp}) + (${offset}))`;
        } else {
          return `((((${phase}) % 1.0) * 2.0 - 1.0) * (${amp}) + (${offset}))`;
        }
      }

      case 'geometry_coords': {
        const field = target.getFieldValue('FIELD') || 'physical_x';
        return `(info ? info.${field} : 0)`;
      }

      case 'geometry_distance': {
        const x1 = this.valueToJs(target, 'X1', '0');
        const y1 = this.valueToJs(target, 'Y1', '0');
        const x2 = this.valueToJs(target, 'X2', '0');
        const y2 = this.valueToJs(target, 'Y2', '0');
        return `Math.sqrt(Math.pow(${x1} - ${x2}, 2) + Math.pow(${y1} - ${y2}, 2))`;
      }

      case 'geometry_radial_phase': {
        const cx = this.valueToJs(target, 'CX', '7.5');
        const cy = this.valueToJs(target, 'CY', '3.0');
        const wl = this.valueToJs(target, 'WAVELENGTH', '4.0');
        const sp = this.valueToJs(target, 'SPEED', '2.0');
        return `((Math.sqrt(Math.pow(info.physical_x - (${cx}), 2) + Math.pow(info.physical_y - (${cy}), 2)) / Math.max(0.1, ${wl}) - (elapsed_ms / 1000.0) * (${sp})) % 1.0 + 1.0) % 1.0`;
      }

      case 'color_rgb': {
        const r = this.valueToJs(target, 'R', '255');
        const g = this.valueToJs(target, 'G', '0');
        const b = this.valueToJs(target, 'B', '0');
        return `[Math.max(0, Math.min(255, Math.round(${r}))), Math.max(0, Math.min(255, Math.round(${g}))), Math.max(0, Math.min(255, Math.round(${b})))]`;
      }

      case 'color_hsv': {
        const h = this.valueToJs(target, 'H', '0');
        const s = this.valueToJs(target, 'S', '100');
        const v = this.valueToJs(target, 'V', '100');
        return `(() => {
          let hNorm = ((${h}) % 360 + 360) % 360;
          let sNorm = Math.max(0, Math.min(1, (${s}) / 100));
          let vNorm = Math.max(0, Math.min(1, (${v}) / 100));
          let c = vNorm * sNorm;
          let x = c * (1 - Math.abs(((hNorm / 60) % 2) - 1));
          let m = vNorm - c;
          let r1 = 0, g1 = 0, b1 = 0;
          if (hNorm < 60) { r1 = c; g1 = x; b1 = 0; }
          else if (hNorm < 120) { r1 = x; g1 = c; b1 = 0; }
          else if (hNorm < 180) { r1 = 0; g1 = c; b1 = x; }
          else if (hNorm < 240) { r1 = 0; g1 = x; b1 = c; }
          else if (hNorm < 300) { r1 = x; g1 = 0; b1 = c; }
          else { r1 = c; g1 = 0; b1 = x; }
          return [Math.round((r1 + m) * 255), Math.round((g1 + m) * 255), Math.round((b1 + m) * 255)];
        })()`;
      }

      case 'color_lerp': {
        const ca = this.valueToJs(target, 'COLOR_A', '[0, 0, 0]');
        const cb = this.valueToJs(target, 'COLOR_B', '[255, 255, 255]');
        const ratio = this.valueToJs(target, 'RATIO', '0.5');
        return `(() => {
          const a = ${ca}, b = ${cb}, t = Math.max(0, Math.min(1, ${ratio}));
          return [
            Math.round(a[0] * (1 - t) + b[0] * t),
            Math.round(a[1] * (1 - t) + b[1] * t),
            Math.round(a[2] * (1 - t) + b[2] * t)
          ];
        })()`;
      }

      case 'color_brightness': {
        const c = this.valueToJs(target, 'COLOR', '[255, 255, 255]');
        const scale = this.valueToJs(target, 'SCALE', '1.0');
        return `(() => {
          const col = ${c}, s = Math.max(0, Math.min(1, ${scale}));
          return [Math.round(col[0] * s), Math.round(col[1] * s), Math.round(col[2] * s)];
        })()`;
      }

      case 'color_cycle': {
        const ca = this.valueToJs(target, 'COLOR_A', '[0, 180, 255]');
        const cb = this.valueToJs(target, 'COLOR_B', '[255, 0, 128]');
        const periodSec = this.valueToJs(target, 'PERIOD_SEC', '2.0');
        return `(() => {
          const pMs = Math.max(100, (${periodSec}) * 1000);
          const phase = (elapsed_ms % pMs) / pMs;
          const ratio = 0.5 - 0.5 * Math.cos(phase * 6.283185307179586);
          const a = ${ca}, b = ${cb};
          return [
            Math.round(a[0] * (1 - ratio) + b[0] * ratio),
            Math.round(a[1] * (1 - ratio) + b[1] * ratio),
            Math.round(a[2] * (1 - ratio) + b[2] * ratio)
          ];
        })()`;
      }

      case 'key_is_pressed':
        return 'Boolean(decays && decays[info ? info.name : ""] > 0.05)';

      case 'key_decay': {
        const rate = this.valueToJs(target, 'DECAY_RATE', '0.85');
        return `((decays && decays[info ? info.name : ""]) || 0.0)`;
      }

      case 'text':
        return JSON.stringify(target.getFieldValue('TEXT') || '');

      case 'logic_boolean':
        return (target.getFieldValue('BOOL') === 'TRUE') ? 'true' : 'false';

      case 'logic_negate':
        return `(!${this.valueToJs(target, 'BOOL', 'false')})`;

      case 'logic_operation': {
        const op = target.getFieldValue('OP') || 'AND';
        const a = this.valueToJs(target, 'A', 'true');
        const b = this.valueToJs(target, 'B', 'true');
        return `(${a} ${op === 'OR' ? '||' : '&&'} ${b})`;
      }

      case 'gsi_state_match': {
        const stateKey = target.getFieldValue('STATE') || 'round.bomb';
        const op = target.getFieldValue('OP') || '==';
        const expectedVal = target.getFieldValue('VALUE') || '';
        return `(() => {
          if (!gsi) return false;
          const parts = "${stateKey}".split(".");
          let curr = gsi;
          for (const p of parts) {
            if (curr && typeof curr === "object" && p in curr) curr = curr[p];
            else return false;
          }
          const actualStr = String(curr ?? "");
          return ${op === '!=' ? `actualStr !== ${JSON.stringify(expectedVal)}` : `actualStr === ${JSON.stringify(expectedVal)}`};
        })()`;
      }

      case 'gsi_numeric_compare': {
        const fieldKey = target.getFieldValue('FIELD') || 'player.state.health';
        const op = target.getFieldValue('OP') || '<';
        const rawVal = target.getFieldValue('VALUE');
        const expectedNum = (rawVal !== null && rawVal !== undefined && !isNaN(Number(rawVal))) ? Number(rawVal) : 0;
        let opSym = '===';
        if (op === '<') opSym = '<';
        else if (op === '<=') opSym = '<=';
        else if (op === '>') opSym = '>';
        else if (op === '>=') opSym = '>=';
        else if (op === '!=') opSym = '!==';
        return `(() => {
          if (!gsi) return false;
          const parts = "${fieldKey}".split(".");
          let curr = gsi;
          for (const p of parts) {
            if (curr && typeof curr === "object" && p in curr) curr = curr[p];
            else return false;
          }
          const n = Number(curr);
          const actualNum = Number.isFinite(n) ? n : 0;
          return actualNum ${opSym} ${expectedNum};
        })()`;
      }

      case 'gsi_player_health_condition': {
        const op = target.getFieldValue('OP') || '<';
        const rawVal = target.getFieldValue('VALUE');
        const expectedNum = (rawVal !== null && rawVal !== undefined && !isNaN(Number(rawVal))) ? Number(rawVal) : 25;
        let opSym = '<';
        if (op === '<=') opSym = '<=';
        else if (op === '>') opSym = '>';
        else if (op === '>=') opSym = '>=';
        else if (op === '==') opSym = '===';
        return `(() => {
          if (!gsi) return false;
          const h = Number(gsi?.player?.state?.health ?? 100);
          return h ${opSym} ${expectedNum};
        })()`;
      }

      case 'gsi_c4_state_condition': {
        const expectedVal = target.getFieldValue('STATE') || 'planted';
        return `(() => {
          if (!gsi) return false;
          const actualStr = String(gsi?.round?.bomb ?? "");
          return actualStr === ${JSON.stringify(expectedVal)};
        })()`;
      }

      case 'gsi_enum_constant': {
        const val = target.getFieldValue('VALUE') || '';
        return JSON.stringify(val);
      }

      case 'gsi_get_number': {
        const path = target.getFieldValue('PATH') || 'player.state.health';
        const def = this.valueToJs(target, 'DEFAULT', '100');
        return `(() => {
          if (!gsi) return ${def};
          const parts = "${path}".split(".");
          let curr = gsi;
          for (const p of parts) {
            if (curr && typeof curr === "object" && p in curr) curr = curr[p];
            else return ${def};
          }
          const n = Number(curr);
          return Number.isFinite(n) ? n : ${def};
        })()`;
      }

      case 'gsi_get_string': {
        const path = target.getFieldValue('PATH') || 'round.bomb';
        const def = this.valueToJs(target, 'DEFAULT', '""');
        return `(() => {
          if (!gsi) return ${def};
          const parts = "${path}".split(".");
          let curr = gsi;
          for (const p of parts) {
            if (curr && typeof curr === "object" && p in curr) curr = curr[p];
            else return ${def};
          }
          return String(curr ?? ${def});
        })()`;
      }

      case 'gsi_get_boolean': {
        const path = target.getFieldValue('PATH') || 'player.state.helmet';
        if (path.startsWith('event.')) throw new Error('事件触发条件已迁移到自动化工作室，请使用自动化事件规则。');
        const def = this.valueToJs(target, 'DEFAULT', 'false');
        return `(() => {
          if (!gsi) return ${def};
          const parts = "${path}".split(".");
          let curr = gsi;
          for (const p of parts) {
            if (curr && typeof curr === "object" && p in curr) curr = curr[p];
            else return ${def};
          }
          return Boolean(curr);
        })()`;
      }

      case 'math_arithmetic': {
        const op = target.getFieldValue('OP') || 'ADD';
        const a = this.valueToJs(target, 'A', '0');
        const b = this.valueToJs(target, 'B', '0');
        let opSym = '+';
        if (op === 'MINUS') opSym = '-';
        else if (op === 'MULTIPLY') opSym = '*';
        else if (op === 'DIVIDE') opSym = '/';
        return `(${a} ${opSym} ${b})`;
      }

      case 'logic_compare': {
        const op = target.getFieldValue('OP') || 'EQ';
        const a = this.valueToJs(target, 'A', '0');
        const b = this.valueToJs(target, 'B', '0');
        let opSym = '===';
        if (op === 'NEQ') opSym = '!==';
        else if (op === 'LT') opSym = '<';
        else if (op === 'LTE') opSym = '<=';
        else if (op === 'GT') opSym = '>';
        else if (op === 'GTE') opSym = '>=';
        return `(${a} ${opSym} ${b})`;
      }

      case 'variables_get': {
        const varModel = target.getField('VAR')?.getVariable?.();
        const varName = varModel ? varModel.name : (target.getFieldValue('VAR') || 'x');
        const cleanName = varName.replace(/[^a-zA-Z0-9_]/g, '_');
        return `var_${cleanName}`;
      }

      case 'math_single': {
        const op = target.getFieldValue('OP') || 'ROOT';
        const num = this.valueToJs(target, 'NUM', '0');
        if (op === 'ROOT') return `Math.sqrt(${num})`;
        if (op === 'ABS') return `Math.abs(${num})`;
        if (op === 'NEG') return `(-(${num}))`;
        if (op === 'LN') return `Math.log(${num})`;
        if (op === 'LOG10') return `Math.log10(${num})`;
        if (op === 'EXP') return `Math.exp(${num})`;
        if (op === 'POW10') return `Math.pow(10, ${num})`;
        return num;
      }

      case 'math_trig': {
        const op = target.getFieldValue('OP') || 'SIN';
        const num = this.valueToJs(target, 'NUM', '0');
        if (op === 'SIN') return `Math.sin(${num})`;
        if (op === 'COS') return `Math.cos(${num})`;
        if (op === 'TAN') return `Math.tan(${num})`;
        if (op === 'ASIN') return `Math.asin(${num})`;
        if (op === 'ACOS') return `Math.acos(${num})`;
        if (op === 'ATAN') return `Math.atan(${num})`;
        return num;
      }

      case 'math_round': {
        const op = target.getFieldValue('OP') || 'ROUND';
        const num = this.valueToJs(target, 'NUM', '0');
        if (op === 'ROUND') return `Math.round(${num})`;
        if (op === 'ROUNDUP') return `Math.ceil(${num})`;
        if (op === 'ROUNDDOWN') return `Math.floor(${num})`;
        return num;
      }

      case 'math_modulo': {
        const a = this.valueToJs(target, 'DIVIDEND', '0');
        const b = this.valueToJs(target, 'DIVISOR', '1');
        return `(${a} % ${b})`;
      }

      default:
        return fallback;
    }
  }
}
