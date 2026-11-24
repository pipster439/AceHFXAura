import test from "node:test";
import assert from "node:assert/strict";

test("input capture API contract: GET status and POST toggles", async () => {
  const requests = [];
  const oldFetch = globalThis.fetch;
  
  let currentStatus = {
    schema_version: 1,
    consent_scope: "daemon_session",
    audio: {
      enabled: false,
      capture: {
        availability: "disabled",
        frames_captured: 0,
        capture_errors: 0
      }
    },
    screen: {
      enabled: false,
      capture: {
        availability: "disabled",
        frames_captured: 0,
        capture_errors: 0
      }
    }
  };

  globalThis.fetch = async (url, options = {}) => {
    requests.push({ url, options });
    if (url === "/api/inputs" && options.method === "POST") {
      const body = JSON.parse(options.body);
      const updated = structuredClone(currentStatus);
      updated.status = "ok";
      if ("audio" in body) {
        updated.audio.enabled = body.audio;
        updated.audio.capture.availability = body.audio ? "fresh" : "disabled";
      }
      if ("screen" in body) {
        updated.screen.enabled = body.screen;
        updated.screen.capture.availability = body.screen ? "fresh" : "disabled";
      }
      currentStatus = updated;
      return {
        ok: true,
        status: 200,
        json: async () => updated
      };
    }
    return {
      ok: true,
      status: 200,
      json: async () => currentStatus
    };
  };

  try {
    // 1. Fetch initial status
    const getRes = await fetch("/api/inputs", { cache: "no-store" });
    const initial = await getRes.json();
    assert.equal(initial.schema_version, 1);
    assert.equal(initial.audio.enabled, false);
    assert.equal(initial.screen.enabled, false);

    // 2. Enable audio
    const postAudio = await fetch("/api/inputs", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ audio: true })
    });
    const afterAudio = await postAudio.json();
    assert.equal(afterAudio.status, "ok");
    assert.equal(afterAudio.audio.enabled, true);
    assert.equal(afterAudio.screen.enabled, false);

    // 3. Enable screen
    const postScreen = await fetch("/api/inputs", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ screen: true })
    });
    const afterScreen = await postScreen.json();
    assert.equal(afterScreen.status, "ok");
    assert.equal(afterScreen.audio.enabled, true);
    assert.equal(afterScreen.screen.enabled, true);

    // 4. Disable all
    const postDisableAll = await fetch("/api/inputs", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ audio: false, screen: false })
    });
    const afterDisable = await postDisableAll.json();
    assert.equal(afterDisable.status, "ok");
    assert.equal(afterDisable.audio.enabled, false);
    assert.equal(afterDisable.screen.enabled, false);

    assert.equal(requests.length, 4);
  } finally {
    globalThis.fetch = oldFetch;
  }
});
