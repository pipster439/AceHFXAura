# Aura read-only apartment / enumeration cache analysis — M2.6

2026-10-02 (Asia/Shanghai). **Read-only; Gate A remains blocked. No ownership/write method executed.**

## Synthesis / experimental answers

**Q1 — Not established as a reproducible Count=3 versus Count=0 rule.** STA was stable: 66/66 fresh category observations across two initialization variants returned S_OK and Count=0. MTA returned descriptors in a few observations but predominantly exited in Enumerate or exceeded the 20-second bound. Apartment selection was the only changed flag within each variant pair; runtime timing and initialization still confound an unconditional count claim. Explicit same-apartment RoInitialize did not make MTA reliable.

**Q2 — Actual returned descriptors:** Vga 1 / Type 0x20000 / 23×1 / 23 lights; ROG STRIX LC III SERIES 1 / Type 0xD1000 / 4×1 / 4 lights; WindowsLighting_LED / Type 0x80000 / 19×6 / 114 lights. Three independent successful Count=3 samples have these same metadata tuples. Three returned index entries have differing process-local IUnknown addresses (diagnostic only); these are not three proven physical identities.

**Q3 — Incomplete.** A fresh MTA GPU-category process returned one Vga 1 object, whereas successful ALL queries returned three. This is direct evidence of one category-specific result consistent with filtering. It is not complete/reproducible membership for all eleven categories: failed/timeout rows remain UNKNOWN, not zero. Do not use lastsynclist indices as a universal get_Type/Enumerate map; the actual cooler Type is 0xD1000 rather than the approved 0x120000 request, and the WindowsLighting object Type 0x80000 conflicts with simplistic ARGB labels. No extra category was added to force a match.

**Q4 — Scope separated.** Test A under STA: same object, same collection IUnknown, even after an empty first result and reversed category order. Test B under STA: fresh activations in one process return different collection IUnknown values while old collection references are retained, so address reuse cannot masquerade as equality. The strict B variant drops every reference before B and deliberately does not compare addresses. This supports instance-local collection caching under STA and excludes one shared collection identity there. It does not exclude process-global backend data/state. MTA A/B returned no complete pair, including one optional cooled retry; its runtime cache scope remains UNKNOWN. Static this+0x10 cache storage is consistent with per-object caching but cannot fill the missing MTA runtime proof. Test C is the 132 fresh-process observations.

**Q5 — Cross-reference confidence.** GPU category mapping is STRONG: Vga Type and 23 light records match LastProfile/GetDeviceCap GPU records; Windows lists an ASUS-subsystem NVIDIA RTX 5070 Ti, but the Aura object has no unique PCI identity. Cooler category is STRONG from matching cached WaterCooler raw Type 856064 and its explicit ROG LC III name; exact physical product identity is PROBABLE because no independent PnP ID was returned. WindowsLighting adapter metadata is exact; linkage to the sole Falchion Ace HFX MI_04 LampArray is UNKNOWN, with only a PROBABLE keyboard candidate. Its name and rectangle are not identity proof. TUF GAMING X870-PLUS WIFI, two G.Skill DIMMs and ARGB cache entries exist but are not in the observed three-object collection.

**Q6 — Conditional scope, no ReleaseControl call.** Existing reviewed static owned-release code takes its instance cached collection, loops device Lights/Count/Item, calls canonical light put_Color(0) and device Apply, then delegates token/restoration helpers. With the observed three objects unchanged, the visible direct loop can issue up to 141 logical color-zero operations and three Apply operations: GPU 23, cooler 4, opaque WindowsLighting matrix 114. This is not a physical LED count, a guarantee all writes happen, or proof restoration succeeds. Switch/future cache mutation and later service restoration effects remain UNKNOWN.

## Concrete future ownership risk

A future Gate A could make the GPU Aura lights and the ROG LC III cooler lights black or hand them to an ASUS default effect during release. The 114-slot WindowsLighting adapter could also be affected; it is not proven to be the Falchion keyboard. Motherboard/DRAM/ARGB are absent from this direct cached loop, but the subsequent service restoration helper may affect other synced groups. More critically, unowned MTA Enumerate already produced process exits and hangs: a post-acquisition hard exit could discard the only release-capable object before RAII/watchdog cleanup. Do not approve ownership or switch production to MTA on this evidence.

## Initialization and failure evidence

Every matrix child performs exactly one CoInitializeEx, one activation and one Enumerate for one approved category. COM_only uses that initialization alone. COM_plus_WinRT adds RoInitialize with the same requested apartment, records its HRESULT (observed S_FALSE), and balances both lifetimes; it does not create another apartment. No WinRT LampArray/DeviceInformation preflight or message pump is performed by this characterizer. M2.5 used winrt::init_apartment plus earlier DeviceInformation/LampArray and shell/SCM/hash queries, and queried SDK2 before enumeration. M2 queried SDK2/3 but enumerated through the base pointer, used framed pipes and its parent job. Those differences are recorded, not treated as interchangeable. Fixed read-only version2-prefix QI controls were also attempted; they did not resolve the MTA exits.

