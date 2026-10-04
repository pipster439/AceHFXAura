# alpha.7 Device Profile Runtime

## Ownership and compatibility

The daemon is the **only live owner** of `device-profiles.json`, selected desired Profile, active submitted Profile, dirty state, apply planning, and magnetic mutation invalidation. `MagneticControlService` constructs one `DeviceProfileRuntime` beside its one `M605Runtime`; all Profile and manual magnetic routes share its `write_mutex_`. WinUI exposes `ProfileControlClient`, a stateless HTTP transport. It has no production file writer or apply planner. If the daemon is unavailable, the client reports that error and does not write locally. The daemon keeps Profile CRUD available when the keyboard is absent.

The device Profile document is separate from `config.json/profiles`, which remain lighting recipes. Existing Automation v2 `activate_profile`, Reactive/Ripple, GSI simulation and lighting selection retain their prior meanings. Device-profile process bindings use distinct stable GUIDs and invoke the daemon activation coordinator; they do not reuse lighting Automation actions. See [Device Profile Automation](DEVICE_PROFILE_AUTOMATION.md).

The daemon reads the Phase 0/1 JSON schema directly: root/profile `schema_version: 1`, stable GUIDs, root `revision`, magnetic defaults and per-key settings, unmanaged lighting reference, automation metadata and opaque extension fields. A versionless v0 envelope is accepted and receives `schema_version: 1` on its next mutation. Unknown root/profile/magnetic/key/lighting fields survive normal mutations; update retains old unknown fields omitted by an older client. The first load creates a Desktop profile and imports only an existing `config.json` default lighting reference. An unreadable legacy config gives a nonfatal import warning. A corrupt existing Profile file is preserved and Profile routes report an error; manual magnetic routes still run. Writes use the existing named config mutex, on-disk revision comparison, flushed same-directory temporary file, and write-through replacement. Expected revision is required for every mutation and activation; mismatch returns HTTP 409.

## API on daemon Control port 19897

All routes are loopback-only. Mutations require JSON and the existing loopback Origin/Referer policy.

| Method | Route | Purpose |
| --- | --- | --- |
| GET | `/api/device-profiles` | List full Profiles and runtime summary. |
| GET | `/api/device-profiles/{guid}` | Get one full Profile and runtime summary. |
| GET | `/api/device-profiles/runtime` | Query daemon runtime state. |
| POST | `/api/device-profiles/create` | `name`, `expected_revision`. |
| POST | `/api/device-profiles/duplicate` | `profile_id`, `name`, `expected_revision`. |
| POST | `/api/device-profiles/rename` | `profile_id`, `name`, `expected_revision`. |
| POST | `/api/device-profiles/update` | `profile_id`, complete `profile`, `expected_revision`. |
| POST | `/api/device-profiles/delete` | `profile_id`, `expected_revision`. |
| POST | `/api/device-profiles/defaults` | `global_defaults`, `expected_revision`. |
| POST | `/api/device-profiles/activate` | `profile_id`, `reason`, `expected_revision`, optional `temporary_override`. |

Reasons are `Manual`, `Automation`, `Startup`, `Reconnect`, `Restore`. Activation returns `outcome: succeeded | deferred | failed`, operation outcomes, selected/active/dirty, document/runtime/mutation revisions and M605 session generation. A deferred activation (HTTP 202) persists the selected desired GUID. A failed apply is HTTP 409 with an explicit error and retains the desired selection. The native WinUI Profile page uses this API; automatic activation is not wired.

## State and apply transaction

`selected_profile_id` is durable desired state. `active_profile_id` means managed fields were fully submitted by this host in the current known M605 session; it is **not** keyboard/firmware readback. `dirty` means desired state has not been confirmed as a complete host submission. `mutation_revision`/`runtime_revision` advance for state changes; the document revision advances only for durable mutations or changed selection. `applied_` is a daemon host-intent map, never physical state.

