# alpha.7 Phase 4B — Device Profile Automation

Device Profile Automation hardware coordination is enabled in the production daemon.
This replaces the Phase 4A/4A.5 decision-only policy. No HID protocol was added.
Physical Phase 4B acceptance remains pending.

## Ownership / data flow

    Existing ForegroundMonitor real cached identity
      → existing main-loop observation (never GSI simulated foreground)
      → DeviceProfileBindingEngine, one 500 ms stability debounce
      → stable AutomationDecision
      → one DeviceProfileAutomationCoordinator worker
      → Runtime authoritative admission inside shared mutation gate
      → existing ActivateLocked(target, Automation)
      → existing planner / M605 safety / typed transactions / shadow verification

BindingEngine remains pure decision logic, without actuator, filesystem or M605
dependencies. Coordinator owns scheduling/single-flight/retry, not diff, HID,
quarantine, generation or recovery. DeviceProfileRuntime remains the sole
Profile document writer and activation authority. WinUI is an API client.

The composition root explicitly starts one worker outside dry-run/simulation.
No per-rule/event/Profile tasks are created. Long plans execute off the render
loop; real foreground observation continues while the mutation gate is occupied.

Legacy config.json/profiles and Automation v2 activate_profile still mean
lighting recipes. RuleEngine/EffectEngine/GSI/lighting behavior is unchanged.

## Compatible configuration

Optional root device_profile_automation remains schema 1:
enabled, fallback_profile_id, bindings[{rule_id, enabled, process_name,
profile_id, priority}]. IDs are stable non-nil GUIDs. Highest signed int32 priority
wins, then canonical rule GUID ascending. Missing target winner fails visibly;
it never falls through to another rule. Fallback is explicit only.

Basename-only storage and Windows invariant lowercase normalization are unchanged.
Unknown extension fields round-trip, and atomic expected_revision mutation keeps
conflict semantics. Missing automation loads disabled without rewriting old files.
Unsupported/malformed automation stays opaque/unavailable while manual core
Profiles remain usable. No old cached binding or implicit schema migration runs.

## Final admission / races

Coordinator captures sequence, config document revision, manual action sequence,
committed/observed foreground, matched rule, target GUID and cached M605 generation.
Cached health only indicates when pending work should be reconsidered. It never
grants write permission.

Inside the SAME mutation gate as CRUD/manual Apply/manual magnetic writes,
ActivateAutomationDecision checks configuration availability/enabled, no hold,
no debounce, matching sequence/revision/rule/target/foreground/manual counter,
target existence and generation. Quarantine/unhealthy authority rejects before
desired selection changes. Existing ActivateLocked then checks service
availability and verifies/opens the actual M605 transport before any staged write.
A lighting connected snapshot alone cannot license HID.

After persistence/transport/planning, admission is checked again before the first
operation. Only the activation's own selected-profile revision increment is
adopted. Foreground can change concurrently without blocking the render thread.
If stale at this point, no stage starts; accepted selection can already be durable
and is dirty, never falsely active.

After admission, focus/shutdown does not cancel staged HID or issue inverse
commands. Existing Runtime generation/failure/quarantine/recovery remains
authoritative. New decisions wait for serialization.

Manual accepted selection sets existing session ManualHold inside this gate,
including deferred/failed hardware submission. Pre-selection rejection does not.
Old automatic tokens cannot write after that hold. Unknown/churn cannot clear it;
stable different known foreground does. Save alone is not a manual action.

## Startup / selection / dedup

Startup loads durable selected state without implicit replay. Real current
foreground is seeded and waits the usual 500 ms debounce. Only a valid enabled
Match/Fallback can activate; disabled/no-match/invalid do not reset selection.

Automatic activation keeps existing durable selected_profile_id semantics.
Success/deferred/accepted failure can select the target. There is no second
automation-selected field. Restart loads that last selection without replay;
a new real stable decision determines automatic work.

Identity = decision sequence + document revision + manual sequence + target +
committed foreground + observation epoch. The epoch changes only on a genuine
normalized observation change, independently of semantic target dedup. It rejects
A→B→A tokens yet permits the newly stable A after a discarded stale attempt.
Consumed success is never repeated each tick. The returned
revision from automatic selected mutation is consumed, preventing self-replay.
Independent edits remain eligible for conservative revalidation. Clean active
target uses the existing planner/SessionApplied checks: zero operations returns
no-op. GUID equality alone is insufficient.

## Outcomes / retry

| Outcome/blocker | Policy |
| --- | --- |
| succeeded / no-op | consume identity, no repeated calls/writes |
| stale / cancelled | discard old attempt; wait for a new decision/configuration |
| NoDecision / invalid / disabled / ManualHold / debounce | no activation or hidden fallback |
| deferred keyboard/transport unavailable | retain accepted desired selection; backoff 1/2/4/8/16/30 seconds, max 30 |
| quarantine / unhealthy | no timed retry loop; reconsider only changed cached health/generation, then authoritative recheck |
| apply failure / partial submission | visible failure, consume identity, no automatic retry or latch clear |

One worker coalesces per-frame wakes and periodically checks caches at 250 ms.
Wake does not bypass due time/dedup. Cached authoritative health/generation change
can promptly wake pending/deferred work. Availability recovery without a reliable
signal uses backoff. Coordinator snapshot and diagnostic GET never presence-probe.

Stop prevents fresh admission and joins the worker. An admitted plan completes
through existing safety without staged interruption. Main stops coordination
before web/M605 teardown. Graceful shutdown may wait for a long RT plan; owned
child/external daemon lifetime remains unchanged.

## API / diagnostics / native UI

| Route | Behavior |
| --- | --- |
| GET /api/device-profiles/automation | config/Profile list/revision plus decision/coordinator; document gate |
| POST /api/device-profiles/automation | existing atomic expected_revision save; extensions/conflicts unchanged |
| GET /api/device-profiles/automation/status | pure caches OUTSIDE hardware gate; no tick, probe, connect or write |
| GET /api/device-profiles/diagnostics | existing versioned export with additive coordinator fields; pure read |

hardware_activation_allowed=true means production coordinator enabled, not safe
current permission. hardware_block_reasons/outcome convey actual blockers.
Standalone/unconnected BindingEngine fixtures have no actuator and retain
disabled policy; attached production overlays coordinator status.

WinUI keeps would-select preview separate from last activation outcome. It polls
cached status once per second and updates status text only. Changed revision
triggers full config refresh when not busy; status-only revision never replaces
config/draft revision. Conflicts retain drafts. No new WinUI activation API exists.

Diagnostics include GUID/basename, monotonic process-local timestamps, attempts,
sequence/outcomes/blockers. No raw packet/path/PID/machine ID. GET never retries.
The outer UTC capture timestamp naturally changes per export. SessionApplied is
host submission intent, not firmware configuration readback.

## Deferred independent work

- Automatic reconnect reapply NOT implemented. Pending foreground retry differs
  from replay of an already successful selected Profile. Generation change alone
  does not replay a consumed successful decision.
- Background daemon/service/tray lifetime unchanged.
- Automatic Lighting Ownership separate; lighting references remain LegacyUnmanaged.
- 51 53 blocked; no new RT bulk path/reset/readback/Hall telemetry.
- Owner physical Phase 4B foreground/hold/unavailable/mixed-Profile acceptance pending.
