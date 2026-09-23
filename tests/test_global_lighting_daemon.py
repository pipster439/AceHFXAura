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
            saved["default_profile"]="override"
            temp=path.with_suffix(".next"); temp.write_text(json.dumps(saved));os.replace(temp,path)
            wait_fps(60)
            settings=request(19897,"/api/lighting/global")[1]
            self.assertEqual(patch(10)[0],200)
            self.assertEqual(request(19897,"/api/lighting/profiles/override")[1]["profile"]["fps"],60)
            time.sleep(.2);self.assertEqual(request(19897,"/api/runtime/status")[1]["runtime"]["fps"],60)

if __name__=="__main__":unittest.main()
