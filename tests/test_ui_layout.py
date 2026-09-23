"""Opt-in desktop layout measurements and XAML-rendered screenshots, never user desktop capture."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_runtime_entrypoints import check_daemon_prerequisites_or_skip, get_authoritative_binary, get_isolated_env, terminate_proc, wait_for_http_ready, KEYMAP_FILE
from test_automation_authoring_daemon import request

class TestUiLayout(unittest.TestCase):
    def test_render_and_measure_native_pages(self):
        target=os.environ.get("AURA_UI_EXE"); output=os.environ.get("AURA_UI_VALIDATION_DIR")
        if not target or not output:self.skipTest("Set AURA_UI_EXE and AURA_UI_VALIDATION_DIR for interactive desktop validation")
        check_daemon_prerequisites_or_skip(self)
        Path(output).mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="aura-ui-") as directory:
            root=Path(directory);config=root/"config.json"
            config.write_text(json.dumps({"default_profile":"base","profiles":{"base":{"type":"reactive","brightness":.5}},"orchestration":{"rules":[]}}))
            env=get_isolated_env(directory);env["AURA_DATA_ROOT"]=directory
            env.pop("AURA_DEV_ROOT",None);env.pop("AURA_DEV_BIN",None)
            env["AURA_UI_VALIDATION_DIR"]=str(Path(output).resolve())
            core=subprocess.Popen([get_authoritative_binary("aura_daemon.exe"),"--dry-run","--config",str(config),"--keymap",KEYMAP_FILE],cwd=root,env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            gui=None
            try:
                self.assertTrue(wait_for_http_ready(19897,path="/api/runtime/status"))
                self.assertEqual(request(19897,"/api/runtime/status")[1]["identity"]["process_id"],core.pid)
                gui=subprocess.Popen([str(Path(target).resolve()),"--validate-layout"],cwd=root,env=env)
                gui.wait(timeout=240)
                result=json.loads((Path(output)/"layout-results.json").read_text(encoding="utf-8"))
                self.assertIsNone(result["error"],result["error"])
                self.assertEqual(len(result["results"]),40)
                self.assertIsNone(core.poll(),"GUI must preserve externally started core")
            finally:
                if gui is not None:terminate_proc(gui)
                terminate_proc(core)

if __name__=="__main__":unittest.main()
