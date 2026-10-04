# LightingService alternative backend runtime analysis — Phase 3 M2.8

Observed 2026-10-02, interactive session 1, medium integrity RID 8192. No AuraSdk activation or
enumeration was performed. No ownership, lighting, fan, HID or vendor-service control operation
was issued. Raw observations live under `audit_artifacts/phase3-m2.8/`; timestamps there are UTC.

## Findings

AceHFXAura can obtain the current LightingService **logical topology and status without AuraSdk**.
ServiceMediator is the preferred read-only discovery candidate. This does not validate a lighting
output backend: addressing, ownership/restore semantics and a reversible first write remain unknown.
Current decision: **ALTERNATIVE_AURA_BACKEND_NOT_READY**.

LMCAP_FRAME is desktop-capture input to screen effects on the installed stack, not a verified
device RGB output bus. WDL exposes only the Falchion Ace HFX MI_04 interface in this observation.

## Current binary and ABI evidence

| Component | Version | SHA256 |
|---|---|---|
| LightingService.exe | 3.10.12.0 | `a711b9776ac0279256d22389478d44e3a51a1909c75a3bf03158cf9d5b91d3db` |
| AuraLayerManager.dll | 1.0.0.2 | `34839834224ee13d46ce9b26c77c5d6864cc663a69ed72dbddf75c64ebf1a184` |
| LM_Support.exe | 1.0.0.2 | `d9e474158b92c17b8654d9a3ed403d493e7c9cf9a04a539d29251364e7f87294` |
| AuraSdk_x64.dll (file/registry only) | 3.7.5.0 | `0a94593c8c7f4e1af99abed0afe2803a6d615318ebe96d85529eb232f6a8b0af` |

ServiceMediator CLSID `95775DC4-77AA-4E94-8CF6-68267EEF1856`, interface IID
`76C92A6B-B131-4DF9-9D24-CCD6014EFB5F`, TypeLib
`61E8C91A-C37E-4831-87C0-2FBD8C5A85D5` v1.0 match the Phase 2 generated reference.
The reference was recovered from the existing rollback backup, not newly invented or used as a
production runtime dependency. `LoadTypeLibEx(REGKIND_NONE)` re-extracted the **installed** EXE's
contract: all three allowlisted methods have INVOKE_FUNC, zero input parameters and BSTR result.
The process-local x64 TypeInfo offsets are marshaler metadata, not x86 implementation offsets.

HKLM 32-bit LocalServer32 points at the installed EXE. AppID
`6CD7A1D2-8046-40BE-817E-E2256FB0889C` has `LocalService=LightingService`.
No HKCU registration shadow was found for either researched CLSID in the queried views.
Actual x64 `CoCreateInstance(CLSCTX_LOCAL_SERVER, verified IID)` succeeded despite there being no
separate HKLM 64-bit ServiceMediator CLSID entry. Registration absence in that view is not an
activation failure. SCM identifies the running LocalSystem server PID 7124/session 0. No second
LightingService process was observed; the PID did not change during the probe.

## Candidate A: actual activation and fixed read-only calls

The standalone medium-integrity worker contains no AuraSdk interface. An immutable IDispatch
allowlist invokes only DISPIDs **3, 4, 63**, with DISPATCH_METHOD and no arguments. There is no
generic method/CLSID/path/DLL command. TypeLib validation precedes activation. Safe query semantics
are supported by both the canonical reference and targeted current native implementation analysis:

| Query | x86 implementation RVA | Supporting static path |
|---|---|---|
| get_QueryAllDeviceStatus, 3 | `0x2D3F50` | queued lambda `0x2DFF30` → RogAuraDeviceManager query `0x2C3EB0`; status XML traversal and BSTR allocation |
| get_QueryAllDeviceCap, 4 | `0x2D4020` | queued lambda `0x2DFCB0` → manager `0x2C27C0`; controller capability/LED XML construction and BSTR allocation |
| get_QueryAllDevice, 63 | `0x2D5110` | queued lambda `0x2DF7D0` → QueryAllDeviceCapEx `0x2C2EB0`; device metadata XML and BSTR allocation |

The x86 vtable at RVA `0x3E0294` places these at slots 9, 10, 26, consistent with the canonical
interface. The native wrappers use the common service task dispatcher at `0x2DED30`.
This scoped analysis supports metadata queries; it does not prove every vendor internal routine
or HAL is fault-free. Other profile, exclusive-status, matrix-status, Hue/OLED and input-bearing
members remain **NotAttempted**, including ambiguous methods. No event subscriptions were made.

Final candidate SHA256: `72f4c4ce4b6f69bb1fe1f1c0da15e937dbe33b25bf173841141ea1ed78cf4fe4`.
Three fresh workers, 15:13:08–15:13:13 +08:00:

