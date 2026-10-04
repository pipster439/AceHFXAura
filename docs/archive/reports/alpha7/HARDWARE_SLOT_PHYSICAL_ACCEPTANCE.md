# HISTORICAL CHECKPOINT / SUPERSEDED

This alpha.7 investigation records its original device state and acceptance boundary. Later physical acceptance and package acceptance supersede its release status; historical failures and observations are retained unchanged. Current truth: [HardwareSlot closure](../../../release/HARDWARE_SLOT_RELEASE_CLOSURE.md) and [package candidate](../../../release/PACKAGE_CANDIDATE_AUDIT.md). Raw captures/private evidence remain local and are not committed.

# Hardware Slot Physical Acceptance — alpha.7

> **HISTORICAL CHECKPOINT / SUPERSEDED — 2026-10-03.**
> Physical evidence and earlier failure records are preserved below. Software CI
> statuses are historical; current release status is maintained only in
> [HARDWARE_SLOT_RELEASE_CLOSURE.md](../../../release/HARDWARE_SLOT_RELEASE_CLOSURE.md).
> These physical trials used alpha.6-labelled binaries; the alpha.7 source version
> does not relabel or repeat that evidence.

## Historical closure checkpoint — 2026-10-03

The fresh bare-selector AP proof remains PASS; the current closure additionally
passed the actual canonical runner, real WinUI ManualHold and real matching-slot
unplug/replug with zero automatic selector. Owner confirmed Aura 1→5→1 restores
shallow red-static / deep rainbow / shallow red-static, with Direct RGB OUT=0.
HardwareSlot core feature gates are closed in these tested paths. Mismatching
reconnect and active HostManaged-RGB→HardwareSlot transitions are software-covered,
not separately physically tested in this run. Package/upgrade/hosted CI gates
remain unexecuted; there is no publication authorization.

Current authoritative results, exact capture hash, phase counts, limitations and
patch scope: [HARDWARE_SLOT_RELEASE_CLOSURE.md](../../../release/HARDWARE_SLOT_RELEASE_CLOSURE.md).
Latest versioned software CI failed the persistent plugin reload daemon case;
physical evidence remains valid, but overall release closure is SOFTWARE_CI_BLOCKED.
The historical failures and pending states below describe earlier trials and
are retained as history; they do not override this dated closure checkpoint.

## Latest checkpoint — fresh bare-selector AP physical PASS (2026-10-03)

Current production HardwareSlot activation, after owner-confirmed shallow slot1
and Gear Link session closure, restored owner-confirmed deep slot5 A actuation.
Sealed uninterrupted capture: four BasicInfo OUT, one exact bare selector5,
completed IN sequence1/1/1/5; zero extra selectors, official companions, staged
Apply or reviewed magnetic/DKS/firmware-lighting setters through Owner reply.
**FRESH_BARE_51_00_AP_PHYSICAL_PASS. KEEP MINIMAL PROTOCOL.**

This supersedes the AP discrimination status of the older invalid trial below;
historical evidence is retained. It does not establish RT/DKS/lighting physical
acceptance or resolve the historical drift sender. Whole MVP acceptance remains
pending the canonical smoke, ManualHold, reconnect and other physical gates.
Owner reply arrived about27 minutes after POST; capture remained continuous and
contained no additional selector. Exact physical press timestamp is unknown.
Full timings, payloads, privacy scope and startup-tool recovery:
`FRESH_AP_BARE_SELECTOR_FINAL.md`. No production changes or companion additions.

## Historical fresh-AP bare-selector trial: INVALID / INCONCLUSIVE

The single current-production HardwareSlot activation on the fresh A contrast
sent exactly one selector5 and obtained BasicInfo1→5 with zero official companions
or bank-content writes. Owner confirmed shallow A before the test and later
reported deep A. However, the uninterrupted capture then contained unrequested
selectors3 and6 at UTC 2026-10-02T21:26:08–09, before the Owner physical test.
Final BasicInfo=6. Owner explicitly said no Profile/hotkey action and that the
deep-A test was after those extra selectors. Sender remains UNKNOWN.

