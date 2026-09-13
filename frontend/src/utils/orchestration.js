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
