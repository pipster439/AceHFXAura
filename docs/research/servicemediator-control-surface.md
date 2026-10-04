# ServiceMediator control surface — M2.9

Current installed LightingService TypeLib was re-extracted with **ITypeLib/ITypeInfo** and
LoadTypeLibEx(REGKIND_NONE), not copied from previous Markdown. Source EXE 3.10.12.0 SHA256
`a711b9776ac0279256d22389478d44e3a51a1909c75a3bf03158cf9d5b91d3db`.
TypeLib `{61E8C91A-C37E-4831-87C0-2FBD8C5A85D5}` v1.0; IServiceMediator
`{76C92A6B-B131-4DF9-9D24-CCD6014EFB5F}`; coclass
`{95775DC4-77AA-4E94-8CF6-68267EEF1856}`. The extractor enumerated all library types,
implemented/inherited interface names/GUIDs/flags, every member, parameter order/flags/names,
recursive pointer/SAFEARRAY types, return VARTYPE, INVOKEKIND and vtable byte offsets.

Evidence: `audit_artifacts/phase3-m2.9/servicemediator-typelib.json` and
`control-callsite-analysis.json`. IServiceMediator exposes **40 declared members**, plus7
inherited IUnknown/IDispatch entries. The raw vtable offsets are x64 TypeInfo-converted offsets;
current server is x86, so native slot=offset/8 and native byte offset=slot*4. Do not use x64
byte offsets against the x86 vtable. Automation VOID/BSTR/UI4 return descriptions are distinct
from native COM HRESULT transport; output retval pointers are represented in the Automation return.
Some PROPERTYPUT final parameters have **no name stored by GetNames**; empty/null names are retained,
not invented as verified names. SAFEARRAY nested element UI4 is confirmed; rank/color interpretation
is not specified by the TypeLib.

## Runtime boundary

Only DISPIDs **3,4,63** are enabled: zero-input FUNC/BSTR metadata methods proven by targeted
implementation review and successful isolated medium-client invocation. No additional getter was
necessary to establish the blockers. GetProfile is READ_ONLY_CONFIRMED only as an E_NOTIMPL stub;
it was not invoked and cannot capture restoration state. Getter-shaped Hue/exclusive/matrix/OLED/
game-mode methods stay UNKNOWN where helper side effects or result meaning remain incomplete.
`get_QuerySetHueResult` is not assumed read-only from its prefix or return type. All40 members are
inventoried below. Confidence labels describe the accompanying claim, not permission to execute.

- READ_ONLY_CONFIRMED: audited metadata path or non-mutating E_NOTIMPL stub.
- STATE_CHANGING_CONFIRMED: current traced mutation path or explicit PROPERTYPUT assignment contract.
- LIKELY_STATE_CHANGING: command/callback/disconnect/cancel surface with non-getter input/action ABI;
  full implementation untraced. Classification is precautionary, not semantic proof from a name.
- UNKNOWN: incomplete mutability/meaning evidence. NotAttempted on the host.

Static caller evidence is from installed native ArmouryCrate.AuraPlugin.dll and targeted service
wrapper/functor/implementation paths. No ASUS control caller was executed. The managed M2.8 add-on
contains no relevant S1 caller. Only the listed functions were decompiled; untraced members remain
untraced rather than receiving speculative semantics.

## Inventory

The complete signature/order/type detail is in JSON. `?` means a parameter name is not stored.
Every non-read-only row includes claim, confidence, ABI evidence, static evidence, caller sequence
and an open question; “not established” is a scoped unknown, not an assertion of global absence.

