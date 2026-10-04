"""Software-only M3 source/ABI evidence tests. Never activates COM or reads hardware."""
import json
import re
import unittest
from pathlib import Path
from asus_call_guards import violations, code_only

ROOT = Path(__file__).resolve().parents[2]


class FanSafetyGuards(unittest.TestCase):
    def test_hreftype_has_no_checked_pointer_narrowing(self):
        source = (ROOT / "src/AceHFXFanWorker/FanLibraryContract.cs").read_text()
        self.assertNotIn(".ToInt32()", source)
        self.assertIn("unchecked((int)(uint)unionStorage)", source)
        self.assertIn("description.vt != 29", source)
        self.assertIn("RequirePointer(description.lpValue)", source)

    def test_validator_test_binary_is_metadata_only(self):
        directory = ROOT / "tests/FanTypeLibValidator.Tests"
        project = (directory / "FanTypeLibValidator.Tests.csproj").read_text()
        self.assertNotIn("ProjectReference", project)
        self.assertNotIn("Program.cs", project)
        for path in directory.glob("*.cs"):
            source = code_only(path.read_text())
            self.assertNotRegex(source, r"CoCreateInstance|GetTypeFromCLSID|DeviceIoControl|FanComActivation|FanReader")
            self.assertFalse(violations("src/AceHFXFanWorker/ValidatorTest.cs", path.read_text()))

    def test_validator_regressions_are_wired_into_software_ci(self):
        source = (ROOT / "tools/ci/run-ci.ps1").read_text()
        self.assertIn("'fan-typelib-validator-test'='fan-typelib-validator-test.log'", source)
        self.assertIn("tests/FanTypeLibValidator.Tests/FanTypeLibValidator.Tests.csproj", source)
        self.assertIn("TestCategory!=OfflineMetadata", source)
        self.assertIn("tests/FanTypeLibValidator.Tests/FanTypeLibValidator.Tests.csproj", (ROOT / "Aura.slnx").read_text())

    def test_write_calls_rejected(self):
        for member in ("SetFanDuty", "EnableManualMode", "EnableRpmMode", "ApplyFanCurve", "ApplyFanCurveButNotSave",
                       "ApplyIndex", "SetCriticalPoint", "SetAic2FanDuty", "RefreshFanCurve", "AddPoint"):
            with self.subTest(member=member):
                self.assertTrue(violations("src/AceHFXFanWorker/Bad.cs", f"fan.{member}(value);"))

    def test_property_mutation_rejected(self):
        for member in ("DutyCycle", "EcFanStop", "FanSourceIndex", "Temperature", "Speed", "RpmTolerance"):
            self.assertTrue(violations("src/AceHFXFanWorker/Bad.cs", f"fan.{member} = value;"))

    def test_readonly_declarations_have_no_setter(self):
        source = (ROOT / "src/AceHFXFanWorker/ReadOnlyFanAbi.cs").read_text()
        self.assertFalse(violations("src/AceHFXFanWorker/ReadOnlyFanAbi.cs", source))
        self.assertNotRegex(code_only(source), r"\b(set|init)\s*[;{]")

    def test_generic_forwarding_rejected(self):
        for source in ("dynamic fan;", "fan.Invoke(member, args);", "target.InvokeMember(method);", "DeviceIoControl(handle);"):
            self.assertTrue(violations("src/AceHFXFanWorker/Bad.cs", source))

    def test_no_force_switch(self):
        source = (ROOT / "src/AceHFXFanWorker/Program.cs").read_text()
        self.assertIn("args.Length != 0", source)
        self.assertNotIn("--force", source)
        self.assertIn('request.Command != "ReadFanSnapshot"', source)

    def test_fixture_has_no_vendor_activation(self):
        source = code_only((ROOT / "tests/FanWorkerFixture/Program.cs").read_text())
        self.assertNotRegex(source, r"CoCreateInstance|GetTypeFromCLSID|DeviceIoControl|EnableManualMode")

    def test_current_typelib_model_covers_required_interfaces(self):
        model = json.loads((ROOT / "tests/fixtures/fancontrol-typelib.json").read_text())
        names = {t["name"] for t in model["types"]}
        self.assertTrue({"IFanControlManager", "IFanControlCollection", "IFanControl", "IFanProfileCollection", "IFanProfile",
                         "IFanCurve", "IFanCurvePoint", "IThermalWeights", "IMultiFanSource", "IDualModeFanCurve"} <= names)
        self.assertEqual(model["libraryGuid"], "{DF5522FB-119C-428D-A62E-8BFE4A2FBC9B}")

    def test_bool_positions_match_raw_typelib(self):
        model = json.loads((ROOT / "tests/fixtures/fancontrol-typelib.json").read_text())
        fan = next(t for t in model["types"] if t["name"] == "IFanControl")
        rpm = next(f for f in fan["functions"] if f["name"] == "IsRpmMode")
        self.assertEqual((rpm["invokeKind"], rpm["returnVt"]), (2, 11))
        manual = next(f for f in fan["functions"] if f["name"] == "EnableManualMode")
        self.assertEqual((manual["invokeKind"], manual["parameters"][0]["vt"]), (1, 11))

    def test_no_per_channel_rpm_getter_in_current_typelib(self):
        model = json.loads((ROOT / "tests/fixtures/fancontrol-typelib.json").read_text())
        fan = next(t for t in model["types"] if t["name"] == "IFanControl")
        rpm_names = {f["name"] for f in fan["functions"] if f["invokeKind"] == 2 and "rpm" in f["name"].lower()}
        self.assertEqual(rpm_names, {"IsRpmMode", "RpmTolerance"})

    def test_reduced_abi_ids_types_match_reextraction(self):
        model = json.loads((ROOT / "tests/fixtures/fancontrol-typelib.json").read_text())
        source = (ROOT / "src/AceHFXFanWorker/ReadOnlyFanAbi.cs").read_text()
        mapping = {"IFanManagerRead": "IFanControlManager", "IFanCollectionRead": "IFanControlCollection", "IFanRead": "IFanControl",
                   "IFanProfilesRead": "IFanProfileCollection", "IFanCurveRead": "IFanCurve", "IFanPointRead": "IFanCurvePoint"}
        scalar = {"uint": 19, "int": 3, "byte": 17, "bool": 11, "string": 8}
        for name, body in re.findall(r"internal interface (\w+)\s*\{(.*?)(?=\n\}|\}\n\[|\}\s*$)", source, re.S):
            record = next(t for t in model["types"] if t["name"] == mapping[name])
            props = re.findall(r"\[DispId\((\d+)\)\]\s*(\w+)\s*(\w+)", body)
            for dispid, kind, prop in props:
                match = [f for f in record["functions"] if f["dispid"] == int(dispid) and f["invokeKind"] == 2]
                self.assertEqual(len(match), 1)
                self.assertEqual(match[0]["returnVt"], scalar.get(kind, 26))
                if prop != "this":
                    self.assertEqual(match[0]["name"], prop)


if __name__ == "__main__":
    unittest.main()
