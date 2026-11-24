import test from 'node:test';
import assert from 'node:assert/strict';
import Blockly from '../src/blockly/index.js';
import { registerCustomBlocks } from '../src/blockly/customBlocks.js';
import { EFFECT_PRESETS } from '../src/blockly/presets.js';
import { assistantContext, numericNodes, parseProposal, prepareProposal, validateCandidate, StudioProposalSession, loadAiSnapshot } from '../src/utils/studioAssistant.js';
registerCustomBlocks();
const snapshot = () => ({ name: 'fixture', publication: { mode: 'continuous', fade_out_ms: 0 }, json: structuredClone(EFFECT_PRESETS[0].blocklyJson) });
const reply = (extra = {}) => ({ action: 'propose', summary: '提高速度', preset: null, edits: [{ node_id: 'n6', value: 2 }], ...extra });
const mockSession = () => new StudioProposalSession({ compileCandidate: async () => ({ success: true }) });

test('mock generate uses existing preset and validates in a disposable workspace', () => {
  const before = snapshot(); const text = JSON.stringify(reply({ preset: EFFECT_PRESETS[0].id, edits: [] }));
  const result = prepareProposal(before, parseProposal(text), 'generate');
  assert.equal(validateCandidate(result.candidate).valid, true); assert.deepEqual(before, snapshot());
  assert.equal(result.changes[0].field, '模板');
});
test('modify numeric speed field without mutating current document', () => {
  const before = snapshot(); const period = numericNodes(before.json).find(n => n.input === 'PERIOD_SEC');
  const result = prepareProposal(before, parseProposal(JSON.stringify(reply({ edits: [{ node_id: period.node_id, value: 2 }] }))), 'modify');
  assert.equal(validateCandidate(result.candidate).valid, true);
  assert.equal(numericNodes(before.json).find(n => n.node_id === period.node_id).value, 3);
  assert.equal(result.changes[0].after, '2');
});
test('explain and error analysis are typed, minimal context', () => {
  for (const intent of ['explain', 'error_analysis']) {
    const context = assistantContext(snapshot(), intent, 'error C2086');
    assert.equal(context.diagnostic, intent === 'error_analysis' ? 'error C2086' : '');
    assert.ok(!JSON.stringify(context).includes('blockly_json'));
    assert.ok(context.nodes.every(n => !('block' in n)));
    const proposal = parseProposal(JSON.stringify(reply({ action: 'explain', edits: [] })));
    assert.equal(prepareProposal(snapshot(), proposal, intent), null);
  }
});
test('reject unknown actions/fields paths commands malformed huge duplicate proposals', () => {
  const invalid = [reply({ action: 'publish' }), reply({ file: '../config.json' }), reply({ preset: '../../evil' }),
    reply({ edits: [{ node_id: '/tmp/file', value: 1 }] }), reply({ edits: [{ node_id: 'n0', value: 'shell' }] }),
    reply({ edits: [{ node_id: 'n0', value: 1, command: 'exec' }] }), reply({ edits: [{ node_id: 'n0', value: 1 }, { node_id: 'n0', value: 2 }] }),
    reply({ action: 'explain', edits: [{ node_id: 'n0', value: 1 }] })];
  for (const proposal of invalid) assert.throws(() => parseProposal(JSON.stringify(proposal)));
  assert.throws(() => parseProposal('{')); assert.throws(() => parseProposal(' '.repeat(65537)));
});
test('candidate rejects missing nodes invalid values and template replacement during modify', () => {
  assert.throws(() => prepareProposal(snapshot(), reply({ edits: [{ node_id: 'n999', value: 2 }] }), 'modify'));
  assert.throws(() => prepareProposal(snapshot(), reply({ edits: [{ node_id: 'n0', value: 999 }] }), 'modify'));
  assert.throws(() => prepareProposal(snapshot(), reply({ preset: EFFECT_PRESETS[0].id, edits: [] }), 'modify'));
  const bad = snapshot(); bad.json.blocks = [{ type: 'unknown_native_code' }]; assert.throws(() => validateCandidate(bad));
});
test('preview apply reject undo are separate and stale edits are preserved', async () => {
  const session = mockSession(); const before = snapshot(); session.begin(before);
  const prepared = session.stage(reply(), 'modify'); assert.equal(prepared.validation.valid, true);
  assert.deepEqual(before, snapshot()); // Preview has not applied or published anything.
  await session.validateNative(before); const after = session.apply(before); session.markApplied(after);
  assert.notDeepEqual(after, before); assert.deepEqual(session.undo(after), before);
  session.begin(before); session.stage(reply(), 'modify'); session.reject(); assert.throws(() => session.apply(before));
  session.stage(reply(), 'modify'); const edited = structuredClone(before); edited.name = 'later_edit';
  assert.throws(() => session.apply(edited)); assert.equal(edited.name, 'later_edit');
  await session.validateNative(before); const next = session.apply(before); session.markApplied(next); const later = structuredClone(next); later.name = 'later_edit';
  assert.throws(() => session.undo(later)); assert.equal(later.name, 'later_edit');
});
test('validation failure cannot apply and repair budget is bounded', () => {
  const session = new StudioProposalSession(); session.begin(snapshot());
  assert.throws(() => session.stage(reply({ edits: [{ node_id: 'n0', value: 999 }] }), 'modify'));
  assert.throws(() => session.apply(snapshot()));
  session.reserveRepair(); session.reserveRepair(); assert.throws(() => session.reserveRepair());
  session.begin(snapshot()); assert.equal(session.repairs, 0);
});
test('AI transaction retains publication metadata and never calls network', async () => {
  const before = snapshot(); const session = mockSession(); session.begin(before);
  const old = globalThis.fetch; globalThis.fetch = () => { throw new Error('AI must not use network for apply/undo'); };
  try { session.stage(reply(), 'modify'); await session.validateNative(before); const after = session.apply(before); assert.deepEqual(after.publication, before.publication); assert.deepEqual(session.undo(after), before); }
  finally { globalThis.fetch = old; }
});
test('AI snapshot loading preserves ordinary Blockly undo history', async () => {
  const workspace = new Blockly.Workspace();
  const until = async predicate => { for (let i = 0; i < 100 && !predicate(); i++) await new Promise(resolve => setTimeout(resolve, 0)); assert.ok(predicate()); };
  try {
    const block = workspace.newBlock('math_number'); block.setFieldValue(3, 'NUM');
    await until(() => workspace.getUndoStack().length >= 2);
    const ordinary = [...workspace.getUndoStack()]; const state = Blockly.serialization.workspaces.save(workspace);
    state.blocks.blocks[0].fields.NUM = 2; loadAiSnapshot(workspace, state);
    await until(() => workspace.getUndoStack().length > ordinary.length);
    assert.deepEqual(workspace.getUndoStack().slice(0, ordinary.length), ordinary);
    workspace.undo(); assert.equal(workspace.getBlockById(block.id).getFieldValue('NUM'), 3);
  } finally { workspace.dispose(); }
});
test('applied AI requests are individually reversible in order without overwriting later edits', async () => {
  const session = mockSession(), firstId = 'a'.repeat(32), secondId = 'b'.repeat(32);
  const before = snapshot();
  const period = numericNodes(before.json).find(n => n.input === 'PERIOD_SEC');
  session.begin(before);
  session.stage(reply({ edits: [{ node_id: period.node_id, value: 2 }] }), 'modify');
  await session.validateNative(before); const first = session.apply(before, firstId); session.markApplied(first);
  session.begin(first);
  session.stage(reply({ edits: [{ node_id: period.node_id, value: 1 }] }), 'modify');
  await session.validateNative(first); const second = session.apply(first, secondId); session.markApplied(second);
  assert.equal(session.undoHistory.length, 2);
  assert.deepEqual(session.undo(second, firstId), before);
  assert.equal(session.undoHistory.length, 0);
});
test('Undo to a request refuses manual edits between dependent AI transactions and exposes conflicts', async () => {
  const session = mockSession(), before = snapshot(), a = 'a'.repeat(32), b = 'b'.repeat(32);
  session.begin(before, a); session.stage(reply(), 'modify');
  await session.validateNative(before); const first = session.apply(before, a);
  const manual = structuredClone(first); manual.publication.fade_out_ms = 73;
  session.begin(manual, b); session.stage(reply({ edits: [{ node_id: 'n6', value: 1 }] }), 'modify');
  await session.validateNative(manual); const second = session.apply(manual, b);
  assert.throws(() => session.undo(second, a), error => error.code === 'undo_conflict' && error.conflicts.length === 1);
  assert.equal(session.undoHistory.length, 2);
  assert.deepEqual(session.undo(second, b), manual);
  assert.throws(() => session.undo(manual, a), error => error.code === 'undo_conflict');
});

