import test from 'node:test';
import assert from 'node:assert/strict';
import { readHostSettings, listenForHostTheme } from '../src/utils/embeddedHost.js';

test('standalone keeps the existing shell tab and stored theme', () => {
  let reads = 0;
  const storage = { getItem(key) { assert.equal(key, 'aura-theme'); reads++; return 'light'; } };
  assert.deepEqual(readHostSettings('', storage), { embedded: false, tab: 'lighting', theme: 'light' });
  assert.deepEqual(readHostSettings('?tab=automation', storage), { embedded: false, tab: 'automation', theme: 'light' });
  assert.deepEqual(readHostSettings('?host=other&tab=studio', storage), { embedded: false, tab: 'studio', theme: 'light' });
  assert.equal(reads, 3);
});

test('embedded accepts only the canonical Studio/Automation initial tabs and host theme', () => {
  const storage = { getItem() { throw Error('Embedded mode must not read the browser theme'); } };
  assert.deepEqual(readHostSettings('?host=winui&tab=studio&theme=dark', storage), { embedded: true, tab: 'studio', theme: 'dark' });
  assert.deepEqual(readHostSettings('?host=winui&tab=automation&theme=light', storage), { embedded: true, tab: 'automation', theme: 'light' });
  assert.deepEqual(readHostSettings('?host=winui&tab=lighting&theme=wrong', storage), { embedded: true, tab: 'studio', theme: 'dark' });
});

test('the one-way host listener ignores business messages and invalid themes', () => {
  let handler; const received = [];
  const webview = { addEventListener(type, callback) { assert.equal(type, 'message'); handler = callback; },
    removeEventListener(type, callback) { assert.equal(type, 'message'); assert.equal(callback, handler); handler = null; } };
  const stop = listenForHostTheme(webview, theme => received.push(theme));
  for (const message of [{ type: 'save', config: {} }, { type: 'theme_changed', theme: 'system' }, { type: 'theme_changed', theme: '<script>' }]) handler({ data: message });
  handler({ data: { type: 'theme_changed', theme: 'light' } });
  assert.deepEqual(received, ['light']);
  stop(); assert.equal(handler, null);
});
