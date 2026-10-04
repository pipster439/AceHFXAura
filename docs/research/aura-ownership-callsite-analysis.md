# Aura ownership targeted callsite analysis — M2.5

2026-10-01. **Static analysis only; no ownership call was executed.** Scope is the four canonical
AuraSdk ownership methods and their immediate token/release helpers. Existing M2 read-only behavior
is unchanged. Evidence archive: `audit_artifacts/phase3-m2.5/`.

## Classification and release gate

`CONFIRMED_STATIC` means an exact generated ABI, manufacturer statement/example, or decoded machine
instruction for the identified file. It is not runtime restoration proof. `STRONG_INFERENCE` connects
those facts conservatively; `UNKNOWN` needs a separately approved runtime experiment.

**ReviewedReleaseValueKnown = true; ReviewedReleaseValue = 0.** Two independent primary evidence
forms agree: ASUS documents zero as the reserved argument and its current installed implementation
explicitly accepts zero in the owned branch. This gate applies to the reviewed x64 SDK hash only;
the experiment rejects SDK/ABI drift. Do not generalize this to arbitrary newer ASUS binaries.

## Primary manufacturer references

- [ASUS IAuraSdk reference](https://www.asus.com/microsite/aurareadydevportal/interface_aura_service_lib_1_1_i_aura_sdk.html):
  manufacturer describes SwitchMode as acquiring LED control before later SDK operations. Proper
  application shutdown can implicitly release control from SDK version 1.02.01; this does not promise
  hard-crash recovery. The page says its HRESULT is S_OK, so that alone cannot validate ownership.
- [ASUS IAuraSdk2 reference](https://www.asus.com/microsite/aurareadydevportal/interface_aura_service_lib_1_1_i_aura_sdk2.html):
  reserve is documented as reserved with zero required; release is described as returning devices to
  their default effect. A default effect is not necessarily an exact prior Armoury/WDL animation.
- [ASUS C++ tutorial](https://www.asus.com/microsite/aurareadydevportal/tutorial_cpp.html):
  the example initializes MTA COM, creates one sdk pointer (optionally IAuraSdk2), acquires before
  enumeration, and explicitly releases using the same pointer. No IAuraSdk3 token call appears in
  this minimal manufacturer flow. Its color/Apply example is excluded from our harness.
- [ASUS Python tutorial](https://www.asus.com/microsite/aurareadydevportal/tutorial_python.html):
  another manufacturer example acquires before Enumerate. It does not establish current WDL support.

Fetched HTML files and SHA256 manifest are preserved under `official-docs/`. Do not interpret a 2021
guide as runtime proof for the current 3.07.05.0 DLL; the native ABI and implementation were checked.

## Local ABI and exact implementation identification

- Canonical generated reference: existing rollback archive
  `src/research/generated/aura/AuraSdk.h`, SHA256
  `62B999D335A0E22EDB431D46A0945130925F0A5057DA45398441D8DF5E6F0D29`.
- Product minimum ABI: `src/AuraWorker/CanonicalAuraAbi.h`, SHA256
  `2672030E2439E99D2397FA8DA9FA13A92CFF08F42851CB13B69CD31EBEC83EFB`.
  Existing M2 exact eight-block validation is retained; no invented COM interface is introduced.
- Installed x64 binary: `C:\Program Files\ASUS\AuraSDK\AuraSdk_x64.dll`, version 3.07.05.0,
  SHA256 `0A94593C8C7F4E1AF99ABED0AFE2803A6D615318EBE96D85529EB232F6A8B0AF`.
- Preferred image base `0x180000000`. RTTI for ATL CComObject<CAuraSdk> identifies COL RVA `0xDE5A0`
  and vtable RVA `0xB6388`; the verified inherited COM slot order binds entries 7–11 below.
  Raw RTTI/vtable mapping, Capstone bytes/disassembly and Ghidra pseudocode are archived. Ghidra's
  project-session empty-file hash is not binary provenance; the explicit on-disk SHA above is.

| Slot | Method | RVA | CONFIRMED_STATIC observation |
|---|---|---|---|
| 7 | Enumerate | 0xFFC0 | `this+0x10` holds a collection; populate only if null, then return/AddRef that same collection |
| 8 | SwitchMode | 0x10030 | calls process-global helper 0x208E0 then 0x21050, returns zero HRESULT unconditionally |
| 9 | ReleaseControl | 0x10050 | token check; unowned path calls 0x20BA0; owned path accepts reserve zero; nonzero returns 0x80070057 there |
| 10 | RequireTokenByType | 0x10190 | forwards VARIANT/count to 0x20DE0 then returns zero; never called by our harness |
| 11 | RequireDeviceControlState | 0x101C0 | forwards type/output pointer to 0x20A70 then returns zero; never called by our harness |

## Q1 — ReleaseControl reserve and side effects

**CONFIRMED_STATIC:** instructions `mov ebx,edx` (0x10069), `test ebx,ebx` (0x10095), branch to
`mov eax,0x80070057` (0x10170) implement the owned-branch reserve check. Zero reaches normal release.
The unowned branch can bypass the reserve check and still release per-type entries. We always use
zero; “all nonzero arguments always fail” would overstate the implementation.

**CONFIRMED_STATIC:** when its own cached collection is non-null and token check succeeds,
0x10050 iterates collection Count/Item, device Lights and light Count/Item. It calls light vtable
offset `0x78` with zero and device offset `0x60`. Canonical ABI maps these to **put_Color(0) and Apply**.
Their individual HRESULTs are not checked. It then calls token release helper 0x20BA0 and returns S_OK.
Thus “the application makes no RGB setter call” cannot promise this vendor release implementation
does no internal RGB operation. This is an explicit future experiment risk, not RGB code in our app.
The experiment requires all pre-enumeration counts zero; a changed/nonzero baseline blocks acquisition
pending review of this side effect. Empty baseline does not prove all internal/vendor hardware paths safe.

**UNKNOWN:** actual restored visual effect, WDL/Armoury precedence, successful hardware resumption,
release elapsed time and behavior when vendor helpers hang/fail. S_OK is insufficient restoration proof.

### M2.5 read-only MTA baseline changes the execution gate

The dedicated candidate was run **without arguments, DRY RUN only**. Its MTA SDK2 instance returned
S_OK/Count=3 for all eleven category calls, with the same collection IUnknown identity each time.
No device/light getter or ownership call was issued in this baseline. An immediate unchanged M2
STA-worker comparison again returned eleven successful zero counts. This is a new actual observation;
its cause (apartment-sensitive implementation, vendor timing/behavior or other internal state) is UNKNOWN.
It does not prove three distinct physical devices, nor prove ownership was acquired.

The nonempty cache makes the installed release's internal put_Color(0)/Apply branch relevant to a
future acquisition. Therefore **ReviewedGateAExecutionEnabled = false** in this delivered candidate,
even though reserve zero is established. The exact execute flag cannot open this compiled gate.
Outcome: **GATE_A_BLOCKED** pending explicit review of vendor-internal RGB side effects, the changed
baseline and restoration evidence policy. Do not reinterpret this as unknown reserve semantics.
Both raw journals/results are retained; no production M2 behavior was changed to manufacture a match.

## Q2/Q3 — Ordering and token policy

**CONFIRMED_STATIC:** ASUS tutorial uses the same sdk object for SwitchMode → Enumerate →
ReleaseControl(0), without explicit IAuraSdk3 token calls. Current SwitchMode forwards to
0x21050 → 0x219C0, whose own log strings label token switching; it updates a shared GUID through
0x23BB0 after conditions/availability checks. Therefore the minimal public API already has internal
token machinery. Gate A must not add public token/control-state calls based on interface availability.

**STRONG_INFERENCE:** this is a supported ordinary Aura acquisition/release route and may change
shared ownership even if later operations fail. Arm a release obligation **before** entering SwitchMode;
a returned failure/SEH is not evidence that its internal transition was atomic or absent.

**UNKNOWN:** whether SwitchMode alone exposes any devices/WDL category on this host. Its zero
HRESULT does not disclose whether internal condition checks actually acquired the shared token.
RequireTokenByType uses SAFEARRAY data internally, and the control-state helper compares GUIDs,
but full type/order/count/result semantics are not established for Gate B and are not implemented.

**CONFIRMED_STATIC new limitation:** Enumerate caches the first populated collection per COM object,
regardless of later category arguments. M2's eleven S_OK/Count-zero observations remain real calls,
but they are not eleven independent fresh hardware enumerations. The user-requested Gate A retains
the same object and the same eleven categories before/after/release; it records collection IUnknown
identity/cached-observation limitation. Post-switch zero cannot disprove acquisition. No second COM
object is created secretly to obtain fresh enumeration or release ownership.

## Q4 — Same process / same instance

**CONFIRMED_STATIC:** 0x208E0 returns process-global DAT_180108158. Constructor helper 0x1FE40 calls
CoCreateGuid to initialize that token identity; token check 0x20990 compares the current shared GUID
to this process token; release 0x20BA0 clears matching per-type/shared entries and delegates restoration.
ReleaseControl also references `this+0x10`, the acquiring instance's cached collection. The official
example retains the same sdk pointer. The installed registration is ThreadingModel=Both and the
manufacturer example initializes MTA.

**STRONG_INFERENCE:** retaining the original instance/process is the most defensible normal release
path. A new independent process has a different token GUID and is not proven able to undo the original
claim. The harness retains that exact IAuraSdk2 instance for its normal/RAII/watchdog release.

**UNKNOWN:** arbitrary concurrent method thread safety. An MTA same-instance watchdog is COM-valid,
but vendor behavior when a read/SwitchMode is already hung is unvalidated. Event waits include 5000 ms
and service calls have no proven finite return bound. A timer can bound the release **attempt**, not
guarantee successful restoration or cancel in-process native calls. Timeout must be RecoveryRequired.

## Search coverage / no broad reverse engineering

Targeted name/source scan covered installed AuraSDK, AURACreator, Armoury Crate Service and GameSDK
Service roots; only the two AuraSDK DLLs contained the four method-name strings. These matches include
TypeLib names and are not independent native caller evidence. No local SDK sample was found there.
The primary ASUS C++/Python examples are the concrete caller evidence. Only the identified SDK's four
methods, Enumerate (to resolve ordering/cache), and immediate token/release helpers were decompiled.
No HAL/fan/driver analysis, running ASUS process attachment, DLL execution, service change or binary patch.

Previous research descriptions of guaranteed 100 ms crash recovery, zero-device ownership necessity,
and safe cross-process ReleaseControl were architectural hypotheses. They are not promoted to proof.
Normal release/restoration must be verified in a separately approved first run before any crash experiment.
