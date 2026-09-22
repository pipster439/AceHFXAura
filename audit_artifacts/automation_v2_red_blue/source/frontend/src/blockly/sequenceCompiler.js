import { KEYBOARD_LAYOUT } from '../constants/keyboardLayout.js';
// Both preview and native code use the same resumable control-flow graph.
// A wait yields a frame; reaching the end starts the script again next frame.
export function compileSequence(workspace, target) {
  const nodes = [];
  let slots = 0;
  const emit = (node) => (nodes.push(node), nodes.length - 1);
  const end = emit({ kind: 'end' });
  const chain = (block, next, loop = null, keySlot = null) => {
    if (!block) return next;
    const after = chain(block.getNextBlock(), next, loop, keySlot);
    if (block.isEnabled?.() === false) return after;
    const type = block.type;
    const node = (data) => emit({ ...data, block, keySlot });
    if (type === 'controls_if') {
      let branch = chain(block.getInputTargetBlock('ELSE'), after, loop, keySlot);
      let count = 0;
      while (block.getInput(`IF${count}`)) count++;
      while (count--) {
        branch = node({ kind: 'if', input: `IF${count}`, yes: chain(block.getInputTargetBlock(`DO${count}`), after, loop, keySlot), no: branch });
      }
      return branch;
    }
    if (type === 'controls_repeat_ext' || type === 'key_for_each' || type === 'controls_whileUntil') {
      const slot = slots++;
      const test = node({ kind: 'loopTest', slot, after });
      const increment = node({ kind: 'increment', slot, next: test });
      const body = chain(block.getInputTargetBlock('DO'), increment, { break: after, continue: increment }, type === 'key_for_each' ? slot : keySlot);
      nodes[test].body = body;
      return node({ kind: 'loopInit', slot, next: test });
    }
    if (type === 'controls_flow_statements') {
      if (!loop) throw new Error('“跳出 / 继续循环”必须放在循环内');
      return node({ kind: 'jump', next: loop[block.getFieldValue('FLOW') === 'CONTINUE' ? 'continue' : 'break'] });
    }
    if (type === 'effect_wait_ms') return node({ kind: 'wait', next: after });
    if (!['key_fill_all', 'key_set_color', 'variables_set', 'math_change', 'key_ripple_effect'].includes(type)) {
      throw new Error(`此积木不能独立执行：${type}`);
    }
    return node({ kind: 'action', next: after });
  };
  let entry = end;
  const tops = workspace?.getTopBlocks(true) || [];
  for (const block of [...tops].reverse()) entry = chain(block, entry);
  const cpp = target.language === 'cpp';
  const expr = (n, input, fallback) => target.value(n.block, input, fallback);
  const go = (id) => `_pc = ${id}; break;`;
  const code = nodes.map((n, id) => {
    let body;
    const idx = `_indices[${n.slot}]`, limit = `_limits[${n.slot}]`;
    switch (n.kind) {
      case 'end': body = target.oneShot
        ? `${cpp ? '_terminal = true; _terminal_at = elapsed_ms; out_frame = _frame; return;' : 'state.terminal = true; state.terminalAt = elapsed_ms; return frame;'}`
        : `_pc = ${entry}; ${cpp ? 'out_frame = _frame; return;' : 'return frame;'} `; break;
      case 'jump': body = go(n.next); break;
      case 'if': body = `_pc = (${expr(n, n.input, 'false')}) ? ${n.yes} : ${n.no}; break;`; break;
      case 'wait': body = `_wake = elapsed_ms + ${cpp ? 'std::clamp<double>' : 'Math.min'}(${cpp ? expr(n, 'MS', '0') + ', 0.0, 60000.0' : '60000, Math.max(0, ' + expr(n, 'MS', '0') + ')'}); _pc = ${n.next}; ${cpp ? 'out_frame = _frame; return;' : 'return frame;'}`; break;
      case 'loopInit': {
        const times = expr(n, 'TIMES', '10');
        body = `${idx} = 0; ${limit} = ${n.block.type === 'key_for_each' ? '68' : cpp ? `std::clamp<double>(std::floor(${times}), 0.0, 10000.0)` : `Math.min(10000, Math.max(0, Math.floor(${times})))`}; ${go(n.next)}`;
        break;
      }
      case 'increment': body = `${idx}++; ${go(n.next)}`; break;
      case 'loopTest': {
        let condition = `${idx} < ${limit}`;
        if (n.block.type === 'controls_whileUntil') {
          condition = expr(n, 'BOOL', 'false');
          if (n.block.getFieldValue('MODE') === 'UNTIL') condition = `!(${condition})`;
        }
        body = `_pc = (${condition}) ? ${n.body} : ${n.after}; break;`;
        if (n.block.type === 'key_for_each') {
          const names = { wasd: ['W','A','S','D'], arrows: ['UP','DOWN','LEFT','RIGHT'], modifier: ['LCTRL','RCTRL','L_CTRL','R_CTRL','LSHIFT','RSHIFT','L_SHIFT','R_SHIFT','LALT','RALT','L_ALT','R_ALT','TAB','CAPS','ENTER'] };
          const zone = n.block.getFieldValue('ZONE') || 'all';
          let filter = 'true';
          if (names[zone]) filter = names[zone].map(name => `name == ${JSON.stringify(name)}`).join(' || ');
          if (zone === 'numeric') filter = cpp ? 'info.physical_row == 1 && info.physical_col >= 2 && info.physical_col <= 13' : 'info.row === 1 && info.col >= 2 && info.col <= 13';
          if (zone === 'navigation') filter = `${cpp ? 'info.physical_col' : 'info.col'} >= 14 || name == "UP" || name == "DOWN" || name == "LEFT" || name == "RIGHT"`;
          const context = cpp
            ? `if (!_keys[${idx}]) { ${idx}++; break; } const auto& info = *_keys[${idx}]; const auto& name = info.key_name;`
            : `const info = keymap[${idx}]; const name = info.name;`;
          body = `if (${idx} >= 68) { ${go(n.after)} } { ${context} if (${filter}) { ${go(n.body)} } ${idx}++; break; }`;
        }
        break;
      }
      case 'action': {
        const single = new Proxy(n.block, { get: (obj, key) => key === 'getNextBlock' ? () => null : typeof obj[key] === 'function' ? obj[key].bind(obj) : obj[key] });
        body = `${target.statement(single)}\n${go(n.next)}`;
        break;
      }
    }
    if (n.keySlot !== null && n.keySlot !== undefined) {
      const keyIndex = `_indices[${n.keySlot}]`;
      body = (cpp
        ? `const auto& info = *_keys[${keyIndex}];\n`
        : `const info = keymap[${keyIndex}];\n`) + body;
    }
    return `case ${id}: { ${body} }`;
  }).join('\n');
  return { code, entry, slots: Math.max(1, slots), keyNames: KEYBOARD_LAYOUT.flat().map(k => JSON.stringify(k.name)).join(', ') };
}