An activation validates schema and temporary overrides, resolves device global baseline → Profile-local defaults → per-key overrides → temporary override, establishes the M605 transport epoch before planning, and preflights unsupported logical IDs and DKS/RT conflicts. Supported operations are ordered as DKS standard, RT off, actuation type 1 reset, actuation exceptions, deadzone table reset, all-key deadzone, per-key deadzone exceptions, RT on, DKS set. The daemon holds the shared mutation gate across the whole plan and final `M605AppliedRuntimeState` comparison. Any failure stops the plan, records per-operation errors, may best-effort resubmit prior intent while health and generation remain unchanged, then clears active and marks dirty. After a destructive deadzone reset, recovery reconstructs the known prior reset/base/exception set in forward order. Unknown prior state cannot be restored. This is not ACID rollback. Persistent M605 quarantine always wins.

**Actuation inheritance ownership:** `51 50` does not clear overrides. The officially captured type 1 sequence is now a fixed-layer-0 typed `ResetAllPerKeyActuationOverrides(common)` operation. Unknown/invalidated intent, first acquisition, common changes, exception removal or submission-shadow drift require reset(common) → effective exceptions in ascending logical-key order. No extra `51 50`, 68-key write fallback or fake per-key common restoration is used. A known same-base exception delta needs only its `51 4F`; clean repeat needs zero writes. Missing trusted baseline remains a blocker. Profile common never mutates durable `global_defaults`. See [typed contract and remaining physical gates](../hardware/M605_ACTUATION_RESET_PRODUCTION.md).

Root `global_defaults` is the durable **device/manual host desired baseline**, not firmware readback. Successful manual typed global actuation, deadzone and RT writes update only their corresponding root field atomically and advance the document revision; failed or indeterminate writes do not claim a new baseline. The manual path still invalidates active Profile intent. When an older document has a null actuation/deadzone baseline, the daemon can use a trusted saved ASUS HostProfile value for effective resolution and reports it separately as `effective_global_defaults` without rewriting the document. If neither source is known, an activation that needs that field fails before HID submission. Profile-local defaults never mutate `global_defaults`.

`GET /api/device-profiles/runtime` returns both root `global_defaults` and daemon-resolved `effective_global_defaults`. The Magnetic page uses these host-desired values for its editable global actuation and deadzone controls, preferring explicit root values; its Profile API snapshot is transient and never writes the document. The legacy global RT control is now write-disabled. Root RT data remains compatibility-only; normal Profile RT uses explicit per-key state. See [RT production model](PROFILE_RT_PRODUCTION_MODEL.md) for restore provenance and safety preflight. A Profile API failure leaves the manual global baseline unknown rather than seeding it from `SessionApplied`. The separate `/api/magnetic/status` `SessionApplied` fields continue to represent the current M605 session's submitted effective values and may differ from the manual baseline after a Profile all-key operation. That difference is shown only as secondary, non-readback information. A manual global Apply requires an explicit UI draft; entering the page or switching Profiles cannot turn the Profile submission into a manual baseline write.

All-key base setters preserve per-key shadow entries. Empty maps alone do not prove inheritance. Successful type 1 updates common, clears only actuation entries and establishes `per_key_actuation_table_known`; type 4 establishes deadzone table knowledge. Reconnect/indeterminate state clears both. Deadzone remains resetType 4 → `51 58` → exceptions. Final Actuation matching requires common-valued keys to have no explicit override. These are known-session host-submission shadows, not firmware readback or passive detection of later ASUS writes. Healthy same-generation recovery reconstructs known prior actuation intent in forward reset(common) → prior exceptions order. Unknown prior values are not invented; quarantine forbids recovery writes. Recovery success still leaves the failed target active unknown/dirty, not ACID rollback.

The verified per-key RT setter only expresses enabled state and press/release sensitivity. It cannot express Profile-wide RT top/bottom or separate-mode fields. Profile-wide RT customization therefore fails preflight with no HID writes; per-key RT overrides remain supported when the trusted HostProfile per-key enabled list and global press/release baseline are known. RT disable and DKS conflict resolution retain their baseline requirements. Failed/interrupted applies preserve declared/touched footprints, but these cannot discover all external overrides. Deadzone uses its verified table reset to establish a clean submitted baseline; Actuation now uses the officially captured type 1 reset; unknown baseline and the other safety blockers remain fail-closed.

