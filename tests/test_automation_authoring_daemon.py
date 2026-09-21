"""Actual daemon authoring routes and same-origin web transport, isolated dry-run."""
import json
import time
import unittest
import urllib.error
import urllib.request
import test_automation_reload_daemon as runtime


def request(port, path, method="GET", body=None, headers=None):
    req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", method=method,
        data=None if body is None else json.dumps(body).encode(),
        headers={"Content-Type": "application/json", **(headers or {})})
    try:
        reply = urllib.request.urlopen(req, timeout=10)
    except urllib.error.HTTPError as error:
        reply = error
    with reply:
        return reply.status, json.loads(reply.read()), reply.headers


class TestAutomationAuthoringDaemon(unittest.TestCase):
    def test_authoring_proxy_promotion_and_legacy_writer_guard(self):
        config = {"default_profile": "base", "profiles": {"base": {"type": "static"}, "other": {"type": "static"}},
            "rules": [{"process": "notrunning.exe", "profile": "base", "suppress_web_ui": True}],
            "opaque": {"retain": [None, 42]}, "orchestration": {"opaque": True, "rules": []}}
        with runtime.TestAutomationReloadDaemon().daemon(config) as (_, _, path):
            deadline = time.monotonic() + 8
            while True:
                try:
                    if request(19898, "/api/automation/v2/records")[0] == 200:
                        break
                except OSError:
                    pass
                if time.monotonic() > deadline:
                    self.fail("authoring web proxy unavailable")
                time.sleep(.05)
            initial = path.read_bytes()
            _, listing, _ = request(19898, "/api/automation/v2/records")
            revision = listing["revision"]
            self.assertEqual(listing["records"][0]["provenance"], "application")
            self.assertEqual(path.read_bytes(), initial)
            self.assertEqual(request(19897, "/api/automation/v2/capabilities")[1]["retrigger"], ["restart", "ignore_while_active"])
            rule = {"id": "http-stable", "model": "automation_v2", "when": {"mode": "state", "condition": {"field": "process", "value": "NOTRUNNING"}},
                    "action": {"type": "activate_profile", "profile": "other"}, "future": {"keep": 7}}
            payload = {"rule": rule, "expected_revision": revision, "position": 0}
            self.assertEqual(request(19898, "/api/automation/v2/rules", "POST", payload)[0], 409)
            self.assertEqual(path.read_bytes(), initial)
            payload["acknowledge_shadowing"] = True
            self.assertEqual(request(19898, "/api/automation/v2/validate", "POST", payload)[0], 200)
            self.assertEqual(path.read_bytes(), initial)
            status, created, _ = request(19898, "/api/automation/v2/rules", "POST", payload)
            self.assertEqual(status, 201)
            revision = created["revision"]
            _, updated, _ = request(19898, "/api/automation/v2/rules", "PATCH", {"expected_revision": revision, "id": rule["id"],
                "patch": {"description": "label"}, "acknowledge_shadowing": True})
            self.assertIn("revision", updated)
            self.assertEqual(json.loads(path.read_text())["orchestration"]["rules"][0]["future"], {"keep": 7})
            revision = updated["revision"]
            # The old whole-config endpoint cannot drop V2, even with a current ETag.
            candidate = json.loads(path.read_text())
            candidate["orchestration"]["rules"] = []
            before = path.read_bytes()
            status, error, _ = request(19898, "/api/config", "POST", candidate, {"If-Match": f'"{revision}"'})
            self.assertEqual(status, 409); self.assertEqual(error["error"], "v2_authoring_required"); self.assertEqual(path.read_bytes(), before)
            candidate = json.loads(before); candidate["unrelated"] = "allowed"
            self.assertEqual(request(19898, "/api/config", "POST", candidate, {"If-Match": f'"{revision}"'})[0], 200)
            revision = request(19898, "/api/automation/v2/records")[1]["revision"]
            # Phase 4 remains its old, sparse, revision-safe API through the proxy.
            status, phase4, _ = request(19898, "/api/automation/rules/0", "PATCH", {"expected_revision": revision, "rule": {"profile": "other"}})
            self.assertEqual(status, 200)
            self.assertTrue(json.loads(path.read_text())["rules"][0]["suppress_web_ui"])
            self.assertEqual(json.loads(path.read_text())["orchestration"]["rules"][0], json.loads(before)["orchestration"]["rules"][0])
            revision = request(19898, "/api/automation/v2/records")[1]["revision"]
            _, deleted, _ = request(19898, "/api/automation/v2/rules", "DELETE", {"id": rule["id"], "expected_revision": revision})
            revision = deleted["revision"]
            before = path.read_bytes()
            status, response, _ = request(19898, "/api/automation/v2/promotions/propose", "POST", {"id": "promoted", "source_index": 0,
                "expected_revision": revision, "position": 0})
            self.assertEqual(status, 200); self.assertEqual(path.read_bytes(), before)
            proposal = response["proposal"]
            self.assertTrue(proposal["proposed_rule"]["dnd"])
            status, _, _ = request(19898, "/api/automation/v2/promotions/commit", "POST", {"expected_revision": revision, "proposal": proposal, "confirm": True})
            self.assertEqual(status, 200)
            final = json.loads(path.read_text())
            self.assertEqual(final["rules"], []); self.assertEqual(final["orchestration"]["rules"][0]["id"], "promoted")
            self.assertEqual(final["opaque"], config["opaque"])
            self.assertEqual(request(19898, "/api/automation/v2/promotions/commit", "POST", {"expected_revision": revision, "proposal": proposal, "confirm": True})[0], 409)
            # CSRF protections run at both HTTP surfaces.
            self.assertEqual(request(19897, "/api/automation/v2/rules", "POST", payload, {"Origin": "https://example.com"})[0], 403)
            self.assertEqual(request(19898, "/api/automation/v2/rules", "POST", payload, {"Origin": "https://example.com"})[0], 403)


if __name__ == "__main__":
    unittest.main()
