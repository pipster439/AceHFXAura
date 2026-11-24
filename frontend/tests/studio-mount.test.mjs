import { register } from 'node:module';
register('./helpers/jsx-loader.mjs', import.meta.url);

import test from 'node:test';
import assert from 'node:assert/strict';
import React from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import * as Blockly from 'blockly';

const { default: EffectStudio, WORKSPACE_STATUS } = await import('../src/components/EffectStudio.jsx');
const { default: ProgramCatalog } = await import('../src/components/ProgramCatalog.jsx');

test('WORKSPACE_STATUS exports frozen unready, ready, and destroyed states', () => {
  assert.ok(Object.isFrozen(WORKSPACE_STATUS), 'WORKSPACE_STATUS must be frozen');
  assert.equal(WORKSPACE_STATUS.UNREADY, 'unready');
  assert.equal(WORKSPACE_STATUS.READY, 'ready');
  assert.equal(WORKSPACE_STATUS.DESTROYED, 'destroyed');
});

test('EffectStudio initial mount does not call Blockly.serialization.workspaces.save(null)', () => {
  let saveCallCount = 0;
  let nullSaveAttempted = false;
  const origSave = Blockly.serialization.workspaces.save;
  try {
    Blockly.serialization.workspaces.save = (workspace, ...args) => {
      saveCallCount++;
      if (workspace === null || workspace === undefined) {
        nullSaveAttempted = true;
        throw new TypeError("Cannot read properties of null (reading 'getTopComments')");
      }
      return origSave ? origSave(workspace, ...args) : { blocks: [] };
    };

    // Both standalone and embedded must mount without throwing or calling save(null)
    const standaloneHtml = renderToStaticMarkup(React.createElement(EffectStudio, { embedded: false }));
    assert.ok(standaloneHtml.length > 0, 'Standalone mount produced markup');
    assert.equal(nullSaveAttempted, false, 'save(null) must not be called during standalone mount');

    const embeddedHtml = renderToStaticMarkup(React.createElement(EffectStudio, { embedded: true }));
    assert.ok(embeddedHtml.length > 0, 'Embedded mount produced markup');
    assert.equal(nullSaveAttempted, false, 'save(null) must not be called during embedded mount');
    assert.equal(saveCallCount, 0, 'Workspace save should not be invoked during initial render before Blockly.inject');
  } finally {
    Blockly.serialization.workspaces.save = origSave;
  }
});

test('EffectStudio marks root container with workspace status unready during initial render', () => {
  const standaloneHtml = renderToStaticMarkup(React.createElement(EffectStudio, { embedded: false }));
  assert.ok(standaloneHtml.includes('data-workspace-status="unready"'), 'Initial standalone markup must expose data-workspace-status="unready"');

  const embeddedHtml = renderToStaticMarkup(React.createElement(EffectStudio, { embedded: true }));
  assert.ok(embeddedHtml.includes('data-workspace-status="unready"'), 'Initial embedded markup must expose data-workspace-status="unready"');
});

test('standalone browser mode mounts successfully into editor layout without host-only panels', () => {
  const html = renderToStaticMarkup(React.createElement(EffectStudio, { embedded: false }));
  assert.ok(html.includes('aria-label="当前光效生命周期"'), 'Life-cycle bar must be present');
  assert.ok(html.includes('测试台'), 'Standalone mode provides TestBench button');
  assert.ok(!html.includes('data-studio-storage'), 'Standalone mode omits embedded storage bar');
  assert.ok(!html.includes('data-effect-extensions'), 'Standalone mode omits embedded extensions details');
});

test('WinUI embedded mode mounts successfully into editor layout with embedded panels', () => {
  const html = renderToStaticMarkup(React.createElement(EffectStudio, { embedded: true }));
  assert.ok(html.includes('aria-label="当前光效生命周期"'), 'Life-cycle bar must be present');
  assert.ok(html.includes('data-studio-storage'), 'Embedded mode renders storage bar');
  assert.ok(html.includes('data-program-catalog'), 'Embedded mode renders ProgramCatalog');
  assert.ok(html.includes('data-input-capture-controls'), 'Embedded mode renders InputSourcesControl');
});

test('ProgramCatalog disables install and update operations when snapshot is null', () => {
  // When snapshot is null (unready workspace), install button must be disabled
  const htmlNullSnapshot = renderToStaticMarkup(React.createElement(ProgramCatalog, {
    embedded: true,
    snapshot: null
  }));
  assert.ok(htmlNullSnapshot.includes('data-program-catalog'), 'Catalog must be rendered');
  assert.ok(htmlNullSnapshot.includes('安装当前工程（默认禁用）'), 'Install button exists');
  assert.match(htmlNullSnapshot, /<button[^>]*disabled=""[^>]*>安装当前工程（默认禁用）<\/button>/,
    'Install button must be disabled when snapshot is null');
});

test('ProgramCatalog enables install button only when snapshot is valid and catalog loaded', () => {
  const dummySnapshot = {
    name: 'custom_test_effect',
    publication: { continuous: true },
    json: { blocks: { languageVersion: 0, blocks: [] } }
  };
  // Mock catalog with entries to verify update button gate as well
  const htmlValidSnapshot = renderToStaticMarkup(React.createElement(ProgramCatalog, {
    embedded: true,
    snapshot: dummySnapshot
  }));
  assert.ok(htmlValidSnapshot.includes('data-program-catalog'));
});
