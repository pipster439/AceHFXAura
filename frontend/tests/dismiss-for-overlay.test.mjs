import test from 'node:test';
import assert from 'node:assert/strict';
import { dismissForOverlay } from '../src/blockly/dismissForOverlay.js';

test('opening a host overlay closes Blockly selection, flyout and chaff without disposing workspace', () => {
  const calls = [];
  const workspace = {
    getToolbox: () => ({ clearSelection: () => calls.push('clearSelection'),
      getFlyout: () => ({ hide: () => calls.push('hideFlyout') }) }),
    getFlyout: () => { throw new Error('toolbox flyout should be used'); },
    hideChaff: () => calls.push('hideChaff'),
    dispose: () => { throw new Error('workspace must stay mounted'); },
  };
  dismissForOverlay(workspace)();
  assert.deepEqual(calls, ['clearSelection', 'hideFlyout', 'hideChaff']);
});

test('an unmounted or toolbox-free editor can still dismiss transient Blockly UI', () => {
  dismissForOverlay(null);
  const calls = [];
  const restore = dismissForOverlay({ getToolbox: () => null,
    getFlyout: () => ({ isVisible: () => true, setVisible: visible => calls.push(`visible:${visible}`) }),
    hideChaff: popupsOnly => calls.push(`hideChaff:${popupsOnly}`) });
  assert.deepEqual(calls, ['hideChaff:true', 'visible:false']);
  restore();
  assert.deepEqual(calls, ['hideChaff:true', 'visible:false', 'visible:true']);
});

test('a previously hidden permanent flyout stays hidden when the host panel closes', () => {
  const values = [];
  const restore = dismissForOverlay({ getToolbox: () => null, hideChaff: () => {},
    getFlyout: () => ({ isVisible: () => false, setVisible: value => values.push(value) }) });
  restore();
  assert.deepEqual(values, [false]);
});
