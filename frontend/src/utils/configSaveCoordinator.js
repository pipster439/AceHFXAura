/**
 * configSaveCoordinator.js - Manages queued config saves with generation-based invalidation
 *
 * Prevents queued stale saves from inheriting newly fetched revisions when an earlier
 * save triggers an HTTP 409 Conflict.
 */

export class ConfigSaveCoordinator {
  constructor({
    fetchConfigFn,
    onConflict,
    onError,
    onSaveSuccess,
    beforeSave
  } = {}) {
    this.generation = 0;
    this.revision = '';
    this.saveChain = Promise.resolve();
    this.saveSeq = 0;
    this.fetchConfigFn = fetchConfigFn;
    this.onConflict = onConflict;
    this.onError = onError;
    this.onSaveSuccess = onSaveSuccess;
    this.beforeSave = beforeSave;
  }

  getRevision() {
    return this.revision;
  }

  setRevision(etag) {
    if (etag) {
      this.revision = etag.replace(/^"|"$/g, '');
    }
  }

  getGeneration() {
    return this.generation;
  }

  bumpGeneration() {
    this.generation += 1;
    return this.generation;
  }

  /**
   * Enqueues a save operation.
   *
   * 1. Captures current generation at enqueue time.
   * 2. Chains behind any pending save.
   * 3. Once awake, checks if generation has changed. If so, aborts without sending.
   * 4. On HTTP 409:
   *    - Increments generation FIRST (invalidating all queued stale saves).
   *    - Triggers conflict callback.
   *    - Fetches latest config/revision.
   * 5. On HTTP 200:
   *    - Updates revision with new ETag.
   *    - Invokes success callback.
   */
  async saveConfig(newConfig, fetchImpl = fetch) {
    if (!newConfig) return false;

    // Capture current generation at enqueue time
    const capturedGen = this.generation;
    const seq = ++this.saveSeq;

    const previous = this.saveChain;
    let release;
    this.saveChain = new Promise(resolve => { release = resolve; });

    await previous;

    try {
      // Abort without sending if generation is no longer current
      if (capturedGen !== this.generation) {
        return false;
      }

      if (this.beforeSave) {
        await this.beforeSave(newConfig);
      }

      const headers = { 'Content-Type': 'application/json' };
      if (this.revision) {
        headers['If-Match'] = `"${this.revision}"`;
      }

      const res = await fetchImpl('/api/config', {
        method: 'POST',
        headers,
        body: JSON.stringify(newConfig)
      });

      if (res.status === 409) {
        // 1. Invalidate all pre-conflict saves
        this.bumpGeneration();

        try {
          if (this.onConflict) {
            const data = await res.json().catch(() => ({}));
            this.onConflict(data);
          }

          if (this.fetchConfigFn) {
            await this.fetchConfigFn();
          }
        } finally {
          // 2. Invalidate any saves queued DURING reconciliation/reload
          this.bumpGeneration();
        }
        return false;
      }

      if (!res.ok) {
        const data = await res.json().catch(() => ({}));
        if (this.onError) {
          this.onError(data);
        }
        return false;
      }

      const newEtag = res.headers.get('ETag');
      if (newEtag) {
        this.setRevision(newEtag);
      }

      if (this.onSaveSuccess) {
        this.onSaveSuccess(newConfig, seq);
      }
      return true;
    } catch (err) {
      if (this.onError) {
        this.onError({ message: err.message });
      }
      return false;
    } finally {
      release();
    }
  }
}
