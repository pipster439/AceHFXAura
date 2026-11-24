import test from 'node:test';
import assert from 'node:assert/strict';
import { StudioConversationTools, projectFingerprint } from '../src/utils/studioConversation.js';
import { numericNodes, StudioProposalSession, compileCandidateNative } from '../src/utils/studioAssistant.js';
import { registerCustomBlocks } from '../src/blockly/customBlocks.js';
registerCustomBlocks();
import { EFFECT_PRESETS } from '../src/blockly/presets.js';

const snapshot = () => ({ name: 'fixture', publication: { mode: 'continuous', fade_out_ms: 0 }, json: structuredClone(EFFECT_PRESETS[0].blocklyJson) });
const proposal = (node, value) => ({ action: 'propose', summary: '保留颜色，调整周期', preset: null, edits: [{ node_id: node.node_id, value }] });
test('multi-turn pending proposal followup stays on same draft and retains colors, then Apply and Undo', async () => {
  const before = snapshot(), tools = new StudioConversationTools(new StudioProposalSession({ compileCandidate: async () => ({ success: true }) }));
  const node = numericNodes(before.json).find(n => n.input === 'PERIOD_SEC');
  node.block.fields.NUM = .9; const original = structuredClone(before);
  const context = await tools.begin(before); assert.deepEqual(context.presets, []); assert.deepEqual(context.capabilities.gsi_fields, []);
  const first = await tools.execute('propose_effect_change', proposal(node, .65), before); assert.equal(first.valid, false); assert.equal(first.simulated, true);
  assert.deepEqual(before, original);
  const secondContext = await tools.begin(before); assert.equal(secondContext.active_proposal.changes[0].after, '0.65');
  const second = await tools.execute('propose_effect_change', proposal(node, .725), before); assert.equal(second.valid, false);
  assert.throws(() => tools.session.apply(before), /原生编译/);
  assert.equal((await tools.execute('validate_candidate', {}, before)).valid, true);
  const candidate = tools.session.apply(before); const values = numericNodes(candidate.json);
  assert.equal(values.find(n => n.input === 'PERIOD_SEC').value, .725);
  assert.deepEqual(values.filter(n => n.parent === 'color_rgb').map(n => n.value), numericNodes(before.json).filter(n => n.parent === 'color_rgb').map(n => n.value));
  tools.session.markApplied(candidate); assert.deepEqual(tools.session.undo(candidate), before);
});
test('actual candidate error is available to followup, no fabricated validation result or Apply', async () => {
  const before = snapshot(), tools = new StudioConversationTools(); await tools.begin(before);
  const invalid = await tools.execute('propose_effect_change', proposal({ node_id: 'n999' }, 2), before);
  assert.equal(invalid.valid, false); assert.match(invalid.diagnostic, /不存在/); assert.throws(() => tools.session.apply(before));
  const error = await tools.execute('get_active_proposal', {}, before); assert.equal(error.diagnostic, invalid.diagnostic);
  assert.equal((await tools.execute('validate_candidate', {}, before)).valid, false);
  const next = await tools.begin(before); assert.equal(next.active_proposal.valid, false);
});
test('stale proposal, project changes mid-request and cancellation are refused without editing', async () => {
  const before = snapshot(), tools = new StudioConversationTools(); await tools.begin(before);
  const node = numericNodes(before.json).find(n => n.input === 'PERIOD_SEC'); await tools.execute('propose_effect_change', proposal(node, 2), before);
  const edited = structuredClone(before); numericNodes(edited.json).find(n => n.input === 'PERIOD_SEC').block.fields.NUM = 4;
  await assert.rejects(tools.execute('get_capabilities', {}, edited), /项目已变化/); assert.throws(() => tools.session.apply(edited));
  await tools.begin(edited); tools.cancel(); await assert.rejects(tools.execute('propose_effect_change', proposal(node, 2), edited), /取消/); assert.throws(() => tools.session.apply(edited));
});
test('unknown, Apply, Publish, shell, path and oversized tools cannot reach project actions', async () => {
  const before = snapshot(), tools = new StudioConversationTools(); await tools.begin(before);
  for (const name of ['apply_project', 'publish', 'shell', 'write_file', 'HID']) await assert.rejects(tools.execute(name, {}, before));
  await assert.rejects(tools.execute('get_build_errors', { path: 'C:/secret' }, before));
  await assert.rejects(tools.execute('get_preset', { preset_id: 'a'.repeat(9000) }, before));
  await assert.rejects(tools.execute('propose_effect_change', { ...proposal({ node_id: 'n0' }, 1), shell: 'exec' }, before));
  assert.deepEqual(before, snapshot());
});
test('diagnostics read only actual selected errors and preset metadata is opt-in per tool', async () => {
  const before = snapshot(), tools = new StudioConversationTools(); await tools.begin(before);
  assert.deepEqual(await tools.execute('get_build_errors', {}, before, { build: 'C2039 unsupported field' }), { diagnostic: 'C2039 unsupported field' });
  const preset = await tools.execute('get_preset', { preset_id: EFFECT_PRESETS[0].id }, before); assert.equal(preset.id, EFFECT_PRESETS[0].id);
  await assert.rejects(tools.execute('propose_effect_change', { action: 'propose', summary: 'replace', preset: preset.id, edits: [{ node_id: 'n0', value: 1 }] }, before));
  assert.equal((await tools.execute('get_preset', { preset_id: 'unknown' }, before)).available, false);
  await assert.rejects(tools.execute('propose_effect_change', { action: 'propose', summary: 'replace', preset: EFFECT_PRESETS[1].id, edits: [] }, before));
});
test('whole graph fingerprint includes nonnumeric changes and project identity', async () => {
  const first = snapshot(), second = snapshot(); assert.equal(await projectFingerprint(first), await projectFingerprint(second));
  second.name = 'other_project'; assert.notEqual(await projectFingerprint(first), await projectFingerprint(second));
});
test('project sessions cannot receive another projects active proposal; clear is memory only', async () => {
  const first = snapshot(), tools = new StudioConversationTools(); await tools.begin(first);
  const node = numericNodes(first.json).find(n => n.input === 'PERIOD_SEC'); await tools.execute('propose_effect_change', proposal(node, 2), first);
  const second = { ...snapshot(), name: 'second' }; const next = await tools.begin(second); assert.equal(next.active_proposal, null);
  await tools.execute('propose_effect_change', proposal(node, 2), second); tools.clear();
  assert.equal((await tools.begin(second)).active_proposal, null); assert.deepEqual(first, snapshot());
});
test('stale pending summary is marked on next turn, applied summary follows current draft', async () => {
  const before = snapshot(), tools = new StudioConversationTools(); await tools.begin(before);
  const node = numericNodes(before.json).find(n => n.input === 'PERIOD_SEC'); await tools.execute('propose_effect_change', proposal(node, 2), before);
  const edit = structuredClone(before); numericNodes(edit.json).find(n => n.input === 'PERIOD_SEC').block.fields.NUM = 4;
  const next = await tools.begin(edit); assert.equal(next.active_proposal.stale, true); assert.equal(next.active_proposal.preview, undefined);
});

