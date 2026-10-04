# Aura enumeration characterizer (M2.6 research only)

Independent medium-integrity research process; not referenced by the production solution or publish.
No vendor ownership or write slot is declared in its generated interface view. The generator checks
the exact existing canonical header SHA and copies only verified vtable prefixes, with unchanged IIDs,
types and slot order. A version2 inherited **base prefix only** exists for a fixed, read-only QI control.
The normal product, its STA worker and the blocked ownership candidate remain unchanged.

```powershell
cmake -S tools/AuraEnumerationCharacterizer -B build/aura-enumeration-characterizer-research -A x64
cmake --build build/aura-enumeration-characterizer-research --config Release
python -m unittest discover -s tests -p test_aura_enumeration_characterizer.py -v
```

Child presets: `--single STA|MTA category` (one call), `--single-version2` (same one-call rule,
QI of inherited read-only prefix); `--same-object`, `--fresh-objects`, `--fresh-objects-strict`
take two different approved categories. A `-winrt` suffix explicitly adds same-apartment RoInitialize
after the sole CoInitializeEx call, balances both counts, and records both HRESULTs. No message pump,
WDL controller, color accessor, arbitrary CLSID/DLL/method or output path exists in the child.
Invalid arguments fail before COM. Categories are the fixed eleven requested by M2.6.

Device metadata is limited to 16 devices and 256 name characters. Lights.Count is bounded to 4096;
optional per-light sampling is deliberately omitted: LightColorSampling=NotAttempted, samples=0.
The light collection prefix does not even expose per-light Item. Failed enumeration has nullable Count.

`characterize.py` orchestrates 66 sequential fresh children (three repetitions, alternating STA/MTA
order) and 24 separate cache tests; process times, PID/TID, OS identity, SDK hash/version, initializers,
HRESULTs, raw stdout/stderr and receipts are retained. It never executes an ownership candidate.
Only fixed read-only observation budgets 20/45 seconds are supported. A timed-out unowned research
child is terminated and marked incomplete; this is not restoration proof or a post-acquisition crash test.
The 45-second budget was used only for one optional cooled cache attempt in M2.6.

Retained old collection IUnknown references in one cache variant prevent pointer-address reuse;
the strict variant drops all references before the next object and deliberately does not compare addresses.
Cross-process pointer values never establish device identity. Fresh activation may still be influenced
by ASUS runtime state outside this process.

**Recorded M2.6 outcome: characterization BLOCKED.** STA remained empty and stable. MTA sometimes
returned real descriptors but frequently exited in Enumerate or exceeded the observation bound, even
with explicit WinRT initialization. Do not repeatedly rerun this matrix to manufacture success, change
production apartment, manipulate services, or approve ownership from partial successes.
See `docs/research/aura-enumeration-apartment-cache-analysis.md` and the M2.6 evidence/report.