Thus the initial transport segment PASS is retained, but the whole physical
discrimination is INVALID. Neither BARE_51_00_AP_PASS nor BARE_51_00_AP_FAIL is
established on this fresh baseline. The historical failure below is not upgraded,
and no official companion is justified. ManualHold/reconnect and release work
remain stopped. Owned daemon and capture are stopped; no automatic recovery or
repeat activation was attempted. See `FRESH_AP_BARE_SELECTOR_RESULT.md` and
`artifacts/fresh-ap-bare-selector-20261003-0452/`.

## Owner correction: physical FAIL; MVP NOT ACCEPTED

Owner explicitly reports that Aura's 5→1→5 selection/BasicInfo succeeded while
physical actuation and firmware lighting did not change. Thus:

- Selector transport: PASS.
- BasicInfo active-slot identity readback: PASS.
- Physical magnetic bank activation using 51 00-only: **FAIL**.
- Firmware lighting: **INCONCLUSIVE while Aura Direct RGB C0 81 is active**.
- Whole HardwareSlot MVP: **NOT ACCEPTED**.

ManualHold, reconnect, release readiness, version bump and packaging are stopped.
The earlier USB/runtime observations below remain evidence of identity changes,
and must not be read as physical bank-load proof. Next: official Gear Link control
with Aura Direct RGB stopped, followed by a capture/static companion audit.

### Official control follow-up checkpoint

Aura Direct RGB has now been gracefully stopped without deleting configuration.
Official Gear Link 1→5→1 and a subsequent 1→5 comparison completed with matching
BasicInfo and zero C0 81 in the switch windows. Owner reports slot 1 WASD RT / V
no DKS, then slot 5 ASD still RT / V no J / lighting all black. Thus official
control also did not show the expected cached distinctions. It does not justify
adding arbitrary companions to Aura. Full-sequence replay remains unexecuted.
The authorized Case B fresh-baseline branch used official slot 5 A=3.2 mm,
PRE5/POST5. Owner confirmed deep A; official slot 1 then restored shallow A.
One audit-only full official-method invocation back to slot 5 restored deep A,
Owner confirmed. This is AP-only physical evidence, with extra status/polling
refresh repetitions in the actual capture. It does not rehabilitate the earlier
51 00-only failure or verify RT/DKS/lighting. All baseline authoring writes are
separate from selection evidence. Full audit completed; no minimization followed.
See `HARDWARE_SLOT_PHYSICAL_ACTIVATION_AUDIT.md`,
`HARDWARE_SLOT_OFFICIAL_CONTROL.md` and `HARDWARE_SLOT_SEQUENCE_DELTA.md`.

Evidence root: `artifacts/hardware-slot-physical-20261003-033241/`.
All timestamps below are UTC; local timezone is Asia/Shanghai (+08:00).

| Gate | Current evidence |
|---|---|
| Current worktree Release build and native WinUI launch | PASS; incremental development launch, no packaging |
| Aura same-handle BasicInfo physical query | PASS; actual OUT / matching device IN |
| Controlled foreground 5 → 1 → 5 | PASS at USB / runtime observation level |
| Unmodified canonical hardware smoke runner | FAILED; original failures retained |
| Owner physical bank behavior / perceived latency | FAIL: actuation and lighting did not change |
| ManualHold physical regression | STOPPED; NOT EXECUTED |
| Reconnect physical regression | STOPPED; NOT EXECUTED |
| Whole-task physical acceptance | NOT ACCEPTED |

No production source, bank contents, safety policy, protocol, CI, release version,
or packaging flow has been changed for this acceptance session. Existing dirty
worktree changes are preserved. No commit, push, tag, package, or release performed.

## Environment and Profile setup