Matrix child identity: Medium RID8192, non-elevated, session1, intended user SID recorded per result, inherited job=true. Exact SDK 3.7.5.0 SHA 0a94593c8c7f4e1af99abed0afe2803a6d615318ebe96d85529eb232f6a8b0af; registration Both, HKCU override absent. Coinitialize and activation HRESULT were captured before vendor enumeration through flushed progress lines. Exit 0xC0000409 was observed inside the Enumerate call span; this does not identify a faulting module/root cause. Application event query returned no matching events. Timed-out **unowned** children were terminated at the fixed read-only observation budget; no owner was acquired or deliberately crashed.

Binary variant hashes and complete PID/TID/UTC/identity/SDK/initializer/getter records are in aura-enumeration-matrix.json. Within each matrix both apartments use the identical binary. The two variant binaries differ in linked WinRT initialization support, so cross-variant comparisons cannot isolate RoInitialize alone. CLI invalid mode/category fails before COM. LightColorSampling=NotAttempted and sampledLightCount=0 everywhere; per-light getters are intentionally absent. No physical lighting or human restoration acceptance occurred.

## Fresh-process repeated counts

N = no Count returned; X = process exit; T = parent read-only timeout. These are not zero.

| Category | COM-only STA r1/r2/r3 | COM-only MTA r1/r2/r3 | COM+WinRT STA r1/r2/r3 | COM+WinRT MTA r1/r2/r3 |
|---|---|---|---|---|
| 0x0 | 0 / 0 / 0 | N(X) / N(X) / N(X) | 0 / 0 / 0 | 3 / N(X) / N(X) |
| 0x10000 | 0 / 0 / 0 | N(X) / N(X) / N(T) | 0 / 0 / 0 | N(X) / N(T) / N(T) |
| 0x20000 | 0 / 0 / 0 | N(X) / N(X) / N(T) | 0 / 0 / 0 | N(X) / 1 / N(X) |
| 0x30000 | 0 / 0 / 0 | N(T) / N(X) / N(X) | 0 / 0 / 0 | N(X) / N(X) / N(X) |
| 0x40000 | 0 / 0 / 0 | N(X) / N(T) / N(X) | 0 / 0 / 0 | N(X) / N(X) / N(X) |
| 0x50000 | 0 / 0 / 0 | N(X) / N(X) / N(X) | 0 / 0 / 0 | N(T) / N(T) / N(X) |
| 0x60000 | 0 / 0 / 0 | N(T) / N(X) / N(T) | 0 / 0 / 0 | N(X) / N(X) / N(X) |
| 0x70000 | 0 / 0 / 0 | N(X) / N(X) / 0 | 0 / 0 / 0 | N(X) / N(T) / N(X) |
| 0x80000 | 0 / 0 / 0 | N(T) / N(X) / N(X) | 0 / 0 / 0 | N(T) / N(T) / N(X) |
| 0x120000 | 0 / 0 / 0 | N(X) / N(X) / N(X) | 0 / 0 / 0 | N(X) / N(X) / N(X) |
| 0x2f0000 | 0 / 0 / 0 | N(T) / N(T) / N(T) | 0 / 0 / 0 | N(X) / N(T) / N(X) |

## Cache A/B raw comparison table