HTTP disconnect/cancellation is checked between typed operations and before final commit. It cannot interrupt an M605 stage already in progress; cancellation after a submission leaves active unknown and dirty, and may trigger best-effort prior-intent resubmission if health is clean.

## Profile-wide apply timing and batch limit

The existing manual batch routes still use one full transaction per selected key; arbitrary-subset batching remains unverified. Actuation common-only Profile apply now costs one type 1 reset transaction, plus per-key exceptions. Mandatory-wait floor is 0.61 s for common-only Actuation; deadzone still needs type 4 plus `51 58` (1.22 s floor), plus exceptions. Every operation retains 210 ms pre-Apply and 400 ms post-Apply; HID/latch/queue time is additional. The former 68-key wait floor was 41.48 s. No arbitrary stages share an Apply, and official URB timing does not authorize shorter settle. Physical latency/preservation validation of this implementation remains pending. Automatic Device Profile hardware activation remains disabled.

The Profile apply response includes a `timing` diagnostic object with total, document, planning and API processing durations; planned counts and executed durations by operation kind; M605 transaction count; queue and shared device-lock waits; transport connect, safety latch, stage/Apply submission and settle durations. These measure host execution and are not firmware acknowledgement. Normal diffing avoids writes for matching session intent, including a zero-operation repeat apply; changing an existing exception submits only its delta when intent is known; deleting an exception resets inheritance and replays retained exceptions. Additive diagnostics include `last_plan.actuation_ownership` (common target, exception count, reset requirement/reasons) and the host actuation-table knowledge flag; GET remains pure read. Dirty/unknown state requires conservative replay.

`M605Runtime` holds `NativeHidBackend::DeviceWriteMutex` across each full staged transaction and both waits. Host lighting `PushFrame` uses the same mutex. The common-value path reduces the number of repeated blocks without introducing unsafe HID concurrency; actual lighting smoothness remains a physical validation item. The WinUI primary Apply command is disabled for a saved, clean, active Profile, preventing an unnecessary repeat activation from the page.

The existing manual typed magnetic routes retain their alpha.6 safety checks and hardware APIs. Just before any possible manual submission, while holding the same gate, they invalidate active intent, preserve selected desired ID and increment mutation revision. On a successful global submission they also persist its desired baseline; a persistence failure is reported explicitly after submission because the hardware result cannot be rolled back. Batch partial submissions invalidate active intent. Profile-originated operations call `M605Runtime` directly under the gate and do not self-invalidate. A manual HTTP request during a Profile transaction receives the existing 409 busy response; it cannot interleave between Profile operations.

## M605 session generation and reconnect limit

`M605Runtime::GetSessionGeneration()` is a monotonic **host transport epoch**. It advances when a previously opened transport is discarded as stale, a fresh validated transport opens, the runtime enters indeterminate state, or explicit external resynchronization opens a fresh session. One remove/open cycle may advance twice. Repeated failed opens do not create verified sessions. Each transition clears `SessionApplied`. Profile state queries and each apply boundary observe the actual M605 transport, invalidate active/applied intent on generation change, retain selected, and mark dirty. Even a zero-operation apply validates the transport again before commit; a plan cannot adopt a new session mid-apply. Generation proves neither firmware readback nor persistence.

The native transport registers Windows HID interface arrival/removal notifications before enumerating/opening. The callback only marks its endpoint stale; it never closes a handle or performs HID I/O. Before arming a new transaction, the runtime checks that notification flag, validates the old endpoint, and opens the current path read-only to validate presence. Confirmed or suspect stale sessions are closed and re-enumerated under the shared device lock; only a fresh validated open permits submission. A failure before stage/latch is a normal unavailable result. Removal after submission begins, including during post-Apply settle, remains indeterminate and quarantined. There is no retry of an unknown transaction. Actual notification timing under physical unplug/replug still needs hardware validation; automatic reconnect **reapply is not wired**.

