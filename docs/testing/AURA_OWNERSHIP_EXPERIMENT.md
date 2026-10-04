# Aura ownership experiment — PREPARED, NOT EXECUTED

**AURA_OWNERSHIP_APPROVAL_REQUIRED = true**

This is a plan, not runnable ownership code. No RGB output is implemented; RGB writes require another
approval after safe acquisition/release is proven. Fan operations and vendor service stop/restart remain excluded.

## Evidence versus hypotheses

Canonical signatures: IAuraSdk SwitchMode(), IAuraSdk2 ReleaseControl(uint reserve),
IAuraSdk3 RequireTokenByType(VARIANT*,int) and RequireDeviceControlState(uint)->int.
M2 QueryInterface support is not an ownership test. All eleven read-only Enumerate/Count queries
succeeded with zero devices on this host; no device/light getter ran. XML is cached topology evidence.

- Hypothesis A: SwitchMode alone exposes normal Aura devices. Empty read-only enumeration does not prove it.
- Hypothesis B: category 0x2F0000 needs a token route. Exact VARIANT contents, order and int result meaning
  remain unproven. Do not interpret 0/1 without evidence or call all ownership APIs blindly.
- Hypothesis C: ReleaseControl(0) restores the prior/default engine, including failures. Reserved parameter
  semantics and same/cross-process restoration remain unvalidated on this host.

## Preconditions for separate approval

Owner selects one safely identifiable non-M605 target and restoration reference. Name/XML location alone
is insufficient identity. Record readable lighting, Dynamic Lighting settings/priority, vendor versions/status.
If enumeration remains empty, explicitly acknowledge that read-only target validation is unavailable.
Provide an independent medium-integrity watchdog that survives WinUI death before ownership is enabled;
kill-on-close cannot prove vendor restoration. Establish exact ownership/resumption verification and maximum
ownership duration, bounded recovery attempts and stop policy. No fallback vendor restart, uninstall,
setting change, HID write or reboot is authorized by this plan.

## Exact proposed sequence (NOT executed)

1. Capture readable lighting/topology and Windows Dynamic Lighting state, with explicit missing evidence.
2. Start fresh isolated ordinary-user worker; check identity, COM support and timeouts.
3. Enumerate and validate the approved target. With zero devices, seek a narrowly scoped acquisition-before-
   enumeration experiment approval; do not silently treat an empty collection as target validation.
4. Gate A, only if explicitly approved: call SwitchMode once in that worker for the approved duration;
   retain exact worker/acquisition history. No setters or Apply.
5. Re-enumerate, validate the target and verify ownership using the agreed evidence/owner observation.
6. If normal/WDL access remains unavailable, stop with results. Gate B requires a revised approval specifying
   exact token VARIANT, count, category, ordering and int result interpretation before token/control-state calls.
7. ReleaseControl with the reviewed reserve value, only in the process that previously acquired under this
   approval. Uncertain acquisition retains a recovery obligation; do not assume ownership is absent.
8. Verify prior/default engine resumes with agreed API evidence and owner observation; compare settings,
   topology and service status. Do not emit a color frame to test restoration.
9. After normal release succeeds, separately approve crash stages: before acquisition (software-safe), after
   confirmed acquisition and during release. Post-acquisition kills may prevent same-process release;
   do not execute them until an approved restoration route has been proven.
10. Clear leases and record final owner/default state; unresolved restoration stops as failed/unknown.

No ownership/resumption PASS from S_OK alone, process exit, zero count, XML lists, registry flags or watchdog
events. Any state-changing next step requires **AURA_WRITE_APPROVAL_REQUIRED**. No ownership experiment,
first RGB write or M3 runs automatically.
