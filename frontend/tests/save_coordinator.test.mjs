import test from 'node:test';
import assert from 'node:assert/strict';
import { ConfigSaveCoordinator } from '../src/utils/configSaveCoordinator.js';

test('queued stale save cannot inherit revision obtained after earlier request receives 409', async () => {
  const postCalls = [];
  let currentServerRevision = 'R1';

  // Mock fetch implementation
  const mockFetch = async (url, options) => {
    if (url === '/api/config' && options?.method === 'POST') {
      const ifMatch = options.headers?.['If-Match']?.replace(/^"|"$/g, '');
      const body = JSON.parse(options.body);
      postCalls.push({ ifMatch, body });

      if (ifMatch !== currentServerRevision) {
        return {
          status: 409,
          ok: false,
          json: async () => ({ error: 'revision_conflict', message: 'Conflict' })
        };
      }
      return {
        status: 200,
        ok: true,
        headers: {
          get: (name) => name.toLowerCase() === 'etag' ? `"${currentServerRevision}"` : null
        },
        json: async () => ({ status: 'ok' })
      };
    }
    throw new Error(`Unexpected fetch: ${url}`);
  };

  const coordinator = new ConfigSaveCoordinator();
  coordinator.setRevision('R1');

  // Reload function that simulates reloading R2 on conflict
  coordinator.fetchConfigFn = async () => {
    coordinator.setRevision('R2');
  };

  // External writer changes server config revision to R2
  currentServerRevision = 'R2';

  // Save A and Save B are both queued while client is on R1
  const promiseA = coordinator.saveConfig({ payload: 'A_from_R1' }, mockFetch);
  const promiseB = coordinator.saveConfig({ payload: 'B_from_R1' }, mockFetch);

  const [resA, resB] = await Promise.all([promiseA, promiseB]);

  assert.equal(resA, false, 'Save A should fail due to 409');
  assert.equal(resB, false, 'Save B should be aborted without sending because generation changed');

  // Verify that only Save A attempted POST, and Save B NEVER posted using If-Match: R2!
  assert.equal(postCalls.length, 1, 'Save B must NOT execute fetch after Save A received 409');
  assert.equal(postCalls[0].ifMatch, 'R1', 'Save A sent If-Match: R1');
  assert.equal(postCalls[0].body.payload, 'A_from_R1');

  // Now verify that a NEW save queued AFTER the reload belongs to the new generation
  const promiseC = coordinator.saveConfig({ payload: 'C_from_R2' }, mockFetch);
  const resC = await promiseC;

  assert.equal(resC, true, 'Save C from new generation should succeed');
  assert.equal(postCalls.length, 2, 'Save C should have executed POST');
  assert.equal(postCalls[1].ifMatch, 'R2', 'Save C sent If-Match: R2');
  assert.equal(postCalls[1].body.payload, 'C_from_R2');
});

test('queued full-document snapshot cannot inherit revision after an earlier successful save', async () => {
  const posts = [];
  let conflicts = 0;
  let reloads = 0;
  const coordinator = new ConfigSaveCoordinator({
    onConflict: () => { conflicts += 1; },
    fetchConfigFn: async () => { reloads += 1; }
  });
  coordinator.setRevision('R1');
  const fetchImpl = async (_url, options) => {
    posts.push({ revision: options.headers['If-Match'], body: JSON.parse(options.body) });
    return { status: 200, ok: true, headers: { get: () => '"R2"' } };
  };
  const first = coordinator.saveConfig({ profiles: { desktop: { brightness: 0.5 } } }, fetchImpl);
  const stale = coordinator.saveConfig({ profiles: { desktop: { brightness: 1.0 } } }, fetchImpl);
  assert.equal(await first, true);
  assert.equal(await stale, false);
  assert.equal(posts.length, 1);
  assert.equal(posts[0].revision, '"R1"');
  assert.equal(conflicts, 1);
  assert.equal(reloads, 1);
});

test('multiple queued stale saves are all invalidated upon 409 conflict', async () => {
  const postCalls = [];
  let currentServerRevision = 'rev_initial';

  const mockFetch = async (url, options) => {
    const ifMatch = options.headers?.['If-Match']?.replace(/^"|"$/g, '');
    const body = JSON.parse(options.body);
    postCalls.push({ ifMatch, body });

    if (ifMatch !== currentServerRevision) {
      return {
        status: 409,
        ok: false,
        json: async () => ({ error: 'revision_conflict' })
      };
    }
    return {
      status: 200,
      ok: true,
      headers: {
        get: () => `"${currentServerRevision}"`
      },
      json: async () => ({})
    };
  };

  const coordinator = new ConfigSaveCoordinator();
  coordinator.setRevision('rev_initial');

  coordinator.fetchConfigFn = async () => {
    coordinator.setRevision('rev_external_update');
  };

  currentServerRevision = 'rev_external_update';

  // Queue saves 1, 2, 3
  const p1 = coordinator.saveConfig({ id: 1 }, mockFetch);
  const p2 = coordinator.saveConfig({ id: 2 }, mockFetch);
  const p3 = coordinator.saveConfig({ id: 3 }, mockFetch);

  const results = await Promise.all([p1, p2, p3]);

  assert.deepEqual(results, [false, false, false]);
  assert.equal(postCalls.length, 1, 'Only the first save attempted network call before 409');
  assert.equal(postCalls[0].ifMatch, 'rev_initial');
});