Quarantine is never cleared by restart, arrival, or a fresh open alone. The existing external-resynchronization confirmation now also requires opening a fresh validated M605 transport while idle. If offline, busy or unable to clear the durable latch, it refuses recovery. The acknowledgement sends zero HID reports; an operator must first restore the device externally. No evidence proves physical replug resets MCU staging RAM.

The existing daemon status snapshot is used only as a conservative availability gate before apply. It can cause a false deferral when M605 is actually reachable; it never increments M605 generation or counts as transport evidence. A verified M605 availability/reconnect signal is still needed.

## Lifecycle and follow-up

Phase 4A adds optional root `device_profile_automation`, a revisioned configuration API and a deterministic **decision-only** engine consuming the existing foreground observer. Phase 4A.5 adds the native draft editor and accepted Manual Apply hold notification inside the daemon selection boundary. Newer/invalid automation subsections are isolated and retained without blocking core manual Profiles. See [Device Profile Automation](DEVICE_PROFILE_AUTOMATION.md). Its decision is separate from selected/active/dirty; no automation caller invokes `ActivateProfile`. Hardware coordination remains deferred and disabled.

The runtime lives in the daemon without any WinUI instance. An attached/external daemon continues running if WinUI closes. Normal WinUI exit still stops the child daemon it started; longer background lifetime is a separate product decision. Deferred work: verified actuation inheritance reset, physical transport lifecycle validation, separate process-based device Profile binding, automatic reconnect reapply, daemon background lifecycle, and Automatic Lighting Ownership. Lighting references remain `LegacyUnmanaged` until ownership arbitration is designed.


## Profile editor revision reconciliation

WinUI keeps one short-lived draft and canonical base for its editing Profile GUID,
plus the daemon global/effective baseline and document revision. Selected/active
runtime identities are distinct from that editor target. Browsing never applies;
manual Apply is the existing explicit daemon API path and retains ManualHold.

A canonical list refresh compares the edited Profile object (including schema and
extensions) and both baseline inputs by JSON content. Selection, runtime, automation,
and unrelated Profile changes transparently rebase a preserved draft to the latest
revision. Dirty drafts with relevant changes require explicit conflict resolution;
a clean draft accepts latest content. Baseline comparison is intentionally conservative:
any baseline change conflicts with an unsaved draft, even if some fields are overridden.
Deleted targets remain visibly conflicted and cannot be silently resurrected.

Continue Draft accepts the latest canonical Profile/baselines as base while retaining
user values; Discard loads that canonical Profile and clears draft/conflict metadata.
Conflict identity belongs to the editing GUID and is cleared when another Profile is
chosen after the page's save/discard confirmation. This editor supports one draft,
not a multi-Profile draft cache. Continue is an explicit overwrite choice, not an
automatic field merge.

Save/Apply fetch the canonical list before submitting and still use expected_revision.
A race after that read still receives HTTP409: reload/reclassify, preserve draft, do
not silently retry. Own successful response revisions are accepted synchronously;
the page's one-second canonical read and local writes share the model busy boundary,
so an in-flight read cannot overwrite a successful local response. Cancellation
leaves the previous editor intact. Diagnostic reads and backend concurrency semantics
are unchanged.

## RT / DKS explicit Standard authoring

An absent/null DKS object means unmanaged, not Standard. The daemon's existing RT
preflight accepts an explicit effective Standard target or known Standard DKS in
the current M605 submission shadow. A non-Standard DKS target conflicts with RT;
unknown DKS blocks before any HID stage. Saved `MagneticHostProfile` does not expose
trusted per-key DKS knowledge. A physical RT gate observation says nothing about DKS.

The Profile editor conservatively asks for explicit document intent when an enabled
RT key has no Standard DKS object in its draft or inherited root key defaults. It
does not read or infer a firmware DKS table. This authoring warning can remain even
when the runtime has a known Standard session shadow: explicit intent survives a
new session. The page disables Save/Apply for known authoring blockers; document-only
serialization still accepts an unmanaged DKS object and old documents remain readable.
Existing document loading/serialization
and server preflight stay unchanged.

