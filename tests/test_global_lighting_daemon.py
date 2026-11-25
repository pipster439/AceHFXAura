"""Actual global FPS API and runtime hot reload; always isolated dry-run."""
import json
import os
import time
import unittest
import test_automation_reload_daemon as runtime
from test_automation_authoring_daemon import request

class TestGlobalLightingDaemon(unittest.TestCase):
    def test_global_fps_validation_revision_preservation_and_hot_reload(self):
        config = {"default_profile":"base","profiles":{"base":{"type":"static"},"override":{"type":"static","fps":60}},"opaque":{"keep":[1,None]}}
        with runtime.TestAutomationReloadDaemon().daemon(config) as (_,_,path):
            code, settings, _ = request(19897,"/api/lighting/global")
            self.assertEqual(code,200); self.assertEqual(settings["api_version"],1); self.assertEqual(settings["fps"],25)
            profile=request(19897,"/api/lighting/profiles/base")[1]
            self.assertEqual(request(19897,"/api/lighting/profiles/base","PATCH",
                {"expected_revision":profile["revision"],"fps":2**64})[0],400)
            original=path.read_bytes()
            def patch(value,**extra):
                return request(19897,"/api/lighting/global","PATCH",{"expected_revision":settings["revision"],"fps":value,**extra})
            for value in (9,101,20.5,None,True,"25",2**64): self.assertEqual(patch(value)[0],400)
            self.assertEqual(patch(40,profiles={})[0],400)
            self.assertEqual(path.read_bytes(),original)
            for origin in ("https://example.com","http://localhost.evil","http://127.0.0.1.evil","http://localhost:123@evil"):
                self.assertEqual(request(19897,"/api/lighting/global","PATCH",{"expected_revision":settings["revision"],"fps":40},{"Origin":origin})[0],403)
            self.assertEqual(patch(40)[0],200)
            self.assertEqual(patch(50)[0],409)
            saved=json.loads(path.read_bytes()); self.assertEqual(saved["profiles"],config["profiles"]); self.assertEqual(saved["opaque"],config["opaque"])
            def wait_fps(fps):
                deadline=time.monotonic()+5
                while request(19897,"/api/runtime/status")[1]["runtime"]["fps"] != fps:
                    self.assertLess(time.monotonic(),deadline);time.sleep(.05)
            wait_fps(40)
            invalid=dict(saved); invalid["fps"]="bad"
            temp=path.with_suffix(".invalid"); temp.write_text(json.dumps(invalid)); os.replace(temp,path)
            deadline=time.monotonic()+5
            while request(19897,"/api/runtime/status")[1].get("config",{}).get("healthy",True):
                self.assertLess(time.monotonic(),deadline);time.sleep(.05)
            failed=request(19897,"/api/runtime/status")[1]
            self.assertIn("/fps",failed["config"]["last_error"])
            self.assertEqual(failed["runtime"]["fps"],40)
            temp=path.with_suffix(".recovered"); temp.write_text(json.dumps(saved)); os.replace(temp,path)
            deadline=time.monotonic()+5
            while not request(19897,"/api/runtime/status")[1]["config"]["healthy"]:
                self.assertLess(time.monotonic(),deadline);time.sleep(.05)
            saved["default_profile"]="override"
            temp=path.with_suffix(".next"); temp.write_text(json.dumps(saved));os.replace(temp,path)
            wait_fps(60)
            settings=request(19897,"/api/lighting/global")[1]
            self.assertEqual(patch(10)[0],200)
            self.assertEqual(request(19897,"/api/lighting/profiles/override")[1]["profile"]["fps"],60)
            time.sleep(.2);self.assertEqual(request(19897,"/api/runtime/status")[1]["runtime"]["fps"],60)

    def test_switch_default_lighting_profile_endpoint(self):
        config = {
            "default_profile": "desktop",
            "profiles": {
                "desktop": {"type": "static", "color": [0, 128, 255]},
                "combat": {"type": "breathing", "color": [255, 40, 40], "period_ms": 1800},
            },
        }
        with runtime.TestAutomationReloadDaemon().daemon(config) as (_, _, path):
            code, lst, _ = request(19897, "/api/lighting/profiles")
            self.assertEqual(code, 200)
            rev = lst["revision"]
            self.assertEqual(request(19897, "/api/lighting/default-profile", "PATCH",
                                     {"expected_revision": rev, "default_profile": "missing"})[0], 404)
            code, switched, _ = request(19897, "/api/lighting/default-profile", "PATCH",
                                          {"expected_revision": rev, "default_profile": "combat"})
            self.assertEqual(code, 200)
            self.assertEqual(switched["default_profile"], "combat")
            self.assertEqual(request(19897, "/api/lighting/default-profile", "PATCH",
                                     {"expected_revision": rev, "default_profile": "desktop"})[0], 409)
            saved = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(saved["default_profile"], "combat")
            deadline = time.monotonic() + 5
            while request(19897, "/api/runtime/status")[1]["runtime"]["active_profile"] != "combat":
                self.assertLess(time.monotonic(), deadline)
                time.sleep(0.05)

    def test_hall_lighting_overlay_endpoint_validation_and_reload(self):
        config = {"default_profile": "desktop", "profiles": {"desktop": {"type": "static", "color": [0, 128, 255]}}}
        with runtime.TestAutomationReloadDaemon().daemon(config) as (_, _, path):
            code, cur, _ = request(19897, "/api/lighting/hall-overlay")
            self.assertEqual(code, 200)
            self.assertFalse(cur["hall_lighting"]["enabled"])
            rev = cur["revision"]
            self.assertEqual(request(19897, "/api/lighting/hall-overlay", "PATCH", {"expected_revision": rev, "mode": "bad"})[0], 400)
            code, updated, _ = request(19897, "/api/lighting/hall-overlay", "PATCH",
                                       {"expected_revision": rev, "enabled": True, "mode": "halo", "color": [120, 240, 255], "intensity": 0.75, "min_depth_mm": 0.2})
            self.assertEqual(code, 200)
            self.assertTrue(updated["hall_lighting"]["enabled"])
            self.assertEqual(updated["hall_lighting"]["mode"], "halo")
            self.assertEqual(request(19897, "/api/lighting/hall-overlay", "PATCH", {"expected_revision": rev, "enabled": False})[0], 409)
            saved = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(saved["hall_lighting"]["mode"], "halo")

if __name__=="__main__":unittest.main()