test('sequential successful saves update revision via returned ETag', async () => {
  const postCalls = [];
  let serverRev = 'v1';

  const mockFetch = async (url, options) => {
    const ifMatch = options.headers?.['If-Match']?.replace(/^"|"$/g, '');
    postCalls.push(ifMatch);
    if (ifMatch === 'v1') {
      serverRev = 'v2';
      return {
        status: 200,
        ok: true,
        headers: { get: () => '"v2"' },
        json: async () => ({})
      };
    } else if (ifMatch === 'v2') {
      serverRev = 'v3';
      return {
        status: 200,
        ok: true,
        headers: { get: () => '"v3"' },
        json: async () => ({})
      };
    }
    return { status: 409, ok: false, json: async () => ({}) };
  };

  const coordinator = new ConfigSaveCoordinator();
  coordinator.setRevision('v1');

  const r1 = await coordinator.saveConfig({ step: 1 }, mockFetch);
  assert.equal(r1, true);
  assert.equal(coordinator.getRevision(), 'v2');

  const r2 = await coordinator.saveConfig({ step: 2 }, mockFetch);
  assert.equal(r2, true);
  assert.equal(coordinator.getRevision(), 'v3');

  assert.deepEqual(postCalls, ['v1', 'v2']);
});

test('save queued during reconciliation reload is aborted without inheriting new revision', async () => {
  const postCalls = [];
  let currentServerRevision = 'R2';

  const mockFetch = async (url, options) => {
    if (url === '/api/config' && options?.method === 'POST') {
      const ifMatch = options.headers?.['If-Match']?.replace(/^"|"$/g, '');
      const body = JSON.parse(options.body);
      postCalls.push({ ifMatch, body });

      if (ifMatch !== currentServerRevision) {
        return {
          status: 409,
          ok: false,
          json: async () => ({ error: 'revision_conflict' })
        };
      }
      return {
        status: 200,
        ok: true,
        headers: {
          get: (name) => name.toLowerCase() === 'etag' ? `"${currentServerRevision}"` : null
        },
        json: async () => ({ status: 'ok' })
      };
    }
    throw new Error(`Unexpected fetch: ${url}`);
  };

  const coordinator = new ConfigSaveCoordinator();
  coordinator.setRevision('R1');

  let resolveReload;
  const reloadPromise = new Promise((resolve) => { resolveReload = resolve; });

  coordinator.fetchConfigFn = async () => {
    // Deliberately hold reload pending until C is queued
    await reloadPromise;
    coordinator.setRevision('R2');
  };

  // 1. A posts R1 -> 409
  const promiseA = coordinator.saveConfig({ payload: 'A_stale_R1' }, mockFetch);

  // Give microtasks a turn to allow A to reach 409 and start fetchConfigFn
  await new Promise((r) => setTimeout(r, 10));

  // 2. C is enqueued while fetchConfigFn is pending
  const promiseC = coordinator.saveConfig({ payload: 'C_queued_during_reconciliation' }, mockFetch);

  // 3. Complete reconciliation/reload
  resolveReload();

  const [resA, resC] = await Promise.all([promiseA, promiseC]);

  assert.equal(resA, false, 'Save A failed due to 409');
  assert.equal(resC, false, 'Save C queued during reconciliation must abort without sending');

  // Verify postCalls only contains Save A
  assert.equal(postCalls.length, 1, 'Save C must not have performed POST');
  assert.equal(postCalls[0].ifMatch, 'R1');

  // 4. Save D queued after reconciliation completes may POST with R2
  const resD = await coordinator.saveConfig({ payload: 'D_after_reconciliation' }, mockFetch);
  assert.equal(resD, true, 'Save D queued after reconciliation should succeed');
  assert.equal(postCalls.length, 2, 'Save D executed POST');
  assert.equal(postCalls[1].ifMatch, 'R2');
  assert.equal(postCalls[1].body.payload, 'D_after_reconciliation');
});

