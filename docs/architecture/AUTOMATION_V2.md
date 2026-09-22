# Automation v2: current alpha behavior

Stages 0–6 and the immutable admission snapshot / safe generation retirement fixes are accepted. This guide describes current source behavior, not real-keyboard or live-CS2 acceptance. The earlier design spike and Stage 1 report remain historical scope records.

## Authoring and compatibility

Rules tagged `model: automation_v2` share state / rising / event conditions. State supports Activate Profile or continuous Trigger Effect (`while_true`); rising/event support one-shot Trigger Effect. Event/rising profile latching is not implemented. Profile arbitration retains first-match precedence, while effect rules are evaluated independently.

Capabilities and CRUD are provided by `AutomationControlService` and `automation_authoring.cpp`. Legacy Application Rules, GSI bindings and orchestration retain provenance and their compatibility executors. Opening projections does not migrate configuration. Application Rule → V2 Promote/Convert is an explicit revision-checked transaction, preserving DND and requiring acknowledgement of shadowing; unknown fields without a mapping block conversion. Legacy Blockly saves cannot erase v2 rules.

## Runtime and retrigger

Each admitted effect binds to an immutable plugin generation; optional finished/opacity lifecycle exports govern capable one-shots. Compatibility envelopes and watchdogs bound other lifetimes. Native plugins execute in-process: no crash/hang isolation is provided.

One-shot policies are `restart`, `ignore_while_active`, `stack` and `queue`. Current fixed limits (`include/config/automation_limits.h`): four stacked instances per rule, 32 active v2 instances globally, four pending items per rule, 64 globally, 2000 ms pending TTL, at most four pending-start attempts per frame. Capacity rejects newest admissions; queue playback is serial per rule. These are bounded observed event batches, not a lossless gameplay log.

Composition is base frame → v2 persistent layers → legacy overlay group → v2 transient layers. Within a v2 class, priority, rule order and admission sequence determine order. Overlay/Replace composition and Alpha/Additive blending are explicit; legacy ordering is preserved separately.

## Publication and reconciliation

Studio publishes explicit continuous or one-shot native code. Existing workspaces keep continuous behavior unless explicitly changed. Draft saving does not change the applied publication. Compile, daemon load/lifecycle validation and revision-checked configuration update must succeed before changing the applied reference. Unique DLLs and generation ownership keep old running effects safe during hot reload; historical DLL files are not automatically cleaned up.

Cosmetic reloads preserve compatible active/pending work. Semantic rule changes reconcile or cancel affected work; new admissions bind new generations while compatible old work retains its generation. Loss of scope or stale GSI cancels scoped work and rebases event/rising cursors so reconnect does not replay old events.

New GSI decisions default to `automation_freshness_ms = 3000`, separate from the approximately ten-second connection indicator and legacy freshness behavior. Stale/missing decision inputs become unknown; continuous render input has its own effect contract. Admission consumes a coherent immutable snapshot of conditions, events and captured foreground scope. Plugin retirement/destruction occurs outside the Automation runtime mutex.

## Evidence and ownership

The native CTest suites exercise production evaluation, authoring, lifecycle, ABI, composition, retrigger and reconciliation. Python integration starts real dry-run daemons and tests publication/configuration transactions with MSVC. See [testing](../testing/TESTING.md) and [manual acceptance](../testing/MANUAL_TESTS.md). WinUI is the desktop client; Web Studio and its local service remain required. Public launcher packaging does not include WinUI.