test('Apply reservation binds immutable candidate and prevents request replacement while checkpoint awaits', async () => {
  const session = mockSession(), before = snapshot(), requestId = 'a'.repeat(32);
  session.begin(before, requestId); const staged = session.stage(reply(), 'modify');
  await session.validateNative(before);
  const reservation = session.reserveApply(before, { requestId, candidateId: staged.candidateId, fingerprint: 'f'.repeat(64) });
  let release;
  const checkpoint = new Promise(resolve => { release = resolve; });
  const apply = (async () => { await checkpoint; return session.commitApply(before, reservation); })();
  assert.throws(() => session.begin(before, 'b'.repeat(32)), /操作正在进行/);
  assert.throws(() => session.stage(reply({ edits: [{ node_id: 'n6', value: 1 }] }), 'modify'), /操作正在进行/);
  assert.throws(() => { reservation.candidate.name = 'mutated'; }, TypeError);
  release(); const applied = await apply;
  assert.equal(numericNodes(applied.json).find(n => n.node_id === 'n6').value, 2);
  assert.deepEqual(session.getUndoableRequestIds(), [requestId]);
});

test('Apply revalidates manual edits, request identity and candidate identity after checkpoint', async () => {
  const session = mockSession(), before = snapshot(), requestId = 'a'.repeat(32);
  session.begin(before, requestId); const staged = session.stage(reply(), 'modify');
  assert.throws(() => session.reserveApply(before, { requestId: 'b'.repeat(32), candidateId: staged.candidateId }));
  assert.throws(() => session.reserveApply(before, { requestId, candidateId: 'old' }));
  await session.validateNative(before);
  const reservation = session.reserveApply(before, { requestId, candidateId: staged.candidateId });
  const manual = structuredClone(before); numericNodes(manual.json)[0].block.fields.NUM = 14;
  assert.throws(() => session.commitApply(manual, reservation), /检查点期间/);
  assert.deepEqual(session.getUndoableRequestIds(), []); session.cancelReservation(reservation);
  assert.equal(numericNodes(manual.json)[0].value, 14);
});