The RT section shows separate counts for keys without explicit Standard and keys
using DKS. **将所选按键设为标准模式** adds explicit `dks.standard=true` desired
objects, preserving the independent RT objects and parameters. Replacing existing
non-Standard DKS requires a native confirmation dialog. **保留 DKS，关闭冲突按键的
快速触发** keeps DKS and the managed RT objects, setting only their `enabled=false`.
Neither action writes hardware. Save remains a revisioned document update; Apply
uses the existing daemon plan (Standard before RT). RT toggle changes never silently
standardize a key.

Profile notification state is separate from polling state. Pure successful reads
do not dismiss Apply errors. RT/DKS authoring notices retain their sequence through
identical polls/renders, update only when the relevant blocker set changes, and clear
when that set is resolved. Other Apply/safety failures remain until explicit dismissal,
another explicit result, or changing the editor context. Success remains a six-second
transient notice; its timeout is bound to the notice sequence and success kind, so
it cannot close a newer blocker. Dismissed identities do not reopen during polling.

## RT ownership during Profile authoring

`rapid_trigger=null` (or absent) means unmanaged: removal of a previously managed
key requires restoration of trusted prior RT intent. An object with `enabled=false`
is instead explicitly managed disabled. Its parameters, continuous/unknown extension
fields, and ownership survive Save/load. The planner's unknown-prior removal guard
is unchanged; disabled RT is never canonicalized to an absent object.

The normal editor has one enable/disable switch. It modifies only `enabled` for
each managed selected key, preserving that key's own parameters and extensions.
**配置所选按键的快速触发** is a one-way draft action for missing objects; it preserves
already managed objects. If the same canonical Profile still has trustworthy RT
values for an accidentally removed draft object, this explicit action restores those
values. Otherwise it authors a new disabled object from the displayed editor values;
it does not claim to recover prior firmware state. There is no ordinary stop-managing
UI, and no automatic repair of persisted missing objects from SessionApplied.

DKS Standard, unmanaged DKS, opening/closing the DKS editor, and custom DKS authoring
do not mutate RT ownership. An enabled RT/custom DKS conflict remains invalid until
an explicit resolution: keep DKS and set RT `enabled=false`, retaining the entire RT
object, or replace DKS with Standard and retain RT. Save is document-only. Existing
Apply order is Standard before RT enable, and RT disable before custom DKS; unchanged
known submission is a no-op. Explicit relinquishment with unknown prior state still
fails before any hardware stage and presents a persistent safety error with technical
details.

### Disabled RT pipeline regression and diagnostic interpretation

The permanent key-1026 fixture covers load, explicit DKS Standard draft mutation,
actual revisioned HTTP update serialization, daemon atomic persistence, effective
resolution and manual Apply. A retained disabled object resolves to identity
`RapidTrigger:1026`, kind `PerKeyRtDisabledState`, with `enabled=false` and
`restore=false`, alongside `Dks:1026` / `DksStandard`. Removing that object after
unknown-prior acquisition must still fail with no new transport stages.

Diagnostic configured/enabled RT counts describe the currently selected saved
Profile. `last_plan` describes the last activation's Profile and document revision;
check its `profile_id` and `document_revision` before comparing it with those counts.
The RT target kind is `PerKeyRtDisabledState`, not a kind literally named
`RapidTrigger`; `RapidTrigger:<logical-id>` is the stable target identity.
Opaque document fields are retained by persistence but intentionally omitted by
the diagnostic allowlist; their omission there is not evidence of document loss.

A Profile already saved with `rapid_trigger=null` is genuinely unmanaged. Do not
infer deleted parameters from Standard DKS, count summaries, other Profiles or
SessionApplied. Recover only from trustworthy same-Profile canonical values, or
let the user explicitly author a new managed state. Debug and Release are separate
outputs: building Release CI does not update the Debug development app/daemon.


## Activation backends: host managed and onboard slot

