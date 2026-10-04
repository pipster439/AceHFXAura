"""Gate A ownership/RGB surface guard. Pure source inspection; never loads vendor code."""
from pathlib import Path
import hashlib
import re

EXPERIMENT_ADAPTER = "tools/AuraOwnershipExperiment/NativeAuraApi.cpp"
ABI_HEADER = "src/AuraWorker/CanonicalAuraAbi.h"
ABI_SHA256 = "2672030e2439e99d2397fa8da9fa13a92cff08f42851cb13b69cd31ebec83efb"
FAN_ABI_HEADER = "src/AceHFXFanWorker/ReadOnlyFanAbi.cs"
FAN_ABI_SHA256 = "ed6f114a070f43389d8c1db6e4abaf4d3fe8e40657700c1fce6231c0581c09e1"
MEDIATOR_ADAPTER = "tools/LightingBackendProbe/MediatorReadOnly.cpp"
PRODUCT_GRAPHS = ("CMakeLists.txt", "Aura.slnx", "tools/package_release.py", "tools/package_winui.py", "tools/package_asus_service.ps1")

def code_only(text, python=False):
    # Avoid diagnostics strings/comments pretending to be call sites. Ordinary C#/C++/Python source.
    comments = r'\#[^\n]*|' if python else ''
    return re.sub(r'""".*?"""|\'\'\'.*?\'\'\'|' + comments + r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', "", text, flags=re.S)

def violations(relative, text):
    code = code_only(text, python=relative.endswith('.py'))
    failures = []
    fan_scope = relative.startswith(("src/AceHFXFanWorker/", "src/AceHFXService/", "src/AsusPlatform/", "winui/Platform/"))
    if fan_scope:
        writes = r"SetFanDuty|EnableManualMode|EnableRpmMode(?:ButNotSave)?|ApplyFanCurve(?:ButNotSave)?|NormalizeFanCurve|ApplyIndex|SaveUserProfile|SaveRpmTargetDuty|SetCriticalPoint|SetAic2FanDuty|SetAiSuiteRequestDDR5|RefreshFanCurve|AddPoint|ApplyButNotSave"
        if re.search(rf"\b(?:{writes})\s*\(", code) or re.search(r"\b(?:DeviceIoControl|WriteProcessMemory|VirtualProtect\w*|WritePort\w*)\s*\(", code):
            failures.append("M3 cooling must remain read-only")
        properties = r"DutyCycle|StartupProfileIndex|EcMode|ThermalWeights|DisplayName|StepUpTime|StepDownTime|TempTolerance|RpmTolerance|EcFanStop|StepUnit|T1DelayTime|FanSourceIndex|Temperature|Speed"
        if re.search(rf"(?:\.|->)\s*(?:{properties})\s*(?:=(?!=)|\+=|-=|\+\+|--)", code):
            failures.append("fan property mutation prohibited")
    if relative.startswith("src/AceHFXFanWorker/"):
        if re.search(r"\b(?:dynamic|InvokeMember|GetMethod|GetDelegateForFunctionPointer|GetProcAddress|LoadLibrary\w*|SwitchMode|ReleaseControl)\b|(?:\.|->)\s*Invoke\s*\(", code):
            failures.append("FanWorker cannot expose generic invocation or other hardware backends")
        if relative.endswith("ReadOnlyFanAbi.cs") and re.search(r"\b(?:set|init)\s*[;{]", code):
            failures.append("fan ABI must expose getters only")
    if relative in PRODUCT_GRAPHS and "LightingBackendProbe" in text:
        failures.append("alternative research probe must not enter product build/package graph")
    ownership = re.findall(r"\b(?:SwitchMode|ReleaseControl)\b", code)
    if ownership and relative != EXPERIMENT_ADAPTER and relative != ABI_HEADER:
        failures.append("ownership reference outside dedicated adapter")
    if relative != ABI_HEADER and re.search(r"\b(?:RequireTokenByType|RequireDeviceControlState|SetLedMatrix|SetFanDuty|EnableManualMode|SetFanCurve|put_DutyCycle)\b", code):
        failures.append("token, matrix or fan write surface")
    aura_scope = relative.startswith(("src/AuraWorker/", "src/AsusPlatform/", "tools/AuraOwnershipExperiment/", "tools/AuraEnumerationCharacterizer/", "tools/AuraMtaCrashProbe/", "tools/LightingBackendProbe/", "tools/ServiceMediatorContract/", "tools/AuraDiagnostics/", "winui/Platform/"))
    # Names in TypeLib evidence strings are data. Executable vendor control references are forbidden.
    mediator_control = r"(?:put_)?(?:SetProfile|SetScript|SetEngine|StartEngine|SetGA401Script|StartGA401Script|SetLedMatrixScript|SetSlashLightScript|SetSlashLightNotifyScript)|SetLedMatrix|(?:set_|put_)?AuraExclusive_Status|Acquire_(?:ledmatrixControl|MatrixControl)|Put_(?:SystemMode_Script|SetBurnLedMatrixScript)|Oled_(?:SetProfile|RestoreLastProfile)|set_XmlGameScript|KillRequest|OnDeviceChange|OnMatrixStatusChange|OnDeviceNotify|SetModernStandbyStatus|SetRegTree|put_AuraInGamePriorityList"
    if aura_scope and re.search(rf"(?:->|\.|::)\s*(?:{mediator_control})\s*\(",code):
        failures.append("ServiceMediator control/write calls remain prohibited")
    if aura_scope and relative != ABI_HEADER:
        if re.search(r"(?:->|\.)\s*(?:Apply|put_\w+)\s*\(|\b(?:IAuraSdk\w*|IAuraRgbLight\w*|IAuraSyncDevice)::(?:Apply|put_\w+)\b", code):
            failures.append("Aura RGB setter or Apply")
        dispatch_code = code
        if relative == MEDIATOR_ADAPTER:
            fixed = r"service->Invoke\(ReadIds\[i\],IID_NULL,LOCALE_INVARIANT,DISPATCH_METHOD,&args,&value,&exception,&argument\)"
            if len(re.findall(fixed,code)) != 1 or not re.search(r"constexpr\s+std::array<DISPID,3>\s+ReadIds=\{3,4,63\}",code):
                failures.append("mediator must retain exactly the reviewed fixed three metadata queries")
            dispatch_code = re.sub(fixed,"",code)
        if re.search(r"\b(?:GetProcAddress|LoadLibrary\w*|GetDelegateForFunctionPointer)\s*\(|(?:->|\.)\s*Invoke\s*\(", dispatch_code):
            failures.append("generic vendor invocation surface")
    if relative.startswith(("tools/LightingBackendProbe/", "tools/ServiceMediatorContract/")):
        if re.search(r"\b(?:IAura\w*|CLSID_AuraSdk|Apply|put_\w+|SetColors?\w*|FromIdAsync|CreateFileMapping\w*|CreateEvent\w*|SetEvent|ResetEvent|SetFan\w*|DeviceIoControl|WriteProcessMemory|VirtualProtect\w*|CreateRemoteThread|memmove|UpdateClientReadTick)\b",code):
            failures.append("alternative probe must expose only metadata and passive reads")
        if re.search(r"\b(?:FILE_MAP_WRITE|PAGE_READWRITE|EVENT_MODIFY_STATE)\b",code):
            failures.append("passive observation cannot request write rights")
    if relative.startswith("tools/ServiceMediatorContract/"):
        if re.search(r"\b(?:CoCreateInstance|CoInitializeEx|RoInitialize|OpenFileMapping\w*|LampArray|subprocess|Popen)\b",code):
            failures.append("contract evidence model cannot execute vendor/OS probes")
    if relative.endswith((".csproj", ".targets", ".props", ".slnx")) and "LightingBackendProbe" in text:
        failures.append("alternative research executable must remain outside product graph")
    if relative.endswith((".csproj", ".targets", ".props", ".slnx")) and "AuraOwnershipExperiment" in text:
        failures.append("experiment must not enter product build/publish graph")
    if relative.startswith("tools/AuraEnumerationCharacterizer/"):
        if re.search(r"\b(?:Apply|put_\w+|SetFan\w*|EnableManualMode|DeviceIoControl|WriteFile|WriteProcessMemory)\b", code):
            failures.append("characterizer must have no write API declarations or calls")
        if re.search(r'#include\s*[<"]CanonicalAuraAbi\.h', text):
            failures.append("characterizer must compile only verified read-only ABI prefixes")
        if re.search(r"\b(?:lpVtbl|GetProcAddress|LoadLibrary\w*)\b", code):
            failures.append("characterizer cannot bypass fixed read-only COM surface")
    if relative.endswith((".csproj", ".targets", ".props", ".slnx")) and "AuraEnumerationCharacterizer" in text:
        failures.append("research characterizer must remain outside product graph")
    if relative.startswith("tools/AuraMtaCrashProbe/"):
        if re.search(r"\b(?:Apply|put_\w+|get_Item|get_Lights|get_Type|get_Name|get_Width|get_Height|SetFan\w*|DeviceIoControl|WriteFile|WriteProcessMemory|lpVtbl|GetProcAddress|LoadLibrary\w*)\b", code):
            failures.append("minimal crash probe must expose only enumeration/count and safe preflight")
        if re.search(r'#include\s*[<"]CanonicalAuraAbi\.h', text):
            failures.append("crash probe requires reduced ABI")
    if relative.startswith("tools/AuraMtaDebugLauncher/") and re.search(r"\b(?:IAura\w*|Version2Prefix|CLSID_AuraSdk|CoCreateInstance|CoInitializeEx|RoInitialize)\b",code):
        failures.append("external debugger must not contain vendor COM activation or interfaces")
    if relative.startswith(("tools/AuraMtaCrashProbe/", "tools/AuraMtaDebugLauncher/")) and re.search(r"\b(?:WriteProcessMemory|VirtualAllocEx|VirtualProtectEx|CreateRemoteThread|SetThreadContext|DebugActiveProcess|Apply|put_\w+|SetFan\w*|DeviceIoControl)\b",code):
        failures.append("MTA compatibility tools cannot inject, patch, attach to other processes or expose writes")
    if relative.endswith((".csproj", ".targets", ".props", ".slnx")) and re.search(r"AuraMta(?:CrashProbe|PreflightProbe|DebugLauncher)",text):
        failures.append("MTA research executables must remain outside product graph")
    return failures

def verify_tree(root):
    for name in PRODUCT_GRAPHS:
        failures = violations(name,(root / name).read_text(encoding="utf-8-sig"))
        if failures:
            raise ValueError(f"{name}: {', '.join(failures)}")
    header = root / ABI_HEADER
    if hashlib.sha256(header.read_bytes()).hexdigest() != ABI_SHA256:
        raise ValueError("Canonical ABI changed; separate reviewed evidence is required")
    if hashlib.sha256((root / FAN_ABI_HEADER).read_bytes()).hexdigest() != FAN_ABI_SHA256:
        raise ValueError("Reduced fan ABI changed; new TypeLib/static review required")
    for directory in ("src", "tools", "winui"):
        for path in (root / directory).rglob("*"):
            if not path.is_file() or any(part in {"bin", "obj", "node_modules", "__pycache__"} for part in path.parts):
                continue
            if path.suffix.lower() not in {".cpp", ".h", ".cs", ".py", ".csproj", ".targets", ".props", ".slnx"}:
                continue
            relative = path.relative_to(root).as_posix()
            # This source-policy module contains patterns, not vendor methods.
            if relative in {"tools/ci/asus_call_guards.py", "tools/ci/check-source.py"}:
                continue
            failures = violations(relative, path.read_text(encoding="utf-8-sig"))
            if failures:
                raise ValueError(f"{relative}: {', '.join(failures)}")
    if "AuraOwnershipExperiment" in (root / "Aura.slnx").read_text(encoding="utf-8"):
        raise ValueError("Experiment must remain outside normal solution/publish graph")
    if "AuraEnumerationCharacterizer" in (root / "Aura.slnx").read_text(encoding="utf-8"):
        raise ValueError("Characterizer must remain outside normal solution/publish graph")
    adapter = code_only((root / EXPERIMENT_ADAPTER).read_text(encoding="utf-8"))
    for method in ("SwitchMode", "ReleaseControl"):
        if len(re.findall(rf"->\s*{method}\s*\(", adapter)) != 1:
            raise ValueError(f"Dedicated adapter must have exactly one fixed {method} call site")
