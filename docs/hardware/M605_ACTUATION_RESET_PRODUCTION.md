# alpha.7 — Typed Actuation inheritance reset

Implementation date: 2026-10-01. This document describes software implementation;
post-implementation hardware acceptance is still pending.

## Evidence and scope

The [official USB capture](M605_RESET_TYPE1_OFFICIAL_USB_CAPTURE.md) on firmware
**1.00.59** observed vendor payload `51 52 01 00 0A 00` plus 58 zero bytes,
identical IN echo, then `50 55` plus 62 zero bytes and identical echo. No extra
`51 50` followed reset. The owner separately confirmed M override removal and
inheritance of later common changes. [1.00.58 static analysis](M605_RESET_TYPE1_AUDIT.md)
supports clearing Actuation override flags and carrying common. These are distinct
version/evidence levels. RT/DZ/DKS/SpeedTap/remap preservation is statically supported,
but complete physical preservation testing remains pending.

## Typed production contract

- `BuildResetAllPerKeyActuationOverrides(double common_millimeters)` and
  `M605Runtime::ResetAllPerKeyActuationOverrides(double common_millimeters)`.
- Common uses the existing global Actuation conversion: finite 0.1–4.0 mm in
  0.1 mm increments, raw 1–40. No guessed default or firmware getter.
- Type is fixed to **1**, layer fixed to **0**. There is no reset-type argument,
  layer argument, reserved-byte argument or new generic reset dispatcher.
- Existing 65-byte Windows Report framing is reused: dummy `00`, vendor `51 52`,
  type `01 00`, common raw, layer `00`, then 58 zeros. The actual USB capture is
  **64 vendor bytes**, not a directly captured 65-byte WriteFile buffer.
- NativeHid validates type 1, common raw 1–40 and every fixed/reserved byte.
  Existing type 4 stays narrowly validated. Type 0/2/3 and nonzero layer are rejected.
- One normal safe M605 worker transaction: shared device mutex, stale-session
  preflight, durable latch Arm, one reset stage, existing **210 ms** pre-Apply
  settle, one `50 55`, existing **400 ms** post-Apply settle, session check, latch
  Clear, then shadow update. USB capture timing does not shorten these waits.
- No capture artifact is replayed. No new HTTP reset/debug button is exposed.

The existing production transport establishes host submission, not firmware
readback. The captured echo is protocol evidence; this change does not add or
claim a new runtime firmware getter/ACK verification mechanism.

## Shadow and planner

Success updates `global_actuation_raw`, clears `per_key_actuation_raw`, and sets
`per_key_actuation_table_known=true`. This means **host-submission ownership in
the known session**, not complete hardware truth. RT/DZ/DKS/SpeedTap/lighting
shadow fields are not modified. A later external ASUS write is not passively
detected merely because the table was once reset.

Device Profile baseline → Profile default → per-key → temporary resolution is
unchanged. Profile common is ephemeral; only the existing successful manual
global route persists the durable baseline. Profile reset never uses that route.

| Transition / condition | Plan |
| --- | --- |
| First ownership, unknown table, dirty/manual invalidation, generation change | type1(effective common) → exceptions |
| Common change | type1(new common) → all remaining exceptions |
| Exception removal or effective exception returns to common | type1(common) → remaining exceptions |
| Known same common, one existing exception changes | one `51 4F` delta |
| Known same common, one new exception | one `51 4F` delta |
| Clean identical effective target in same healthy session | zero hardware operations |
| Missing trusted baseline, unsupported keys, RT/DKS safety blockers | no hardware submission |

Exceptions are submitted in ascending logical-key order. Internal effective
targets can enumerate managed keys for final comparison, but common-valued keys
are **not** materialized as 68 per-key writes. A common-only first apply is one
reset transaction; common plus W exception is two. There is no additional `51 50`.
Final matching requires inherited keys to have no explicit actuation shadow
override; a redundant per-key common value is not accepted as inheritance.

## Failure and recovery

Open failure before latch/stage does not quarantine. Once a stage may have been
submitted, stage/Apply/post-settle removal failures preserve existing persistent
quarantine. Reset success followed by exception failure cannot commit active/clean.
Session change between operations cannot adopt a new session or commit clean.

Only with unchanged generation and healthy non-quarantined runtime can known
prior intent be re-submitted. Actuation recovery uses **forward reset(prior
common) → prior exceptions**; it never restores inheritance via a fake per-key
common write. Unknown prior values are not invented. Outcome remains failed,
active unknown and dirty even if best-effort re-submission succeeded. This is not
ACID rollback. No unsafe retry or automatic quarantine clearing was added.

## Diagnostics and automation

Additive diagnostic fields: host shadow `per_key_actuation_table_known` and
`last_plan.actuation_ownership` containing `common_target_mm`, `exception_count`,
`reset_required`, `reset_reasons`, fixed `layer=0`. Operation kinds distinguish
`ResetAllPerKeyActuationOverrides` from `KeyActuation`; timing counts real planned
and executed operations. Reasons include `UnknownOrInvalidatedIntent`,
`FirstOwnershipAcquisition`, `UnknownOverrideTable`, `CommonChange`,
`ExceptionRemoval`, and `SubmissionShadowDrift`.

Diagnostic GET remains cached pure read: no presence probe, connect, HID, generation
advance, Profile mutation or quarantine mutation. No raw packets were added to
diagnostics. Diagnostic schema/API versions stay 1; fields are additive.

**Automatic Device Profile hardware activation remains disabled.** Phase 4A/4A.5
decision evaluation does not call `DeviceProfileRuntime::ActivateProfile`; Phase
4B is not connected. Manual Save/Apply and ManualHold continue their existing paths.

## Physical acceptance gate

Software/mock results do not establish release PASS. Await owner authorization:

1. Global baseline 1.0 with external M4.0 → inherited Profile Apply → M≈1.0;
   later manual global1.5 and Desktop Apply → M follows1.5.
2. Common4.0 → all non-exception keys≈4.0; common4.0/W0.5 → W≈0.5.
3. Remove W exception → genuine common inheritance; change common later to verify
   W follows it instead of retaining an explicit4.0 override.
4. Apply, unplug/replug, Apply → fresh generation, reset ownership rebuilt,
   no stale-handle1167 false quarantine. In-flight removal must still quarantine.
5. Compare perceptible RT/DZ/DKS before/after type1-containing Apply; also validate
   SpeedTap/remap preservation before extending use. Stop expansion on any anomaly.
6. Check latency and lighting with common-only and exception plans. Mandatory wait
   floor is0.61s per transaction, not a physical measured latency claim.

Other layers/onboard banks/power-cycle behavior remain unverified. ResetType0,
Profile-wide RT, automatic reconnect replay, Lighting Ownership, background
lifetime, Hall telemetry/51 2C and Phase4B remain outside this implementation.