const nativeFetch = (requests, compile = async () => ({ success: true })) => async (url, options = {}) => {
  requests.push({ url, options });
  if (options.signal?.aborted) throw new DOMException('cancelled', 'AbortError');
  const body = url === '/api/status' ? { web_api_version: 2, studio_publish_ready: true, sdk_headers_found: true, msvc_found: true } :
    url === '/api/gsi/current' ? { studio_runtime: 2, logical_keyboard_version: 1 } :
      url === '/api/compile_effect' ? await compile(options) : (() => { throw new Error('Unexpected endpoint ' + url); })();
  return { ok: true, json: async () => body };
};
const gatedTools = fetchImpl => new StudioConversationTools(new StudioProposalSession({
  compileCandidate: (candidate, options) => compileCandidateNative(candidate, { ...options, fetchImpl }) }));
const graphArgs = () => ({ summary: '创建蓝色灯效', preserve_rgb: false, operations: [
  { op: 'create', id: 'fill', type: 'key_fill_all' }, { op: 'create', id: 'rgb', type: 'color_rgb' },
  ...['R', 'G', 'B'].flatMap((field, index) => [
    { op: 'create', id: field, type: 'math_number', fields: { NUM: index === 2 ? 255 : 0 } },
    { op: 'connect', parent: 'rgb', slot: { kind: 'input', name: field }, child: field }
  ]), { op: 'connect', parent: 'fill', slot: { kind: 'input', name: 'COLOR' }, child: 'rgb' }
] });

