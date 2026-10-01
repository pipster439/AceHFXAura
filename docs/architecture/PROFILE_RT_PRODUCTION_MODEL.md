# alpha.7 Device Profile RT production cutover

## Authoritative model

RT product model is explicit per-key state. A `keys[].rapid_trigger` object
owns that logical key's `enabled`, `press_mm`, `release_mm` values. Absent/null
means unmanaged; an object with `enabled=false` means explicitly managed OFF.
These states are not interchangeable. Physical keyboard master-switch state
is separate and is not written by this model.

Normal Device Profile RT uses `51 54` only. `separate_mode` remains an editor/
compatibility field; different values use press/release selectors, not a
firmware separate-mode bit. Continuous is fixed OFF by the typed writer.
Unknown extension fields survive persistence and C# clone/edit round trips;
explicit unsupported continuous values block validation rather than being
discarded or submitted.

`global_rapid_trigger` remains compatibility-only in all layers, including
root defaults. The daemon now performs a narrow, backed-up v1 host-intent
migration on load: empty objects are removed; complete enabled intent with
neutral zero top/bottom and continuous OFF becomes explicit per-key objects
for the audited 68-key scope. Existing explicit state wins. Unsupported or
ambiguous legacy data is retained and the unchanged activation safety blocker
still applies. New writes cannot create/edit legacy objects. This is document
migration only, not hardware Apply or a firmware common/inheritance model.
See [Legacy migration contract](LEGACY_GLOBAL_RT_MIGRATION.md).

## Captured protocol and typed boundary

Official Armoury Crate captures A–F on firmware **1.00.59** contain zero `51 53`.
They contain `51 54` with 64-byte vendor payload:

| USB offset | Meaning |
|---|---|
| 0–1 | `51 54` |
| 2–3 | selector LE16: 0 both sides, 1 press, 2 release |
| 4–5 | verified wire ID LE16 |
| 6 | sensitivity raw, 0.1 mm units |
| 7 | continuous; captured OFF only |
| 8 | explicit enable 0/1 |
| 9–63 | reserved zero |

Existing transport framing adds dummy ReportID 0: the typed `Report` has 65
bytes; capture fixtures and tests distinguish the two lengths. Builders accept
verified logical keys, sensitivity 0.1–2.5 mm in 0.1 steps, and a boolean enable.
There is no public raw selector, wire, reserved bytes, layer, or continuous-ON
parameter. Pair builder reuses the semantic Press/Release builders.

`SetPerKeyRapidTriggerUnified/Press/Release` reuse the existing FIFO worker,
shared device mutex, transport preflight, persistent latch, stage, 210 ms
pre-Apply settle, `50 55`, 400 ms post-Apply settle, and generation checks.
There is no timing reduction, cross-key staged grouping, or unsafe retry.
Existing manual pair setters continue as two stages followed by one Apply.
The Profile planner's independently typed selectors each use one transaction.

## Decision table

| Transition | Plan |
|---|---|
| First explicit state, equal sensitivity | one Unified, including enable |
| First explicit state, different sensitivities | Press then Release |
| Same owned session state | zero operations |
| Press only changed | Press only |
| Release only changed | Release only |
| Enable changed, equal values | Unified with target enable |
| Enable changed, different values | Press + Release, both target enable |
| Add key | only new key's complete state |
| Delete managed object, trusted prior exists | restore prior complete intent, then release ownership |
| Delete object, prior unknown | preflight failure, zero HID for entire plan |
| Manual invalidation/session replacement | invalidate active; fully resubmit managed targets when safe |
| Legacy global object | preflight compatibility blocker, zero HID |

Operations sort deterministically: RT OFF before DKS assignment; RT ON after
explicit Standard, then logical key and selector. No other 67 keys are written
to acquire RT ownership. Actuation/Deadzone table-reset semantics are separate
and unchanged.

## Prior-state provenance