Schema v1 adds optional `activation_backend` (`host_managed` when absent) and
`hardware_slot`. A hardware-slot Profile requires slot1–5; slot6 remains reserved.
Unknown backends fail closed. No existing Profile changes backend on load. Names,
GUIDs, automation bindings and opaque extensions retain their existing semantics.
Host magnetic fields can remain in a Profile when changing backend, but are inactive
in hardware-slot mode and hidden in the editor. The firmware bank contents are
maintained by ASUS Gear Link / Armoury Crate, never synchronized or rewritten by Aura.

The existing Runtime dispatches under its mutation gate. HostManaged uses the
existing planner. HardwareSlot uses only a typed bank selector and fresh BasicInfo
verification, with no magnetic target resolution, prior restoration, staged Apply,
reset, setter replay or polling-rate companion. ManualHold and P4B admission are shared.
Native output validation permits only65-byte reports `00 51 00 00 00 <slot1..6>`
followed by59 zeros, and `00 12 00` followed by62 zeros. Only slot1–5 is reachable
from ordinary Profiles. BasicInfo accepts a65-byte report with report ID0, command
12 00, reserved prefix zero, bank count6 and active slot1–6 at report offset11
(vendor payload offset10). No magnetic configuration is decoded or inferred.

The existing daemon hardware owner serializes query/select/confirmation with
DeviceWriteMutex and respects persistent quarantine and transport generation. A
fresh initial query can confirm a same-slot no-op; cache alone cannot. Selector
verification waits100ms before each of at most3 fresh queries (each native response
window100ms). Captured confirmed transitions were first observed at100.199,
100.460 and100.880ms; observation gaps are not firmware latency measurements.
Mismatch, invalid response or timeout leaves active unknown and dirty, with no
rollback/reselect loop. This operation does not arm/clear a staged magnetic latch.

`hardware_slot_status` separates desired from observed slot and timestamps/provenance.
Diagnostics GET and automation status remain cached, with no HID query. Explicit
POST `/api/device-profiles/hardware-slot/refresh` performs read-only verification
under normal loopback origin safeguards. The existing Runtime state path attempts
one BasicInfo observation per hardware transport generation (including first
observation/reconnect), never automatic slot reapply. While the selected backend
is hardware_slot, automation is enabled, the device is connected and safety is
Clean, the daemon additionally attempts one BasicInfo observation per10 seconds.
Failed observations reserve the same interval; there is no retry burst. This
bounded observer shares the mutation gate and never selects/reclaims a slot.
A matching reconnect confirms active without a selector; mismatch remains dirty.
Observed bank changes expire magnetic host shadow/prior knowledge. External writers
remain possible; last observation cannot guarantee an indefinitely unchanged bank.

HardwareSlot activation reserves firmware lighting ownership before selection.
The frame admission gate shares the Profile mutation mutex: it suppresses all
Aura Direct RGB frames, including the shutdown blackout, across bank activation,
external drift and disconnect. Startup with a saved hardware-slot Profile also
suppresses the first frame. No firmware lighting setter or unverified release
packet is sent. Physical restoration of a firmware effect remains an acceptance
gate; stopping frames alone is not a claim that firmware lighting has resumed.
Saving a backend edit does not release this reservation. A successful HostManaged
activation restores Aura frame admission. Rapid transitions are serialized with
frame submission, so no late frame can follow a completed bank selection.

Diagnostics expose lighting_owner, actual selector_count (not activation attempts),
and external_drift_observation interval/last attempt. A mismatching observation
clears active and marks dirty; the UI warns about external operations without
claiming a sender. Auto reclaim is not implemented. Foreground policy/ManualHold
still decides future activation; an observation itself does not create a new
automation decision or success notification.

Local opt-in smoke: `pwsh ./tools/hardware/run-hardware-slot-smoke.ps1
-AllowHardwareWrites -SlotA 5 -SlotB 1`. Create the two Aura hardware-slot Profiles
first. The tool uses Notepad/CharMap, reuses revisioned temporary-rule restoration,
queries actual slots before stage acceptance, and never authors bank contents.
CI/GITHUB_ACTIONS rejects write mode before HTTP or app launch. Without the switch,
only cached diagnostics are collected. Physical behavior is always an Owner gate.