test('project switch invalidates pending operation and project histories remain independent', async () => {
  const session = mockSession(), first = snapshot(), second = { ...snapshot(), name: 'second' };
  session.begin(first, 'a'.repeat(32)); const staged = session.stage(reply(), 'modify');
  await session.validateNative(first);
  const reservation = session.reserveApply(first, { requestId: 'a'.repeat(32), candidateId: staged.candidateId });
  session.activateProject(second.name);
  assert.throws(() => session.commitApply(first, reservation), /检查点期间/);
  session.begin(first, 'a'.repeat(32)); session.stage(reply(), 'modify'); await session.validateNative(first); const afterA = session.apply(first, 'a'.repeat(32));
  session.begin(second, 'b'.repeat(32)); session.stage(reply(), 'modify'); await session.validateNative(second); const afterB = session.apply(second, 'b'.repeat(32));
  session.activateProject(first.name);
  assert.deepEqual(session.getUndoableRequestIds(), ['a'.repeat(32)]);
  assert.deepEqual(session.undo(afterA, 'a'.repeat(32)), first);
  session.activateProject(second.name);
  assert.deepEqual(session.undo(afterB, 'b'.repeat(32)), second);
});

test('failed loader does not create or remove Undo transaction; explicit restore and editor reload invalidate it', async () => {
  const session = mockSession(), before = snapshot(), requestId = 'a'.repeat(32);
  session.begin(before, requestId); const staged = session.stage(reply(), 'modify');
  await session.validateNative(before);
  const apply = session.reserveApply(before, { requestId, candidateId: staged.candidateId });
  assert.throws(() => session.commitApply(before, apply, () => { throw new Error('load failed'); }), /load failed/);
  assert.deepEqual(session.getUndoableRequestIds(), []); session.cancelReservation(apply);
  const after = session.apply(before, requestId);
  const undo = session.reserveUndo(after, { requestId });
  assert.throws(() => session.commitUndo(after, undo, () => { throw new Error('load failed'); }), /load failed/);
  assert.deepEqual(session.getUndoableRequestIds(), [requestId]); session.cancelReservation(undo);
  session.invalidateProject(before.name); assert.deepEqual(session.getUndoableRequestIds(), []); assert.equal(session.canUndo(after), false);
  assert.deepEqual(new StudioProposalSession().getUndoableRequestIds(before.name), []);
});

