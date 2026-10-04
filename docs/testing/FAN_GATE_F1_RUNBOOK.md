# Fan Gate F1 — blocked, non-executable preparation

FAN_GATE_F1_BLOCKED. No target, duty argument, API sequence or executable switch is approved. Do not use CPU_FAN as the default first target. Do not select any chassis header by name/index alone. CPU_OPT config-only tach has no editable independent curve.

Required before candidate preparation:

1. Separately review/deploy the read-only broker/child to protected Program Files and prove real SYSTEM/session0 worker activation via medium IPC. Run 10 serialized read cycles at 1Hz; save HRESULTs/latencies/identity and errors. Do not restart vendor services.
2. Establish a current supported per-channel RPM and temperature sensor getter contract (including S_FALSE/no-value semantics), unique runtime Id -> FanInfo -> actual physical header mapping and FanStore key mapping. No raw register reads/writes or invented sensor joins.
3. Establish current mode/profile/curve snapshot completeness and hardware-vs-cache semantics, native point limits and units. Prove service minimum duty/RPM/critical/fan-stop rules and a non-CPU critical candidate with safe bounds.
4. Identify a vendor-supported deterministic safe/default restore contract, service-side behavior on client/worker failure, and bounded failure cleanup. EnableManualMode(false) or COM release alone is insufficient evidence.
5. Review live thermal abort sources/thresholds and recovery verification, then obtain separate human approval for a fixed executable candidate. Hard write TTL must be at most 5 seconds; never make it indefinitely user-expandable.

A future sequence can be specified only after those contracts exist: capture complete baseline; validate channel/source/temperature/RPM; acquire the proven mode; make one small safe bounded change; observe; restore original/proven default mode; verify recovery; release; stop. Exact calls/arguments remain Unknown. No code implements this sequence now.

Human observations to retain later: PhysicalHeaderIdentity, BeforeRpm/Temperature/Mode, VisibleOrAudibleResponse, AfterRestoreRpm/Mode, UnexpectedBehavior. A successful HRESULT is not physical response or restored-state proof.

Abort on unsafe temperature, unexpected RPM drop/zero, timeout/disconnect, wrong mapping, failed mode/write/restore/release/post-check. Execute only the reviewed vendor-supported safe restore once; otherwise RECOVERY_REQUIRED and stop for human review. No retries, alternate API/HID/Aura fallback, raw SIO/EC writes, vendor-service restart or reboot.

Current blocking evidence is machine-readable in audit_artifacts/phase3-m3.0/f1-readiness.json and fan-restore-contract.json. This document is not write authorization and is not a reason to begin M3.1.