| Repetition / PID | Activation ms | Status ms / UTF-16 bytes | Capability ms / bytes | Device-list ms / bytes | Outcome |
|---|---:|---:|---:|---:|---|
| 1 / 26028 | 10.5421 | 32.4523 / 65404 | 2.1927 / 30268 | 0.6895 / 6340 | all S_OK, BSTR, XML parsed |
| 2 / 26912 | 9.4021 | 11.7715 / 65404 | 1.6938 / 30268 | 0.6820 / 6340 | all S_OK, BSTR, XML parsed |
| 3 / 26196 | 8.4409 | 21.7775 / 65404 | 1.6485 / 30268 | 0.6827 / 6340 | all S_OK, BSTR, XML parsed |

An earlier three-worker pass before adding the autonomous deadline also succeeded (PIDs 20008,
30064, 11928). It is preserved separately in `initial-probes/`; do not combine the two executable
builds into one candidate identity. Total observed: six activations/eighteen metadata calls, no
fail-fast and no timeout. This is a small local read-only reliability sample, not control acceptance.

Each worker has an independent 20-second deadline; the supervisor also bounds it to 20 seconds
and kills only that child. A native deadline exits 124. Activation/query progress is flushed first;
started-but-not-returned operations retain null HRESULT/payload fields. The parent bounds output
to 4 MiB; each BSTR is limited to 1 MiB of UTF-16 payload. XML forbids DTD/entities, excessive
size/depth/node/device counts. Client isolation cannot guarantee LocalServer health or cancellation
of a vendor-side queued operation. No crash was observed, so no new debug launcher/dump was needed.

## Topology without AuraSdk

DISPID 63 returned these nine logical records on all final trials:

| Type | Model | Vendor index | Width × height / count | Record evidence | Physical interpretation |
|---|---|---:|---|---|---|
| DIMM | EneTech / GSkillDram | 0 | 8 × 1 / 8 | RuntimeExact | DRAM controller record; exact physical module/LED mapping unknown |
| DIMM | EneTech / GSkillDram | 1 | 8 × 1 / 8 | RuntimeExact | second DRAM controller record; same qualification |
| VGA card | VGA0 / Vga | 0 | 23 × 1 / 23 | RuntimeExact | GPU logical record; no PCI/serial ID returned |
| AIO Cooler | ROG STRIX LC III SERIES | 0 | 4 × 1 / 4 | RuntimeExact | cooler logical record; physical zone mapping unvalidated |
| WDL_Keyboard | ROG FALCHION ACE HFX | 0 | 19 × 6 / 114 | RuntimeExact | strong correlation with sole WDL keyboard; no shared interface/container ID in mediator XML |
| Mainboard_Master | TUF GAMING X870-PLUS WIFI | 0 | 2 × 1 / 2 | RuntimeExact | motherboard logical record; not independent hardware acceptance |
| AddressableStrip | ARGB HEADER | 0 | 120 × 1 / 120 | RuntimeExact | configured header capacity, physical strip length unknown |
| AddressableStrip | ARGB HEADER | 1 | 120 × 1 / 120 | RuntimeExact | same qualification |
| AddressableStrip | ARGB HEADER | 2 | 120 × 1 / 120 | RuntimeExact | same qualification |

Source-qualified IDs retain type/model/index/record position and never join by similar names.
RuntimeExact means the service returned that metadata, not that a physical device was individually
opened or validated. ConfigExact identifies exact file content. StrongCorrelation/Unknown cover
cross-source physical hypotheses. Accessible/control-validated remain NotAttempted.

Status XML has Group/Mainboard/HUE sections. Capability XML has Group/Mainboard/WDL_Keyboard;
the latter is an empty section. **Group declares 1122 logical lights but contains only 45 LED XML
records**; Mainboard declares 2 and lists 2. The nine device records sum to 519 logical slots.
These differing representations are not a safe SetLedMatrix addressing specification. No flat
offset/zone mapping was inferred, and ARGB capacity was not treated as attached strip length.

The three runtime XML responses match the current GetDeviceStatus.xml, GetDeviceCap.xml and
QueryAllDevice.xml after XML canonicalization. This validates correspondence, not independent
physical evidence: the files may themselves be generated/cached service outputs.
DevLastStatConfig.xml lists synchronized Mainboard_Master/Vga/GSkillDram/AddressableStrip/WaterCooler
and unsynchronized Mouse/WDL_Keyboard. Those configuration records do not prove access or ownership.
The historical M2.6 Count=3 and GPU Count=1 evidence, confidence labels and hypothetical 141-slot
release scope were preserved; the new nine-record source must not replace those observations.

## Candidate B: current protocol correction and passive observation

Current managed consumer/producer were inspected locally with ILSpy, without executing either:

- AuraLayerManager `PipeClient.CaptureScreen2` → SharedMemoryClient → SampleRect → screen-effect bitmap.
- Layer setup obtains **physical desktop bounds**, creates a screen capture mapping, and can launch
  `LM_Support.exe` (including `--global` from session 0). No helper was launched by this task.
- LM_Support CaptureLoop → MultiScreenCapture.CaptureToBuffer → SharedMemoryServer.FlipBuffer.
  It captures via DXGI/GDI, flips a double buffer and signals FRAME_READY in vendor code.
