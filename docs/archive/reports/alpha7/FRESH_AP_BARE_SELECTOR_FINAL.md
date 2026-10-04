# HISTORICAL CHECKPOINT / SUPERSEDED

This alpha.7 investigation records its original device state and acceptance boundary. Later physical acceptance and package acceptance supersede its release status; historical failures and observations are retained unchanged. Current truth: [HardwareSlot closure](../../../release/HARDWARE_SLOT_RELEASE_CLOSURE.md) and [package candidate](../../../release/PACKAGE_CANDIDATE_AUDIT.md). Raw captures/private evidence remain local and are not committed.

# Fresh AP Bare Selector Final

## Conclusion

**FRESH_BARE_51_00_AP_PHYSICAL_PASS / BARE_51_00_AP_PASS**.

On the fresh, physically distinguishable A-key baseline, the current Aura
production HardwareSlot backend restored deep actuation using one bare `51 00`
selector and BasicInfo verification. Owner explicitly confirmed shallow A before
the test and deep A afterward. No additional selector was captured through the
Owner reply. **KEEP MINIMAL PROTOCOL**: this tested AP activation does not require
`25 00`, `25 01`, `51 31`, magnetic replay, or staged Apply.

The earlier whole-MVP physical failure remains historical evidence. Official
control also failed to reproduce the old host-cache distinctions, and the fresh
AP contrast now succeeds without companions. Stale/non-distinct bank contents
are a supported contributor to the earlier failure; this does not establish the
cause of every RT/DKS/lighting symptom or identify the historical drift sender.

## Physical baseline and isolation

- Slot1: Owner replied **“是的”** to the shallow-A PRE question.
- Slot5: prior fresh official baseline set A to **3.2 mm**, with deep first press
  already physically confirmed. No bank-content editing occurred in this case.
- Official BasicInfo PRE: `12 00`, real 64-byte response, slot1 at
  `2026-10-03T06:51:35.482Z`; webpage showed HFX / 配置文件 1.
- Gear Link tab and its WebHID session closed at `2026-10-03T07:09:41.929Z` through
  Computer Use. No Gear Link getter/refresh method was used during Aura activation.
- ASUS services were retained. Candidate handles are not treated as sender proof.
- Aura automation was already OFF in the existing isolated acceptance document;
  one Manual production activation was issued. No temporary automation rule was
  installed and no WinUI window was needed.

## Exact current production path

Current binary SHA256: `179f474e315c56af8f211b0dea866fd2194f3bfd0b3c2245acb561827ff6910b`.
Target Aura Profile: `ae368645-1ced-43fa-a899-e90b6abc7723`, backend
`hardware_slot`, slot5, expected document revision24.

1. POST `/api/device-profiles/hardware-slot/refresh`: confirmed slot1.
2. Immediate capture guard: no startup selector or prohibited companion/setter.
3. POST `/api/device-profiles/activate`: Manual, fresh expected_revision.
4. Production prequery1 → exact selector5 → production BasicInfo5 verification.
5. Stop further software actions; ask Owner the A-key question immediately.

| Retained frame | UTC | Direction / observation |
|---:|---|---|
|4|2026-10-03T07:12:26.335523Z|completed BasicInfo IN, slot1|
|6|2026-10-03T07:12:26.348521Z|completed BasicInfo IN, slot1|
|8|2026-10-03T07:12:26.427637Z|completed BasicInfo IN, slot1|
|9|2026-10-03T07:12:26.427751Z|exact selector OUT, slot5|
|12|2026-10-03T07:12:26.543801Z|completed BasicInfo IN, slot5|

Selector vendor payload: `51 00 00 00 05` followed by **59 zero bytes**,
64 bytes total. BasicInfo OUT: `12 00` followed by62 zero bytes.
Full request/response bytes, endpoints and completion status are retained in
`parsed/exact-usb-timeline.json`; frame numbers refer to privacy-retained records.

Production result: succeeded, selected=active=target GUID, dirty=false,
requested=observed=5, source=BasicInfo, selector_sent=true,
verification_result=confirmed, empty magnetic plan, zero M605 staged transactions.

## Timing and uninterrupted evidence

