# Aura MTA Enumerate fail-fast analysis — Phase 3 M2.7

2026-10-02, Asia/Shanghai. Local, read-only application compatibility diagnosis.

## Finding and decision

**MTA stability remains blocked.** All 18 new vendor trials failed before Enumerate returned;
19 fatal exception events were captured because the first process exposed two failing worker
threads. Every event is non-continuable (`ExceptionFlags=1`), second-chance, code
`0xC0000409`, NumberParameters=1, ExceptionInformation=[5]. Windows SDK 10.0.26100.0
`um/winnt.h:23046` maps 5 to **FAST_FAIL_INVALID_ARG**. This is not evidence of a stack-buffer
overrun. The native exception representation follows [Microsoft __fastfail documentation](https://learn.microsoft.com/en-us/cpp/intrinsics/fastfail?view=msvc-170). Count and Enumerate HRESULT remain null when the process dies before return.

The identified immediate mechanism is an invalid-parameter fail-fast in the **statically linked
CRT of the ASUS keyboard or mouse HAL, reached from log-header wide-string formatting during
COM class initialization on SDK-created worker threads**. This narrows the fault substantially,
but does not establish the particular malformed value, formatting length, or upstream race.
No supported initialization or category workaround has been validated.

`ReviewedGateAExecutionEnabled = false`. Ownership remains unexecuted. No M3.

## Evidence strength

- CONFIRMED_DYNAMIC: live exception code/flags/parameters, TID, module base/address/RVA,
  OS thread context, native unwind and successful pre-termination minidump creation.
- CONFIRMED_STATIC: installed-file hash matches the Ghidra program's executable SHA256;
  the exception instructions load ECX=5 and execute `int 29h`; both call chains enter
  safe wide printf formatting and their private CRT invalid-parameter/_invoke_watson paths.
- STRONG_INFERENCE: the current MTA failure is a HAL initialization/log-formatting compatibility
  failure, rather than a device getter, app ownership method, or a demonstrated Job restriction.
- UNKNOWN: exact offending format argument/output length, why rare earlier MTA trials survived,
  whether a vendor-internal initialization race or other process state controls it, and the
  wait mechanism of the prior ~18-second successes/20-second timeouts.

No patch, hook, injected code, privileged run, driver, persistent debugger registry setting,
security bypass, or vendor-service operation was used. The local Ghidra analysis reads only the
captured HAL call chain. Its working project and proprietary binaries are excluded from packaging.

## Fault modules and native frames

Faults: keyboard `AacKbHal_x64.dll + 0x107A8C` (7 events), mouse
`AacMouseHal_x64.dll + 0x206054` (12 events). These are HAL-internal CRT copies,
not a demonstrated fault in the separately loaded ucrtbase.dll. Each crashing TID differs from
its process's Enumerate-calling TID. Both appear through SDK worker -> COM factory -> HAL
initialization while the app's main MTA thread is inside Enumerate.

Representative keyboard stack, trial `timing-02-COM_ONLY-0`, PID 59412, caller TID 57328,
fault TID 43856 (inner-to-outer, module-relative):

```text
AacKbHal_x64.dll +107A8C  _invoke_watson: ECX=5, int 29h
AacKbHal_x64.dll +107A22  invalid-parameter path
AacKbHal_x64.dll +10D7C3  safe wide printf invalid-parameter return site
AacKbHal_x64.dll +0157C8  formatting wrapper, capacity 0x200 wchar_t
AacKbHal_x64.dll +07AD66  log header formatting caller
AacKbHal_x64.dll +00E0DD  HAL initialization
AacKbHal_x64.dll +086D86  COM factory path
combase.dll             (full module offsets in stacks/timing-02-COM_ONLY-0.json)
AuraSdk_x64.dll +014890 / +014141 / +014059 / +09193B
kernel32.dll / ntdll.dll thread entry
```

Representative mouse stack, `pilot-exact`, PID 48172, caller TID 54916, fault TID 36728:

```text
AacMouseHal_x64.dll +206054  _invoke_watson: ECX=5, int 29h
AacMouseHal_x64.dll +205FE9 / +206005  invalid-parameter dispatch
AacMouseHal_x64.dll +20AE94  __stdio_common_vswprintf_s
AacMouseHal_x64.dll +01A558  wide safe-format wrapper
AacMouseHal_x64.dll +03FC09 / +040E2A  formatting wrapper, capacity 0x200 wchar_t
AacMouseHal_x64.dll +044783  log header formatting caller
AacMouseHal_x64.dll +01A099 / +0E2D8B  HAL initialization / COM class factory
combase.dll +089B06          CoCreateInstance vicinity
AuraSdk_x64.dll +014890 / +014141 / +014059 / +09193B
kernel32.dll +02CDF7 / ntdll.dll +07EE4C
```

The local static routines format `[%s][%s][%d] ` into a 512-wchar buffer. Both CRT implementations
reject null format/destination or zero size, and can invoke invalid-parameter handling after
formatting reports `-2` (range failure). This identifies candidate failure branches, **not which
branch was taken**. We did not recover the original arguments or assert a buffer overflow.
The observed formatter call occurs before the later logger file-open/write code. Missing log
directories or an invalid FILE pointer are therefore **not established causes of these stacks**.
No log directory/ACL, DLL logging flag, handler or binary was changed to suppress the fault.

Export-only names with large displacements (such as DllUnregisterServer + a large offset) are
nearest symbol labels, not proof those exported methods ran. Module/RVA and the statically verified
CRT functions above are the meaningful attribution. No vendor private PDB was available.
The first `pilot-com-only` had correct exception/context/module evidence but incomplete unwind;
subsequent trials use explicit module image paths and recover the full chain.

## Differential results

| Profile | Delay ms | Trials | Completed | Fail-fast | Timeout | Count |
| --- | --- | --- | --- | --- | --- | --- |
| COM_ONLY | 0 | 5 | 0 | 5 | 0 | NotReturned |
| RO_INITIALIZE | 0 | 1 | 0 | 1 | 0 | NotReturned |
| SDK2_QI | 0 | 1 | 0 | 1 | 0 | NotReturned |
| GPU_ONLY | 0 | 1 | 0 | 1 | 0 | NotReturned |
| M2_5_EXACT_PREFLIGHT | 0 | 1 | 0 | 1 | 0 | NotReturned |
| COM_ONLY | 250 | 3 | 0 | 3 | 0 | NotReturned |
| COM_ONLY | 1000 | 3 | 0 | 3 | 0 | NotReturned |
| COM_ONLY | 5000 | 3 | 0 | 3 | 0 | NotReturned |

ALL=0 except GPU_ONLY=0x20000. Every process activates a fresh SDK and calls Enumerate once.
No device metadata or light getters exist in the compiled surface. All vendor trials use medium
RID 8192, non-elevated SID S-1-5-21-1987165031-2871585381-1769346942-1001, session 1, MTA type 1 /
qualifier 0, successful initialization/activation, and inherited Job membership=true.
SDK2_QI and exact preflight record successful SDK2 QI. No Job escape was attempted.

Four timing conditions each have three trials. Abnormal-exit cooldown is 30 seconds, serial;
start/end UTC timestamps and the scheduler's actual sleeps are preserved in raw JSON. The
activation delay moves process elapsed time but does not remove the subsequent ~41–116 ms
Enumerate-to-exception failure. Total process elapsed includes debugger/dump overhead.
Observation budget is 35 seconds from Enumerate entry, with a separate 65-second launch bound.
None of these real trials timed out or reached the 5-second stack-sampling threshold.
No successful ~18-second wait path was reproduced, so its RPC/event/WinRT cause remains unknown.

After the user's scope reduction, only four minimum controls were added: repository CWD,
CoInitializeEx+RoInitialize, SDK2 QI, and documented/previously approved GPU category. They each
failed once; no extra category matrix or combinatorial preflight batch was run. The earlier
MTA GPU success is preserved as a real prior observation, but GPU filtering does not reliably
avoid keyboard/mouse HAL initialization: the new GPU trial crashed in the mouse HAL before a
collection/HRESULT could return. No device membership can be inferred from its null Count.

## M2.5 comparison and fidelity limits

`M2_5_EXACT_PREFLIGHT` reproduces the safe API ordering as practically isolated here:
RoInitialize-only MTA -> LightingService SCM state/PID -> SDK hash/version -> six HKCU WDL values ->
LampArray DeviceInformation FindAllAsync (2-second bound, one interface found) -> topology/config
hashes -> fixed Aura activation -> SDK2 QI -> Enumerate(0). It also failed in the mouse HAL.

This named profile is not byte-for-byte identical to the old candidate: it omits GUID/mutex,
journal/filesystem output, UI/observer worker arrangement and repeated review helpers; keeps the
base SDK reference alongside its SDK2 prefix; and records LampArray count rather than every
interface's ID/name/enabled getters. The old candidate enumerated on a std::thread with an
internal observer; the probe enumerates on its own main thread. Both initialize MTA on the calling
thread and have inJob=true. Earlier pilots used the probe's Release directory as CWD, whereas the
minimum controls use G:/Aura; that control also failed. Debugger presence, native modules,
metadata preflight and reference lifetimes differ. Consequently the result does not prove that
all M2.5 process details are irrelevant or that apartment alone explains the older difference.
No ownership candidate was rebuilt/run to answer that question.

M2.6 supplied 66/66 stable STA S_OK/Count=0 calls; COM-only MTA 22 fail-fast/10 old-budget
timeouts/1 completion and COM+WinRT MTA 23/8/2. Those raw observations are retained unchanged.
The successful Vga 1 (23 lights), LC III (4), WindowsLighting_LED (114), GPU Count=1 sample,
confidence labels, per-instance STA cache and hypothetical 141 logical-light owned-release
scope remain historical evidence. M2.7 does not replace them or validate physical identity,
ownership or restoration. Keep current production STA read-only behavior and represent zero
as successful empty enumeration; do not claim hardware capability or substitute zero for MTA failure.


### Preserved successful-path controls

| Phase/PID | Init | First category | Enum ms | Count | SDK2 QI | In Job | Module evidence |
|---|---|---|---|---|---|---|---|
| M2.6/58464 | COM_only | 0x70000 | 18219.0 | 0 | NotAttempted | true | NotCaptured |
| M2.6/64380 | COM_plus_WinRT | 0x0 | 18138.1 | 3 | NotAttempted | true | NotCaptured |
| M2.6/59596 | COM_plus_WinRT | 0x20000 | 18208.9 | 1 | NotAttempted | true | NotCaptured |

The prior GPU success used **COM plus WinRT**, while the new GPU_ONLY control used COM only.
The new failure proves GPU-only filtering is not sufficient in that configuration; it does not
exclude every GPU+preflight combination. No further combinations were run after scope reduction.
All three successful historical controls used the same SDK 3.7.5.0 hash, medium identity/session 1
and an inherited Job; successes therefore contradict Job membership as a sufficient cause.
Successful loaded-HAL maps/native wait stacks were not captured in M2.6, preventing a symmetric
module-by-module comparison. The extracted prior rows are in prior-success-comparison.json,
without modifying the original matrix. All 13 SCM records in the actual M2.6 final snapshot
match M2.7's initial snapshot as well as M2.7's final snapshot. This does not observe all vendor
internal runtime state and cannot prove apartment or a Job is the only differing factor.

## Bounded tooling and tests

The launcher accepts only fixed local profiles, delays and ASCII trial IDs. Executables and output
roots are fixed to this repository's M2.7 build/audit directories; no arbitrary target, command,
DLL, category, attach or output path is accepted. It contains no vendor COM interface. The probe's
ABI is extracted from the pinned canonical SHA256 and includes only Enumerate, Count and required
preceding ABI slots; SDK2 exposes only its inherited read-only prefix. Research tools are excluded
from normal product publish. Source guards also reject patch/injection/attach and control APIs.

Debug events preserve every exception parameter and the OS context; MiniDumpWriteDump type 0x1061
(DataSegs, IndirectlyReferencedMemory, UnloadedModules, ThreadInfo) runs before exception
continuation, ClientPointers=false. Raw dump bytes stay local. Module path/base/size/RVA,
local symbol offsets and dump hashes are the shareable derived evidence. No persistent WER/
AeDebug changes or external network symbol path is configured.

Both x64 Release CMake projects build with /W4 /WX. Software tests: 12/12 M2.7 model/source tests,
6/6 Gate A guard tests, 8/8 existing CI infrastructure tests, source guard passed. Native fixture
verification: completion, FAST_FAIL_FATAL_APP_EXIT=7, timeout, and three rejected CLI inputs = 6/6;
these are debugger implementation checks, **not vendor crash evidence**. The first fixture run
exposed a deadline-vs-fatal classification bug; its raw output is retained and corrected final
fixtures pass. No full product/WinUI suite or physical lighting acceptance was run in M2.7.

## Provenance and local artifacts

| Module | File version | SHA256 |
| --- | --- | --- |
| AacAudioHal_x64.dll | 1.3.95.0 | `c252aee03409db836aaebdd8062f6464eef2b1313fb79afa4ba0ad135009969f` |
| AacVgaHal_x64.dll | 0.0.8.2 | `600da404bd72a9d33b5088954f6af1f0fc2d9dd69a8caca1ed8ff3d1f2cf5eb5` |
| AacAIOFanHal_x64.dll | 1.5.10.0 | `7278c04ded4602cadb258add4657035aabcb606710a28bf6b2ba547ceaef622e` |
| AacKbHal_x64.dll | 1.3.46.0 | `52d575bf942b7551b3f120c446bf0d853e36f9225c6b9a17407a80e0b1829f04` |
| AacMouseHal_x64.dll | 1.2.1.34 | `b7b8eb624ccdf7cd0686cd8de05f74be2eff707515d3cc00c4de812cbac3ff8a` |
| AuraSdk_x64.dll | 3.7.5.0 | `0a94593c8c7f4e1af99abed0afe2803a6d615318ebe96d85529eb232f6a8b0af` |

Module versions/hashes were read externally after trials; they are not independently captured
before every trial. SDK hash is also logged inside exact preflight. Matching keyboard and mouse
Ghidra executable SHA256 confirm the static inputs; a .gpr file's empty-file hash is not provenance.
Final tool hashes are captured in final-tool-binary-hashes.json. The first pilot/timing binaries
preceded the GPU/CLI restriction revision and were not separately hash-frozen; their raw native
module/context/dump evidence is retained, with this limitation explicit.

Artifacts in audit_artifacts/phase3-m2.7:

- mta-trials.json: 18 real trials; fixture rows excluded. Each raw file records identity, COM/WinRT/
  QI stages, exception events and exact start/end times.
- crash-events.json: 19 complete exception records, including absolute addresses, flags, parameters,
  TIDs, module bases/RVAs, contexts and native frames.
- module-maps.json: modules before Enumerate and loaded during the call, per PID.
- stacks/: derived native exception stacks; long-call samples are empty because calls failed early.
- dumps/: local raw minidumps. dump-hashes.json publishes hashes/size only, not dump bytes.
- raw/: trial JSON and software/native fixture/build logs. Original defective fixture output retained.
- source-preservation.json, services-before/after.json, broker-files-preservation.json,
  previous-evidence-references.json: unchanged platform/ownership/M2.6 and broker evidence.
- Local Ghidra static-project and build trees are excluded from the evidence archive.

## Raw trial table

Times below are Asia/Shanghai (+08:00); machine-readable originals use UTC. The duration column
measures Enumerate entry to the first fatal event, excluding the selected activation delay.

| Trial | Start (+08:00) | PID | Caller TID | Fatal TID | Enum→fault ms | First module/RVA | Result |
| --- | --- | --- | --- | --- | --- | --- | --- |
| pilot-com-only | 2026-10-02T02:51:00.871+08:00 | 16684 | 29560 | 63268 | 43.89 | AacKbHal_x64.dll+0x107a8c | FailFast |
| pilot-exact | 2026-10-02T02:52:59.634+08:00 | 48172 | 54916 | 36728 | 115.5 | AacMouseHal_x64.dll+0x206054 | FailFast |
| timing-01-COM_ONLY-0 | 2026-10-02T02:54:43.500+08:00 | 50960 | 49808 | 60968 | 45.49 | AacMouseHal_x64.dll+0x206054 | FailFast |
| timing-02-COM_ONLY-0 | 2026-10-02T02:55:13.808+08:00 | 59412 | 57328 | 43856 | 42.21 | AacKbHal_x64.dll+0x107a8c | FailFast |
| timing-03-COM_ONLY-0 | 2026-10-02T02:55:44.105+08:00 | 20512 | 29560 | 61920 | 41.16 | AacMouseHal_x64.dll+0x206054 | FailFast |
| timing-04-COM_ONLY-250 | 2026-10-02T02:56:14.388+08:00 | 31540 | 5424 | 46428 | 51.74 | AacKbHal_x64.dll+0x107a8c | FailFast |
| timing-05-COM_ONLY-250 | 2026-10-02T02:56:44.967+08:00 | 13112 | 64152 | 66084 | 46.38 | AacMouseHal_x64.dll+0x206054 | FailFast |
| timing-06-COM_ONLY-250 | 2026-10-02T02:57:15.510+08:00 | 58816 | 64704 | 37340 | 43.78 | AacMouseHal_x64.dll+0x206054 | FailFast |
| timing-07-COM_ONLY-1000 | 2026-10-02T02:57:46.082+08:00 | 46304 | 38808 | 41976 | 54.73 | AacMouseHal_x64.dll+0x206054 | FailFast |
| timing-08-COM_ONLY-1000 | 2026-10-02T02:58:17.419+08:00 | 40456 | 52624 | 40464 | 47.34 | AacKbHal_x64.dll+0x107a8c | FailFast |
| timing-09-COM_ONLY-1000 | 2026-10-02T02:58:48.714+08:00 | 58400 | 41864 | 60000 | 45.27 | AacKbHal_x64.dll+0x107a8c | FailFast |
| timing-10-COM_ONLY-5000 | 2026-10-02T02:59:20.018+08:00 | 61536 | 33036 | 66184 | 45.36 | AacMouseHal_x64.dll+0x206054 | FailFast |
| timing-11-COM_ONLY-5000 | 2026-10-02T02:59:55.307+08:00 | 58952 | 63084 | 48112 | 51.31 | AacKbHal_x64.dll+0x107a8c | FailFast |
| timing-12-COM_ONLY-5000 | 2026-10-02T03:00:30.612+08:00 | 63316 | 23216 | 11460 | 49.25 | AacMouseHal_x64.dll+0x206054 | FailFast |
| minimum-controls-01-COM_ONLY-0 | 2026-10-02T03:08:59.138+08:00 | 61860 | 62728 | 52804 | 43.67 | AacMouseHal_x64.dll+0x206054 | FailFast |
| minimum-controls-02-RO_INITIALIZE-0 | 2026-10-02T03:09:29.470+08:00 | 6336 | 63236 | 54424 | 46.16 | AacMouseHal_x64.dll+0x206054 | FailFast |
| minimum-controls-03-SDK2_QI-0 | 2026-10-02T03:09:59.800+08:00 | 40016 | 65240 | 47796 | 46.84 | AacKbHal_x64.dll+0x107a8c | FailFast |
| minimum-controls-04-GPU_ONLY-0 | 2026-10-02T03:10:30.140+08:00 | 46040 | 12992 | 57624 | 43.72 | AacMouseHal_x64.dll+0x206054 | FailFast |

## Open questions and next review

The immediate fatal CRT/log formatting mechanism is identified; the exact argument/range failure
and vendor fix remain unknown. Read-only initialization order, activation delay up to 5 seconds,
repository CWD and GPU category did not provide a safe stable MTA path. No random COM security
initialization, message pump, elevated run, Job escape, log-path modification, binary patch or
exception-handler bypass is justified by this evidence. Five consecutive successful fresh-process
MTA Enumerate(0) calls have not been obtained (current phase: zero successes). Retain the production
STA path; any further compatibility investigation needs its own narrow review before additional
live vendor trials. Do not change ownership or restoration gates.

ReviewedGateAExecutionEnabled = false

GATE_A_MTA_STABILITY_BLOCKED
