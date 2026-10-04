# AuraSdk Gate A archived plan

Status: **BlockedByAuraSdkRuntimeInstability**, recorded in Phase 3 M2.8 (2026-10-02).

The [M2.5 plan](../../testing/AURA_GATE_A_RUNBOOK.md) and
[ownership/callsite analysis](../../research/aura-ownership-callsite-analysis.md) remain available.
The later [M2.6 characterization](../../research/aura-enumeration-apartment-cache-analysis.md) and
[M2.7 diagnosis](../../research/aura-mta-enumerate-failfast-analysis.md) also remain unchanged.

Reviewed candidate remains local at `audit_artifacts/phase3-m2.5/gate-a-candidate-reviewed/`.
SHA256 of its executable: `3e3113e34e1766d7250bd1d3cef3daacab866a7b4a11c04929d25df6709c6c51`.
`ReviewedGateAExecutionEnabled=false`; no enabled candidate was built or executed in M2.8.
The old dry-run path is unsuitable for alternative backend startup because it still calls AuraSdk
enumeration. There is no automatic fallback from ServiceMediator/WDL to this experiment.

No ownership was acquired and no restoration test was performed. The hypothetical vendor-owned
release loop of 141 logical light slots in the historical MTA Count=3 collection remains prior static
scope evidence, not a release operation or proof of restored hardware state.

The next review concerns [alternative write prerequisites](../../testing/AURA_ALTERNATIVE_WRITE_GATE.md).
It must not reopen Gate A or begin fan control automatically.
