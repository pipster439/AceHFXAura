import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("asus_call_guards", ROOT / "tools/ci/asus_call_guards.py")
guards = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guards)

class AuraGateAGuards(unittest.TestCase):
    def test_product_ownership_reference_is_rejected(self):
        for path in ("src/AuraWorker/AuraWorker.cpp", "winui/App.xaml.cs", "src/AsusPlatform/Aura/Backend.cs"):
            for code in ("sdk->SwitchMode();", "sdk.ReleaseControl(0);", "auto call=&IAuraSdk::SwitchMode;"):
                self.assertTrue(guards.violations(path, code))

    def test_only_experiment_adapter_can_call_ownership(self):
        self.assertFalse(guards.violations(guards.EXPERIMENT_ADAPTER, "sdk->SwitchMode(); sdk->ReleaseControl(0);"))
        self.assertTrue(guards.violations("tools/AuraOwnershipExperiment/Other.cpp", "sdk->SwitchMode();"))

    def test_token_and_rgb_calls_blocked_in_experiment(self):
        for code in ("sdk->RequireTokenByType(t,1);", "sdk->RequireDeviceControlState(0,&v);", "d->Apply();", "l->put_Color(0);", "m.SetLedMatrix(a);", "m.SetFanDuty(a);", "m.EnableManualMode();"):
            self.assertTrue(guards.violations(guards.EXPERIMENT_ADAPTER, code))

    def test_generic_invocation_is_blocked(self):
        for code in ("sdk->Invoke(id, args);", "LoadLibraryW(userPath);", "GetProcAddress(dll,method);"):
            self.assertTrue(guards.violations(guards.EXPERIMENT_ADAPTER, code))

    def test_product_reference_would_ship_experiment_and_is_blocked(self):
        self.assertTrue(guards.violations("winui/Aura.WinUI.csproj", '<ProjectReference Include="AuraOwnershipExperiment" />'))

    def test_current_tree_passes(self):
        guards.verify_tree(ROOT)

if __name__ == "__main__":
    unittest.main()