Before acquisition the daemon records a key's trusted durable per-key baseline,
otherwise a complete pre-acquisition SessionApplied state in this generation,
otherwise complete trusted saved HostProfile intent. It never substitutes the
Profile's own last write as the original prior state. Restoration uses the
current durable per-key baseline preferentially and preserves complete values
including disabled state. On successful restoration the ownership footprint
and prior record are released.

Session/manual invalidation discards remembered pre-acquisition intent. A
surviving ownership footprint prevents accidentally re-capturing our old
Profile writes as external prior intent. A later removal therefore needs a
trusted durable baseline or a complete saved intent, or is blocked.

The existing ASUS XML adapter parses a known enabled list and shared editor
press/release values, with active saved-profile identity. These are **saved host
intent**, not firmware per-key readback. Its verified schema has no continuous
field; `rt_continuous` remains unknown. Such a partial HostProfile alone does
not authorize full restoration. It may continue providing the existing manual
DKS/RT-OFF safety parameters; that compatibility path is explicitly separate.
No new XML field is guessed. Root explicit per-key objects and complete current
session submissions can provide restoration today.

## DKS safety and failures

Custom DKS + enabled RT on the same key blocks preflight. Enabling RT does not
silently convert a DKS key to Standard. Unknown DKS also blocks RT ON unless the
Profile explicitly requests Standard or the current session shadow already
confirms the host submitted Standard. A fresh reconnect clears this knowledge:
an RT-only Profile may need an explicit Standard assignment before replay.
Known Standard is not redundantly rewritten for a mere RT parameter delta.

The existing manual API's **explicit** `resolve_dks`/`resolve_rt` confirmation
workflow is preserved for alpha.6 compatibility. It can perform Standard/RT-OFF
as separate transactions and reports uncertainty on partial failure. Normal
Profile RT activation does not opt into that manual workaround implicitly.

Stage/Apply/post-Apply failure stops, invalidates active, marks dirty, and keeps
persistent quarantine authoritative. Selector1 success/selector2 failure cannot
commit active. For clean pre-stage failure/cancellation, known prior Profile
intent may be forward re-submitted under existing recovery policy; it is not
ACID rollback. Any session change prevents clean commit and unsafe recovery.

## Shadow and diagnostics

Per-key SessionApplied includes enable, press/release raw and separate known
flags, continuous OFF. Selector0 updates both; selector1/2 updates only its side.
Shadow updates occur only after the safe transaction completes. A partially
known key is not presented as a complete RT state. RT success leaves Actuation,
Deadzone and DKS shadow untouched. All fields remain host-submission only.

Pure-read diagnostics expose `last_plan.rapid_trigger_management` counts,
`PerKeyRtUnified/Press/Release`, `restore`, `restore_source`, `disable`, legacy
presence, timing and generation. There are no raw HID exports or diagnostic
connect/probe/mutation actions.

## Remaining gates and limitations

### Physical gate observation (alpha.7 update)

The observed physical switch is independent of per-key Profile intent. OFF does
not delete objects, disable submitted keys, invalidate a clean Profile, or cause
any automatic write. Gate notification and input-collection generation are cached
separately from host-submission-only SessionApplied. See
[RT hardware gate evidence and contract](../hardware/M605_RT_HARDWARE_GATE.md).
Initial state without a notification is Unknown. The normal per-key writer and
M605 reconnect/quarantine/timing remain unchanged.

- New Aura RT planner/selector path requires physical acceptance after review.
- Continuous ON, other bank/layer and power-cycle behavior are unverified.
- `51 53` remains blocked/unverified for production use; static 1.00.58 layout
  is not a physical validation of firmware 1.00.59.
- External ASUS HID writes during a live Aura session cannot be passively
  detected; SessionApplied remains a submission shadow.
- Per-selector settle is 610 ms minimum. 68 equal-value RT keys require 68
  transactions (41.48 s mandated wait); independent values can require 136
  (82.96 s). No all-key shortcut is enabled. UI Profile HTTP timeout is reviewed
  in the implementation report. No automatic process Profile apply is enabled.

Phase4B remains disabled. Automation decisions do not call
`DeviceProfileRuntime::ActivateProfile`; no automatic HID write was added.
