"""Real web/daemon contracts and package smoke; no physical hardware or Steam writes."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from test_runtime_entrypoints import (get_authoritative_binary, get_free_port, get_isolated_env,
    terminate_proc, wait_for_http_ready, check_daemon_prerequisites_or_skip)
from test_automation_authoring_daemon import request


class TestGsiConfigurationContract(unittest.TestCase):
    def test_real_backend_install_conflict_whitelist_and_atomic_failure(self):
        binary = get_authoritative_binary("aura_web_ui.exe")
        with tempfile.TemporaryDirectory(prefix="aura-cfg-") as temp:
            root = Path(temp); cfg = root / "config.json"; cfg.write_text('{}')
            target = root / "allowed"; target.mkdir()
            (target / "gamestate_integration_test.cfg").write_text("whitelist fixture")
            denied = root / "denied"; denied.mkdir()
            port = get_free_port()
            proc = subprocess.Popen([binary,"--port",str(port),"--config",str(cfg)],cwd=root,
                env=get_isolated_env(temp),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            try:
                self.assertTrue(wait_for_http_ready(port))
                status, value, _ = request(port,"/api/gsi/cfg")
                self.assertEqual(status,200); self.assertEqual(value["gsi_api_version"],1)
                self.assertIsInstance(value["paths"],list)
                body = {"target_dir":str(target),"expected_cfg_revision":"missing"}
                self.assertEqual(request(port,"/api/gsi/install-cfg","POST",body,{"Origin":"https://example.com"})[0],403)
                self.assertEqual(request(port,"/api/gsi/install-cfg","POST",{"target_dir":str(denied)})[0],400)
                self.assertEqual(request(port,"/api/gsi/install-cfg","POST",body)[0],200)
                installed = target / "gamestate_integration_aura.cfg"
                before = installed.read_bytes(); self.assertIn(b"127.0.0.1:19897",before)
                self.assertEqual(request(port,"/api/gsi/install-cfg","POST",body)[0],409)
                self.assertEqual(installed.read_bytes(),before)
                # Replacement failure must preserve the previous file, not truncate it.
                os.chmod(installed,0o444)
                try: self.assertEqual(request(port,"/api/gsi/install-cfg","POST",{"target_dir":str(target)})[0],500)
                finally: os.chmod(installed,0o666)
                self.assertEqual(installed.read_bytes(),before)
                self.assertFalse(list(target.glob("*.tmp-*")))
            finally: terminate_proc(proc)


class TestPackagedRuntime(unittest.TestCase):
    def test_fresh_package_uses_small_canonical_config(self):
        package = os.environ.get("AURA_PACKAGE_DIR")
        if not package: self.skipTest("Set AURA_PACKAGE_DIR to the final extracted WinUI package")
        check_daemon_prerequisites_or_skip(self)
        payload = Path(package).resolve() / "runtime-payload"
        with tempfile.TemporaryDirectory(prefix="aura-fresh-package-") as temp:
            root = Path(temp)
            proc = subprocess.Popen([str(payload/"aura_daemon.exe"), "--dry-run",
                "--keymap", str(payload/"calibrated_keymap.json"), "--runtime-root", str(payload)],
                cwd=root, env=get_isolated_env(temp), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                self.assertTrue(wait_for_http_ready(19897, path="/api/runtime/status"))
                config = json.loads((root/"config.json").read_text(encoding="utf-8"))
                self.assertEqual(config["default_profile"], "desktop")
                self.assertEqual(config["fps"], 25)
                self.assertEqual(config["hardware_backend"], "auto")
                self.assertEqual(config["orchestration"]["rules"], [])
                self.assertEqual(set(config["profiles"]), {"desktop", "ambient_wave"})
            finally: terminate_proc(proc)

    def test_package_without_checkout_assets(self):
        package = os.environ.get("AURA_PACKAGE_DIR")
        if not package: self.skipTest("Set AURA_PACKAGE_DIR to the final extracted WinUI package")
        check_daemon_prerequisites_or_skip(self)
        sys.path.insert(0,str(Path(__file__).resolve().parents[1] / "tools"))
        from package_winui import verify_package
        manifest = verify_package(package)
        payload = Path(package).resolve() / "runtime-payload"
        with tempfile.TemporaryDirectory(prefix="aura-package-runtime-") as temp:
            root=Path(temp); config=root / "config.json"
            config.write_text(json.dumps({"default_profile":"base","profiles":{"base":{"type":"static"}}}))
            proc=subprocess.Popen([str(payload/"aura_daemon.exe"),"--dry-run","--config",str(config),
                "--keymap",str(payload/"calibrated_keymap.json"),"--runtime-root",str(payload)],cwd=root,
                env=get_isolated_env(temp),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            try:
                self.assertTrue(wait_for_http_ready(19897,path="/api/runtime/status"))
                self.assertTrue(wait_for_http_ready(19898))
                core=request(19897,"/api/runtime/status")[1]; web=request(19898,"/api/status")[1]
                self.assertEqual(core["identity"]["product_version"],manifest["version"])
                self.assertEqual(web["daemon_instance_id"],core["identity"]["instance_id"])
                self.assertEqual(Path(web["sdk_include_dir"]).resolve(),(payload/"include").resolve())
                self.assertTrue(web["sdk_headers_found"])
                self.assertTrue(web["studio_publish_ready"], "Package publication test requires installed MSVC/Windows SDK")
                original = config.read_bytes()
                source = '''#include "engine/effect.h"
#include "engine/plugin_interface.h"
class PackagedEffect : public aura::Effect {
 public: void Render(uint64_t, aura::FrameBuffer& frame, const aura::Keymap&) override { frame.Clear(); }
};
AURA_PLUGIN_EXPORT uint32_t AuraGetPluginApiVersion() { return AURA_PLUGIN_API_VERSION; }
AURA_PLUGIN_EXPORT const char* AuraGetEffectName() { return "package_smoke"; }
AURA_PLUGIN_EXPORT aura::Effect* AuraCreateEffect() { return new PackagedEffect; }
AURA_PLUGIN_EXPORT void AuraDestroyEffect(aura::Effect* effect) { delete effect; }
'''
                from test_automation_reload_daemon import request as publication_request
                compiled = publication_request(19898,"/api/compile_effect",{"name":"package_smoke","code":source})
                self.assertEqual(compiled[0],200,compiled[1])
                loaded = publication_request(19898,"/api/reload_plugin",{"name":"package_smoke"})
                self.assertEqual(loaded[0],200,loaded[1])
                self.assertTrue(loaded[1]["daemon_synced"])
                self.assertTrue((root/"plugins/effect_package_smoke.dll").is_file())
                self.assertEqual(config.read_bytes(),original,"Compile/load must not silently mutate profile configuration")
                status, queued, _=request(19897,"/api/gsi/simulation","POST",{"enabled":True})
                self.assertEqual(status,202)
                deadline=time.monotonic()+5
                while request(19897,"/api/gsi/simulation")[1]["applied_sequence"] < queued["sequence"]:
                    self.assertLess(time.monotonic(),deadline);time.sleep(.05)
                current=request(19897,"/api/gsi/current")[1]
                self.assertEqual(current["source"],"simulation")
                self.assertEqual(current["instance_id"],core["identity"]["instance_id"])
                self.assertEqual(current["data"]["player.state.health"],100)
            finally: terminate_proc(proc)


if __name__=="__main__": unittest.main()