- The producer targets approximately 33 ms with fresh consumer feedback, 1000 ms when feedback
  is zero/stale for 3 seconds. These are static intervals, not measured host cadence.
- Desktop-resolution changes cause a dispose/reinitialize attempt. Whether the kernel object is
  actually recreated while other handles remain open was not experimentally tested. Lighting-service
  start/topology lifecycle behavior remains Unknown; no restart or device manipulation was performed.

This establishes desktop image direction/semantics, not direct LED addressing. The earlier Phase 2
synthetic MMF benchmark was not an RGB end-to-end benchmark and supplies no production-bus proof.

Current static header: Pack=1, 64 bytes, magic `0x46434D4C`, version 1; width/height/stride/format
at offsets 8/12/16/20; frame index/active buffer/capture state at 24/28/32; **64-bit** feedback tick
at 36; desktop origins at 44/48 in the nominal reserved tail. Format 0 is four-byte desktop pixels,
two buffers, `64 + 2 × stride × height`. The older prose's 32-bit feedback tick was incorrect.
Static factory ACL allows SYSTEM and Authenticated Users GenericAll; **this is not a live-object
ACL observation** and is not copied into project IPC. Such shared memory is untrusted input.

Actual read-only OpenFileMapping/OpenEvent outcomes:

| Object | Result | Error | Live size / ACL / header / cadence |
|---|---|---:|---|
| Local\\LMCAP_FRAME | Unavailable | 2 / FILE_NOT_FOUND | all unknown/null; no sampling |
| Global\\LMCAP_FRAME | Unavailable | 2 | all unknown/null; no sampling |
| Local\\LMCAP_FRAME_READY | Unavailable | 2 | no wait, consume or signal |
| Global\\LMCAP_FRAME_READY | Unavailable | 2 | no wait, consume or signal |

LM_Support was not present in the process observation. No mapping was created to make this pass.
Consequently frame changes/cadence/active-buffer transitions/visual correlation are **NotAttempted**,
not zero-valued success. The supplied observer can passively sample an existing valid map for at
most five seconds/20 Hz; bounds/coherency tests use software buffers only.

There was no production MMF reader to repair. The new research observer requests FILE_MAP_READ
views, SECTION_QUERY for exact section size and READ_CONTROL separately for ACL metadata. Its
pointer remains inside a scoped try/finally; only copied bytes/statistics escape. It validates the
current header, dimensions, stride, format, active buffer, exact section size and 128 MiB ceiling;
integer multiplication cannot wrap. It never mutates feedback or waits on the auto-reset event.
No MMF adapter/producer is wired into the application.

## Candidate C: Windows Dynamic Lighting

Final DeviceInformation enumeration PID 27416/session 1/medium returned **one** enabled LampArray
interface: ROG FALCHION ACE HFX, HID VID_0B05/PID_1B7E/MI_04, usage page `0x59`, usage `1`.
PnP parent reports the same product name and container
`356590B3-0861-51A7-B803-CA6BA8A7CF7B`. This is an exact OS interface/USB-parent association,
not a merge with the mediator object. No motherboard, GPU, DRAM or cooler LampArray interface was
returned by this selector; this does not mean the physical devices are absent.

HKCU Microsoft Lighting AmbientLightingEnabled=0, Brightness=100, EffectType/EffectMode=0,
UseSystemAccentColor=1; IsLampArrayEnabled is absent. Interface enabled is distinct from system
lighting enabled and from application lighting control. No setting was changed.
LampArray.FromIdAsync, lamp getters requiring a control object, colors and IsEnabled setters were
not used. Opening an object has not been established ownership-neutral in this project, so WDL
control availability, true lamp count/update limits and restoration remain Unknown. The 114/19×6
count is mediator metadata, not a new Windows lamp-count measurement.

## Minimum runtime / limitations

SCM declares LightingService dependencies RPCSS, Audiosrv, DcomLaunch. Static native imports do not
directly name Armoury UI/CEF/UserSessionHelper/ArmourySocketServer. Queries succeeded without
observing an Armoury UI/CEF/socket-server process, but ArmouryCrate.Service and UserSessionHelper
were running, together with AacAmbientLighting and existing HAL components. Dynamic loads, initial
configuration and background interactions remain unproven. **LightingService + required HAL only
has not been validated**; no uninstallation/service-stop test is authorized here. LMCAP screen input
also needs its capture helper/add-on, not merely the hardware service.

All 13 monitored SCM records/PIDs/configuration fields match before/after. AceHFXService was already
Stopped/manual at this milestone's start and remains so; this task neither restarted it nor repeated
M1 broker acceptance. Its state is not evidence against the medium-user mediator query path.
Gate A remains false, and no alternative first write is ready. See
[architecture](../architecture/AURA_BACKEND_SELECTION.md) and [write prerequisites](../testing/AURA_ALTERNATIVE_WRITE_GATE.md).
