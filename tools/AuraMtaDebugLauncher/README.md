# Fixed Aura MTA compatibility triage

Local research only. This launcher starts only the two repository crash probes or
the software fixture at fixed M2.7 build locations. It cannot accept another executable,
attach to an existing process, accept arbitrary arguments/categories, inject code,
change thread execution context, modify vendor binaries, or change services.
The product solution/publish graph does not include these tools.

Build from the repository root (x64, Release):

```powershell
cmake -S tools/AuraMtaCrashProbe -B audit_artifacts/phase3-m2.7/probe-build -A x64
cmake --build audit_artifacts/phase3-m2.7/probe-build --config Release
cmake -S tools/AuraMtaDebugLauncher -B audit_artifacts/phase3-m2.7/debug-build -A x64
cmake --build audit_artifacts/phase3-m2.7/debug-build --config Release
python -B -m unittest discover -s tests -p test_aura_mta_triage.py -v
```

No build or software CI step activates vendor COM. Software fixture example:

```powershell
audit_artifacts/phase3-m2.7/debug-build/Release/AuraMtaDebugLauncher.exe COMPLETE 0 unique-fixture-id
```

The command format is `fixed-profile fixed-delay unique-trial-id`. It writes exclusively
to `audit_artifacts/phase3-m2.7/raw/<id>.json` and refuses an existing output.
Native dumps stay under the sibling `dumps/` directory. No WER/AeDebug configuration is
changed. DEBUG_ONLY_THIS_PROCESS applies to the newly launched probe only.

Vendor profiles are COM_ONLY, RO_INITIALIZE, SDK2_QI, M2_5_EXACT_PREFLIGHT,
LAMP_PREFLIGHT, METADATA_DELAY and the fixed GPU_ONLY compatibility control.
Delays are 0, 250, 1000, 5000 ms, with a 35-second Enumerate observation budget.
M2.7 live trials are finished and unstable; **do not repeat the batch or execute ownership**.
`trials.py` preserves the already executed schedule and reduces the existing JSON offline
when called without `--run`. Its live scheduler waits 30 seconds after abnormal exits,
10 seconds after completed exits, and invokes only these fixed profiles serially.

Primary probe contains no device/light interface and no metadata getters. SDK2 QI exposes
only the inherited enumeration prefix. Its required collection IDispatch/_NewEnum slots
are retained before Count solely for ABI correctness; _NewEnum is never called.
Rich preflight is compiled into a separate executable. It observes registry, service,
file metadata and LampArray interface discovery, without opening a LampArray controller.

Stacks use local PDBs and export symbols without a network symbol path. Large export
offsets are approximate nearest labels, not identification of the executing function.
Addresses/module RVAs and the locally verified static CRT functions are the primary evidence.
The first pilot preceded the explicit image-path symbol loader fix; its module/RVA/context
are valid but its short unwind is not used to reconstruct the full call chain.

Fatal exception metadata takes precedence over a later exit-cleanup deadline. Dumps are
written while the debug event is stopped, before DBG_EXCEPTION_NOT_HANDLED continuation.
If fault termination itself stalls, the launched probe alone can be ended after ten seconds;
this is reported separately from an Enumerate timeout. No observed final vendor trial
needed this fallback. Ordinary long-call sampling briefly suspends/resumes a thread;
none of the current vendor trials lasted long enough to sample. Debugger overhead is
an observation limitation and cannot prove unobserved release/restoration behavior.
