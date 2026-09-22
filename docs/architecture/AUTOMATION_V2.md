# Automation v2: current alpha behavior

Stages 0–6 and the immutable admission snapshot / safe generation retirement fixes are accepted. This guide describes current source behavior, not real-keyboard or live-CS2 acceptance. The earlier design spike and Stage 1 report remain historical scope records.

## Authoring and compatibility

Rules tagged `model: automation_v2` share state / rising / event conditions. State supports Activate Profile or continuous Trigger Effect (`while_true`); rising/event support one-shot Trigger Effect. Event/rising profile latching is not implemented. Profile arbitration retains first-match precedence, while effect rules are evaluated independently.


## Runtime and retrigger

Each admitted effect binds to an immutable plugin generation; optional finished/opacity lifecycle exports govern capable one-shots. Compatibility envelopes and watchdogs bound other lifetimes. Native plugins execute in-process: no crash/hang isolation is provided.

One-shot policies are `restart`, `ignore_while_active`, `stack` and `queue`. Current fixed limits (`include/config/automation_limits.h`): four stacked instances per rule, 32 active v2 instances globally, four pending items per rule, 64 globally, 2000 ms pending TTL, at most four pending-start attempts per frame. Capacity rejects newest admissions; queue playback is serial per rule. These are bounded observed event batches, not a lossless gameplay log.

Composition is base frame → v2 persistent layers → v2 transient layers. Within a v2 class, priority, rule order and admission sequence determine order. Overlay/Replace composition and Alpha/Additive blending are explicit.

## Publication and reconciliation

Studio publishes explicit continuous or one-shot native code. Existing workspaces keep continuous behavior unless explicitly changed. Draft saving does not change the applied publication. Compile, daemon load/lifecycle validation and revision-checked configuration update must succeed before changing the applied reference. Unique DLLs and generation ownership keep old running effects safe during hot reload; historical DLL files are not automatically cleaned up.

Cosmetic reloads preserve compatible active/pending work. Semantic rule changes reconcile or cancel affected work; new admissions bind new generations while compatible old work retains its generation. Loss of scope or stale GSI cancels scoped work and rebases event/rising cursors so reconnect does not replay old events.

New GSI decisions default to `automation_freshness_ms = 3000`, separate from the approximately ten-second connection indicator used for connection status. Stale/missing decision inputs become unknown; continuous render input has its own effect contract. Admission consumes a coherent immutable snapshot of conditions, events and captured foreground scope. Plugin retirement/destruction occurs outside the Automation runtime mutex.

## Evidence and ownership

The native CTest suites exercise production evaluation, authoring, lifecycle, ABI, composition, retrigger and reconciliation. Python integration starts real dry-run daemons and tests publication/configuration transactions with MSVC. See [testing](../testing/TESTING.md) and [manual acceptance](../testing/MANUAL_TESTS.md). WinUI is the desktop client; Web Studio and its local service remain required. Public launcher packaging does not include WinUI.

## V2-only configuration and authoring

`orchestration.rules` contains only `model: "automation_v2"` records, in meaningful config order. Missing retired fields and empty arrays are tolerated without semantics. Nonempty top-level `rules`, `gsi_bindings`, `orchestration.event_overlays`, or non-V2 orchestration records cause migration-required diagnostics; there is no fallback executor. Invalid reloads retain the last valid plan.

Blockly has separate state, rising and event rule containers. State Play Effect uses while_true; rising/event Play Effect uses one_shot. Event leaves exist only in WHEN, never inside the action. The shared backend parser requires event-mode WHEN to contain a positive occurrence leaf outside every NOT subtree; comparison-only and NOT-only event rules are rejected. AND/OR can carry an event witness, while an OR comparison branch alone still cannot admit an event. Activate Profile is state-only; DND remains its rule metadata. Effects are typed references selected from `/api/automation/v2/effects`, a read-only projection of profiles and published plugin generations. The existing backend resolver/validator remains authoritative; unavailable references fail authoring validation.

V2 capabilities/records/effects are read-only. POST/PATCH/DELETE `/api/automation/v2/rules` retain stable-ID CRUD; PUT atomically replaces a complete V2 rule list with `expected_revision`. All candidate records use the production parser and reference validation before atomic replacement. General config writes preserve V2 records and cannot bypass the authoring transaction. Generic writer locking/revision/atomic replacement remains shared.

## Simulation authority

Web loopback `/api/gsi/simulation` forwards to the existing daemon. POST accepts validated enable/heartbeat/foreground/health/armor/round_kills/bomb/round_phase/increment_kill controls, queues bounded owner-thread work and returns 202 with a sequence; GET exposes applied sequence, authority and current simulation inputs. The request limit is 16 KiB; Host/Origin/Referer and JSON protections apply.

The owner processes one command before each real Automation evaluation, so baseline seeding cannot swallow subsequent queued increments. Input authority and live payload ingestion share a mutex. Entering simulation creates a fresh epoch, seeds detector trackers without occurrences, and rebases all rule edge memory. Live CS2 POSTs return 200 but cannot mutate the simulated stream. Exiting clears active/pending simulated work on the owner thread and waits for the next live payload to seed a fresh baseline. Source changes do not create rising transitions. Normal stale/recovery semantics remain unchanged.

Heartbeat defaults to 1000 ms, shortened to one third of configured freshness when necessary; pause stops automatic refresh. Simulation UI displays Fresh/Stale, telemetry age and configured threshold from the exact RuleEngine reconciliation snapshot (`freshness.fresh`, `age_ms`, `threshold_ms`, `evaluated_at_ms`); absent telemetry has null age and is stale. These values are sampled on the owner tick, not recomputed by HTTP or JavaScript. Payload changes use the production UpdateFromPayload/DetectGameEvents path. Rendering uses existing EffectEngine/output, with no new framebuffer API. Local Studio Effect Preview is a different, explicitly labeled single-effect tool.

The [development migration utility](../development/AUTOMATION_MIGRATION.md) is never part of daemon startup. Hardware legacy_hal, Plugin ABI v1 and host LegacyEnvelope are unrelated supported compatibility mechanisms.
