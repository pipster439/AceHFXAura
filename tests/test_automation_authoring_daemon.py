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
        return reply.status, json.loads(reply.read() or b"{}"), reply.headers


class TestAutomationAuthoringDaemon(unittest.TestCase):
    def test_v2_transactions_catalog_and_write_protection(self):
        config = {"default_profile": "base", "profiles": {"base": {"type": "static"}, "other": {"type": "static"}},
                  "opaque": {"retain": [None,42]}, "orchestration": {"rules": []}}
        with runtime.TestAutomationReloadDaemon().daemon(config) as (_, _, path):
            deadline=time.monotonic()+8
            while True:
                try:
                    if request(19898,"/api/automation/v2/records")[0]==200: break
                except OSError: pass
                if time.monotonic()>deadline:self.fail("web proxy unavailable")
                time.sleep(.05)
            before=path.read_bytes()
            listing=request(19898,"/api/automation/v2/records")[1]
            catalog=request(19898,"/api/automation/v2/effects")[1]
            self.assertEqual(len(catalog["effects"]),2);self.assertEqual(path.read_bytes(),before)
            rule={"id":"stable","model":"automation_v2","when":{"mode":"state","condition":{"field":"process","value":"notrunning.exe"}},"action":{"type":"activate_profile","profile":"other"}}
            payload={"expected_revision":listing["revision"],"rules":[rule]}
            self.assertEqual(request(19898,"/api/automation/v2/rules","PUT",payload)[0],200)
            self.assertEqual(request(19898,"/api/automation/v2/rules","PUT",payload)[0],409)
            self.assertEqual(json.loads(path.read_text())["opaque"],config["opaque"])
            revision=request(19898,"/api/automation/v2/records")[1]["revision"]
            invalid={**rule,"id":"missing","action":{"type":"trigger_effect","lifetime":"while_true","effect":{"kind":"plugin","name":"unpublished"}}}
            self.assertEqual(request(19897,"/api/automation/v2/rules","POST",{"rule":invalid,"expected_revision":revision})[0],422)
            candidate=json.loads(path.read_text());candidate["orchestration"]["rules"]=[]
            self.assertEqual(request(19898,"/api/config","POST",candidate,{"If-Match":revision})[0],409)
            for port in (19897,19898):
                self.assertEqual(request(port,"/api/automation/v2/rules","PUT",payload,{"Origin":"https://example.com"})[0],403)

    def test_event_witness_validation_on_daemon_and_web(self):
        config={"default_profile":"base","profiles":{"base":{"type":"static"}}}
        with runtime.TestAutomationReloadDaemon().daemon(config) as (_,_,path):
            kill={"event":"event.kill"};health={"field":"player.state.health","op":"<","value":20}
            rule={"id":"witness","model":"automation_v2","when":{"mode":"event"},"action":{"type":"trigger_effect","lifetime":"one_shot","effect":{"kind":"profile_effect","name":"base"}}}
            deadline=time.monotonic()+8
            while True:
                try:
                    if request(19898,"/api/automation/v2/records")[0]==200:break
                except OSError:pass
                if time.monotonic()>deadline:self.fail("web proxy unavailable")
                time.sleep(.05)
            for port in (19897,19898):
                revision=request(port,"/api/automation/v2/records")[1]["revision"]
                before=path.read_bytes()
                for condition,accepted in ((health,False),({"not":kill},False),(kill,True),({"and":[kill,health]},True),({"or":[kill,health]},True)):
                    rule["when"]["condition"]=condition
                    status,body,_=request(port,"/api/automation/v2/validate","POST",{"rule":rule,"expected_revision":revision})
                    self.assertEqual(status,200 if accepted else 422)
                    if not accepted:
                        self.assertIn("positive occurrence",body["message"])
                        self.assertEqual(request(port,"/api/automation/v2/rules","PUT",{"rules":[rule],"expected_revision":revision})[0],422)
                self.assertEqual(path.read_bytes(),before)

    def test_simulation_freshness_projects_owner_evaluation(self):
        config={"default_profile":"base","profiles":{"base":{"type":"static"}},"orchestration":{"automation_freshness_ms":600,"rules":[]}}
        with runtime.TestAutomationReloadDaemon().daemon(config):
            def wait_for(predicate):
                deadline=time.monotonic()+4
                while time.monotonic()<deadline:
                    value=request(19897,"/api/gsi/simulation")[1]
                    if predicate(value):return value
                    time.sleep(.02)
                self.fail("freshness did not transition")
            self.assertEqual(request(19897,"/api/gsi/simulation","POST",{"enabled":True})[0],202)
            fresh=wait_for(lambda v:v["enabled"] and v["freshness"]["fresh"])
            self.assertEqual(fresh["freshness"]["threshold_ms"],600)
            self.assertLessEqual(fresh["freshness"]["age_ms"],600)
            self.assertEqual(request(19897,"/api/gsi/simulation","POST",{"heartbeat":False})[0],202)
            stale=wait_for(lambda v:not v["freshness"]["fresh"])
            self.assertGreater(stale["freshness"]["age_ms"],600)
            self.assertEqual(stale["freshness"]["threshold_ms"],600)
            self.assertEqual(request(19897,"/api/gsi/simulation","POST",{"heartbeat":True})[0],202)
            recovered=wait_for(lambda v:v["freshness"]["fresh"])
            self.assertGreater(recovered["freshness"]["evaluated_at_ms"],stale["freshness"]["evaluated_at_ms"])

    def test_simulation_isolation_and_protections(self):
        config={"default_profile":"base","profiles":{"base":{"type":"static"}}}
        with runtime.TestAutomationReloadDaemon().daemon(config):
            def control(body):
                status,result,_=request(19897,"/api/gsi/simulation","POST",body)
                self.assertEqual(status,202)
                deadline=time.monotonic()+3
                while request(19897,"/api/gsi/simulation")[1]["applied_sequence"]<result["sequence"]:
                    if time.monotonic()>deadline:self.fail("simulation command unprocessed")
                    time.sleep(.02)
            control({"enabled":True,"heartbeat":False})
            self.assertEqual(request(19897,"/gsi","POST",{"player":{"state":{"health":1,"round_kills":99}}})[0],200)
            current=request(19897,"/api/gsi/current")[1]
            self.assertEqual(current["data"]["player.state.health"],100)
            control({"increment_kill":True})
            current=request(19897,"/api/gsi/current")[1]
            self.assertEqual(current["data"]["player.state.round_kills"],1)
            self.assertEqual(request(19897,"/api/gsi/simulation","POST",{"event.kill":True})[0],422)
            self.assertEqual(request(19897,"/api/gsi/simulation","POST",{"health":-1})[0],422)
            self.assertEqual(request(19897,"/api/gsi/simulation","POST",{"health":1},{"Host":"evil.invalid"})[0],403)
            self.assertEqual(request(19897,"/api/gsi/simulation","POST",{"health":1},{"Origin":"https://example.com"})[0],403)
            control({"enabled":False})
            self.assertEqual(request(19897,"/gsi","POST",{"player":{"state":{"health":80,"round_kills":200}},"round":{"bomb":"exploded"}})[0],200)
            self.assertEqual(request(19897,"/api/gsi/current")[1]["data"]["player.state.health"],80)

if __name__ == "__main__":
    unittest.main()