| DISPID / member | Classification | Claim / confidence | TypeLib contract | Static evidence | ASUS caller evidence | Open question |
|---|---|---|---|---|---|---|
| 1 `SetProfile` | STATE_CHANGING_CONFIRMED | Profile parser/engine and LastProfile persistence; not a transient target handle / CONFIRMED_STATIC | PROPERTYPUT; (deviceId:BSTR, ?:BSTR) -> VOID | 0x2d9340; 0x2cfad0 | AuraPlugin 0x24a5e0 -> slot 7; callers 0x13a550/0x13a6b0 use deviceId="1" plus XML | Physical addressing, prerequisite control and deterministic restore are not established |
| 2 `GetProfile` | READ_ONLY_CONFIRMED | Not implemented on installed native vtable: returns E_NOTIMPL (0x80004001) / CONFIRMED_STATIC | PROPERTYGET; (deviceId:BSTR) -> BSTR | 0x1cc3b0 | No useful capture/restore payload; not invoked | No payload: E_NOTIMPL |
| 3 `get_QueryAllDeviceStatus` | READ_ONLY_CONFIRMED | Metadata XML traversal/BSTR / CONFIRMED_STATIC | FUNC; (none) -> BSTR | 0x2dff30 -> 0x2c3eb0 | M2.8 and M2.9 isolated read-only runtime verified | Metadata is not ownership/physical-state proof |
| 4 `get_QueryAllDeviceCap` | READ_ONLY_CONFIRMED | Capability XML construction/BSTR / CONFIRMED_STATIC | FUNC; (none) -> BSTR | 0x2dfcb0 -> 0x2c27c0 | M2.8 and M2.9 isolated read-only runtime verified | Metadata is not ownership/physical-state proof |
| 5 `get_QueryHueStatus` | UNKNOWN | ABI does not establish mutability, normal restoration or target isolation / UNKNOWN | FUNC; (none) -> UI4 | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 6 `get_QuerySetHueResult` | UNKNOWN | ABI does not establish mutability, normal restoration or target isolation / UNKNOWN | FUNC; (BridgesInfo:BSTR) -> UI4 | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 7 `get_QueryHueBridges` | UNKNOWN | ABI does not establish mutability, normal restoration or target isolation / UNKNOWN | FUNC; (none) -> UI4 | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 9 `get_QueryHueBridgesInfo` | UNKNOWN | ABI does not establish mutability, normal restoration or target isolation / UNKNOWN | FUNC; (index:UI4) -> BSTR | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 10 `get_NeedPushButton` | UNKNOWN | ABI does not establish mutability, normal restoration or target isolation / UNKNOWN | FUNC; (index:UI4) -> UI4 | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 11 `OnDeviceChange` | LIKELY_STATE_CHANGING | ABI does not establish mutability, normal restoration or target isolation / UNKNOWN | FUNC; (deviceId:UI4, pid:VT_23, vid:VT_23) -> VOID | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 15 `get_AuraExclusive_Status` | UNKNOWN | Loads setting-store last profile; reads exclusivemode with fallback 1; not PID/SID ownership / CONFIRMED_STATIC | FUNC; (none) -> UI4 | 0x2e0af0 | No matching end-to-end ASUS acquire/restore/release caller recovered | Physical addressing, prerequisite control and deterministic restore are not established |
| 16 `set_AuraExclusive_Status` | STATE_CHANGING_CONFIRMED | AuraRequireToken internal call, global manager flag and exclusivemode persistence / CONFIRMED_STATIC | FUNC; (status:UI4) -> UI4 | 0x2e0dd0 -> 0x2b7e00 | No matching end-to-end ASUS acquire/restore/release caller recovered | Physical addressing, prerequisite control and deterministic restore are not established |
| 51 `Oled_GetCapability` | UNKNOWN | ABI does not establish mutability, normal restoration or target isolation / UNKNOWN | FUNC; (none) -> BSTR | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 52 `Oled_GetProfile` | UNKNOWN | ABI does not establish mutability, normal restoration or target isolation / UNKNOWN | FUNC; (none) -> BSTR | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 53 `Oled_SetProfile` | LIKELY_STATE_CHANGING | ABI does not establish mutability, normal restoration or target isolation / UNKNOWN | FUNC; (profile:BSTR) -> VOID | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 54 `Oled_RestoreLastProfile` | LIKELY_STATE_CHANGING | ABI does not establish mutability, normal restoration or target isolation / UNKNOWN | FUNC; (none) -> VOID | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 60 `SetScript` | STATE_CHANGING_CONFIRMED | Engine stop/rebuild script path / CONFIRMED_STATIC | PROPERTYPUT; (profile:BSTR, ?:I4) -> VOID | 0x2e0080 -> 0x2d0d30 | AuraPlugin 0x248df0 slot 23: supplied XML, rhs=0; critical section in 0x12a330 | Physical addressing, prerequisite control and deterministic restore are not established |
| 61 `SetEngine` | STATE_CHANGING_CONFIRMED | Engine manager mode/configuration branch / CONFIRMED_STATIC | PROPERTYPUT; (?:BSTR) -> VOID | 0x2dbfe0 | AuraPlugin 0x24ba50 slot 24; supplied engine string | Physical addressing, prerequisite control and deterministic restore are not established |
| 62 `set_XmlGameScript` | LIKELY_STATE_CHANGING | ABI does not establish mutability, normal restoration or target isolation / UNKNOWN | FUNC; (profile:BSTR) -> UI4 | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 63 `get_QueryAllDevice` | READ_ONLY_CONFIRMED | Logical controller metadata XML / CONFIRMED_STATIC | FUNC; (none) -> BSTR | 0x2df7d0 -> 0x2c2eb0 | M2.8 and M2.9 isolated read-only runtime verified | Metadata is not ownership/physical-state proof |
| 64 `StartEngine` | STATE_CHANGING_CONFIRMED | cmd=1 installs script/start path; cmd=0 stops and attempts lastscript load, then changes global mode / CONFIRMED_STATIC | PROPERTYPUT; (profile:BSTR, cmd:I4, ?:I4) -> VOID | 0x2dc6c0 | AuraPlugin 0x24bc80 slot 27: profile XML, cmd=1, caller rhs; critical section in 0x1383d0 | Physical addressing, prerequisite control and deterministic restore are not established |
| 65 `SetGA401Script` | STATE_CHANGING_CONFIRMED | PROPERTYPUT assignment declaration; payload/routing/restore untraced / CONFIRMED_STATIC | PROPERTYPUT; (profile:BSTR, ?:I4) -> VOID | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 66 `StartGA401Script` | STATE_CHANGING_CONFIRMED | PROPERTYPUT assignment declaration; payload/routing/restore untraced / CONFIRMED_STATIC | PROPERTYPUT; (?:I4) -> VOID | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 67 `Hue_Disconnect` | LIKELY_STATE_CHANGING | ABI does not establish mutability, normal restoration or target isolation / UNKNOWN | FUNC; (none) -> VOID | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 68 `SetLedMatrix` | STATE_CHANGING_CONFIRMED | AP match and global matrix enable flag; special device-type matrix distribution; not nine-record routing / CONFIRMED_STATIC | FUNC; (psaArray:PTR(SAFEARRAY(UI4)), APname:BSTR) -> VOID | 0x2d7450 -> 0x2cf6d0 | No SetLedMatrix caller/payload producer identified in scoped AuraPlugin scan | Physical addressing, prerequisite control and deterministic restore are not established |
| 69 `Acquire_ledmatrixControl` | STATE_CHANGING_CONFIRMED | control=1/0 changes global flag and persistent AP tag; no per-device index / CONFIRMED_STATIC | FUNC; (control:INT, APname:BSTR) -> VOID | 0x2d9e10 -> 0x2d17b0 | AuraPlugin 0x24b0a0 slot 32; 0x1387c0 forwards (param2==0), caller AP string | Physical addressing, prerequisite control and deterministic restore are not established |
| 70 `SetLedMatrixScript` | STATE_CHANGING_CONFIRMED | Script/control globals, LedMatrixConfig AP registry write and completion wait / CONFIRMED_STATIC | PROPERTYPUT; (profile:BSTR, ?:I4) -> VOID | 0x2e0590 | End-to-end first-write/restore caller not identified | Physical addressing, prerequisite control and deterministic restore are not established |
| 101 `KillRequest` | LIKELY_STATE_CHANGING | Cancels a service request; not a demonstrated lighting restoration API / CONFIRMED_STATIC | FUNC; (none) -> VOID | 0x2d5660 | Not invoked | Physical addressing, prerequisite control and deterministic restore are not established |
| 102 `Acquire_MatrixControl` | STATE_CHANGING_CONFIRMED | type/model selection map; modifies MATRIX DATA/MatrixStatus.ini/script/status, no index parameter / CONFIRMED_STATIC | FUNC; (typename:BSTR, modelname:BSTR, status:I4) -> VOID | 0x2d76c0 | AuraPlugin 0x249040 slot 35; 0x129550 uses MATRIX_Laptop plus stored model and status; 0x129850 iterates matched records with status=1 | Physical addressing, prerequisite control and deterministic restore are not established |
| 103 `Query_MatrixControl_Status` | UNKNOWN | Reads map entry by type/model; meaning/side effects of all helper operations not completely audited / UNKNOWN | FUNC; (typename:BSTR, modelname:BSTR) -> I4 | 0x2db340 -> 0x2c1df0 | Unknown numeric status; not invoked | Physical addressing, prerequisite control and deterministic restore are not established |
| 104 `OnMatrixStatusChange` | LIKELY_STATE_CHANGING | ABI does not establish mutability, normal restoration or target isolation / UNKNOWN | FUNC; (status_info:BSTR) -> VOID | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 105 `Put_SystemMode_Script` | LIKELY_STATE_CHANGING | ABI does not establish mutability, normal restoration or target isolation / UNKNOWN | FUNC; (script:BSTR, layername:BSTR, time:I4) -> VOID | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 106 `OnDeviceNotify` | LIKELY_STATE_CHANGING | ABI does not establish mutability, normal restoration or target isolation / UNKNOWN | FUNC; (profile:BSTR) -> VOID | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 107 `Put_SetBurnLedMatrixScript` | LIKELY_STATE_CHANGING | ABI does not establish mutability, normal restoration or target isolation / UNKNOWN | FUNC; (profile:BSTR, time:I4) -> VOID | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 108 `get_DeviceGameModeStatus` | UNKNOWN | Builds game-mode metadata; downstream semantics not fully audited / UNKNOWN | FUNC; (none) -> BSTR | 0x2dfdd0 -> 0x2bf690 | Not invoked | Physical addressing, prerequisite control and deterministic restore are not established |
| 109 `SetModernStandbyStatus` | LIKELY_STATE_CHANGING | ABI does not establish mutability, normal restoration or target isolation / UNKNOWN | FUNC; (status:I4) -> VOID | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 110 `SetRegTree` | LIKELY_STATE_CHANGING | ABI does not establish mutability, normal restoration or target isolation / UNKNOWN | FUNC; (regpath:BSTR, name:BSTR, value:I4) -> VOID | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 111 `put_AuraInGamePriorityList` | LIKELY_STATE_CHANGING | ABI does not establish mutability, normal restoration or target isolation / UNKNOWN | FUNC; (PriorityList:BSTR) -> VOID | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 112 `SetSlashLightScript` | STATE_CHANGING_CONFIRMED | PROPERTYPUT assignment declaration; payload/routing/restore untraced / CONFIRMED_STATIC | PROPERTYPUT; (profile:BSTR, ?:I4) -> VOID | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |
| 113 `SetSlashLightNotifyScript` | STATE_CHANGING_CONFIRMED | PROPERTYPUT assignment declaration; payload/routing/restore untraced / CONFIRMED_STATIC | PROPERTYPUT; (profile:BSTR, ?:I4) -> VOID | NotAnalyzed: outside bounded S1 trace | NotEstablished in scoped caller evidence | Physical addressing, prerequisite control and deterministic restore are not established |

## Findings that prevent a normal restore contract

GetProfile returns E_NOTIMPL; profile writes select XML scope and may persist settings. Matrix
AP control is global and uses a persisted label. Matrix software-mode control selects type/model,
without a record index. AuraExclusive setter calls an internal vendor token path and writes a global
setting; status bits are not an authenticated lease. StartEngine cmd0 has a global stop/lastscript
reload branch, whose exact previous-state and error semantics remain unverified.

No complete acquire -> uniquely addressed temporary write -> deterministic restore -> release
caller/error-cleanup sequence was identified. Ordinary BSTR/COM cleanup does not restore lighting.
See [addressing](servicemediator-addressing-model.md) and [restore contract](servicemediator-restore-contract.md).

`ReviewedGateAExecutionEnabled = false`

`SERVICE_MEDIATOR_S1_BLOCKED`