test('bounded authoritative Undo IDs expose eviction and preserve chat-independent history', async () => {
  const session = mockSession(); let current = snapshot();
  const ids = [];
  for (let i = 1; i <= 21; i++) {
    const id = i.toString(16).padStart(32, '0'); ids.push(id); session.begin(current, id);
    session.stage(reply({ edits: [{ node_id: 'n6', value: i }] }), 'modify'); await session.validateNative(current); current = session.apply(current, id);
  }
  assert.deepEqual(session.getUndoableRequestIds(), ids.slice(1));
  assert.equal(session.canUndo(current), true); session.reject(); assert.deepEqual(session.getUndoableRequestIds(), ids.slice(1));
  const manual = structuredClone(current); numericNodes(manual.json)[0].block.fields.NUM = 1;
  assert.equal(session.canUndo(manual), false);
  for (let i = 0; i < 20; i++) session.activateProject(`other_${i}`);
  session.activateProject('fixture'); assert.deepEqual(session.getUndoableRequestIds(), []);
});

test('presets carry capabilities and each validates using both real transpilers', () => {
  const ids = new Set();
  for (const p of EFFECT_PRESETS) {
    assert.ok(!ids.has(p.id)); ids.add(p.id); assert.ok(p.name && p.description && p.tags.length && p.capabilities.includes('local_simulation'));
    const value = { ...snapshot(), json: structuredClone(p.blocklyJson) };
    assert.equal(validateCandidate(value).valid, true, p.id);
    if (JSON.stringify(p.blocklyJson).includes('gsi_')) assert.ok(p.requiredInputs.includes('cs2_gsi'));
    if (JSON.stringify(p.blocklyJson).includes('key_decay')) assert.ok(p.requiredInputs.includes('keyboard'));
  }
  assert.ok(ids.has('template_static') && ids.has('template_reactive') && ids.has('template_gradient') && ids.has('kill_wave'));
});
test('diagnostic entry points carry only selected error and its kind', () => {
  for (const kind of ['validation', 'build', 'plugin_load']) {
    const context = assistantContext(snapshot(), 'error_analysis', 'fixture error', kind);
    assert.equal(context.diagnostic_kind, kind); assert.equal(context.diagnostic, 'fixture error');
    assert.deepEqual(Object.keys(context).sort(), ['capabilities', 'diagnostic', 'diagnostic_kind', 'intent', 'name', 'nodes', 'presets', 'publication']);
  }
  assert.equal(assistantContext(snapshot(), 'modify', 'unrelated logs', 'plugin_load').diagnostic, '');
});
