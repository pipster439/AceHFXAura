export async function ensureStudioRuntime() {
  const res = await fetch('/api/gsi/current');
  const data = await res.json();
  if (!res.ok || data.studio_runtime !== 2) throw new Error('请先重新编译并启动本分支的守护进程，再应用新版工作室配置');
}

export async function requestJson(url, body) {
  const res = await fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
  const data = await res.json();
  if (!res.ok || data.success === false || data.status === 'error') throw new Error(data.compiler_output || data.message || `请求失败 (${res.status})`);
  return data;
}

// Stage an immutable build before switching any profile references. Failed
// compilation, daemon reload or config save leaves the previous version usable.
export async function stageEffect(name, workspace, transpiler, onLog = () => {}) {
  // Snapshot both sources before the first await; editing during a build cannot
  // silently pair a newer program with an older saved Blockly workspace.
  const canonicalCode = transpiler.transpile(name, workspace);
  const pluginName = `studio_${name.slice(0, 20)}_${crypto.randomUUID().replaceAll('-', '').slice(0, 20)}`;
  const code = transpiler.transpile(pluginName, workspace);
  const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(canonicalCode));
  const revision = Array.from(new Uint8Array(digest), b => b.toString(16).padStart(2, '0')).join('');
  await ensureStudioRuntime();
  const compiled = await requestJson('/api/compile_effect', { name: pluginName, code });
  onLog(compiled.compiler_output || '光效生成完成');
  const loaded = await requestJson('/api/reload_plugin', { name: pluginName });
  if (loaded.daemon_synced !== true) throw new Error('守护进程尚未确认加载，当前方案仍使用上一版。请确认后台已启动后重试。');
  return { pluginName, revision };
}

export function getEffectLifecycleStatus(effect) {
  if (!effect) {
    return { status: 'draft', label: '草稿', color: 'amber', badgeClass: 'bg-amber-500/15 text-amber-400 border-amber-500/30' };
  }
  const hasPublished = Boolean(effect.published_at || effect.applied_plugin_name);
  if (!hasPublished) {
    return { status: 'draft', label: '草稿', color: 'amber', badgeClass: 'bg-amber-500/15 text-amber-400 border-amber-500/30' };
  }
  const publishedAt = effect.published_at || 0;
  const sourceUpdatedAt = effect.source_updated_at || effect.updated_at || 0;
  if (sourceUpdatedAt > publishedAt) {
    return { status: 'modified', label: '未发布修改', color: 'sky', badgeClass: 'bg-sky-500/15 text-sky-400 border-sky-500/30' };
  }
  return { status: 'published', label: '已发布', color: 'emerald', badgeClass: 'bg-emerald-500/15 text-emerald-400 border-emerald-500/30' };
}

export function effectConfig(config, name, blocklyJson, build) {
  const previous = config.blockly_effects?.[name] || {};
  const now = Date.now();
  const effect = {
    ...previous,
    name,
    version: 2,
    blockly_json: blocklyJson,
    source_updated_at: now,
    updated_at: now
  };
  const profiles = { ...config.profiles };
  if (build) {
    effect.published_at = now;
    effect.source_updated_at = now;
    effect.applied_revision = build.revision;
    effect.applied_blockly_json = blocklyJson;
    effect.applied_plugin_name = build.pluginName;
    profiles[name] = { ...(profiles[name] || {}), type: 'plugin', plugin_name: build.pluginName, title: name, fps: profiles[name]?.fps || 25 };
  }
  return { ...config, profiles, blockly_effects: { ...config.blockly_effects, [name]: effect } };
}
