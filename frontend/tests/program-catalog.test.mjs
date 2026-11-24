import test from 'node:test';
import assert from 'node:assert/strict';
import Blockly from '../src/blockly/index.js';
import { registerCustomBlocks } from '../src/blockly/customBlocks.js';
import { EFFECT_PRESETS } from '../src/blockly/presets.js';
import { validateCatalogList, validateReceipt, catalogOperationWithReceipt, installRequest, prepareProgramPackage, PROGRAM_CATALOG_LIMITS } from '../src/utils/programCatalog.js';
registerCustomBlocks();

const response = (value, ok = true, status = 200) => ({ ok, status, text: async () => JSON.stringify(value) });
const fixture = () => ({ name: 'fixture', publication: { mode: 'continuous', fade_out_ms: 0 }, json: structuredClone(EFFECT_PRESETS[0].blocklyJson) });
const id = 'a'.repeat(32), hash = 'b'.repeat(64);
const list = (overrides = {}) => ({ schema_version: 1, revision: 1, entries: [], diagnostic: '', receipt_count: 0, ...overrides });
const receipt = (overrides = {}) => ({ status: 'ok', operation_id: id, id, plugin_name: `ir_${id}`, revision: 2, active: hash, enabled: false, removed: false, running_instances: 'Previously admitted generations remain pinned until their normal lifecycle ends', ...overrides });

test('real Blockly source becomes bounded IR with local simulation and capability inputs', () => {
  const prepared = prepareProgramPackage(fixture(), { name: 'fixture', version: '1.0.0' });
  assert.equal(prepared.program.schema_version, 1); assert.ok(prepared.program.nodes.length); assert.deepEqual(prepared.inputs, { keyboard: false, cs2_gsi: false });
  assert.equal(prepared.preview.length, 3); assert.equal(prepared.preview[0].length, 68); assert.ok(JSON.stringify(prepared.program).length < PROGRAM_CATALOG_LIMITS.requestBytes);
});

test('catalog list and receipt validation enforce runtime tier, hashes, inputs and exact fields', () => {
  assert.deepEqual(validateCatalogList(list()), list()); assert.deepEqual(validateReceipt(receipt()), receipt());
  assert.throws(() => validateCatalogList(list({ entries: [{ id, plugin_name: 'wrong', execution_tier: 'native', loaded: false, generation: 0, name: 'fixture', active: hash, enabled: false, removed: false, versions: [] }] })), /目录条目/);
  assert.throws(() => validateReceipt({ ...receipt(), extra: true }), /响应或请求字段/);
});

test('install request is source plus IR, exact revision, and operation identity', () => {
  const request = installRequest(fixture(), { id, name: 'fixture', version: '1.0.0' }, 0, id);
  assert.equal(request.operation, 'install'); assert.equal(request.expected_revision, 0); assert.equal(request.operation_id, id); assert.ok(request.program.nodes); assert.equal(request.source.name, 'fixture');
  assert.throws(() => installRequest(fixture(), { id, name: 'fixture', version: 'bad version' }, 0, id), /名称或版本/);
});

test('default global fetch transport works and all catalog actions preserve operation IDs', async () => {
  const requests = [], pending = { operation: 'enable', operation_id: id, expected_revision: 1, id };
  const oldFetch = globalThis.fetch; globalThis.fetch = async (url, options = {}) => { requests.push({ url, options }); if (requests.length === 1) throw Object.assign(new Error('timeout'), { code: 'timeout' }); return response(receipt({ revision: 2, enabled: true })); };
  const result = await catalogOperationWithReceipt(pending); globalThis.fetch = oldFetch; assert.equal(result.kind, 'completed'); assert.equal(requests.length, 2); assert.match(requests[1].url, /operation_id=/);
  const body = JSON.parse(requests[0].options.body); assert.equal(body.operation_id, id); assert.equal(body.expected_revision, 1);
});

test('network and HTTP 503 uncertainty retains the same operation ID as pending', async () => {
  for (const failure of [Object.assign(new TypeError('network down'), { code: 'network' }), response({ status: 'error', error: 'catalog_unavailable', message: 'busy' }, false, 503)]) {
    const requests = [], oldFetch = globalThis.fetch; globalThis.fetch = async (url, options = {}) => { requests.push({ url, options }); if (options.method === 'POST') { if (failure?.status) return failure; throw failure; } return response({ status: 'error', error: 'receipt_not_found', message: 'missing' }, false, 404); };
    const result = await catalogOperationWithReceipt({ operation: 'enable', operation_id: id, expected_revision: 1, id }); globalThis.fetch = oldFetch;
    assert.equal(result.kind, 'pending'); assert.equal(result.operation_id, id); assert.equal(requests.filter(item => item.options.method === 'POST').length, 1);
  }
});

test('ambiguous timeout retains operation ID when receipt is not found; no second POST', async () => {
  const requests = []; const pending = { operation: 'disable', operation_id: id, expected_revision: 1, id };
  const result = await catalogOperationWithReceipt(pending, { fetchImpl: async (url, options = {}) => { requests.push({ url, options }); if (options.method === 'POST') throw Object.assign(new Error('timeout'), { code: 'timeout' }); return response({ status: 'error', error: 'receipt_not_found', message: 'not ready' }, false, 404); } });
  assert.equal(result.kind, 'pending'); assert.equal(result.operation_id, id); assert.equal(requests.filter(item => item.options.method === 'POST').length, 1);
});

test('install payload and results are bounded, source uses safe Blockly graph, no native compiler required', () => {
  const prepared = prepareProgramPackage(fixture(), { name: 'fixture', version: '1.0.0' });
  assert.ok(new TextEncoder().encode(JSON.stringify(prepared.program)).length < PROGRAM_CATALOG_LIMITS.requestBytes); assert.ok(prepared.source.json);
  assert.throws(() => prepareProgramPackage({ ...fixture(), name: '../escape' }, { name: 'fixture', version: '1.0.0' }), /工程名称/);
  assert.throws(() => validateCatalogList({ ...list(), diagnostic: 'x', receipt_count: -1 }), /目录列表/);
});
