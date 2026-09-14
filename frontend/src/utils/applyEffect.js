export async function ensureStudioRuntime() {
  let web, daemon;
  try {
    const res = await fetch('/api/status', { cache: 'no-store' });
    web = await res.json();
    if (!res.ok) throw new Error('Web UI 未响应');
  } catch (err) {
    throw new Error(`无法连接 Web UI：${err.message}`);
  }
  if (web.web_api_version !== 2) {
    throw new Error(`Web UI 版本不兼容：当前 API v${web.web_api_version ?? '未知'}，要求 v2。请重新构建并启动 aura_web_ui.exe，然后刷新页面。`);
  }
  try {
    const res = await fetch('/api/gsi/current', { cache: 'no-store' });
    daemon = await res.json();
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
  } catch (err) {
    throw new Error(`无法连接 daemon：${err.message}`);
  }
  if (daemon.studio_runtime !== 2) {
    throw new Error(daemon.daemon_running === false
      ? 'daemon 不在线：请启动最新的 aura_daemon.exe 后重试。'
      : `未检测到兼容的 Studio Runtime：当前 v${daemon.studio_runtime ?? '未知'}，要求 v2。请关闭旧 daemon，启动最新构建。`);
  }
}

export async function requestJson(url, body) {
  let res;
  try {
    res = await fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
  } catch (err) {
    throw new Error(`无法连接 Web UI：${err.message}`);
  }
  const data = await res.json().catch(() => ({}));
  if (!res.ok || data.success === false || data.status === 'error') {
    const err = new Error(data.message || `请求失败 (${res.status})`);
    err.stage = data.stage || (url.includes('compile') ? 'msvc_compile' : 'daemon_reload');
    err.detail = data.compiler_output || data.log || '';
    throw err;
  }
  return data;
}

// Stage an immutable build before switching any profile references. Failed
// compilation, daemon reload or config save leaves the previous version usable.
export async function stageEffect(name, workspace, transpiler, onLog = () => {}) {
  if (!/^[a-zA-Z_][a-zA-Z0-9_]{0,47}$/.test(name)) throw new Error('插件名非法：须以字母或下划线开头，最多 48 个字符。');
  let progress = '';
  const report = line => { progress += `${line}\n`; onLog(progress); };
  report('1/5 检查运行环境…');
  await ensureStudioRuntime();
  // The editor is locked during publishing; both transpilations happen before
  // the compile request so the revision and generated plugin share one source.
  report('2/5 生成 C++ 源码…');
  let canonicalCode, code;
  const pluginName = `studio_${name.slice(0, 20)}_${crypto.randomUUID().replaceAll('-', '').slice(0, 20)}`;
  try {
    canonicalCode = transpiler.transpile(name, workspace);
    code = transpiler.transpile(pluginName, workspace);
  } catch (err) {
    throw new Error(`C++ 源码生成失败：${err.message}`);
  }
  const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(canonicalCode));
  const revision = Array.from(new Uint8Array(digest), b => b.toString(16).padStart(2, '0')).join('');
  report('3/5 调用 MSVC 编译…');
  const compiled = await requestJson('/api/compile_effect', { name: pluginName, code });
  report(`MSVC 输出：\n${compiled.compiler_output || '(无输出)'}`);
  report('4/5 加载 DLL 并等待 daemon 确认…');
  const loaded = await requestJson('/api/reload_plugin', { name: pluginName });
  if (loaded.daemon_synced !== true) throw new Error('守护进程尚未确认加载，当前方案仍使用上一版。请确认后台已启动后重试。');
  report('5/5 daemon 已确认插件加载，正在保存配置…');
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

export function sanitizeEffectName(rawName, fallback = 'custom_effect') {
  if (typeof rawName !== 'string') rawName = String(rawName || '');
  const clean = rawName
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9_]/g, '_')
    .replace(/_+/g, '_')
    .replace(/^_+|_+$/g, '');
  return clean || fallback;
}

export function getNextCloneName(srcName, existingEffects = {}) {
  const existingKeys = Array.isArray(existingEffects)
    ? existingEffects
    : Object.keys(existingEffects || {});
  const existingSet = new Set(existingKeys);

  let root = srcName;
  const match = srcName.match(/^(.*?)_copy(?:_(\d+))?$/);
  if (match) {
    root = match[1];
  }

  if (!existingSet.has(`${root}_copy`)) {
    return `${root}_copy`;
  }

  let index = 2;
  while (existingSet.has(`${root}_copy_${index}`)) {
    index++;
  }
  return `${root}_copy_${index}`;
}

export function renameEffectInConfig(config, oldName, newName) {
  const clean = sanitizeEffectName(newName, '');
  if (!clean) throw new Error('名称不能为空');
  if (clean === oldName) return config;
  if (config?.blockly_effects?.[clean]) throw new Error('目标名称已存在');

  const effects = { ...(config?.blockly_effects || {}) };
  const oldEffect = effects[oldName];
  if (!oldEffect) throw new Error(`源光效不存在: ${oldName}`);

  delete effects[oldName];
  effects[clean] = {
    ...oldEffect,
    name: clean
  };

  const profiles = { ...(config?.profiles || {}) };
  if (profiles[oldName]) {
    profiles[clean] = {
      ...profiles[oldName],
      title: clean
    };
    delete profiles[oldName];
  }

  const orch = JSON.parse(JSON.stringify(config?.orchestration || {}));
  if (orch.fallback_profile === oldName) orch.fallback_profile = clean;
  if (Array.isArray(orch.rules)) {
    orch.rules.forEach(r => {
      if (r.target_profile === oldName) r.target_profile = clean;
    });
  }
  if (Array.isArray(orch.event_overlays)) {
    orch.event_overlays.forEach(ov => {
      if (ov.effect === oldName) ov.effect = clean;
    });
  }

  let defaultProfile = config?.default_profile;
  if (defaultProfile === oldName) defaultProfile = clean;

  return {
    ...config,
    default_profile: defaultProfile,
    blockly_effects: effects,
    profiles,
    orchestration: orch
  };
}