| Variant | Apartment | Test | Order | PID | Counts | Same collection | Same metadata | Outcome |
|---|---|---|---|---|---|---|---|---|
| COM_only | STA | --same-object | 0x10000→0x20000 | 45936 | [0, 0] | True | True | Succeeded |
| COM_only | STA | --same-object | 0x20000→0x10000 | 56460 | [0, 0] | True | True | Succeeded |
| COM_only | STA | --same-object | 0x0→0x2f0000 | 50392 | [0, 0] | True | True | Succeeded |
| COM_only | STA | --same-object | 0x2f0000→0x0 | 60900 | [0, 0] | True | True | Succeeded |
| COM_only | STA | --fresh-objects | 0x10000→0x20000 | 63784 | [0, 0] | False | True | Succeeded |
| COM_only | STA | --fresh-objects | 0x20000→0x10000 | 58836 | [0, 0] | False | True | Succeeded |
| COM_only | STA | --fresh-objects | 0x0→0x2f0000 | 38020 | [0, 0] | False | True | Succeeded |
| COM_only | STA | --fresh-objects | 0x2f0000→0x0 | 53868 | [0, 0] | False | True | Succeeded |
| COM_only | STA | --fresh-objects-strict | 0x10000→0x20000 | 62072 | [0, 0] | None | True | Succeeded |
| COM_only | STA | --fresh-objects-strict | 0x20000→0x10000 | 41348 | [0, 0] | None | True | Succeeded |
| COM_only | STA | --fresh-objects-strict | 0x0→0x2f0000 | 21432 | [0, 0] | None | True | Succeeded |
| COM_only | STA | --fresh-objects-strict | 0x2f0000→0x0 | 53460 | [0, 0] | None | True | Succeeded |
| COM_only | MTA | --same-object | 0x10000→0x20000 | 29332 | UNKNOWN | UNKNOWN | UNKNOWN | EXIT 0xc0000409 |
| COM_only | MTA | --same-object | 0x20000→0x10000 | 37016 | UNKNOWN | UNKNOWN | UNKNOWN | EXIT 0xc0000409 |
| COM_only | MTA | --same-object | 0x0→0x2f0000 | 66056 | UNKNOWN | UNKNOWN | UNKNOWN | TIMEOUT |
| COM_only | MTA | --same-object | 0x2f0000→0x0 | 59396 | UNKNOWN | UNKNOWN | UNKNOWN | EXIT 0xc0000409 |
| COM_only | MTA | --fresh-objects | 0x10000→0x20000 | 56788 | UNKNOWN | UNKNOWN | UNKNOWN | EXIT 0xc0000409 |
| COM_only | MTA | --fresh-objects | 0x20000→0x10000 | 52804 | UNKNOWN | UNKNOWN | UNKNOWN | EXIT 0xc0000409 |
| COM_only | MTA | --fresh-objects | 0x0→0x2f0000 | 17600 | UNKNOWN | UNKNOWN | UNKNOWN | TIMEOUT |
| COM_only | MTA | --fresh-objects | 0x2f0000→0x0 | 16272 | UNKNOWN | UNKNOWN | UNKNOWN | TIMEOUT |
| COM_only | MTA | --fresh-objects-strict | 0x10000→0x20000 | 58972 | UNKNOWN | UNKNOWN | UNKNOWN | EXIT 0xc0000409 |
| COM_only | MTA | --fresh-objects-strict | 0x20000→0x10000 | 62416 | UNKNOWN | UNKNOWN | UNKNOWN | EXIT 0xc0000409 |
| COM_only | MTA | --fresh-objects-strict | 0x0→0x2f0000 | 15324 | UNKNOWN | UNKNOWN | UNKNOWN | TIMEOUT |
| COM_only | MTA | --fresh-objects-strict | 0x2f0000→0x0 | 62300 | UNKNOWN | UNKNOWN | UNKNOWN | TIMEOUT |
| COM_plus_WinRT | STA | --same-object | 0x10000→0x20000 | 42696 | [0, 0] | True | True | Succeeded |
| COM_plus_WinRT | STA | --same-object | 0x20000→0x10000 | 60896 | [0, 0] | True | True | Succeeded |
| COM_plus_WinRT | STA | --same-object | 0x0→0x2f0000 | 66144 | [0, 0] | True | True | Succeeded |
| COM_plus_WinRT | STA | --same-object | 0x2f0000→0x0 | 56940 | [0, 0] | True | True | Succeeded |
| COM_plus_WinRT | STA | --fresh-objects | 0x10000→0x20000 | 63472 | [0, 0] | False | True | Succeeded |
| COM_plus_WinRT | STA | --fresh-objects | 0x20000→0x10000 | 47920 | [0, 0] | False | True | Succeeded |
| COM_plus_WinRT | STA | --fresh-objects | 0x0→0x2f0000 | 66388 | [0, 0] | False | True | Succeeded |
| COM_plus_WinRT | STA | --fresh-objects | 0x2f0000→0x0 | 57800 | [0, 0] | False | True | Succeeded |
| COM_plus_WinRT | STA | --fresh-objects-strict | 0x10000→0x20000 | 55760 | [0, 0] | None | True | Succeeded |
| COM_plus_WinRT | STA | --fresh-objects-strict | 0x20000→0x10000 | 56644 | [0, 0] | None | True | Succeeded |
| COM_plus_WinRT | STA | --fresh-objects-strict | 0x0→0x2f0000 | 63440 | [0, 0] | None | True | Succeeded |
| COM_plus_WinRT | STA | --fresh-objects-strict | 0x2f0000→0x0 | 62396 | [0, 0] | None | True | Succeeded |
| COM_plus_WinRT | MTA | --same-object | 0x10000→0x20000 | 44608 | UNKNOWN | UNKNOWN | UNKNOWN | EXIT 0xc0000409 |
| COM_plus_WinRT | MTA | --same-object | 0x20000→0x10000 | 61244 | UNKNOWN | UNKNOWN | UNKNOWN | TIMEOUT |
| COM_plus_WinRT | MTA | --same-object | 0x0→0x2f0000 | 50076 | UNKNOWN | UNKNOWN | UNKNOWN | EXIT 0xc0000409 |
| COM_plus_WinRT | MTA | --same-object | 0x2f0000→0x0 | 66356 | UNKNOWN | UNKNOWN | UNKNOWN | EXIT 0xc0000409 |
| COM_plus_WinRT | MTA | --fresh-objects | 0x10000→0x20000 | 63840 | UNKNOWN | UNKNOWN | UNKNOWN | EXIT 0xc0000409 |
| COM_plus_WinRT | MTA | --fresh-objects | 0x20000→0x10000 | 23024 | UNKNOWN | UNKNOWN | UNKNOWN | EXIT 0xc0000409 |
| COM_plus_WinRT | MTA | --fresh-objects | 0x0→0x2f0000 | 58780 | UNKNOWN | UNKNOWN | UNKNOWN | EXIT 0xc0000409 |
| COM_plus_WinRT | MTA | --fresh-objects | 0x2f0000→0x0 | 65824 | UNKNOWN | UNKNOWN | UNKNOWN | EXIT 0xc0000409 |
| COM_plus_WinRT | MTA | --fresh-objects-strict | 0x10000→0x20000 | 51248 | UNKNOWN | UNKNOWN | UNKNOWN | EXIT 0xc0000409 |
| COM_plus_WinRT | MTA | --fresh-objects-strict | 0x20000→0x10000 | 55504 | UNKNOWN | UNKNOWN | UNKNOWN | EXIT 0xc0000409 |
| COM_plus_WinRT | MTA | --fresh-objects-strict | 0x0→0x2f0000 | 54540 | UNKNOWN | UNKNOWN | UNKNOWN | TIMEOUT |
| COM_plus_WinRT | MTA | --fresh-objects-strict | 0x2f0000→0x0 | 63904 | UNKNOWN | UNKNOWN | UNKNOWN | EXIT 0xc0000409 |

