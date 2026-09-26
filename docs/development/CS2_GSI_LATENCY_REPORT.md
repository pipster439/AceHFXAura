# CS2 GSI latency cadence change (2026-09-24)

## Scope and result

The new-install `gamestate_integration_aura.cfg` template now uses `buffer=0.01`, `throttle=0.0`, `timeout=0.5`, and keeps `heartbeat=1.0`. Existing files are detected as `different` by the existing exact-byte comparison; Aura does not silently replace them. An Owner must use the existing cfg installation action to update an existing CS2 installation.

The `event.kill` detector still uses an increase in `player.state.round_kills`, falling back to an increase in `player.match_stats.kills`. One counter jump is one event occurrence. Automation v2 evaluation, source selection, effect composition, HID, and simulation cadence are unchanged.

Reference checked at `eachkinji/CS2KillConfirmOverlay` commit `3263cfe15eb3cc781bdc53608976476444fd8280`: [its cfg](https://github.com/eachkinji/CS2KillConfirmOverlay/blob/3263cfe15eb3cc781bdc53608976476444fd8280/KillConfirmService/gsi/gamestate_integration_killconfirm.cfg) uses `0.5/0.01/0.0` and heartbeat `15.0`; [its GSI handler](https://github.com/eachkinji/CS2KillConfirmOverlay/blob/3263cfe15eb3cc781bdc53608976476444fd8280/KillConfirmService/src/gsi/update.rs) compares `round_kills` and records `handler_ms` when publishing a kill. Its 15-second heartbeat would exceed Aura's default 3000 ms Automation freshness threshold, so it was not adopted.

## Expected latency budget

| Segment | Before | After | Meaning |
| --- | ---: | ---: | --- |
| CS2 GSI buffer setting | 100 ms | 10 ms | Nominal configured collection window; actual dispatch timing is game-dependent. |
| CS2 GSI throttle setting | 100 ms | 0 ms | Configured minimum gap after a successful response; the delay attributable to it depends on the prior payload. |
| Healthy-path HTTP timeout | 5 s | 0.5 s | Retry/failure bound, not an ordinary per-kill delay. |
| Aura frame boundary at 25 FPS | 0–40 ms | 0–40 ms | Typical default; a selected profile can use 10–100 FPS. |
| Parse, Automation, effect construction, push | Unmeasured | Instrumented | Depends on payload size, rules, plugins, backend, and host load. |

The cadence settings can remove up to about 190 ms of configured buffering/throttle exposure in a worst alignment, but the two maxima do not necessarily occur together. The Owner's observed 100–200 ms is the baseline, not a guarantee that every kill improves by 190 ms. CS2's game update timing, local HTTP work, frame scheduling, and physical LED update remain to be measured. Keeping heartbeat at 1 second leaves two missed heartbeat intervals within the default 3-second freshness window, assuming delivery continues normally.

## Event-level instrumentation

On each detected **real GSI** kill, the daemon emits one `[GSI latency]` INFO record with the source epoch and packet sequence, and monotonic millisecond stamps for:

1. `payload_received_ms`: accepted payload at the GSI state lock, after HTTP JSON parsing;
2. `event_kill_detected_ms`: after the existing counter-delta detector returns;
3. `automation_admitted_ms`: after an `event.kill`-related Automation decision is admitted;
4. `effect_layer_activated_ms`: after successful layer creation or restart;
5. `frame_pushed_ms`: after `PushFrame` returns success for that frame.

Stages 3–5 are `null` when inapplicable. For a queued effect activated on a later frame, a second `deferred` record uses the same epoch/packet key. Only event occurrences produce these logs; unchanged heartbeat packets do not. All stamps use the daemon's steady clock. `frame_pushed_ms` means the Aura push call succeeded, not that the physical LED changed or a hardware ACK was observed. If multiple kills restart the same rule within one frame, an earlier activation can be superseded before that push. The first stamp does not include CS2's pre-POST time, so these logs alone cannot measure kill-to-light latency.

## Subscription audit

The template was left intact apart from cadence. All subscribed primitive fields enter the generic flattened telemetry snapshot and `/api/gsi/current`; published effect plugins and Automation field conditions can read them, so deleting a category solely from hard-coded references would change the product contract.

| Subscription | Current consumer | Assessment |
| --- | --- | --- |
| `player_state`, `player_match_stats` | Kill/headshot/damage detector, V2 rules, UI metrics | Required for runtime; match stats supply the kill fallback. |
| `round`, `bomb`, `map` | Bomb/round/match transitions, V2 fields, UI | Required for current runtime and UI. |
| `player_weapons` | Studio field dictionary and GSI diagnostic field table | Current authoring/diagnostic read; retain. |
| `player_id`, `provider` | Generic current payload/telemetry projection and identity fields | Retain for current diagnostics and plugins; not part of kill detection. |
| `phase_countdowns` | Diagnostic field table | Diagnostic read; retain. |
| `allplayers_id/state/match_stats/weapons/position`, `allgrenades`, `player_position` | Generic telemetry and diagnostic field table | No hard-coded Automation detector consumer found. The diagnostic UI advertises these fields, so keep them. Flattening produces leaf keys (for example `allplayers_state.<id>.health`); table rows keyed only by a category name may show no value, which is a separate UI issue. |
| `map_round_wins` | Generic telemetry/diagnostics and user-defined field consumers | No hard-coded detector/UI card found; potential future use, retained this round. |

## Verification

- Release MSVC build: `aura_daemon`, `aura_web_ui`, `test_gsi_rules`, `test_automation_v2`, `test_gsi_simulation`, `test_automation_effect_runtime`.
- CTest: `gsi_rules`, `automation_v2`, `gsi_simulation`, `automation_effect_runtime`.
- Isolated web install test: `test_real_backend_install_conflict_whitelist_and_atomic_failure` checks exact served cfg values, byte-identical installed file, whitelist, stale-revision conflict, and atomic failure preservation. It writes only a temporary allowlisted fixture directory, not Steam.
- Detector tests check no duplicate `event.kill` from ten repeated packets, 1-second heartbeat freshness, 3000/3001 ms boundary, event regression, cfg `missing/matching/different`, and activation trace correlation.
- Real daemon integration smoke was attempted but skipped because an Aura daemon already held the single-instance mutex. No live CS2 or physical hardware measurement is claimed.

## Changed files

- Cfg generation/discovery: `src/web/web_server.cpp`, `src/web/web_server.h`.
- Receipt and event trace metadata: `include/gsi/automation_input.h`, `src/gsi/gsi_adapter.cpp`, `include/config/rule_engine.h`, `src/config/automation_evaluation.cpp`.
- Layer and frame trace: `include/engine/automation_effect_runtime.h`, `src/engine/automation_effect_runtime.cpp`, `src/main.cpp`.
- Regression checks: `tests/test_winui_productization.py`, `tests/test_gsi_rules.cpp`, `tests/test_automation_v2.cpp`, `tests/test_automation_effect_runtime.cpp`.
- Delivery artifacts: this report and `cs2_gsi_latency.patch` (code/tests only).

## Owner real-CS2 A/B

1. Keep the same Aura config, kill rule/effect, game mode, hardware backend, frame rate, and system load. Save the current `gamestate_integration_aura.cfg` as the A baseline. Confirm the old cfg has `0.1/0.1/5.0` and `heartbeat=1.0` if using it as A.
2. With A installed, restart CS2 so it reloads the integration cfg. Capture at least 20–30 distinct kills. Record a synchronized game/keyboard high-frame-rate video or equivalent visual timing, and retain Aura's `[GSI latency]` records. Do not infer game-to-LED timing from daemon logs alone.
3. Install the new cfg through Aura's existing cfg deployment UI, then verify `/api/gsi/cfg` reports `template_match=matching` for the detected CS2 cfg path and the installed file has the four exact values. Restart CS2. Repeat the same sample for B.
4. Compare median and p95 kill-to-visible-light delay, packet-receipt-to-push delay, missed/duplicate kill counts, and GSI freshness/connection status. The event log's epoch/packet key helps correlate internal stages; `null` admissions identify scope/freshness/effect issues.
5. If B causes unstable GSI delivery or no measurable benefit, restore the saved A cfg and restart CS2. Decide acceptance from the real-device measurements.

No commit, push, tag, or release was made.