- Software startup-through-POST gate: **843.167 ms**.
- API call elapsed: **128.850 ms**.
- Runtime activation total: **126.935 ms**.
- Selector submission → completed slot5 BasicInfo: **116.050 ms**.
- Final continuous case starts `2026-10-03T07:12:24.951728Z`, sealed `2026-10-03T07:39:38.8493785Z`,
  duration **1633.898 seconds**.
- Owner question was issued immediately after the software gate. The reply was
  received at `2026-10-03T07:39:38.5718235Z`, **1632.027 seconds after POST**.
  Exact physical key-press time is not available; do not claim a measured
  immediate physical latency. Capture continued uninterrupted through the reply.
- Owner replied **“是的”** to whether A had restored the deep slot5/3.2 mm feel.
  This is user physical AP confirmation, not a numeric AP parameter readback.

Capture: `usb/fresh_ap_bare_selector_final.pcap`.
SHA256: `b720d45d4f89e676f507cf485ac1b254c3b8ae3fddb0528c7cdbcebd6950ca1b`. Privacy manifest: complete, hash verified.

## Whole final-case OUT counts

| Family | Count |
|---|---:|
|12 00 BasicInfo|4|
|51 00 selector5|1|
|Any other51 00|0|
|25 00 /25 01 /51 31|0 /0 /0|
|50 55 staged Apply|0|
|51 50 /51 4F AP|0 /0|
|51 58 /51 59 DZ|0 /0|
|51 52 reset /51 53 bulk RT /51 54 per-key RT|0 /0 /0|
|51 21 Standard /51 23 DKS|0 /0|
|51 2C /51 2D /50 40 firmware lighting|0 /0 /0|

There is no selector or bank-content replay gap between the valid case capture
start, startup, PRE, activation, POST and physical reply. The filter retains these
reviewed HFX MI_01 command families, descriptors and matching responses, while
excluding ordinary key input, serial/account information and unrelated devices.
`C0 81` Direct RGB is excluded from this retained capture. There was no existing
lighting-disable launch setting used; **do not claim RGB streaming OFF or prove
firmware lighting acceptance from this AP-only experiment**. AP is unaffected
by that lighting-ownership limitation.

## Harness startup issue and evidence boundary

An earlier startup attempt exited before HID initialization because the audit
caller passed a private shutdown-event name without creating the event.
`src/main.cpp` requires that event to exist. Its separate capture
`usb/startup_failed_no_hid.pcap` is retained with complete privacy manifest and
zero reviewed HID commands. The audit-only caller was corrected to create the
existing lifecycle event; no daemon/protocol/source change was made.

A subsequent passive-worker restart initially consumed its stale exit command;
no Aura startup occurred. Starting the worker with the current capture command
established the final continuous case before the first actual HID test.
Neither preliminary acquisition window is represented as part of the valid
case. The first actual production activation was issued exactly once.

After sealing the capture, the current owned daemon was identity-verified and
stopped via its existing private shutdown event; exit confirmed, no forced kill.
Configuration and daemon binary hashes are unchanged. Isolated document state
changes are limited to the existing production activation contract.

## Acceptance boundary and next owner review

- Bare51 00 + BasicInfo + fresh AP physical contrast: **PASS**.
- Historical unauthorized host selector sender: **UNKNOWN**; not attributed to
  Gear Link, Edge, ASUS services, Aura or Owner.
- Whole HardwareSlot MVP: **not yet fully accepted**. RT/DKS/firmware lighting,
  canonical smoke runner, ManualHold and reconnect physical gates remain open.
- No companion protocol research or production modification is justified by this
  result. After owner review, resume ManualHold, reconnect and external-writer
  robustness; assess remaining release blockers separately.
- No firmware/NVM implementation, wear or commit-timing claim follows from this
  test. No commit, push, package or release. Stop after this single physical result.

## Evidence references

Evidence root: `audit_artifacts/fresh-ap-bare-selector-final-20261003-135631/`.
`state/official-slot1-pre.json`, `screenshots/official-slot1-pre.png`,
`state/gear-link-isolation.json`, `state/aura-pre.json`,
`state/activation-request.json`, `state/production-activation.json`,
`state/software-ready.json`, `state/owner-physical-result.json`,
`parsed/post-usb-gate.json`, `parsed/exact-usb-timeline.json`,
`parsed/final-result.json`, `usb/*.privacy.json`,
`state/daemon-cleanup.json`, `state/final-hashes.json`, daemon/capture logs.