test('actual conversation graph proposal uses native compile-only transport before explicit Apply', async () => {
  const requests = [], tools = gatedTools(nativeFetch(requests)), before = { ...snapshot(), json: {} }, requestId = 'a'.repeat(32);
  await tools.begin(before, requestId);
  assert.deepEqual((await tools.execute('get_project_graph', {}, before)).nodes, []);
  assert.equal((await tools.execute('get_graph_schema', {}, before)).operations.maxItems, 256);
  const candidate = await tools.execute('propose_graph_change', graphArgs(), before);
  assert.equal(candidate.valid, false); assert.equal(candidate.native_compiled, false); assert.equal(requests.length, 0);
  assert.throws(() => tools.session.apply(before, requestId), /原生编译/);
  const verified = await tools.execute('validate_candidate', {}, before);
  assert.equal(verified.valid, true); assert.equal(verified.native_compiled, true); assert.equal(verified.candidate_id, candidate.candidate_id);
  const posts = requests.filter(request => request.options.method === 'POST'); assert.equal(posts.length, 1);
  assert.equal(posts[0].url, '/api/compile_effect');
  const body = JSON.parse(posts[0].options.body); assert.match(body.name, /^studio_graph_fixture|^studio_fixture/);
  assert.match(body.code, /Auto-generated.*Blockly/); assert.ok(!requests.some(request => /reload|preview|save|config/.test(request.url)));
  assert.deepEqual(before.json, {});
  const after = tools.session.apply(before, requestId); assert.notDeepEqual(after.json, before.json);
  assert.deepEqual(tools.session.undo(after, requestId), before);
  await tools.execute('get_capabilities', {}, before);
});

test('native compiler failure is explicit and bounded repair preserves the initial draft', async () => {
  const requests = []; let compilations = 0;
  const tools = gatedTools(nativeFetch(requests, async () => ++compilations === 1 ? { success: false, message: 'compiler fixture rejection' } : { success: true }));
  const before = { ...snapshot(), json: {} }; await tools.begin(before, 'a'.repeat(32));
  await tools.execute('propose_graph_change', graphArgs(), before);
  const failed = await tools.execute('validate_candidate', {}, before); assert.equal(failed.valid, false); assert.match(failed.diagnostic, /compiler fixture rejection/);
  assert.throws(() => tools.session.apply(before, 'a'.repeat(32)), /原生编译/); assert.deepEqual(before.json, {});
  await tools.execute('propose_graph_change', graphArgs(), before);
  assert.equal((await tools.execute('validate_candidate', {}, before)).valid, true);
  assert.equal(compilations, 2);
  await tools.execute('propose_graph_change', graphArgs(), before);
  await assert.rejects(tools.execute('propose_graph_change', graphArgs(), before), /最多三个候选/);
});

test('compile success arriving after cancel, project edit or replacement cannot authorize Apply', async () => {
  for (const action of ['cancel', 'edit', 'replace']) {
    let release; const compiling = new Promise(resolve => { release = resolve; }); let started;
    const entered = new Promise(resolve => { started = resolve; });
    const requests = [], tools = gatedTools(nativeFetch(requests, async () => { started(); return compiling; }));
    const before = { ...snapshot(), json: {} }; let current = before; await tools.begin(before, 'a'.repeat(32));
    await tools.execute('propose_graph_change', graphArgs(), before);
    const validation = tools.execute('validate_candidate', {}, before, {}, { getCurrent: () => current });
    await entered;
    if (action === 'cancel') tools.cancel();
    if (action === 'edit') current = { ...before, name: 'changed' };
    if (action === 'replace') await tools.execute('propose_graph_change', graphArgs(), before);
    release({ success: true });
    if (action === 'edit') assert.equal((await validation).valid, false);
    else await assert.rejects(validation, /取消|变化|丢弃/);
    assert.throws(() => tools.session.apply(before, 'a'.repeat(32)));
    assert.deepEqual(tools.session.getUndoableRequestIds(), []);
  }
});

test('native validation cannot be forged by setting readiness metadata or reused after candidate mutation', async () => {
  const before = snapshot(), requests = [], tools = gatedTools(nativeFetch(requests)); await tools.begin(before, 'a'.repeat(32));
  const node = numericNodes(before.json).find(value => value.input === 'PERIOD_SEC');
  await tools.execute('propose_effect_change', proposal(node, 2), before);
  tools.session.pending.validation.native_compiled = true;
  assert.throws(() => tools.session.apply(before, 'a'.repeat(32)), /原生编译/);
  await tools.execute('validate_candidate', {}, before);
  numericNodes(tools.session.pending.candidate.json)[0].block.fields.NUM = 42;
  assert.throws(() => tools.session.apply(before, 'a'.repeat(32)), /原生编译/);
});