Retained-identity B can keep the old collection alive after releasing the explicit SDK reference. Undocumented internal backreferences cannot be excluded. Strict B drops it; addresses may be reused and are not compared. Neither cross-process pointers nor empty counts prove stable device membership.

## Raw fresh-process result table

HRESULT is decimal signed; “?” means no returned HRESULT. Failed Enum never receives a fake Count=0. Full raw streams are archived.

| Variant | Apartment | Category | Rep | PID/TID | Init HR | Activate HR | Enum HR | Count HR / value | ms | Outcome |
|---|---|---|---|---|---|---|---|---|---|---|
| COM_only | STA | 0x0 | 1 | 6748/37880 | 0 | 0 | 0 | 0 / 0 | 44.9 | Succeeded |
| COM_only | MTA | 0x0 | 1 | 58228/51768 | 0 | 0 | ? | ? / None | 159.7 | EXIT 0xc0000409 |
| COM_only | STA | 0x10000 | 1 | 38700/62420 | 0 | 0 | 0 | 0 / 0 | 42.9 | Succeeded |
| COM_only | MTA | 0x10000 | 1 | 35324/59236 | 0 | 0 | ? | ? / None | 131.9 | EXIT 0xc0000409 |
| COM_only | STA | 0x20000 | 1 | 65452/60148 | 0 | 0 | 0 | 0 / 0 | 42.6 | Succeeded |
| COM_only | MTA | 0x20000 | 1 | 65980/60448 | 0 | 0 | ? | ? / None | 132.4 | EXIT 0xc0000409 |
| COM_only | STA | 0x30000 | 1 | 50328/61092 | 0 | 0 | 0 | 0 / 0 | 43.8 | Succeeded |
| COM_only | MTA | 0x30000 | 1 | 51776/66296 | 0 | 0 | ? | ? / None | 20020.5 | TIMEOUT |
| COM_only | STA | 0x40000 | 1 | 62124/57672 | 0 | 0 | 0 | 0 / 0 | 43.8 | Succeeded |
| COM_only | MTA | 0x40000 | 1 | 65572/16444 | 0 | 0 | ? | ? / None | 134.1 | EXIT 0xc0000409 |
| COM_only | STA | 0x50000 | 1 | 54324/62324 | 0 | 0 | 0 | 0 / 0 | 43.0 | Succeeded |
| COM_only | MTA | 0x50000 | 1 | 63644/40080 | 0 | 0 | ? | ? / None | 133.5 | EXIT 0xc0000409 |
| COM_only | STA | 0x60000 | 1 | 40148/56088 | 0 | 0 | 0 | 0 / 0 | 44.1 | Succeeded |
| COM_only | MTA | 0x60000 | 1 | 33260/62808 | 0 | 0 | ? | ? / None | 20031.1 | TIMEOUT |
| COM_only | STA | 0x70000 | 1 | 42568/48440 | 0 | 0 | 0 | 0 / 0 | 43.9 | Succeeded |
| COM_only | MTA | 0x70000 | 1 | 44608/61140 | 0 | 0 | ? | ? / None | 135.6 | EXIT 0xc0000409 |
| COM_only | STA | 0x80000 | 1 | 60968/64388 | 0 | 0 | 0 | 0 / 0 | 43.2 | Succeeded |
| COM_only | MTA | 0x80000 | 1 | 13360/64948 | 0 | 0 | ? | ? / None | 20024.1 | TIMEOUT |
| COM_only | STA | 0x120000 | 1 | 66176/66324 | 0 | 0 | 0 | 0 / 0 | 44.7 | Succeeded |
| COM_only | MTA | 0x120000 | 1 | 52656/48248 | 0 | 0 | ? | ? / None | 136.3 | EXIT 0xc0000409 |
| COM_only | STA | 0x2f0000 | 1 | 51636/8524 | 0 | 0 | 0 | 0 / 0 | 51.9 | Succeeded |
| COM_only | MTA | 0x2f0000 | 1 | 58228/49740 | 0 | 0 | ? | ? / None | 20025.1 | TIMEOUT |
| COM_only | MTA | 0x0 | 2 | 29100/65972 | 0 | 0 | ? | ? / None | 138.7 | EXIT 0xc0000409 |
| COM_only | STA | 0x0 | 2 | 64920/5108 | 0 | 0 | 0 | 0 / 0 | 45.9 | Succeeded |
| COM_only | MTA | 0x10000 | 2 | 63464/63664 | 0 | 0 | ? | ? / None | 134.9 | EXIT 0xc0000409 |
| COM_only | STA | 0x10000 | 2 | 8784/26764 | 0 | 0 | 0 | 0 / 0 | 46.6 | Succeeded |
| COM_only | MTA | 0x20000 | 2 | 59776/60932 | 0 | 0 | ? | ? / None | 131.9 | EXIT 0xc0000409 |
| COM_only | STA | 0x20000 | 2 | 26976/47868 | 0 | 0 | 0 | 0 / 0 | 45.7 | Succeeded |
| COM_only | MTA | 0x30000 | 2 | 63716/42696 | 0 | 0 | ? | ? / None | 143.9 | EXIT 0xc0000409 |
| COM_only | STA | 0x30000 | 2 | 65660/65656 | 0 | 0 | 0 | 0 / 0 | 46.0 | Succeeded |
| COM_only | MTA | 0x40000 | 2 | 63264/62044 | 0 | 0 | ? | ? / None | 20017.4 | TIMEOUT |
| COM_only | STA | 0x40000 | 2 | 65448/66488 | 0 | 0 | 0 | 0 / 0 | 45.4 | Succeeded |
| COM_only | MTA | 0x50000 | 2 | 51484/50960 | 0 | 0 | ? | ? / None | 138.3 | EXIT 0xc0000409 |
| COM_only | STA | 0x50000 | 2 | 57452/41316 | 0 | 0 | 0 | 0 / 0 | 46.6 | Succeeded |
| COM_only | MTA | 0x60000 | 2 | 27948/55788 | 0 | 0 | ? | ? / None | 150.1 | EXIT 0xc0000409 |
| COM_only | STA | 0x60000 | 2 | 57736/56668 | 0 | 0 | 0 | 0 / 0 | 46.0 | Succeeded |
| COM_only | MTA | 0x70000 | 2 | 60036/23020 | 0 | 0 | ? | ? / None | 146.0 | EXIT 0xc0000409 |
| COM_only | STA | 0x70000 | 2 | 37132/63544 | 0 | 0 | 0 | 0 / 0 | 44.1 | Succeeded |
| COM_only | MTA | 0x80000 | 2 | 29936/61696 | 0 | 0 | ? | ? / None | 139.5 | EXIT 0xc0000409 |
| COM_only | STA | 0x80000 | 2 | 61056/65748 | 0 | 0 | 0 | 0 / 0 | 46.0 | Succeeded |
| COM_only | MTA | 0x120000 | 2 | 20012/44640 | 0 | 0 | ? | ? / None | 134.7 | EXIT 0xc0000409 |
| COM_only | STA | 0x120000 | 2 | 60084/44972 | 0 | 0 | 0 | 0 / 0 | 44.3 | Succeeded |
| COM_only | MTA | 0x2f0000 | 2 | 17296/65836 | 0 | 0 | ? | ? / None | 20031.3 | TIMEOUT |
| COM_only | STA | 0x2f0000 | 2 | 61144/61612 | 0 | 0 | 0 | 0 / 0 | 46.9 | Succeeded |
| COM_only | STA | 0x0 | 3 | 66352/64072 | 0 | 0 | 0 | 0 / 0 | 46.8 | Succeeded |
| COM_only | MTA | 0x0 | 3 | 20276/53328 | 0 | 0 | ? | ? / None | 147.7 | EXIT 0xc0000409 |
| COM_only | STA | 0x10000 | 3 | 19632/46356 | 0 | 0 | 0 | 0 / 0 | 46.1 | Succeeded |
| COM_only | MTA | 0x10000 | 3 | 5152/29100 | 0 | 0 | ? | ? / None | 20025.0 | TIMEOUT |
| COM_only | STA | 0x20000 | 3 | 64788/45488 | 0 | 0 | 0 | 0 / 0 | 47.9 | Succeeded |
| COM_only | MTA | 0x20000 | 3 | 26316/62548 | 0 | 0 | ? | ? / None | 20019.5 | TIMEOUT |
| COM_only | STA | 0x30000 | 3 | 29712/64464 | 0 | 0 | 0 | 0 / 0 | 44.9 | Succeeded |
| COM_only | MTA | 0x30000 | 3 | 64992/10736 | 0 | 0 | ? | ? / None | 134.5 | EXIT 0xc0000409 |
| COM_only | STA | 0x40000 | 3 | 65284/64288 | 0 | 0 | 0 | 0 / 0 | 49.4 | Succeeded |
| COM_only | MTA | 0x40000 | 3 | 64572/63224 | 0 | 0 | ? | ? / None | 145.2 | EXIT 0xc0000409 |
| COM_only | STA | 0x50000 | 3 | 65712/62172 | 0 | 0 | 0 | 0 / 0 | 46.3 | Succeeded |
| COM_only | MTA | 0x50000 | 3 | 55952/57364 | 0 | 0 | ? | ? / None | 141.5 | EXIT 0xc0000409 |
| COM_only | STA | 0x60000 | 3 | 59788/57852 | 0 | 0 | 0 | 0 / 0 | 46.7 | Succeeded |
| COM_only | MTA | 0x60000 | 3 | 44916/61984 | 0 | 0 | ? | ? / None | 20019.1 | TIMEOUT |
| COM_only | STA | 0x70000 | 3 | 66336/38740 | 0 | 0 | 0 | 0 / 0 | 44.2 | Succeeded |
| COM_only | MTA | 0x70000 | 3 | 58464/10764 | 0 | 0 | 0 | 0 / 0 | 18254.2 | Succeeded |
| COM_only | STA | 0x80000 | 3 | 57012/53320 | 0 | 0 | 0 | 0 / 0 | 48.9 | Succeeded |
| COM_only | MTA | 0x80000 | 3 | 48648/60700 | 0 | 0 | ? | ? / None | 141.7 | EXIT 0xc0000409 |
| COM_only | STA | 0x120000 | 3 | 64992/28056 | 0 | 0 | 0 | 0 / 0 | 44.9 | Succeeded |
| COM_only | MTA | 0x120000 | 3 | 51376/59248 | 0 | 0 | ? | ? / None | 142.3 | EXIT 0xc0000409 |
| COM_only | STA | 0x2f0000 | 3 | 57800/65268 | 0 | 0 | 0 | 0 / 0 | 47.8 | Succeeded |
| COM_only | MTA | 0x2f0000 | 3 | 65208/55400 | 0 | 0 | ? | ? / None | 20026.9 | TIMEOUT |
| COM_plus_WinRT | STA | 0x0 | 1 | 59028/41128 | 0 | 0 | 0 | 0 / 0 | 44.9 | Succeeded |
| COM_plus_WinRT | MTA | 0x0 | 1 | 64380/48996 | 0 | 0 | 0 | 0 / 3 | 18171.0 | Succeeded |
| COM_plus_WinRT | STA | 0x10000 | 1 | 13420/47832 | 0 | 0 | 0 | 0 / 0 | 44.8 | Succeeded |
| COM_plus_WinRT | MTA | 0x10000 | 1 | 55148/39596 | 0 | 0 | ? | ? / None | 163.4 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x20000 | 1 | 61784/63604 | 0 | 0 | 0 | 0 / 0 | 44.0 | Succeeded |
| COM_plus_WinRT | MTA | 0x20000 | 1 | 63456/53480 | 0 | 0 | ? | ? / None | 132.5 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x30000 | 1 | 59668/60428 | 0 | 0 | 0 | 0 / 0 | 44.8 | Succeeded |
| COM_plus_WinRT | MTA | 0x30000 | 1 | 440/65752 | 0 | 0 | ? | ? / None | 138.2 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x40000 | 1 | 57596/63728 | 0 | 0 | 0 | 0 / 0 | 44.4 | Succeeded |
| COM_plus_WinRT | MTA | 0x40000 | 1 | 63492/56516 | 0 | 0 | ? | ? / None | 135.7 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x50000 | 1 | 60852/65600 | 0 | 0 | 0 | 0 / 0 | 44.9 | Succeeded |
| COM_plus_WinRT | MTA | 0x50000 | 1 | 50180/38132 | 0 | 0 | ? | ? / None | 20019.1 | TIMEOUT |
| COM_plus_WinRT | STA | 0x60000 | 1 | 29332/17544 | 0 | 0 | 0 | 0 / 0 | 44.5 | Succeeded |
| COM_plus_WinRT | MTA | 0x60000 | 1 | 66264/4668 | 0 | 0 | ? | ? / None | 136.5 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x70000 | 1 | 8784/49688 | 0 | 0 | 0 | 0 / 0 | 44.7 | Succeeded |
| COM_plus_WinRT | MTA | 0x70000 | 1 | 58780/61472 | 0 | 0 | ? | ? / None | 135.4 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x80000 | 1 | 60744/41364 | 0 | 0 | 0 | 0 / 0 | 43.2 | Succeeded |
| COM_plus_WinRT | MTA | 0x80000 | 1 | 63704/14976 | 0 | 0 | ? | ? / None | 20022.9 | TIMEOUT |
| COM_plus_WinRT | STA | 0x120000 | 1 | 66088/57060 | 0 | 0 | 0 | 0 / 0 | 43.8 | Succeeded |
| COM_plus_WinRT | MTA | 0x120000 | 1 | 63584/57612 | 0 | 0 | ? | ? / None | 135.8 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x2f0000 | 1 | 53900/41316 | 0 | 0 | 0 | 0 / 0 | 44.6 | Succeeded |
| COM_plus_WinRT | MTA | 0x2f0000 | 1 | 63188/66084 | 0 | 0 | ? | ? / None | 133.1 | EXIT 0xc0000409 |
| COM_plus_WinRT | MTA | 0x0 | 2 | 45844/49804 | 0 | 0 | ? | ? / None | 138.2 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x0 | 2 | 21384/54588 | 0 | 0 | 0 | 0 / 0 | 43.1 | Succeeded |
| COM_plus_WinRT | MTA | 0x10000 | 2 | 46300/61648 | 0 | 0 | ? | ? / None | 20026.0 | TIMEOUT |
| COM_plus_WinRT | STA | 0x10000 | 2 | 50380/65572 | 0 | 0 | 0 | 0 / 0 | 45.1 | Succeeded |
| COM_plus_WinRT | MTA | 0x20000 | 2 | 59596/45920 | 0 | 0 | 0 | 0 / 1 | 18239.9 | Succeeded |
| COM_plus_WinRT | STA | 0x20000 | 2 | 61544/60952 | 0 | 0 | 0 | 0 / 0 | 44.7 | Succeeded |
| COM_plus_WinRT | MTA | 0x30000 | 2 | 54440/64960 | 0 | 0 | ? | ? / None | 133.7 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x30000 | 2 | 56848/20064 | 0 | 0 | 0 | 0 / 0 | 44.6 | Succeeded |
| COM_plus_WinRT | MTA | 0x40000 | 2 | 49212/37872 | 0 | 0 | ? | ? / None | 137.7 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x40000 | 2 | 56940/59692 | 0 | 0 | 0 | 0 / 0 | 44.8 | Succeeded |
| COM_plus_WinRT | MTA | 0x50000 | 2 | 63608/66472 | 0 | 0 | ? | ? / None | 20027.7 | TIMEOUT |
| COM_plus_WinRT | STA | 0x50000 | 2 | 62500/56220 | 0 | 0 | 0 | 0 / 0 | 45.4 | Succeeded |
| COM_plus_WinRT | MTA | 0x60000 | 2 | 52616/47352 | 0 | 0 | ? | ? / None | 135.3 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x60000 | 2 | 49200/24460 | 0 | 0 | 0 | 0 / 0 | 45.2 | Succeeded |
| COM_plus_WinRT | MTA | 0x70000 | 2 | 34272/66236 | 0 | 0 | ? | ? / None | 20026.3 | TIMEOUT |
| COM_plus_WinRT | STA | 0x70000 | 2 | 61728/62988 | 0 | 0 | 0 | 0 / 0 | 43.2 | Succeeded |
| COM_plus_WinRT | MTA | 0x80000 | 2 | 57496/61372 | 0 | 0 | ? | ? / None | 20019.9 | TIMEOUT |
| COM_plus_WinRT | STA | 0x80000 | 2 | 61080/47376 | 0 | 0 | 0 | 0 / 0 | 45.5 | Succeeded |
| COM_plus_WinRT | MTA | 0x120000 | 2 | 55096/7032 | 0 | 0 | ? | ? / None | 134.9 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x120000 | 2 | 50120/57028 | 0 | 0 | 0 | 0 / 0 | 45.1 | Succeeded |
| COM_plus_WinRT | MTA | 0x2f0000 | 2 | 30380/23208 | 0 | 0 | ? | ? / None | 20026.9 | TIMEOUT |
| COM_plus_WinRT | STA | 0x2f0000 | 2 | 64284/55760 | 0 | 0 | 0 | 0 / 0 | 44.4 | Succeeded |
| COM_plus_WinRT | STA | 0x0 | 3 | 56896/61492 | 0 | 0 | 0 | 0 / 0 | 45.1 | Succeeded |
| COM_plus_WinRT | MTA | 0x0 | 3 | 59508/65284 | 0 | 0 | ? | ? / None | 138.3 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x10000 | 3 | 61092/53900 | 0 | 0 | 0 | 0 / 0 | 44.8 | Succeeded |
| COM_plus_WinRT | MTA | 0x10000 | 3 | 49788/13276 | 0 | 0 | ? | ? / None | 20023.4 | TIMEOUT |
| COM_plus_WinRT | STA | 0x20000 | 3 | 64140/57576 | 0 | 0 | 0 | 0 / 0 | 45.1 | Succeeded |
| COM_plus_WinRT | MTA | 0x20000 | 3 | 53964/59144 | 0 | 0 | ? | ? / None | 136.0 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x30000 | 3 | 38168/66320 | 0 | 0 | 0 | 0 / 0 | 44.1 | Succeeded |
| COM_plus_WinRT | MTA | 0x30000 | 3 | 66048/59136 | 0 | 0 | ? | ? / None | 136.8 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x40000 | 3 | 29720/49508 | 0 | 0 | 0 | 0 / 0 | 44.8 | Succeeded |
| COM_plus_WinRT | MTA | 0x40000 | 3 | 65000/56024 | 0 | 0 | ? | ? / None | 136.7 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x50000 | 3 | 59524/59748 | 0 | 0 | 0 | 0 / 0 | 46.0 | Succeeded |
| COM_plus_WinRT | MTA | 0x50000 | 3 | 63852/54480 | 0 | 0 | ? | ? / None | 132.6 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x60000 | 3 | 38948/65860 | 0 | 0 | 0 | 0 / 0 | 43.7 | Succeeded |
| COM_plus_WinRT | MTA | 0x60000 | 3 | 34956/15640 | 0 | 0 | ? | ? / None | 137.2 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x70000 | 3 | 65584/39180 | 0 | 0 | 0 | 0 / 0 | 45.1 | Succeeded |
| COM_plus_WinRT | MTA | 0x70000 | 3 | 54324/54376 | 0 | 0 | ? | ? / None | 134.6 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x80000 | 3 | 63856/65220 | 0 | 0 | 0 | 0 / 0 | 44.0 | Succeeded |
| COM_plus_WinRT | MTA | 0x80000 | 3 | 58372/36276 | 0 | 0 | ? | ? / None | 139.1 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x120000 | 3 | 66184/58004 | 0 | 0 | 0 | 0 / 0 | 44.3 | Succeeded |
| COM_plus_WinRT | MTA | 0x120000 | 3 | 60456/61872 | 0 | 0 | ? | ? / None | 133.9 | EXIT 0xc0000409 |
| COM_plus_WinRT | STA | 0x2f0000 | 3 | 62300/49504 | 0 | 0 | 0 | 0 / 0 | 44.4 | Succeeded |
| COM_plus_WinRT | MTA | 0x2f0000 | 3 | 59648/6140 | 0 | 0 | ? | ? / None | 137.2 | EXIT 0xc0000409 |

## Evidence / unchanged boundaries

`audit_artifacts/phase3-m2.6/` contains the merged matrix, independent variant matrices, 48 cache attempts, nine initialization/cooled controls, all raw streams, exact device metadata and confidence bridge, cached XML snapshots, host/PnP evidence, old M2.5 static release artifacts, tests/build logs, source/binary hashes and preservation checks. No sub-agents, new reverse engineering, DLL patch, vendor service changes, HID path or RGB sampling. Production code and the false Gate A execution gate remain unchanged.

GATE_A_CHARACTERIZATION_BLOCKED
