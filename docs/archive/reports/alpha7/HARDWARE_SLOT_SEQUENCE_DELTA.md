# HISTORICAL CHECKPOINT / SUPERSEDED

This alpha.7 investigation records its original device state and acceptance boundary. Later physical acceptance and package acceptance supersede its release status; historical failures and observations are retained unchanged. Current truth: [HardwareSlot closure](../../../release/HARDWARE_SLOT_RELEASE_CLOSURE.md) and [package candidate](../../../release/PACKAGE_CANDIDATE_AUDIT.md). Raw captures/private evidence remain local and are not committed.

# Hardware Slot Sequence Delta — alpha.7

## Scope and status

Current HFX official UI control, firmware 1.00.59, Gear Link extension 1.00.28.
Transport evidence does not establish physical bank-load success. Owner reports
that official slot 5 selection did not restore the expected RT/DKS differences;
no companion has been added to production.

## Actual current switch sequences

In each ordinary official selector window, the reviewed OUT order is:

1. `51 00 00 00 <slot>` with the remaining 59 vendor bytes zero.
2. `25 00` with remaining vendor bytes zero.
3. `25 01` with remaining vendor bytes zero.
4. `12 00` with remaining vendor bytes zero.
5. `51 31 00 00 03` with the remaining 59 vendor bytes zero.

The refresh suffix repeats, with **actual counts** recorded below. Do not assume
all switches repeat it four times. Exact full OUT/IN hex, frames, order and UTC
timestamps are in `parsed/official-control-sequence.json` under the evidence root.

| Official UI action UTC | Selector target | RT gate query | SpeedTap query | BasicInfo | Polling write |
|---|---:|---:|---:|---:|---:|
| 2026-10-02T20:12:40.388Z | 1 | 4 | 4 | 4 | 4 |
| 2026-10-02T20:13:06.509Z | 5 | 4 | 4 | 5 | 4 |
| 2026-10-02T20:13:34.845Z | 1 | 2 | 2 | 10 | 2 |

Counts cover OUT only. BasicInfo includes explicit authority observations;
these additional reads must not be mistaken for a required activation step.
All three windows have matching device IN responses. Official selectors have
the same payload contract as Aura's current typed selector.

Aura's current activation uses BasicInfo pre-query, optional 51 00 selector,
and BasicInfo confirmation. It does not send the RT gate, SpeedTap or polling
refresh companions.

## Companion classification

| Command | Static responsibility | Classification | Physical activation necessity |
|---|---|---|---|
| 12 00 | BasicInfo, including slot identity | VERIFIED READ-ONLY / status refresh | Identity only; does not prove bank contents |
| 25 00 | getRapidTriggerState, boolean field at payload 4 | VERIFIED READ-ONLY / status refresh | NOT VERIFIED |
| 25 01 | getSpeedTapState, boolean field at payload 4 | VERIFIED READ-ONLY / status refresh | NOT VERIFIED |
| 51 31, payload 4 = 3 | setPollingRate | Actual polling-rate write during UI refresh | NOT VERIFIED as bank-load/commit action |
| 51 00 | Profile selection | Verified selector / identity change | Aura 51 00-only physical result FAIL |

51 31 is **not read-only**, even though it is called from a function named
refreshPollingRate. Main-source refresh calls setPollingRate using the host
Profile's polling index. Do not turn its observation into a bank-commit claim.

Connection initialization has separate 12 12, 12 03 and 27 00 activity. It was
not part of these three switch windows. Current switch windows show no 22 01,
50 55, 50 40, AP/DZ/RT/DKS content setters or firmware lighting setters.
No C0 81 output occurred in the official-control windows.

## Static source references

Original current public sources saved under
`artifacts/hardware-slot-official-control-20261003-0409/browser/`:

| Original | SHA256 |
|---|---|
| main-1.00.28-7038-1784769136-06d08f.js | AC9E44C8352962C59EDDB58C8D4A634B5A84C42900BE44E65BFCD0A4214A6779 |
| chunk-1.00.28-7038-1784769136-06d08f-BI62m6xD.js | BE3360D6D6C6D13F6325DF9C6383BF2117FB6235A3581638A33A9471EAE9E657 |
| useDeviceStore-CR2vYrqN.js | AAE03833B99BAD8142F95A2E1106E4BF0CE45A32CE2210C72D1CCFF4A6D6449E |

Existing derived beautified main: ordinary changeProfile path and refresh chain
around lines 16279–16303, RT refresh 16596, polling refresh 16379–16390, and
setPollingRate wrapper 16699. Shared SDK: Profile builder 5473, RT gate getter
4029, SpeedTap getter 4228, keyboard polling builder 7630. Minified symbols are
source navigation aids, not protocol contracts.

## Pending physical gate

Do not replay the chain until official control has a positive physical magnetic
result. Do not add the chain to production without a successful full-sequence
physical experiment and subsequent evidence-backed minimization.

The later Case B baseline rebuilding stage is separate: it contains official
51 4F/51 59 setters and 50 55 saves. Those are **authoring traffic**, not ordinary
selection traffic, and must not be copied into a slot-activation sequence.

## Completed one-experiment checkpoint

Fresh A=3.2 mm direct authoring had Owner physical PASS; an official slot 1
selection restored shallow A. One full official-method invocation then restored
deep A at slot 5, with zero content replay / Apply. Only Actuation is physically
confirmed in this comparison; cached RT/DKS and black lighting remain unresolved.

The invocation requested four refresh rounds. Actual capture contains nine
rounds, with one selector5 and exact same reviewed payloads. The extra rounds'
caller was not attributed. See `HARDWARE_SLOT_PHYSICAL_ACTIVATION_AUDIT.md`:
this is not an isolated exact-repetition replay. No necessity for 51 31, 25 00
or 25 01 is proven and none was added to Aura. Minimum sequence testing is
deferred to Owner review.
