import Blockly, { loadSafeWorkspaceJson } from '../blockly/index.js';
import { JsTranspiler, PREVIEW_68_KEYS } from '../blockly/jsTranspiler.js';
import { normalizePublication } from '../blockly/publication.js';
import { deriveEffectCapabilities, CAPABILITY_DIAGNOSTICS } from './effectCapabilities.js';
import { DISCRETE_GSI_STATES, NUMERIC_GSI_STATES, BOOLEAN_GSI_STATES } from '../constants/gsiDictionary.js';

const clone = value => JSON.parse(JSON.stringify(value));
const keys = new Set(PREVIEW_68_KEYS.map(k => k.name));
const numbers = new Set(NUMERIC_GSI_STATES.map(s => s.key));
const booleans = new Set(BOOLEAN_GSI_STATES.map(s => s.key));
const enums = new Map(DISCRETE_GSI_STATES.map(s => [s.key, s.options.map(o => o[1])]));
export const BENCH_LIMITS = Object.freeze({ bytes: 65536, duration: 60000, events: 512, assertions: 128 });
export function exactFields(value, required, optional = []) {
  if (!value || Array.isArray(value) || typeof value !== 'object' || required.some(k => !Object.hasOwn(value, k)) ||
      Object.keys(value).some(k => !required.includes(k) && !optional.includes(k))) throw new Error('字段缺失或包含未知字段');
}
function key(value) { if (!keys.has(value)) throw new Error('未知预览键位'); }
function processName(value) {
  if (typeof value !== 'string' || !/^[A-Za-z0-9_. -]{1,64}$/.test(value) || value.includes('..')) throw new Error('前台进程仅接受名称，不接受路径或代码');
}
export function gsiValue(field, value) {
  if (numbers.has(field) && typeof value === 'number' && Number.isFinite(value) && value >= 0 && value <= 1000000 &&
      (!['player.state.health', 'player.state.armor'].includes(field) || value <= 100)) return;
  if (booleans.has(field) && typeof value === 'boolean') return;
  if (enums.get(field)?.includes(value)) return;
  throw new Error('不支持的 GSI 字段或数值');
}
function patch(value) {
  if (!value || Array.isArray(value) || typeof value !== 'object' || Object.keys(value).length > 32) throw new Error('GSI patch 无效');
  for (const [field, val] of Object.entries(value)) gsiValue(field, val);
}
function initial(value) {
  exactFields(value, ['held_keys', 'gsi', 'foreground_process']);
  if (!Array.isArray(value.held_keys) || value.held_keys.length > 68 || new Set(value.held_keys).size !== value.held_keys.length) throw new Error('初始按键无效');
  value.held_keys.forEach(key); patch(value.gsi); processName(value.foreground_process);
}
function time(value, duration) { if (!Number.isInteger(value) || value < 0 || value > duration) throw new Error('时间必须是场景范围内的非负整数'); }
function assertion(a, duration) {
  if (a.type === 'completes') exactFields(a, ['type']);
  else if (a.type === 'frame_count') { exactFields(a, ['type', 'value']); if (!Number.isInteger(a.value) || a.value < 1 || a.value > 4000) throw new Error('帧数断言无效'); }
  else if (a.type === 'sample') {
    exactFields(a, ['type', 'at_ms', 'key', 'rgb']); time(a.at_ms, duration); key(a.key);
    if (!Array.isArray(a.rgb) || a.rgb.length !== 3 || a.rgb.some(v => !Number.isInteger(v) || v < 0 || v > 255)) throw new Error('RGB 断言无效');
  } else if (a.type === 'state') {
    exactFields(a, ['type', 'at_ms', 'field', 'value']); time(a.at_ms, duration);
    if (a.field === 'foreground_process') processName(a.value);
    else if (a.field?.startsWith('key:')) { key(a.field.slice(4)); if (typeof a.value !== 'boolean') throw new Error('按键状态断言无效'); }
    else gsiValue(a.field, a.value);
  } else throw new Error('未知断言类型');
}
export function parseScenario(input) {
  const text = typeof input === 'string' ? input : JSON.stringify(input);
  if (typeof text !== 'string' || new TextEncoder().encode(text).length > BENCH_LIMITS.bytes) throw new Error('场景超过 64 KiB');
  const s = JSON.parse(text);
  exactFields(s, ['schema_version', 'name', 'duration_ms', 'events'], ['frame_ms', 'initial_state', 'assertions']);
  if (s.schema_version !== 1 || typeof s.name !== 'string' || !/^[\p{L}\p{N}_ -]{1,64}$/u.test(s.name)) throw new Error('场景版本或名称无效');
  time(s.duration_ms, BENCH_LIMITS.duration); if (!s.duration_ms) throw new Error('场景时长必须大于零');
  s.frame_ms ??= 40;
  if (!Number.isInteger(s.frame_ms) || s.frame_ms < 20 || s.frame_ms > 1000) throw new Error('帧间隔须为 20–1000 ms');
  s.initial_state ??= { held_keys: [], gsi: { 'player.state.health': 100, 'player.state.round_kills': 0 }, foreground_process: 'app_a.exe' };
  initial(s.initial_state);
  if (!Array.isArray(s.events) || s.events.length > BENCH_LIMITS.events) throw new Error('事件数量超过限制');
  let previous = -1;
  for (const e of s.events) {
    time(e.at_ms, s.duration_ms); if (e.at_ms < previous) throw new Error('事件时间必须按顺序排列'); previous = e.at_ms;
    if (e.type === 'key_down' || e.type === 'key_up') { exactFields(e, ['at_ms', 'type', 'key']); key(e.key); }
    else if (e.type === 'gsi_patch') { exactFields(e, ['at_ms', 'type', 'values']); patch(e.values); }
    else if (e.type === 'foreground_process') { exactFields(e, ['at_ms', 'type', 'process']); processName(e.process); }
    else if (e.type === 'timeline' || e.type === 'reset') exactFields(e, ['at_ms', 'type']);
    else throw new Error('未知场景事件');
  }
  s.assertions ??= [];
  if (!Array.isArray(s.assertions) || s.assertions.length > BENCH_LIMITS.assertions) throw new Error('断言数量超过限制');
  s.assertions.forEach(a => assertion(a, s.duration_ms));
  return s;
}
export const exportScenario = value => JSON.stringify(parseScenario(value), null, 2);
function setField(object, path, value) {
  const parts = path.split('.'); let target = object;
  for (const p of parts.slice(0, -1)) target = target[p] ??= {};
  target[parts.at(-1)] = value;
}
function getField(object, path) { return path.split('.').reduce((value, p) => value?.[p], object); }
export function compileBenchProject(project) {
  const manifest = deriveEffectCapabilities(project);
  if (!manifest.features.includes('simulation_supported')) throw new Error(manifest.diagnostics.map(c => CAPABILITY_DIAGNOSTICS[c]).join(' '));
  if (!project?.json || new TextEncoder().encode(JSON.stringify(project)).length > 262144) throw new Error('工程无效或超过 256 KiB');
  const group = Blockly.Events.getGroup(), undo = Blockly.Events.getRecordUndo(); const workspace = new Blockly.Workspace();
  try { loadSafeWorkspaceJson(project.json, workspace, true); return JsTranspiler.compile(workspace, normalizePublication(project.publication)); }
  finally { workspace.dispose(); Blockly.Events.setGroup(group); Blockly.Events.setRecordUndo(undo); }
}
// The scheduler supplies only explicit monotonic scenario times. UI timers choose
// when to call step; they never supply renderer time or data. No transport exists.
export class StudioTestBench {
  constructor(project, scenario) {
    this.project = clone(project); this.scenario = parseScenario(scenario);
    const times = new Set([0, this.scenario.duration_ms, ...this.scenario.events.map(e => e.at_ms), ...this.scenario.assertions.filter(a => 'at_ms' in a).map(a => a.at_ms)]);
    for (let t = 0; t <= this.scenario.duration_ms; t += this.scenario.frame_ms) times.add(t);
    this.times = [...times].sort((a, b) => a - b); this.reset();
  }
  resetInputs() {
    const start = this.scenario.initial_state;
    this.held = new Set(start.held_keys); this.gsi = {}; this.process = start.foreground_process;
    for (const [field, value] of Object.entries(start.gsi)) setField(this.gsi, field, value);
    this.render = compileBenchProject(this.project);
  }
  reset() { this.index = 0; this.eventIndex = 0; this.origin = 0; this.frameCount = 0; this.results = []; this.resetInputs(); return this.step(); }
  step() {
    if (this.index >= this.times.length) return this.current;
    const t = this.times[this.index++];
    while (this.eventIndex < this.scenario.events.length && this.scenario.events[this.eventIndex].at_ms <= t) {
      const e = this.scenario.events[this.eventIndex++];
      if (e.type === 'key_down') this.held.add(e.key);
      if (e.type === 'key_up') this.held.delete(e.key);
      if (e.type === 'gsi_patch') for (const [field, value] of Object.entries(e.values)) setField(this.gsi, field, value);
      if (e.type === 'foreground_process') this.process = e.process;
      if (e.type === 'reset') { this.resetInputs(); this.origin = t; }
    }
    // Existing preview renderer consumes a 0..1 key-energy map. This bench
    // maps held/released input to 1/0; it does not invent a second decay model.
    const energy = Object.fromEntries([...this.held].map(k => [k, 1]));
    const frame = clone(this.render(t - this.origin, this.gsi, energy));
    if (!Array.isArray(frame) || frame.length !== 68 || frame.some(rgb => rgb.length !== 3 || rgb.some(v => !Number.isFinite(v) || v < 0 || v > 255))) throw new Error('模拟帧无效');
    this.frameCount++;
    this.current = { time_ms: t, frame, held_keys: [...this.held].sort(), gsi: clone(this.gsi), foreground_process: this.process, completed: this.index === this.times.length, frame_count: this.frameCount };
    for (const a of this.scenario.assertions.filter(a => a.at_ms === t)) {
      const actual = a.type === 'sample' ? frame[PREVIEW_68_KEYS.findIndex(k => k.name === a.key)] :
        a.field === 'foreground_process' ? this.process : a.field.startsWith('key:') ? this.held.has(a.field.slice(4)) : getField(this.gsi, a.field);
      this.results.push({ assertion: a, passed: JSON.stringify(actual) === JSON.stringify(a.type === 'sample' ? a.rgb : a.value), actual });
    }
    if (this.current.completed) for (const a of this.scenario.assertions.filter(a => !('at_ms' in a)))
      this.results.push({ assertion: a, passed: a.type === 'completes' || this.frameCount === a.value, actual: a.type === 'completes' ? true : this.frameCount });
    return clone(this.current);
  }
  run() { while (!this.current.completed) this.step(); return { ...clone(this.current), assertions: clone(this.results), passed: this.results.every(a => a.passed) }; }
}
const scenario = (name, duration_ms, events, assertions = [{ type: 'completes' }]) => parseScenario({ schema_version: 1, name, duration_ms, events, assertions });
export const BUILTIN_SCENARIOS = [
  scenario('单键轻按', 400, [{ at_ms: 80, type: 'key_down', key: 'W' }, { at_ms: 160, type: 'key_up', key: 'W' }]),
  scenario('按住后释放', 1200, [{ at_ms: 80, type: 'key_down', key: 'W' }, { at_ms: 800, type: 'key_up', key: 'W' }]),
  scenario('快速重复按键', 640, Array.from({ length: 8 }, (_, i) => ({ at_ms: 80 + i * 60, type: i % 2 ? 'key_up' : 'key_down', key: 'W' }))),
  scenario('CS2 生命值变化', 1200, [100, 50, 10].map((health, i) => ({ at_ms: i * 400, type: 'gsi_patch', values: { 'player.state.health': health } }))),
  scenario('CS2 本回合击杀状态', 1200, [0, 1, 2].map((kills, i) => ({ at_ms: i * 400, type: 'gsi_patch', values: { 'player.state.round_kills': kills } }))),
  scenario('前台进程切换', 800, [{ at_ms: 400, type: 'foreground_process', process: 'app_b.exe' }], [{ type: 'state', at_ms: 400, field: 'foreground_process', value: 'app_b.exe' }, { type: 'completes' }])
];
