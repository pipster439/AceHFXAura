# Alternative Aura first-write prerequisites — M2.10 final decision

**RUNBOOK ONLY. No executable write candidate or write command exists.**

**SERVICE_MEDIATOR_WRITE_BACKEND_REJECTED. S1 candidate preparation is closed on current evidence.**
The final bounded AuraPlugin lifecycle trace recovered ordinary application/replacement, but no unique
physical target plus complete current baseline, deterministic previous-state restoration, release and
bounded failure cleanup. Constant deviceId1, XML Group/led keys and persisted LastScript/LastProfile
are insufficient. cmd0 stops global engines and reloads current LastScript; read failure still changes
mode. Internal RestoreAll and ApplyLastMode are not validated rollback transactions.
See [lifecycle](../research/aura-profile-lifecycle.md),
[restore](../research/aura-profile-restore-analysis.md) and
`audit_artifacts/phase3-m2.10/backend-decision.json`.

No exact target/payload/apply/acquire/restore/release specification can safely be supplied. Do not
create an executable from the historical prerequisites below. No further generic static round or
control-only live experiment is requested. ServiceMediator remains read-only discovery; AuraSdk write
is unsupported; LMCAP is capture input; WDL remains a Falchion-specific candidate. GateA remains false.

## Historical M2.9 prerequisites (preserved, not an active candidate plan)

Gate S1 preparation status: **BLOCKED_ADDRESSING_AND_RESTORE_CONTRACT_UNKNOWN**.
M2.9 final decision: **SERVICE_MEDIATOR_S1_BLOCKED**. No target, payload, executable candidate or
write command is approved/prepared. Current TypeLib confirms the API shape, but GetProfile is an
E_NOTIMPL stub; matrix AP/control is global/persistent; no current-device matrix payload/normal
restore sequence is established. See [control surface](../research/servicemediator-control-surface.md),
[addressing](../research/servicemediator-addressing-model.md) and
[restore](../research/servicemediator-restore-contract.md).
LightingServiceMediator is preferred for read-only discovery; no lighting output backend has been
qualified. Gate L1 is not recommended: installed LMCAP_FRAME is desktop capture input. WDL requires
its own subset/control-opening/restore review. Archived AuraSdk Gate A remains false and must not be
used as a fallback. See [backend selection](../architecture/AURA_BACKEND_SELECTION.md).

## Evidence required before S1 can be prepared

1. Establish the exact target key/type/model/index and one zone/light addressing contract. Current
   nine logical records and 1122/45 Group discrepancy are insufficient. ARGB capacity is not physical
   strip length; do not select a whole group merely because no per-device mapping is known.
2. Establish the relevant control acquisition/release and baseline-restore semantics by targeted
   source/static/documentation review. M2.8 did not invoke any such method. A saved status/profile XML
   is evidence, not proof that replaying it is deterministic, transient or safe.
3. Prove a same-owner-process bounded normal restore path, including error/timeout behavior, with
   fixtures. Acquisition/write/restore calls need reviewed semantics and bounded behavior; a process
   timeout cannot be presented as physical restoration. Preserve baseline ownership/effect/settings.
4. Prepare a separate medium-integrity fixed-function candidate, outside the normal application,
   with no arbitrary vendor method/path/DLL/CLSID/target command. It must default to no write and stay
   disabled until source, binary hash and this runbook are reviewed.
5. Obtain explicit human approval for the named target, exact calls, visible effect, TTL and restore
   strategy. M2.8 authorization covers none of these writes.

Unknown restore means **stop preparation of an executable gate**, not choose a default script,
assume vendor auto-restore, release a different COM object or fall back to AuraSdk/HID.

## Required future bounded sequence (not implemented/executed)

M2.9 cannot fill in exact target/zone, API arguments, color payload, acquisition, normal restore or
release. These remain **UNKNOWN**, not placeholders that an executable may substitute. Observed
ASUS `SetProfile("1", XML)`/matrix control conventions are static evidence, not an S1 sequence.
Do not use Group, motherboard Back Plate-1/2, ARGB capacity or WDL keyboard as a default target.
Do not assume StartEngine cmd0 or control0 restores the prior owner/effect. No executable path for
any of these operations exists in the research probe.

Any future specification based on a materially new supported contract must name the exact visible effect and affected physical
zone, complete baseline (identity/session, component/ABI hashes, SCM, topology/status/profile/script,
AP/control context, config/WDL hashes), exact calls/arguments, restore/release and post-checks. Human
evidence fields: **VisualStateBefore**, **VisualStateDuringWrite**, **VisualStateAfterRestore**,
**UnexpectedAffectedDevices**, **OwnerRestorationConfirmed**. Missing baseline/control/restore proof
blocks candidate preparation; a field named status cannot identify the owner without its contract.

- Baseline: OS identity/integrity/session, SCM state/PID, vendor/component hashes/versions, current
  status/profile/settings/topology/config hashes, WDL observation and explicitly identified target.
  Record user observations VisualStateBefore, VisualStateDuringWrite and VisualStateAfterRestore.
- Obtain the reviewed control context, if required, in the same self-contained process as restore.
  A launcher/WinUI exit must not remove its only restore-capable object. The read-only worker's
  kill-on-close/forced-exit pattern is not a control-worker release strategy.
- At most one approved temporary effect on the known zone. **Hard ownership/effect TTL ≤5 seconds**,
  not user-expandable; start timing immediately before the first potentially state-changing call.
  Reserve part of that budget for the bounded restore path. A successful call returning late does
  not reset or extend the TTL.
- Autonomous deadline, finally/RAII, ordinary cancellation/console-close paths attempt the reviewed
  restoration exactly as specified. No parallel COM call/marshaling assumption may be invented to
  interrupt a hung vendor call. If bounded normal restoration cannot be proved, the gate stays blocked.
- Requery read-only status/topology, compare settings/config/SCM and collect the owner's visual report.
  S_OK, process exit, a lease flag, zero Count or unchanged service PID alone is not restoration proof.

## Stop conditions and residual risk

Wrong target/identity/version/ABI, ambiguous addressing/ownership, unexpected affected zone, failed
addressing validation/acquisition/write/restore/release/post-check, timeout or contradictory post-check → **RECOVERY_REQUIRED** and
human review. No automatic vendor-service restart, Armoury restart, reboot, fan command, HID fallback,
token API, second write, alternate API or further RGB write is allowed. Do not test forced owner termination, logoff or shutdown
until the approved normal write/restore/visual sequence succeeds separately.

Owner hard-crash/ExitProcess can lose the only restore-capable object. The exact vendor recovery
behavior is unknown. A watchdog does not remove that risk; this task neither forces a crash nor
claims automatic recovery. A future ServiceMediator group write could affect motherboard, DRAM,
GPU, cooler or multiple ARGB zones simultaneously if its matrix is misunderstood. No such write is
recommended now. WDL, if selected later, may contend with the Falchion keyboard's existing lighting
owner; its interface name does not authorize replacing M605 direct HID control.

`ReviewedGateAExecutionEnabled = false`

`ALTERNATIVE_WRITE_APPROVAL_REQUIRED = true` for any separately prepared future experiment.
