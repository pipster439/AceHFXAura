// One authoritative rule list. Old arrays are imported once, in their original
// runtime precedence order, then replaced by projections in the simple editors.
export function canonicalConfig(config = {}) {
  if (config.orchestration?.version === 2) return config;
  const modern = (config.orchestration?.rules || []).map((r, i) => ({ ...r, id: r.id || `advanced_${i}` }));
  const gsi = (config.gsi_bindings || []).map((r, i) => ({
    id: `gsi_${i}`, process: 'cs2.exe', dnd: false,
    condition: { field: r.field, op: r.operator || r.op || '==', value: r.value },
    target_profile: r.profile
  }));
  const simple = (config.rules || []).filter(r => !modern.some(m =>
    m.process === r.process && m.target_profile === r.profile && !Object.keys(m.condition || {}).length
  )).map((r, i) => ({ id: `process_${i}`, source: 'process', process: r.process, dnd: !!r.suppress_web_ui, condition: {}, target_profile: r.profile }));
  return {
    ...config, blockly_orchestrator: undefined, rules: [], gsi_bindings: [],
    orchestration: { ...config.orchestration, version: 2, rules: [...modern, ...gsi, ...simple], event_overlays: config.orchestration?.event_overlays || [], fallback_profile: config.orchestration?.fallback_profile || config.default_profile || 'desktop' }
  };
}

export function processRows(config) {
  return canonicalConfig(config).orchestration.rules.filter(r => !Object.keys(r.condition || {}).length && (r.source === 'process' || r.process)).map(r => ({ id: r.id, process: r.process, profile: r.target_profile, suppress_web_ui: !!r.dnd }));
}

export function gsiRows(config) {
  return canonicalConfig(config).orchestration.rules.filter(r => r.process === 'cs2.exe' && r.condition?.field && !r.condition.field.startsWith('process')).map(r => ({ id: r.id, field: r.condition.field, operator: r.condition.op, value: r.condition.value, profile: r.target_profile }));
}

export function replaceSimpleRows(config, kind, rows) {
  const next = canonicalConfig(config);
  const old = kind === 'process' ? processRows(next) : gsiRows(next);
  const ids = new Set(old.map(r => r.id));
  const converted = rows.map((r, i) => kind === 'process'
    ? { id: r.id || `process_${Date.now()}_${i}`, source: 'process', enabled: !!r.process?.trim(), process: r.process, condition: {}, target_profile: r.profile, dnd: !!r.suppress_web_ui }
    : { id: r.id || `gsi_${Date.now()}_${i}`, process: 'cs2.exe', condition: { field: r.field, op: r.operator || '==', value: r.value }, target_profile: r.profile, dnd: false });
  // Preserve the positions of existing rules relative to advanced rules.
  const byId = new Map(converted.map(r => [r.id, r]));
  const rules = next.orchestration.rules.flatMap(r => ids.has(r.id) ? (byId.has(r.id) ? [byId.get(r.id)] : []) : [r]);
  for (const r of converted) if (!ids.has(r.id)) {
    if (kind === 'gsi') rules.unshift(r); else rules.push(r);
  }
  return { ...next, blockly_orchestrator: undefined, orchestration: { ...next.orchestration, rules } };
}

/**
 * Condition evaluator for orchestration rules and overlay state conditions
 */
export function evalCondition(cond, proc, gsiVals = {}) {
  if (!cond || Object.keys(cond).length === 0) return true;
  if (cond.type === 'not') return !evalCondition(cond.conditions?.[0], proc, gsiVals);
  if (cond.type === 'and') return (cond.conditions || []).every(c => evalCondition(c, proc, gsiVals));
  if (cond.type === 'or') return (cond.conditions || []).some(c => evalCondition(c, proc, gsiVals));

  const field = cond.field || '';
  const op = cond.op || '==';
  const expected = cond.value;

  let actual;
  if (field === 'process.name' || field === 'process') {
    actual = proc;
  } else {
    actual = gsiVals[field];
  }

  if (actual === undefined || actual === null) {
    if (typeof expected === 'number') actual = 0;
    else if (typeof expected === 'boolean') actual = false;
    else actual = '';
  }

  if (op === '==' || op === '===') {
    return String(actual).toLowerCase() === String(expected).toLowerCase();
  }
  if (op === '!=' || op === '!==') {
    return String(actual).toLowerCase() !== String(expected).toLowerCase();
  }
  const numActual = Number(actual);
  const numExpected = Number(expected);
  if (op === '<') return numActual < numExpected;
  if (op === '<=') return numActual <= numExpected;
  if (op === '>') return numActual > numExpected;
  if (op === '>=') return numActual >= numExpected;
  if (op === 'contains') return String(actual).toLowerCase().includes(String(expected).toLowerCase());
  return false;
}

/**
 * Evaluates whether an overlay is currently active.
 * 对 event.kill 这类事件优先读取 /api/gsi/current 返回的 data[eventName]，
 * 不要把 ov.event 直接和 events[].name 比较。
 * 不新增 daemon API，也不要复制一份事件别名表。
 */
export function evaluateOverlayStatus(ov, { isSimMode, recentSimEvent, liveGsi, effectiveProcess, effectiveGsi } = {}) {
  const isState = ov?.trigger === 'state';
  let isActive = false;
  let reason = '';

  if (isState) {
    isActive = evalCondition(ov.condition, effectiveProcess, effectiveGsi);
    reason = isActive ? '条件已满足' : '条件未满足';
  } else {
    const eventName = ov?.event || '';
    if (isSimMode) {
      const match = recentSimEvent && (
        recentSimEvent === eventName ||
        recentSimEvent === (eventName.startsWith('event.') ? eventName.slice(6) : `event.${eventName}`)
      );
      isActive = Boolean(match);
      reason = isActive ? '事件触发中' : `待命 (${eventName})`;
    } else {
      const data = liveGsi?.data || {};
      let val = data[eventName];
      if (val === undefined || val === null) {
        const altKey = eventName.startsWith('event.') ? eventName.slice(6) : `event.${eventName}`;
        val = data[altKey];
      }

      if (val !== undefined && val !== null) {
        isActive = Boolean(val);
        reason = isActive ? `检测到 ${eventName}` : `待命 (${eventName})`;
      } else {
        isActive = false;
        reason = `待命 (${eventName})`;
      }
    }
  }

  return { isActive, reason };
}