Preflight found no Gear Link / Armoury Crate frontend and no AuraOwnershipExperiment,
old Aura/daemon, or owned smoke writer running. ASUS background services were
retained. Process evidence: `state/process-before.json`.

Current Release daemon: `build/dev-winui/Release/aura_daemon.exe`.
Current WinUI: `winui/bin/x64/Release/net10.0-windows10.0.26100.0/win-x64/Aura.exe`.
Daemon instance: `78CB35F3-5B84-48BC-9B4B-52306A29B4A9`.
Binary product version remains `0.1.0-alpha.6`; version bump is deferred.
Fresh incremental development launch took 87,684 ms including native and WinUI
builds. See `logs/dev-launch.log` and `state/runtime-startup.json`.

Acceptance data is isolated under the evidence root's `data/` directory via
`AURA_DATA_ROOT`. No real user document was copied, migrated, or overwritten.
WinUI Computer Use created and **only Saved** these Aura document Profiles:

| Aura Profile | GUID | Backend | Existing bank |
|---|---|---|---|
| HW Slot 5 | ae368645-1ced-43fa-a899-e90b6abc7723 | hardware_slot | 5 |
| HW Slot 1 | b43db7b1-e783-457c-8365-6af3545347e1 | hardware_slot | 1 |

No manual Apply was used to prewarm the first BasicInfo gate. The native Profile
editor shows a slot picker restricted to 1–5, separate target/current slot fields,
hidden magnetic editing, ASUS bank-authoring guidance, and firmware lighting
ownership limitations. Screenshots are under `screenshots/`.

## Same-handle BasicInfo gate

POST `/api/device-profiles/hardware-slot/refresh`, before any selector:

- Query success; observed slot **6**; source **BasicInfo**.
- Runtime query duration **2.5574 ms**, one query.
- OUT retained frame **44133**, `2026-10-02T19:40:50.488393Z`.
- Matching completed IN frame **44134**, `19:40:50.489515Z`.
- USB response interval **1.122 ms**.
- Target USB address 12, MI_01 endpoints OUT 0x0D / IN 0x85.
- Request vendor payload: `12 00` followed by 62 zero bytes.
- Response prefix: `12 00 00 00 59 00 01 00 06 04 06 00`; active-slot byte
  vendor offset 10 = 6. Exact full payloads are in `state/usb-analysis.json`.
- Before/after health Clean; persistent safety quarantine false and unchanged.
- At this gate selector, staged Apply, and magnetic setter counts were **0**.

This proves real active-bank identity readback using Aura's current native path.
It does not read bank contents, RT parameters, DKS configuration, AP/DZ, or lighting.
No parser relaxation, FlushQueue workaround, or official companion was added.

## Unmodified smoke runner results

The requested `tools/hardware/run-hardware-slot-smoke.ps1 -AllowHardwareWrites
-SlotA 5 -SlotB 1` was actually executed. It is **not marked PASS**.

1. `smoke/`: Notepad foreground discovery exceeded its independent 15 s deadline;
   temporary rules were not installed, and no selector was sent.
2. `smoke-retry/`: discovery left CharMap foreground when temporary rules were
   installed. CharMap → slot1 and then Notepad → slot5 were real, successful,
   distinct decisions; the runner counted both inside the first Notepad stage
   and rejected its greater-than-one-attempt assertion. Original configuration
   was restored in finally.
3. Audit-only controlled copies added a **pre-binding real-foreground prerequisite**,
   without changing production or skipping the runner's safety/duplicate checks.
   Early copies did not meet foreground preparation in time and installed no rules.
4. `controlled-final/`: Notepad same-slot activation was query-confirmed no-op,
   followed by another recorded no-op attempt for decision 5; CharMap then sent
   one selector to bank1. The canonical attempt-count assertion still failed.
   No duplicate selector or staged Apply was observed. This same-decision extra
   no-op is a real diagnostic observation and needs separate review; its source
   is not claimed proven from a screenshot or an assertion message.