test('old or incomplete native service readiness blocks compile rather than assuming success', async () => {
  for (const status of [ {}, { web_api_version: 1 }, { web_api_version: 2, studio_publish_ready: true, sdk_headers_found: true, msvc_found: false } ]) {
    const calls = [], tools = gatedTools(async url => { calls.push(url); return { ok: true, json: async () => status }; });
    const before = { ...snapshot(), json: {} }; await tools.begin(before);
    await tools.execute('propose_graph_change', graphArgs(), before);
    const result = await tools.execute('validate_candidate', {}, before);
    assert.equal(result.valid, false); assert.match(result.diagnostic, /原生编译不可用/); assert.deepEqual(calls, ['/api/status']);
  }
});

test('cached native validation never recompiles and failed attempts are bounded without automatic replay', async () => {
  const requests = [], before = { ...snapshot(), json: {} }, tools = gatedTools(nativeFetch(requests));
  await tools.begin(before, 'a'.repeat(32)); await tools.execute('propose_graph_change', graphArgs(), before);
  await tools.execute('validate_candidate', {}, before); await tools.execute('validate_candidate', {}, before);
  assert.equal(requests.filter(value => value.url === '/api/compile_effect').length, 1);
  const failed = [], failing = gatedTools(nativeFetch(failed, async () => ({ success: false, message: 'fixture compile failure' })));
  await failing.begin(before); await failing.execute('propose_graph_change', graphArgs(), before);
  for (let i = 0; i < 4; i++) assert.equal((await failing.execute('validate_candidate', {}, before)).valid, false);
  assert.equal(failed.filter(value => value.url === '/api/compile_effect').length, 3);
});

test('compiled candidate receipt binds fingerprint and graph result capacity is explicit', async () => {
  const before = { ...snapshot(), json: {} }, tools = gatedTools(nativeFetch([]));
  await tools.begin(before, 'a'.repeat(32)); const staged = await tools.execute('propose_graph_change', graphArgs(), before);
  await tools.execute('validate_candidate', {}, before);
  assert.throws(() => tools.session.reserveApply(before, { requestId: 'a'.repeat(32), candidateId: staged.candidate_id, fingerprint: 'wrong' }), /指纹不匹配/);
  const malformed = graphArgs(); malformed.code = 'not trusted source';
  await assert.rejects(tools.execute('propose_graph_change', malformed, before), /工具参数无效/);
  assert.throws(() => tools.session.apply(before, 'a'.repeat(32)));
});

test('client compiler timeout aborts transport and never authorizes a late candidate', async t => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  let entered; const started = new Promise(resolve => { entered = resolve; }); let compileCalls = 0;
  const fetchImpl = nativeFetch([], options => new Promise((resolve, reject) => {
    compileCalls++; entered(); options.signal.addEventListener('abort', () => reject(new DOMException('cancelled', 'AbortError')), { once: true });
  }));
  const before = { ...snapshot(), json: {} }, tools = gatedTools(fetchImpl); await tools.begin(before);
  await tools.execute('propose_graph_change', graphArgs(), before);
  const running = tools.execute('validate_candidate', {}, before); await started;
  t.mock.timers.tick(30000); const result = await running;
  assert.equal(result.valid, false); assert.equal(result.native_compiled, false); assert.equal(compileCalls, 1);
  assert.throws(() => tools.session.apply(before));
});

test('large safe graph remains readable via explicit tools without a truncated numeric summary', async () => {
  const before = { ...snapshot(), json: { blocks: { blocks: Array.from({ length: 300 }, (_, index) => ({ type: 'math_number', id: `number_${index}`, fields: { NUM: index } })) } } };
  const tools = new StudioConversationTools(), context = await tools.begin(before);
  assert.deepEqual(context.nodes, []); assert.match(context.diagnostic, /数值摘要限制/);
  assert.equal((await tools.execute('get_project_graph', {}, before)).nodes.length, 300);
  const oversized = { ...before, json: { blocks: { blocks: Array.from({ length: 1000 }, (_, index) => ({ type: 'math_number', id: `number_${index}`, fields: { NUM: index } })) } } };
  await tools.begin(oversized);
  await assert.rejects(tools.execute('get_project_graph', {}, oversized), /48 KiB/);
  const schema = await tools.execute('get_graph_schema', {}, oversized);
  assert.ok(new TextEncoder().encode(JSON.stringify(schema)).length <= 49152);
  const fill = schema.blocks.find(block => block.type === 'key_fill_all');
  assert.deepEqual(fill.inputs, [{ name: 'COLOR', kind: 'value', accepts: ['Color'] }]);
});
