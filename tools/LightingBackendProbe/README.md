# Lighting backend read-only research probe (M2.8)

M2.9 extends **only `--abi` metadata extraction**: all library types, inherited interfaces, parameter
names/order/flags, recursive pointer/SAFEARRAY element types and invocation kinds are returned.
The runtime immutable query allowlist remains3/4/63; no new control/status getter is enabled.
M2.9 builds/evidence are separately retained under `audit_artifacts/phase3-m2.9`.

Standalone x64 C++20/Windows SDK and Python 3 tool; excluded from the product graph/publish.
No AuraSdk activation/enumeration or ownership/write interface exists in this tool.

```powershell
cmake -S tools/LightingBackendProbe -B audit_artifacts/phase3-m2.8/build -A x64
cmake --build audit_artifacts/phase3-m2.8/build --config Release
# Native --abi loads the installed LightingService TypeLib with REGKIND_NONE, without activation.
& audit_artifacts/phase3-m2.8/build/Release/LightingBackendProbe.exe --abi
# Explicit local read-only research. Three fixed isolated mediator runs, one WDL PnP enumeration.
python tools/LightingBackendProbe/run.py
# Fixed Local/Global mappings/events: read existing objects only; no event wait/consume/signal.
python tools/LightingBackendProbe/observe.py
# Recompute from the existing evidence and fixed local metadata files; no vendor COM.
python tools/LightingBackendProbe/analyze.py
# Pure software and source policy tests; never activate vendor COM.
python -B -m unittest discover -s tests -p test_lighting_backend_probe.py -v
```

The only mediator commands are immutable zero-input DISPIDs 3, 4, 63, `DISPATCH_METHOD`,
BSTR results, validated against the current canonical TypeLib. `IDispatch` is private implementation
plumbing; no command line, IPC or public generic invocation interface accepts a method ID/arguments.
Paths/CLSID are fixed; activation requires medium integrity, a nonzero interactive session and an
already-running LightingService. GetProfile/ownership status/matrix status/Hue/OLED queries are
not attempted. Windows LampArray **DeviceInformation only**, no control object/FromIdAsync.

The owning launcher kills only its own read-only child at 20 seconds; the native child independently
exits with code 124 at 20 seconds if the launcher disappears. This contains client-side faults/hangs;
it cannot guarantee the vendor LocalServer's own health or cancel a vendor-side queued operation.
Activation/query progress is flushed before each call. Failed/timeout results retain null fields.

MMF views request FILE_MAP_READ only; SECTION_QUERY/READ_CONTROL are metadata rights. A read-only
view lives only inside try/finally, copies bounded byte ranges and never returns a native pointer.
Header magic/version/dimensions/stride/format/active buffer are checked before payload access;
unbounded-integer size arithmetic is rejected against exact section size and the 128 MiB ceiling.
Concurrent header changes invalidate a sample. At most 100 samples/20 Hz over five seconds; each
sample copies/hashes a bounded active buffer, with no frame bytes in evidence. Zero-sized/missing
sections retain null headers/sizes. Event handles request READ_CONTROL only and never consume an
auto-reset event. No LastClientReadTick feedback, write view or producer is implemented.

Current evidence shows LMCAP is **desktop capture input**, not a verified device RGB output protocol.
Do not reuse this observer as a lighting producer. See the [backend decision](../../docs/architecture/AURA_BACKEND_SELECTION.md).