These failures remain available in console logs and smoke summaries. No runner
failure was rewritten as success, and no production workaround was made.

## Bounded controlled foreground acceptance

After restoring the failed runner's temporary configuration, a separate bounded
Computer Use sequence installed revisioned temporary Notepad / CharMap rules,
preserving original fallback/bindings. Each stage invoked a real fresh BasicInfo
refresh and saved diagnostics. This is separate acceptance evidence, **not a
canonical smoke runner PASS**.

| Foreground | Decision | Attempt | Requested / observed | Selector | Runtime total ms | Decision → completion ms |
|---|---:|---:|---|---|---:|---:|
| Notepad | 8 | 6 | 5 / 5 | one | 123.0079 | 161 |
| CharMap | 9 | 7 | 1 / 1 | one | 123.7107 | 128 |
| Notepad | 10 | 8 | 5 / 5 | one | 122.9453 | 127 |

All three stages: selected = active = intended Aura Profile GUID, dirty false,
health Clean, generation 1, quarantine false, HardwareSlot backend, empty magnetic
plan/effective targets, and zero M605 staged transactions.

Exact sequence OUT selector frames/timestamps:

| Retained frame | UTC | Slot |
|---:|---|---:|
| 153018 | 2026-10-02T19:55:21.353351Z | 5 |
| 154861 | 2026-10-02T19:55:36.098512Z | 1 |
| 156734 | 2026-10-02T19:55:51.087708Z | 5 |

Selector vendor payload is `51 00 00 00 <slot> 00` followed by 58 zero bytes.
Each actual change is independently followed by matching BasicInfo; the 123 ms
runtime totals include prequery, select, and verification. They are not measured
physical perceptual latency. Further exact USB timing is retained in the timeline.

Evidence: `state/controlled-sequence-summary.json`, the three
`state/acceptance-*.json` snapshots, screenshots, and compact activation timeline.

## Continuous capture and safety

The single USB capture ran continuously from `2026-10-02T19:33:09Z` through
official control, authorized fresh AP baseline and final full-method physical
confirmation, and stopped/flushed at `2026-10-02T20:37:07.8474554Z`.
Final SHA256: `86e964f30f8ef614e12a6e67872302184b69d9b4e7deb9aa78a692ef771e511c`.

Original pre-Owner-failure checkpoint parse: 29 BasicInfo OUT / 29 matching completed IN, 6 selector OUT
including documented preparation attempts, and **0** reviewed magnetic/DKS/
firmware-lighting setters or `50 55`. `51 31` count is 0. Existing Aura Direct RGB
`C0 81` streaming is recorded separately and is not bank authoring; physical
firmware-lighting presentation remains an ownership limitation to assess.

The retained capture is privacy filtered: ordinary key reports, serial queries,
USB string descriptors, and unrelated device traffic are discarded. It is not
a complete bus packet inventory. Reviewed selector/status/setter reports are
retained for exact acceptance auditing; filter code is preserved in the evidence.

In that initial controlled foreground window, selectors corresponded to the
documented decisions. Later Owner manual actions and official control have
separate timeline records; do not label the cumulative 31 selectors as the
original foreground test. Historical Gear Link 5 → 3 → 1
sender remains UNKNOWN. This session does not attribute that history.

## Next gate and release review

STOP for Owner review. No ManualHold/reconnect or release-readiness work follows
the physical failure. The full-method experiment established AP restoration
on its fresh contrast. The subsequent one-shot bare-selector trial lost authority
before physical checking due to additional selectors3 and6; the minimal physical
sequence and RT/DKS/lighting therefore remain unresolved.

Release decisions remain pending: resolve/review the canonical smoke runner's
preparation/count failure and observed extra no-op attempt; close physical
ManualHold/reconnect; assess whether current Direct RGB ownership actually blocks
the intended bank lighting experience; then version bump and package/runtime
smoke in a separately authorized phase. No release readiness is claimed yet.
