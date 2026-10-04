# HISTORICAL CHECKPOINT / SUPERSEDED

This alpha.7 investigation records its original device state and acceptance boundary. Later physical acceptance and package acceptance supersede its release status; historical failures and observations are retained unchanged. Current truth: [HardwareSlot closure](../../../release/HARDWARE_SLOT_RELEASE_CLOSURE.md) and [package candidate](../../../release/PACKAGE_CANDIDATE_AUDIT.md). Raw captures/private evidence remain local and are not committed.

# Hardware Slot Official Control — alpha.7

## Current checkpoint

Official Gear Link selection was performed without editing bank contents:
Default → 1 → 5 → 1. BasicInfo responses matched the requested identities.
After the Owner asked for the configuration reference, a further official 1 → 5
selection established the current physical comparison point.

**Official control did not restore the expected cached magnetic differences.**
Owner confirmed on slot 1 that WASD exhibits RT and V has no DKS. After the
official return to slot 5, Owner reports ASD still has RT, V does not output J,
and lighting remains all black. A depth was not confirmed in that response.
Aura's earlier 51 00-only physical acceptance remains **FAIL / NOT ACCEPTED**.

## Current configuration reference

Source: a read-only snapshot of Gear Link's existing host Profile store, taken
2026-10-02T20:16:38.909Z. This is **not firmware magnetic-configuration readback**.
BasicInfo verifies only active-slot identity. No parameter getter was replayed.

| Field | Slot 1 | Slot 5 |
|---|---|---|
| Common actuation | 1.0 mm | 2.0 mm |
| A actuation | 1.0 mm inherited in host cache | 3.0 mm explicit in host cache |
| Common top/bottom deadzone | 0.2 / 0.2 mm | 0.3 / 0.2 mm |
| B deadzone | Common values in host cache | 0.4 / 0.1 mm explicit in host cache |
| Configured RT keys | W, A, S, D | W |
| RT sensitivity mode | Unified 0.2 mm | Independent press 0.5 / release 1.5 mm |
| Continuous RT | false | false |
| V DKS | No configured trigger object in host cache | DKS test action targets J |

Slot 5 also stores a unified sensitivity value of 0.1 mm, but independent mode
is true. **Do not describe its active intended RT settings as unified 0.1 mm.**
V's source logical ID is 1026 and the target J logical ID is 773, as confirmed
by the official shared SDK key enum. Raw trigger-position metadata is preserved
in the reference JSON; its complete physical lifecycle is not inferred here.
Slot 1's absent host DKS object is not a trusted firmware Standard readback.

## Direct RGB isolation

The uniquely identified development daemon was gracefully stopped using its
existing owned shutdown event at 2026-10-02T20:09:18.598792Z. Its lighting
configuration was preserved. ASUS services were retained. No production edit
was made. The official-control capture windows contain **zero C0 81** reports.

Stopping streaming did not visibly restore lighting: Owner reports all black.
The reason is unresolved; firmware-lighting restoration is not claimed.

## Evidence

- `artifacts/hardware-slot-official-control-20261003-0409/state/ui-actions.jsonl`
- `artifacts/hardware-slot-official-control-20261003-0409/state/slot1-slot5-host-reference.json`
- `artifacts/hardware-slot-official-control-20261003-0409/parsed/official-control-sequence.json`
- `artifacts/hardware-slot-official-control-20261003-0409/screenshots/`
- `artifacts/hardware-slot-physical-20261003-033241/state/direct-rgb-stopped.json`
- Continuous privacy-filtered USB capture:
  `artifacts/hardware-slot-physical-20261003-033241/usb/hardware_slot_acceptance_continuous.pcap`

The capture was stopped after final Owner confirmation at
2026-10-02T20:37:07.8474554Z; the privacy receiver finalized successfully.
Final SHA256: `86e964f30f8ef614e12a6e67872302184b69d9b4e7deb9aa78a692ef771e511c`.
The retained capture excludes unreviewed traffic and
ordinary key-input reports; absence claims apply to its reviewed command scope.

## Case B: fresh minimal baseline

This takes the previously authorized Case B branch: establish distinguishable
test bank state through official UI before interpreting Aura protocol differences.
At 2026-10-02T20:29:43.042Z, immediate official BasicInfo PRE returned slot 5;
the agent changed only the A actuation authoring value from 3.0 to 3.2 mm. The
POST at 20:29:43.319Z returned slot 5. Official host cache subsequently stored
KeyA raw 32. Other authored field values were retained.

The official per-key editor also resubmits its existing deadzone list: B top
0.4 / bottom 0.1 mm, unchanged. This intentional **baseline stage** physically
contains 51 4F ×4, 51 59 ×4 and 50 55 ×2, plus layout queries. These commands
are not part of the preceding pure Profile switch windows and cannot be used
as proposed switch companions. Its application-level authoring intent was one
A actuation change; the extra writes are official UI companion behavior.

Owner confirmed the new A=3.2 mm requires a deep first press. An official
selection back to slot 1, with no content writes, yielded fresh BasicInfo1 and
Owner confirmed shallow actuation returned. This gives a usable **AP physical
contrast** after baseline rebuilding. It does not validate the stale RT/DKS
configuration reference or lighting restoration.

One audit-only official-method sequence then selected slot 5 without authoring
bank contents. Owner confirmed the 3.2 mm deep actuation returned. See
`HARDWARE_SLOT_PHYSICAL_ACTIVATION_AUDIT.md` for the actual repetition deviation
and narrow physical result. ManualHold/reconnect/release remain stopped.
Final hardware identity is 5, while the untouched page selector still shows 1:
the harness deliberately did not mutate Gear Link's host selection store.
