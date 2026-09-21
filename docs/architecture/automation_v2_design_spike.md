# Automation v2 technical design spike

Status: **approved for implementation** by Owner. Phase 4 v1 is accepted and merged into main; approved starting baseline is `7b2e1c7887cf9c6f7e5a9601530b19b00282ed24`. Source audit: 2026-09-21 against that checkout. The current implementation authorization covers Stage 0 and Stage 1 (ABI/factory substrate) only; stop after Stage 1 for review. Source descriptions below record the pre-Stage-1 audit, not claims that the implementation still lacks subsequently completed substrate work.

Authoritative product boundary: [automation_architecture_review.md](automation_architecture_review.md). Studio defines **HOW TO RENDER**; Automation defines **WHEN TO RUN / WHAT TO DO**; Game Integration supplies telemetry. Business predicates belong only in Automation. An Effect may continuously map health, team, weapon, or other GSI input to pixels. Animation waits, interpolation, and completion belong inside the Effect.

Stage 0 / Stage 1 delivery: [implementation and acceptance report](automation_v2_stage1_report.md). ABI/factory substrate is implemented and tested; stopped before Stage 2. Full CTest passes; three existing launcher entrypoint failures reproduce on the exact approved baseline. See the report for the historical fixture gap and Owner acceptance limits.

Recommendation: extend the existing ConditionNode/RuleEngine and OverlayManager, retain EffectEngine's live-base composition, and add a host-owned instance sidecar plus optional DLL exports. Do not add another rule engine, effect database, or Effect virtual method. Product “Automation v2” is not the existing `orchestration.version = 2` format marker.

Owner-approved initial scope: state → Activate Profile; state → Trigger Effect while_true; rising/event → Trigger Effect one_shot. Initial retrigger policies are restart and ignore_while_active only. Automation decisions use `automation_freshness_ms = 3000`, independently of the existing approximately 10 s GSI connection/status threshold, and one coherent AutomationInputSnapshot per decision batch. Phase 4 top-level rules remain visible and unchanged unless the user explicitly promotes/converts them. Latching, stack and queue remain future design extensions, not initial runtime/UI requirements.

## 1. Current runtime topology

```text
Foreground monitor -----------> RuleEngine::MatchProfile ---> EffectEngine active Profile
GSI HTTP -> GsiState ----------> ConditionNode / legacy binding evaluation
             |                 OverlayManager::UpdateBindingsFromGsi
             |                           |
             +-> IGsiReader -------> Profile and active overlay RenderWithContext
                                             |
                         current base frame -> overlays -> hardware PushFrame

Studio Blockly -> generated C++ -> immutable named DLL -> PluginManager
               -> blockly_effects + profiles in config.json
Legacy Orchestrator Blockly -> orchestration.rules / event_overlays
```

The main loop matches profiles, updates bindings, renders, pushes hardware, then checks configuration mtime on a nominal 50 ms interval ([main.cpp](../../src/main.cpp), `sync_event_overlays`, lines 592–625; loop 771–844). Actual cadence is bounded by frame time. Preview substitutes the base frame, but overlays still apply.

### Code evidence index

| Area | Verified source and behavior |
| --- | --- |
| AST | [rule_engine.cpp](../../src/config/rule_engine.cpp), `ConditionNode::FromJson`, `ToJson`, `Evaluate` (57–197): accepts type/op with conditions/condition, and/or/not shorthand, field/op/operator/value/target_value aliases. Empty leaf and empty AND are true; empty OR/NOT false; NOT evaluates first child. Unknown comparison falls back to equality. |
| Comparison | Same file, process leaves: case-insensitive equality/inequality with optional `.exe`; no process contains support. Other leaves call [gsi_adapter.cpp](../../src/gsi/gsi_adapter.cpp), `EvaluateLocked` (516–607): missing field false, player.state/player_state aliasing, numeric string conversion, case-insensitive string equality/contains, numeric equality tolerance 1e-4. AST NOT of a missing field can therefore become true. |
| Precedence | `RuleEngine::MatchProfile` (733–814): first matching orchestration rule, then legacy GSI bindings only for CS2/CSGO with active GSI, then legacy process rules, orchestration fallback, default profile. Array order, not overlay priority, determines profile selection. Modern AST evaluation has no automatic freshness gate. DND is separately evaluated by `ShouldSuppressWebUi`. |
| Config | `RuleEngine::LoadConfig` (383–701): parses legacy arrays and both orchestration arrays, skips disabled orchestration profile rules, accepts target_profile/profile and effect/profile aliases; fade_ms takes precedence over fade_out_ms. Event overlay enabled is not checked. Invalid required fields/references can reject the entire new config. Commit of parsed state is under lock; reload failure retains old config. |
| Events | [gsi_adapter.cpp](../../src/gsi/gsi_adapter.cpp), `UpdateFromPayload`, `DetectGameEvents`, `TriggerEvent`, `SyncEventFieldsToFlatState` (64–489): flattens updated subtrees; compares previous telemetry; initializes baseline without initial health events. One kill counter jump creates one record with diff, not one record per kill. |
| Overlay lifecycle | [overlay_manager.cpp](../../src/engine/overlay_manager.cpp), `UpdateBindingsFromGsi` (95–146): hard gate on active GSI and CS2/CSGO foreground, event sequence comparison with boolean-edge fallback, condition callback, per-binding restart, persistent state instances, ascending stable priority sort. `ClearBindings` clears active instances and cursors too. |
| Composition | Same file, `ComputeWeight`, `IsExpired`, `ApplyOverlays`; [effect_engine.cpp](../../src/engine/effect_engine.cpp), `Tick`: fresh base then overlay buffer; legacy blend treats black as transparent, replace includes black, add saturates. Persistent weight is always 1; expiration ignores persistent instances. |
| Profile rendering | [effect.h](../../include/engine/effect.h), `Profile::Render`: base Effect, key overrides, then profile brightness. Triggering a profile-backed legacy overlay uses only base_effect, not overrides/brightness/fps. |
| Plugin ownership | [plugin_manager.cpp](../../src/engine/plugin_manager.cpp), `LoadPluginInternal`, `ReloadPlugin`, `CreateEffect`; [plugin_manager.h](../../include/engine/plugin_manager.h), `PluginHandle`: absolute shadow DLL load, alias registry, create/destroy exports, shared_ptr deleter captures module handle; FreeLibrary after last handle owner. |
| ABI and context | [plugin_interface.h](../../include/engine/plugin_interface.h): version constant 1, IGsiReader, elapsed/keymap/GSI EffectContext, optional-looking render typedef. Loader does not actually probe/use that render export. Rendering invokes Effect virtual methods. GetString pointer lasts only until next GetString on that thread. |
| Studio generation | [cppTranspiler.js](../../frontend/src/blockly/cppTranspiler.js), `transpile` (14–117): version 0x00010000, create/destroy/name exports, per-instance sequence state, RenderWithContext override. [sequenceCompiler.js](../../frontend/src/blockly/sequenceCompiler.js), end case (52): resets PC to entry, so sequences loop rather than finish. |
| Studio serialization | [EffectStudio.jsx](../../frontend/src/components/EffectStudio.jsx), `saveWorkspace`; [applyEffect.js](../../frontend/src/utils/applyEffect.js), `stageEffect` (78), `effectConfig` (124): Blockly JSON in blockly_effects[name], draft and applied snapshots, SHA-256 revision, applied_plugin_name, profile indirection. Compile and daemon confirmation precede config reference change. |
| Legacy serialization | [orchestratorSerializer.js](../../frontend/src/blockly/orchestratorSerializer.js), `serializeWorkspace`, `processStatementChain`, `restoreWorkspace`: emits orchestration version 2, separate rules/event_overlays, clears legacy arrays, stores blockly_orchestrator.version 1 workspace JSON. Branch conditions become AST; waits rejected. Restore prefers stored workspace before reconstructing rules. |
| Frontend migration | [orchestration.js](../../frontend/src/utils/orchestration.js), `canonicalConfig`: imports modern/GSI/simple in precedence order unless already version 2; clears old arrays. Its JS evalCondition substitutes zero/false/empty for missing data, unlike daemon. It is UI simulation, not a runtime authority. |

Corrections to historical baseline facts: ABI versions are **not checked** today; per-binding factories and state overlays already exist; Web [web_server.cpp](../../src/web/web_server.cpp), config POST around 866–918 already checks If-Match/428/409 in this snapshot. These observations are not Phase 4 acceptance or authorization to change its implementation.

## 2. Existing reusable primitives

- One ConditionNode AST and evaluator for both profile eligibility and effect eligibility; keep the same process normalization and GSI field resolver.
- RuleEngine's ordered profile selection, transactional config replacement, and legacy parsers.
- GsiState's telemetry transition detector, event sequences, freshness signal, and IGsiReader rendering interface.
- OverlayManager's per-binding identity, effect factory hook, persistent/transient instances and scratch buffer; EffectEngine's current-base rendering order.
- PluginManager's shadow generations and DLL-pinning deleter; existing profile factories and Studio publication indirection.

“One engine” means one daemon evaluation path produces action decisions. OverlayManager executes lifecycle/composition decisions; it must cease independently interpreting business conditions in the new path. Legacy adapters feed that same path. UI simulation should use daemon evaluation results or shared conformance fixtures, never become another scheduler.

## 3. Current limitations

1. No arbitrary compound-condition rising-edge memory; event mode requires an event name. Overlay gating prevents arbitrary non-game process effects.
2. Public event booleans are not occurrences: kill stays true 1500 ms, damage 1000 ms; some event.* names are sustained states. `event.round_mvp` currently aliases victory, not a verified MVP event. Flash aliases flashed; alias sequences are not uniformly published.
3. Per-event sequences are exposed as doubles. Different values trigger once regardless of delta; several occurrences between render ticks coalesce. The recent-event deque (50 records) is diagnostics, not an acknowledged runtime event stream. GsiState::Clear clears published fields but does not clear event_sequences_; neither field absence nor counter reset is a safe replay boundary.
4. State and transient overlays share one priority list; there is no explicit class ordering. RGB has no coverage/alpha plane; black transparency is a heuristic.
5. Duration/attack/fade are manager-owned; no completion/opacity capability. Factories can fall back to a shared profile Effect; stateful builtins are not guaranteed fresh instances.
6. Main synchronization clears all overlays on config and plugin reload. Direct plugin reload does not itself replace an already-running base instance; config reload/name transitions can replace it later.
7. Individual GSI reads lock, but an entire condition tree/frame is not one coherent snapshot. Lazy DLL load/factory invocation occurs under the overlay update lock. “Zero allocation” comments do not cover instance creation, sorting, or all plugin code.
8. Legacy serializer is not a lossless editor for future fields despite its comment: it regenerates IDs and only writes known fields. A stored workspace can be older than authoritative JSON rules.

## 4. Proposed Automation v2 rule/action model

Extend `orchestration.rules` with opt-in discriminated records marked `model: "automation_v2"`, leaving existing records readable. Add `orchestration.automation_model: 2` as a capability marker; retain existing `orchestration.version: 2`. Do not add a parallel automation.rules evaluator. Legacy event_overlays remain an accepted input adapter, not a second execution path.

A compiled rule contains stable id, enabled, orthogonal dnd policy metadata, scope ConditionNode, when { mode, condition }, one action, order, and compatibility provenance. `dnd` is not an action. `scope` is an optional Automation predicate controlling admission/cancellation (e.g. CS2 foreground); omitted scope is true. `condition` is the existing AST with a typed event-occurrence leaf extension. No Studio trigger data is consulted.

Actions: `activate_profile { profile }` or `trigger_effect { effect, lifetime, composition, blend, priority, retrigger }`. Lifetime is `while_true` or `one_shot`. Scope and condition always go through the same ConditionNode traversal. Initial runtime rule memory is previous truth/validity, input cursor and active instance IDs—not a second predicate interpreter. No profile latch or queued-action state is required initially.

The initial runtime and UI support exactly these pairings:

| WHEN | DO | Initial behavior |
| --- | --- | --- |
| state | Activate Profile | Eligible while true; existing profile precedence applies. |
| state | Trigger Effect / while_true | One persistent instance while true. |
| rising or event | Trigger Effect / one_shot | Admit a one-shot using restart or ignore_while_active. |

Reject event/rising → Activate Profile, even if `activation: "latch_until_scope_exit"` is supplied. Also reject state + one_shot and event/rising + while_true rather than retriggering every tick or inventing a stop condition. Use rising for a one-shot on state entry. New process predicates initially retain equality/inequality and .exe normalization; arbitrary executable names and AND/OR/NOT composition are supported, not a new wildcard/process-tree matching language.

Profile actions remain first-eligible in array order; effect actions all evaluate independently. Numeric priority is only for layers. Legacy tiers preserve their existing relative precedence. New profile records participate in the orchestration tier at their written position; they do not silently override a preceding legacy orchestration rule.

Top-level legacy `rules`, including Phase 4 Application Rules, remain valid and visible in Automation with their source/tier identified. A V2 process-only Activate Profile rule belongs to the higher orchestration tier and therefore can shadow an equivalent top-level rule. This is existing runtime precedence, not permission for authoring tools to duplicate rules silently. Section 12 defines an explicit Promote/Convert-to-v2 flow and confirmation gate; no automatic migration is permitted.

**Future extension only — latch_until_scope_exit:** a later separately approved event/rising → Activate Profile feature could set eligibility until scope exit, invalid required telemetry, disable/delete, semantic edit or restart. It would participate in normal first-eligible arbitration and would not persist across daemon restart. Keep this possible contract documented, but do not implement a latch, accept the pairing, expose it in UI, or require latch acceptance tests in initial Automation v2.

## 5. State vs Event vs Rising Edge semantics

| Requested case | Evaluation and lifetime |
| --- | --- |
| A: health < 15 → LowHealth | `mode: state`; condition true selects LowHealth subject to profile precedence. At 15 or above it loses eligibility. Already low on startup selects immediately. |
| B: health < 15 false → true → warning | `mode: rising`; emit only after an observed valid false followed by valid true. No repeated trigger while true. Initial true, reconnect true and edited-rule true seed the baseline without firing. |
| C: event.kill → kill | `mode: event`; each admitted detector occurrence can trigger even if public event.kill stayed true throughout. No use of pulse boolean edges for new event leaves. |
| D: while bomb planted → bomb_pulse | `mode: state`, trigger_effect lifetime while_true; one instance for the rule while true, released immediately on false/unknown/scope exit. No restart every frame and no duration expiry. |

Use three-valued results for new rules: true, false, unknown. Missing/type-invalid/stale GSI leaves are unknown, NOT unknown remains unknown; AND false dominates, OR true dominates, otherwise unknown propagates. Only true admits actions. A process-only OR branch may legitimately succeed without GSI. Preserve old boolean semantics under a legacy compatibility policy within the same evaluator. Do not rewrite legacy NOT behavior silently.

### Coherent AutomationInputSnapshot

Each decision batch constructs one host-owned immutable `AutomationInputSnapshot` before evaluating any rules:

| Snapshot member | Capture contract |
| --- | --- |
| foreground_process | Capture once from the monitor at Automation admission/dispatch time, normalize once, and reuse for every process leaf and rule in that batch. |
| telemetry | One immutable telemetry snapshot with source epoch, revision and receipt timestamp. State/rising batches use the selected state snapshot; event batches use the snapshot corresponding to that occurrence batch. No leaf reads live mutable GsiState independently. |
| occurrences | One immutable canonical occurrence set for an event batch; empty for non-event batches. Never merge events from different batches to satisfy an AND. |
| admitted_at_ms / telemetry_age_ms / automation_fresh | One admission time and one freshness calculation against that telemetry snapshot and the config generation's automation_freshness_ms. No per-leaf clock/freshness re-read. |

Scope and when predicates, all recursive AND/OR/NOT leaves, and all rules evaluated in the batch use this same snapshot through ConditionNode. If monitor or GSI changes mid-evaluation, it is observed in a later batch. This extends the existing evaluator's input contract; it does not create another evaluator or alter Plugin ABI/IGsiReader's vtable.

For event batches, process predicates explicitly mean **foreground at Automation admission/dispatch time**. They do not mean foreground at exact gameplay event occurrence time or GSI receipt time. Pairing a buffered telemetry/event batch with the captured admission-time process is intentional; it cannot prove which application was foreground when the game event happened. Do not recapture foreground separately for each event leaf or action.

Process changes and telemetry updates evaluate state/rising rules; each render can reconcile state and freshness expiration even if no new payload arrives. Temporal memory advances for every evaluated input, including when action creation fails, preventing delayed replay. Scope entry seeds rising memory; it does not invent an edge. Unknown disarms edge memory until a valid baseline exists. Scope predicates are state-only; event leaves there are rejected.

### Separate connection/status and Automation freshness

Keep the existing approximately 10000 ms `IsActive` connection/status threshold for compatibility. Introduce `orchestration.automation_freshness_ms`, initially/default `3000`, for new Automation v2 decisions only. New rules must not use the status-active boolean as their decision freshness gate. The UI may correctly show GSI connected while Automation reports telemetry stale for decisions.

At admission, no telemetry or telemetry age **greater than 3000 ms** makes new GSI-dependent leaf conditions unknown, including event-occurrence leaves from an over-age batch; 3000 ms itself remains fresh. Unknown propagates with the existing new-rule three-valued logic, so a genuinely independent process-only OR branch can still succeed. A queued input batch is judged by its own immutable telemetry timestamp, not made fresh by a newer unrelated payload. Use a monotonic receipt timestamp for decision age; retain historical status semantics separately.

When decision freshness expires, cancel GSI-scoped persistent layers and GSI-scoped transient feedback, clear/rebase their temporal state and event cursors, and clear/rebase GSI-scoped action queues/profile latches **if those deferred features are introduced later**. “GSI-scoped” includes a new rule whose explicit scope depends on GSI or whose action eligibility requires valid GSI state/occurrences; process-only rules remain unaffected. No queue or latch implementation is required in the initial release. Do not cancel an independent process-only branch merely because another OR branch references unavailable GSI; reconciliation uses the snapshot's eligibility/validity result.

On recovery, state profile/persistent actions can become eligible immediately, but rising conditions seed a fresh baseline rather than firing for unknown→true. Rebase occurrence cursors without replaying events from the stale interval. Subsequent valid false→true transitions and subsequent admitted occurrences can fire normally. Legacy rules, gsi_bindings and event_overlays retain their existing freshness/boolean semantics until explicit conversion; this 3 s threshold must not silently change them. This is an Automation decision policy, not a change to how Effects consume continuous rendering input.

Consume each input batch once globally, and independently offer it to all eligible rules; one rule must never steal an occurrence from another. Advance rule cursors even when disabled, out of scope, ignored while active, capacity-limited or unresolved. On re-enable/recovery establish a new baseline instead of catching up missed business actions.

For occurrences, extend the existing detector output with a bounded internal ordered packet batch: source epoch, packet sequence, monotonic receipt time, canonical event IDs/ordinals, and the corresponding immutable telemetry snapshot. Drain batches on the owning runtime thread, constructing the AutomationInputSnapshot above for each admission. The adapter normalizes data and emits observations; only Automation decides actions. This small input delivery buffer preserves ordering/condition values for initial restart/ignore policies and future stack/queue fidelity; it is not a deferred action queue. The current sequence delta alone cannot reconstruct packet ordering or condition values. Suggested bound: 256 pending batches; overflow drops oldest, emits a counter, and rebases rather than replaying an invented backlog. No guarantee of events the game never reported.

Event-mode AST evaluates once per packet batch using its AutomationInputSnapshot: that batch's event set and telemetry plus the foreground captured once at Automation admission/dispatch. `event.kill AND process == cs2.exe`, `event.kill OR event.damage`, `event.kill AND NOT event.headshot` are well-defined. Two positive event leaves in AND mean co-occurrence in the same batch, not “sometime recently.” Multiple matching leaves in one batch yield one action; separate batches yield separate actions. A kill counter diff of two remains one detector occurrence unless a later explicitly approved data-source change expands it.

Require at least one positively satisfied occurrence leaf in the successful boolean expression; NOT event alone cannot fire on every unrelated packet, and `(process==cs2) OR event.kill` cannot fire on damage solely because the process branch is true. Implement this as truth plus positive-occurrence-witness metadata in the same AST traversal: true AND combines witnesses; true OR retains witnesses from true branches; NOT contributes no positive witness. Event leaves are not legal in new state/rising mode; use an actual state field there. Repeated identical payloads without detected transitions emit nothing. Reconnect/start/scope entry seed cursors and discard pre-entry occurrences; no historical replay. Simultaneous kill/headshot filtering must be tested explicitly.

## 6. Rendering/layer model

Normal output order is fixed:

```text
fresh Base Profile -> persistent state layers -> transient event/rising layers -> final frame
```

Within each class sort ascending `(priority, rule_order, instance_sequence)`; higher priority renders later. Equal priority follows stable config order, then monotonically assigned instance sequence (future stack instances would render newer instances later). Priority never moves a persistent layer above the transient class. Restart keeps rule order, assigns a new instance sequence. Profile brightness and overrides remain base-local, matching current behavior; they do not globally scale overlays.

Keep the smallest current-compatible definition: **Overlay** uses RGB-nonblack coverage; **Replace** covers every routed pixel including black. Replace is a full-coverage visual replacement, not a profile switch, exclusive lock, or destruction/suspension of lower layers. No saved frame is restored. Future explicit per-key coverage is outside this spike's minimal ABI extension.

Let L be the current accumulated lower frame, E the rendered effect, w its clamped scalar opacity, m its coverage (Overlay: 0 on black, otherwise 1; Replace: 1). For each routed channel:

- Alpha: `L' = (1 - m*w)*L + m*w*E`.
- Additive: `L' = clamp(L + m*w*E, 0, 255)`.

Thus Replace + Alpha at w=1 replaces all routed pixels, black included. Replace + Additive is intentionally the same as Overlay + Additive for RGB-only output (adding black changes nothing); document the equivalence, do not invent an exclusive “replace then add” algorithm. UI may show Additive once with a note that coverage mode has no visible distinction. Owner approval required for this naming. Legacy blend/replace/add map to Overlay+Alpha/Replace+Alpha/Overlay+Additive, retaining black-key semantics and current byte rounding.

To preserve old visual ordering, legacy-only overlay configs retain their existing single priority group. Migration to new fixed classes is explicit and previewed, not applied automatically. Mixed legacy/new configurations place the legacy group after persistent v2 and before transient v2; legacy priority remains internal to its group. This deterministic compatibility compromise must be exposed during migration.

### HealthGradient + Kill proof

For every tick t, base B(t)=HealthGradient(GSI health at t), and flash W=(255,255,255). Alpha output is `(1-w(t))*B(t)+w(t)*W`. If illustrative B changes from (51,204,0) at 80 HP to (166,89,0) at 35 HP while w=0.4, output becomes approximately (202,155,102), not the old-health result (133,224,102). At w=0 the newest B is revealed. This follows directly from existing Tick rendering the base before ApplyOverlays, and remains true for the proposed ordering. No framebuffer snapshot is stored at trigger time. It assumes the base effect actually samples current health each frame; a Studio animation that intentionally caches or waits on an old value will not provide that behavior. This is an algebra/code-path proof, not observed hardware acceptance.

Preview remains the existing alternate base followed by layers for compatibility; changing editor preview isolation is a separate product decision.

## 7. Retrigger policy

Initial runtime and UI implement **restart** and **ignore_while_active** only. Reject explicit stack/queue requests as unsupported capabilities; do not silently reinterpret them as restart. Stack/queue implementation and authoring are deferred until the basic lifecycle path is accepted and a follow-up scope is approved.

| Policy | Scope | Meaning for an already active rule/action |
| --- | --- | --- |
| restart (default) | Initial | Construct a fresh instance and swap it at elapsed=0; never reuse mutable Effect state. If construction fails retain the old running instance until its normal end. |
| ignore_while_active | Initial | Consume the occurrence but leave current animation unchanged. No replay when it ends. |
| stack | Deferred runtime and UI | Construct another independent instance; proposed per-rule maximum 4, global active maximum 32. On cap, drop newest trigger and record diagnostic. |
| queue | Deferred runtime and UI | FIFO of trigger tokens, proposed maximum 4 pending per rule; start next after completion. Drop newest on overflow; expire queued tokens after 2 s and cancel on scope exit/edit/Automation staleness. Pin the accepted factory generation, not a pre-rendered frame. |

Deferred stack/queue limits remain design proposals for their later review, not initial acceptance requirements. Initial restart/ignore permits one active transient per rule and retains an overall bounded instance budget (suggested 32); no pending action queue. A watchdog caps each playback (including restarts); repeated restart activity may intentionally keep feedback visible while inputs continue. System memory stays bounded; rate limiting is not inferred as a new business condition.

Two kills 300 ms apart: restart visibly re-emphasizes the second kill without piling up layers; a future opt-in stack could let both coexist. Damage spam: recommend ignore_while_active per damage rule to prevent an indefinitely restarted flash; deferred queue would usually be poor feedback for damage. Round-start/victory: recommend ignore_while_active to avoid duplicate-looking replay, while deduplication by source cursor is mandatory for all policies. Round occurrences are not automatically repeated every pulse frame. Stateful while_true actions have exactly one instance and no retrigger policy. Future queue/stack would preserve detector occurrences, not an inferred count of kills hidden in one packet.

## 8. TriggeredEffectInstance design

Host-only sidecar, not a new base class or virtual interface:

```cpp
struct TriggeredEffectInstance { // illustrative fields, not an ABI struct
    uint64_t instance_id, generation_id, start_ms;
    std::string rule_id;
    std::shared_ptr<Effect> effect; // original DLL destroy function in deleter
    std::shared_ptr<const PluginGeneration> generation; // handle + immutable exports
    FinishedFn finished; OpacityFn opacity; // from this exact generation only
    LifecyclePolicy lifecycle; // plugin, legacy envelope, continuous
    CompositionPolicy composition;
    uint64_t watchdog_ms;
};
```

A proposed optional lifecycle capability revision 1 has these C exports (Windows calling convention explicitly `__cdecl`; fixed-width scalar status, no STL or C++ bool result):

```cpp
extern "C" uint32_t __cdecl AuraGetEffectLifecycleVersion(); // returns 1
extern "C" uint32_t __cdecl AuraIsEffectFinished(
    const aura::Effect* effect, uint64_t elapsed_ms); // 0 running, 1 finished
extern "C" float __cdecl AuraGetEffectOpacity(
    const aura::Effect* effect, uint64_t elapsed_ms); // finite [0,1]
```

Effect* remains opaque to the host callback; it is the exact pointer returned by that DLL's factory, not a host wrapper. These are optional exports, appropriate for v1 extension without vtable changes. The separate lifecycle revision removes ambiguity about future same-name export signatures; this is not a claim that Plugin ABI v1.1 already exists. Probe once per loaded generation. Callback metadata must be returned atomically with the newly created Effect, not looked up later by its mutable alias.

All render/lifecycle/destruction operations for an instance run serially on its owning runtime thread. After construction, elapsed starts at zero on first render. Each tick: enforce wall-clock lifetime deadline; clear temporary RGB buffer; render even when opacity would be zero (animation must advance); query finished, and if finished remove without composition; otherwise query opacity and compose. Functions must be nonblocking, noexcept in contract, and return consistent state after Render. Same elapsed value is used for all three. Terminal final frames must be emitted before the Effect reports finished; one-shot creators should end at zero opacity for smooth removal. Opacity modulates composition, not RGB brightness a second time.

The sidecar owns effect and generation until callbacks cease; the deleter pins the old DLL through destruction. Reload publishes a new immutable generation for future instances only. Never call a new DLL's callback with an old DLL's instance. Registry alias changes must update aliases atomically without silently merging distinct exports. Destruction is deferred until no frame snapshot references the sidecar; no FreeLibrary from an unsafe in-flight call.

Capability/fallback matrix:

| Available capability | One-shot behavior |
| --- | --- |
| lifecycle revision 1 + finished + opacity | Effect owns timing/opacity/completion; host watchdog only. |
| revision 1 + finished only | Effect owns completion, opacity=1; abrupt ending is explicit capability behavior. |
| opacity only / unknown lifecycle revision / no exports | Use a legacy envelope for both opacity and completion; ignore partial opacity to avoid double fading. Warn once when plugin lifecycle was requested. |
| native builtin | Use host factory sidecar capability if implemented; otherwise same legacy fallback. |

Suggested watchdog: 5000 ms default, explicitly configurable in 1–60000 ms for one-shots; no watchdog duration for continuous while_true instances. Legacy envelope expiration remains its configured duration, not silently shortened to 5 s. A while_true action requires a continuous effect contract; if a supposedly continuous capability reports finished, remove it and mark completed-for-this-true-interval, without respawning each frame. Re-arm only after false then true.

A watchdog is a lifetime bound checked between calls, **not** protection against a hung Render or memory corruption in an in-process native DLL. C++ exception containment can recover from cooperative throws, not arbitrary access violations/deadlocks. Process isolation is the only strong containment direction and is explicitly outside the minimal evolution; do not promise crash-proof execution of arbitrary native plugins.

### Transition from duration_ms / fade_out_ms / attack_ms

Keep an exact LegacyEnvelope policy for existing event_overlays, including existing attack-before-fade precedence and the duration<=fade behavior (no fade window). Do not “fix” legacy timing during migration. Persistent legacy state ignores all three timing fields today and continues to do so.

New capable one-shots export their own curve/completion; Automation contains only composition/routing and a safety deadline. Existing v1 effects can be triggered through a documented compatibility envelope (defaults 1200/400/0). An explicit fallback envelope may be carried under compatibility metadata; it must not multiply the plugin's own opacity. Studio must explicitly opt into one-shot generation with terminal PC/completion and opacity definition; old workspaces remain looping, even when their sequence has a syntactic end. Timing migration is opt-in per effect publication, never inferred from block count or dark output.

## 9. Plugin ABI compatibility strategy

Actual behavior: header constant `AURA_PLUGIN_API_VERSION=1`; Studio emits `0x00010000`; loader reads optional AuraGetPluginApiVersion/GetPluginApiVersion, defaults absent to 1, records/logs raw value, and **does not compare it with anything**. Missing create or destroy fails; missing name uses filename. Therefore the encoding mismatch currently does not cause rejection, but an incompatible DLL can pass through unchecked.

Normalize before calling create or any optional capability:

| Raw value | Normalized contract | Decision |
| --- | --- | --- |
| no version export | historical unversioned v1.0 | Accept in existing compatibility path, diagnose once. |
| 1 | v1.0 | Accept canonical header spelling. |
| 0x00010000 | v1.0 | Accept existing Studio spelling. |
| anything else (including 0, 2, 0x00010001) | unrecognized | Reject candidate generation with actionable diagnostic; keep old generation. |

Do not generally divide arbitrary numbers by 65536 or accept all major=1 values. There is no defined minor ABI contract to justify that. Future recognized encodings need an explicit compatibility table. Keep the header's canonical 1 for new generators after all consumers accept both. Store raw and normalized values for diagnostics, keep prefixed/unprefixed legacy exports, and do not change Effect, IGsiReader or EffectContext layouts. Lifecycle exports negotiate independently.

This preserves correctly built v1 and existing Studio DLLs, but deliberately stops accepting unknown versions previously tolerated by the unchecked loader. Owner explicitly approved this tightened behavior for Stage 1. Matching a version is necessary, not proof of MSVC C++ ABI/toolchain compatibility; existing compiler/runtime/architecture constraints remain. Test real old binaries, not only rebuilt fixtures.

## 10. Effect identity/resolution

No new Effect Asset subsystem is needed. Reuse existing `profiles` and `blockly_effects` names with a small typed reference:

| Reference | Resolution |
| --- | --- |
| `{kind:"profile_effect", name:"warning"}` | Resolve current profiles.warning recipe; instantiate only its base effect. Existing Studio publications already maintain this stable profile key and update its immutable plugin_name. |
| `{kind:"plugin", name:"vendor_flash"}` | Explicit PluginManager canonical export name/registered alias; resolved generation is pinned. Prefer stable export names over DLL paths for new writes. |
| builtin effect | Store parameters in an ordinary named profile and use profile_effect. Reuse CreateEffectFromProfile factory; no extra builtin registry or new config asset type. |
| legacy string | Preserve historical profile-first lookup, plugin fallback, plus existing make_effect behavior; warn on profile/plugin collision instead of silently changing resolution. |

Current sync first picks profile.base_effect, else PluginManager.CreateEffect; its make_effect then tries profile.plugin_name or the raw name before falling back to the captured base effect. That means a builtin profile whose name collides with a plugin can actually be superseded during trigger creation. New typed references remove this ambiguity; compatibility reads retain it until explicit migration.

New instances must come from factories, not a shared Profile::base_effect pointer, including builtins with mutable state. Factor the existing profile recipe factory for reuse without changing Effect's vtable. profile_effect intentionally excludes profile key overrides/brightness/fps, as legacy overlays do; use Activate Profile to get the complete profile behavior. Rename is a config transaction that rewrites references (existing names are not rename-proof UUIDs). Delete with references is rejected by authoring APIs; runtime missing references fail gracefully. Studio draft changes never become runnable until publication succeeds. Re-publication changes future instances through the stable profile key; running instances retain their accepted generation. Deferred queued tokens would do the same. Arbitrary plugin path reads remain compatibility-only.

## 11. Hot reload semantics

All changes become runtime commands applied atomically at a frame boundary, not direct network-thread edits of live instance collections. Compile/load/parse outside the rendering critical section. Use one immutable config generation and the coherent AutomationInputSnapshot defined in section 5 per decision batch. Deferred queue/latch behavior below documents future compatibility expectations only; it does not expand initial implementation scope.

| Change | Current code | Proposed v2 behavior |
| --- | --- | --- |
| config reload | ClearBindings cancels all overlays; rebuild; base restarted even same name. | Preserve active instances/cursors for identical rule IDs and semantic definitions. Cancel deleted, disabled or semantically edited rules; new/edited edge rules seed, not fire. Cosmetic edits retain state. Future queues/latches follow the same preserve/cancel boundary. |
| effect recipe changes only | Rebuild bindings and clear all. | New triggers use new recipe; active transients keep pinned recipe generation; persistent layer replaces at frame boundary. Base replaces only when selected profile identity/recipe changes. Future queued tokens also pin the accepted recipe generation. |
| active profile changes | Base resets start time; overlays are not directly cleared by SetActiveProfile. | Transients continue over the newest base; persistent layers follow their own conditions/scope. Explicit scope, not base-profile identity, controls cancellation. |
| plugin DLL reload | Registry swaps generation; main callback clears bindings/overlays. | Existing transients finish on old pinned generation; future triggers use new. Replace continuous base/persistent instances once at frame boundary if they reference that plugin; failed replacement retains old. Future queues would drain on their pinned generation. |
| GSI decision stale / disconnect | Overlay gate uses existing approximately 10 s status freshness; modern profile AST may still read stale data. | New Automation GSI leaves become unknown when snapshot age exceeds automation_freshness_ms (initially 3000), even if status is still connected. Cancel GSI-scoped persistent/transient layers and rebase cursors/rising memory; clear/rebase future GSI-scoped queues/latches. Process-only rules continue. Do not change legacy gates or rendering IGsiReader status semantics. |
| foreground changes | Legacy overlays cleared outside CS2/CSGO. | If explicit scope becomes false cancel that rule's instances (and future queue/latch). Unscoped effects finish; state predicates reconcile normally. Merely leaving an admission-time event condition does not retroactively cancel an unscoped shot. |

Semantic definition includes when/scope/action/composition/retrigger/priority and the effective Automation freshness policy; excludes display labels. Changing automation_freshness_ms reconciles affected new GSI rules and rebases edge memory rather than inventing a transition. Legacy id-less records receive generation-local identity and conservatively cancel on reload. Legacy-only configs retain their existing clear-on-reload policy until explicit migration. Epoch/reset, stale/disabled intervals and scope changes must not turn an old event_sequence value into a new occurrence. Recovering fresh true state admits persistent/profile state immediately, but rising seeds again rather than firing on recovery.

## 12. Legacy migration

1. Keep all existing readers: top-level rules, gsi_bindings, orchestration.rules, event_overlays, aliases and blockly JSON. Compile them in memory to one internal rule/action representation with provenance and compatibility policy; this is not a persisted config migration. Preserve tier/order, CS2 gate, legacy freshness, pulse fallback, restart-by-binding, timing and single-layer ordering. Do not execute original and translated forms twice. Top-level Phase 4 rules remain in their original config array and visible/editable in Automation; do not auto-run canonicalConfig or another writer migration just to open or save the v2 editor.
2. Legacy event overlay becomes event occurrence trigger plus its existing condition; trigger=state becomes while_true with legacy gate. Legacy event.* conditions in profile rules remain pulse/state predicates, not silently reinterpreted occurrences.
3. Enable strict new AST/action validation only for model=automation_v2 records. Existing permissive AST semantics remain readable; migration UI must call out semantic changes such as NOT missing, stale gating, explicit coverage and layer groups.
4. Preserve unknown fields and existing workspace bytes when unrelated rules change. New rules are not round-tripped through the legacy serializer. Until that editor preserves unsupported records and IDs, block destructive whole-list save of a v2-bearing config or keep it read-only with an explicit explanation. Do not claim its current “zero loss” comment guarantees compatibility.
5. Use revision-checked edits that preserve Phase 4 process rules. Do not alter Phase 4 API scope or index/revision semantics as part of this spike. V2 authoring needs stable IDs behind the existing application-rule projection. Never silently create an equivalent higher-tier V2 process-only Activate Profile rule that shadows an existing top-level application rule; use the explicit flow below or require confirmation of intentional coexistence.
6. Downgrade: old daemon ignores model markers and can reject action-only records for missing target_profile. Thus new-format configs are **not** promised to run on old daemon. Require daemon capability before saving, back up old config for explicit rollback, and never duplicate shadow rules just to make downgrade appear functional.

### Phase 4 coexistence and explicit Promote/Convert-to-v2

- Automation lists top-level application rules alongside V2 rules with source and precedence visible. Opening the UI or editing another rule does not rewrite, hide, or migrate Phase 4 records.
- Before creating or editing a V2 process-only profile rule, the authoring UI/API checks normalized process identity (including case/.exe equivalence) against existing top-level rules. Equivalent process+profile rules require explicit Promote/Convert-to-v2 or confirmation before any write. Same process with a different profile also needs an explicit shadowing warning/confirmation because higher orchestration precedence changes the effective selection.
- Promote/Convert-to-v2 shows a concrete before/after diff: the original rule, proposed state→Activate Profile rule, selected orchestration position, and higher-tier precedence (including possible precedence over legacy GSI bindings). Preserve process matching semantics and the exact target profile. Map top-level `suppress_web_ui: true/false` to orchestration rule-level `dnd: true/false` (omitted legacy suppress_web_ui has the existing false default). `dnd` is orthogonal rule policy metadata, never a Trigger Effect or Activate Profile action or a field nested inside an action. Preserve both explicit false and true. Unknown legacy fields without a defined semantic preservation mapping must **block automatic conversion**, even if their JSON could be copied. Leave the original unchanged; do not treat confirmation of precedence as permission to discard unknown semantics.
- Only after explicit user confirmation perform a revision-checked transaction that creates the V2 record and removes the converted top-level record. No partial conversion or silently retained duplicate. If the user instead explicitly chooses coexistence, keep both and clearly mark the lower-tier rule as shadowed for that process.
- The future authoring API must enforce the same explicit promotion/confirmation contract as the UI and reject unconfirmed shadowing writes; a UI warning alone is insufficient. The exact future request shape is outside Phase 4's locked API scope. These are implementation requirements for later authoring, not a request for approval or production changes in this documentation task.

Future conversion acceptance fixtures are pinned in [promotion_expectations.json](../../tests/fixtures/automation_v2_stage0/promotion_expectations.json): true→true, false→false, omitted→false, process/target preservation, explicit ordering/precedence disclosure, and refusal of unknown fields without a semantic mapping. They specify future behavior; Stage 1 does not implement or claim to pass a Promote/Convert API. The captured Phase 4 fixture retains `custom_extra_tag` specifically to exercise the unknown-field blocker later.

## 13. Failure handling

| Failure | Required behavior |
| --- | --- |
| missing effect/profile reference | Authoring validation rejects; external config reload leaves prior valid runtime if invalid. If a previously resolved effect disappears at runtime, skip only the affected trigger, consume its token, preserve base/lower layers, report rule id and reference. Never render a missing overlay as a black Replace. |
| Studio compilation failure | Keep published profile/plugin reference; show compile stage/log; draft remains editable. No trigger changes and no automatic compilation on render thread. |
| plugin load/version/factory failure | Keep previous generation; failed new action is unavailable. If no previous base exists use existing safe configured fallback/black placeholder with diagnostic. A placeholder must not be mistaken for a successfully resolved v2 overlay. |
| missing/type-invalid condition field | New AST unknown propagation; no accidental health=0 or NOT-missing trigger. Legacy evaluation preserved. |
| stale GSI | Keep approximately 10 s connection/status threshold separately; default automation_freshness_ms=3000 for new Automation rules. At age >3000 ms new GSI leaves become unknown, scoped persistent/transient layers cancel, cursors rebase and rising seeds on recovery. Future scoped queues/latches clear/rebase too. Legacy freshness stays unchanged. Effect IGsiReader can still expose old values; rendering fallback remains an Effect contract, not an implicit consequence of the 3 s decision threshold. |
| malformed legacy event_overlay | Retain current all-or-nothing config load rejection for structurally invalid records; last valid runtime continues. On cold start report invalid config and use application startup fallback policy; never silently treat malformed AST as unconditional new action. Valid-but-unresolvable legacy effects can remain skipped as today. |
| callback throws / invalid opacity | Catch cooperative exceptions at host boundary, retire affected instance, preserve lower frame. Clamp finite opacity to [0,1]; NaN/Inf or invalid finished status is a capability failure, retire and diagnose once. |
| runaway lifetime / capacity | Watchdog retires shot; overflow drops newest trigger (input buffer overflow differs as specified above); bounded diagnostics and counters, no allocation storm. |
| unsupported initial pairing/policy | Reject event/rising→Activate Profile, stack or queue as unsupported in the initial schema/capability validation; preserve the last valid runtime on rejected reload. Do not reinterpret a latch as state eligibility or a deferred retrigger policy as restart. |

Build candidate config and validate shape, depth (suggest 32), node count (256/rule), numeric limits, duplicate IDs and supported event names before publication. Runtime cannot guarantee survival of arbitrary native memory corruption; that residual limitation is explicit. No missing asset, ordinary config error, or absent telemetry should throw through the render loop.

## 14. Proposed config examples

These are **proposed fragments, not accepted by the current daemon**. Referenced profiles must already exist. Event leaves use a new `event` key; ordinary leaves retain today's syntax. The four examples are independent demonstrations; enabling A with a different base naturally means the base is no longer HealthGradient while A wins.

```json
{
  "orchestration": {
    "version": 2,
    "automation_model": 2,
    "automation_freshness_ms": 3000,
    "fallback_profile": "desktop",
    "rules": [
      {
        "id": "low-health-profile", "model": "automation_v2", "enabled": true,
        "scope": {"field": "process.name", "op": "==", "value": "cs2.exe"},
        "when": {"mode": "state", "condition": {"field": "player_state.health", "op": "<", "value": 15}},
        "action": {"type": "activate_profile", "profile": "LowHealth"}
      },
      {
        "id": "low-health-warning", "model": "automation_v2", "enabled": true,
        "scope": {"field": "process.name", "op": "==", "value": "cs2.exe"},
        "when": {"mode": "rising", "condition": {"field": "player_state.health", "op": "<", "value": 15}},
        "action": {
          "type": "trigger_effect", "effect": {"kind": "profile_effect", "name": "warning"},
          "lifetime": "one_shot", "composition": "overlay", "blend": "alpha",
          "priority": 10, "retrigger": "restart", "watchdog_ms": 5000
        }
      },
      {
        "id": "kill-flash", "model": "automation_v2", "enabled": true,
        "scope": {"field": "process.name", "op": "==", "value": "cs2.exe"},
        "when": {"mode": "event", "condition": {"event": "event.kill"}},
        "action": {
          "type": "trigger_effect", "effect": {"kind": "profile_effect", "name": "kill"},
          "lifetime": "one_shot", "composition": "overlay", "blend": "alpha",
          "priority": 20, "retrigger": "restart", "watchdog_ms": 5000
        }
      },
      {
        "id": "bomb-persistent", "model": "automation_v2", "enabled": true,
        "scope": {"field": "process.name", "op": "==", "value": "cs2.exe"},
        "when": {"mode": "state", "condition": {"field": "bomb.state", "op": "==", "value": "planted"}},
        "action": {
          "type": "trigger_effect", "effect": {"kind": "profile_effect", "name": "bomb_pulse"},
          "lifetime": "while_true", "composition": "overlay", "blend": "additive", "priority": 10
        }
      },
      {
        "id": "cs2-base", "model": "automation_v2", "enabled": true, "dnd": false,
        "when": {"mode": "state", "condition": {"field": "process.name", "op": "==", "value": "cs2.exe"}},
        "action": {"type": "activate_profile", "profile": "HealthGradient"}
      }
    ],
    "event_overlays": []
  }
}
```

Compound event condition example (same `when.condition` slot):

```json
{
  "type": "and",
  "conditions": [
    {"type": "or", "conditions": [{"event": "event.kill"}, {"event": "event.damage"}]},
    {"type": "not", "conditions": [{"field": "round.phase", "op": "==", "value": "over"}]}
  ]
}
```

Compatibility envelope for a non-capable one-shot action, when explicitly chosen during migration:

```json
{
  "compatibility": {
    "lifecycle": "legacy_envelope",
    "duration_ms": 1200,
    "fade_out_ms": 400,
    "attack_ms": 0
  }
}
```

Existing config remains valid without rewriting:

```json
{
  "orchestration": {
    "version": 2,
    "rules": [{"id": "game", "process": "cs2.exe", "condition": {}, "target_profile": "HealthGradient"}],
    "event_overlays": [{"id": "old-kill", "event": "event.kill", "effect": "kill", "duration_ms": 1200, "fade_ms": 400, "blend_mode": "blend", "priority": 20}]
  }
}
```

## 15. Required daemon extensions

1. RuleEngine compiles all accepted formats to one rule/action plan; ConditionNode gains strict validation, new event leaves, validity/witness metadata and one AutomationInputSnapshot per decision batch (foreground captured once, immutable telemetry, one occurrence set). Initial schema accepts only the three approved pairing categories and restart/ignore_while_active. Legacy policy remains in that evaluator, not a parallel service.
2. GSI detector exposes bounded immutable occurrence batches and epoch/cursor state; retain public pulses for compatibility and display. Canonicalize aliases through one adapter mapping, with no invented MVP occurrence.
3. OverlayManager receives start/stop/reconcile commands; sidecars implement distinct factories, lifecycle, restart/ignore_while_active, instance limits and deterministic composition. Add explicit new layer classes plus legacy group. Stack, action queue and profile latch runtime/UI are deferred.
4. PluginManager normalizes known version spellings and returns effect plus same-generation metadata; probes optional exports; keeps callbacks/module pinned together. Prepare loads off the frame path. Fresh construction still requires a bounded cooperative factory contract or prepared instances; do not claim native constructors can be preempted.
5. Main coordinates config/input/instance commands at frame boundaries; evaluate new-rule automation_freshness_ms=3000 independently of status IsActive and reconcile expiration without new GSI packets. Reuse profile factory recipes for fresh builtin instances and retain last-known-good generation on failures.
6. Expose capability, validation, active rule/instance, drop/watchdog and asset-resolution status for authoring/diagnostics. Minimal fields on existing status APIs are sufficient; no second scheduler service.

## 16. Things explicitly NOT required

- Current implementation authorization stops at Stage 1 ABI/factory substrate. No Automation v2 execution, occurrence batches, ConditionNode v2, composition or authoring implementation; no Phase 4 API changes or branch operations.
- No new Effect virtual methods, IGsiReader vtable changes, ABI-breaking context fields, or mandatory recompilation of existing DLLs.
- No independent Automation rule interpreter in Studio, OverlayManager or WinUI; no business trigger blocks in newly authored Effects.
- No Effect Asset database, mandatory UUID migration, plugin marketplace, graph execution framework, wall-clock scheduler, or durable event broker.
- No arbitrary scripts as actions, multi-action sequencing, wait nodes in Automation, general temporal “event A then B” language, per-pixel alpha ABI, or process-isolated plugin sandbox in the minimum implementation.
- No automatic editing of legacy Blockly workspaces, no implicit one-shot conversion, no redefinition of Phase 4 Application Rule CRUD.
- No initial event/rising→Activate Profile or latch runtime/UI. No initial stack/queue runtime/UI; these are future designs gated on basic lifecycle acceptance and follow-up scope approval.
- No automatic promotion of top-level application rules, no silent equivalent V2 shadow rules, and no silent 3 s freshness change to legacy rules or the approximately 10 s connection/status signal.

## 17. Implementation stages

| Stage (after Phase 4 acceptance) | Scope and exit evidence |
| --- | --- |
| 0: approved baseline/fixtures | Owner approved implementation and accepted Phase 4 v1 at 7b2e1c7. Verify HEAD, pin suppress_web_ui→rule-level dnd, and capture prebuilt binaries/configs before building. See [Stage 0 manifest](../../tests/fixtures/automation_v2_stage0/manifest.json) and [fixture provenance](../../tests/fixtures/automation_v2_stage0/README.md); no historical binary is rebuilt. |
| 1: ABI/factory substrate | Version normalization and generation-bound sidecar factory; fixture tests for absent/1/0x00010000/unknown versions, missing/partial exports, failed reload, destructor-before-unload. No new user rules yet. |
| 2: unified evaluation | Compile legacy plus proposed records to one plan; coherent AutomationInputSnapshot and occurrence batch delivery; separate 3 s Automation / approximately 10 s status freshness; state/rising/event conformance, missing/NOT/stale cases, initial/recovery suppression, compound event witness semantics. Reject event/rising→profile. Keep legacy compatibility outputs identical. |
| 3: basic lifecycle/composition | Add fresh instances, fixed new layer classes, restart and ignore_while_active only; golden RGB tests for black coverage, priority ties, mixed legacy ordering, live health change during flash, zero-opacity progress, watchdog, malformed callback results. Accept the basic lifecycle path before considering stack/queue. |
| 4: publication/reload | Explicit Studio continuous/one-shot generation without old workspace changes; preserve draft/applied distinction; test config edits, DLL changes, scope/GSI loss, running old generations and new base reveal. No queued-action or latch runtime. |
| 5: authoring/coexistence | WHEN/DO editor uses initial daemon schema; show legacy application rules, prevent unconfirmed higher-tier shadowing, provide explicit Promote/Convert-to-v2 with preview/diff/revision check/rollback, preserve unsupported legacy editor save guards. Never auto-migrate. Do not turn the existing Phase 4 editor into an advanced editor incidentally. |
| Deferred follow-up, not initial delivery | After basic lifecycle acceptance and follow-up scope approval, implement bounded stack/queue runtime and UI with cap/expiry/staleness/reload tests. Event/rising→profile latching requires its own later approval and is not implied by accepting stack/queue. |

Initial acceptance traces: health 20→10→8→20→10 fires B twice and selects A only while low; two kill packets 300 ms apart produce two admissions even within the pulse window; two packets before a render tick remain two admissions; same packet duplicate produces none; bomb true for 30 s with fresh telemetry retains one instance; newest health visible throughout flash fade; DLL old callbacks are never paired with new objects; malformed config leaves last good runtime; no rule executed both as legacy and translated action. Cover kill+headshot same packet, restart versus ignore, counter reset/alias handling and process-only operation with disconnected GSI. Reject deferred policy/pairing requests without altering valid runtime state.

Freshness acceptance: after the last valid packet, age 3000 ms is fresh and 3001 ms makes new GSI leaves unknown while the approximately 10 s status may still report connected. GSI-scoped persistent layers cancel without waiting for another packet; recovery already-low health seeds rising without a warning shot, then a later valid false→true fires. Legacy rule results remain unchanged by the new threshold. Expired event batches cannot borrow a newer packet's timestamp.

Snapshot acceptance: change process or telemetry during an AND/OR/NOT evaluation and verify every leaf still sees the single admitted snapshot; a later batch sees the update. A buffered kill received while CS2 was foreground but admitted after a foreground change uses the admission-time foreground, never a claimed occurrence-time foreground.

Coexistence acceptance: an existing Phase 4 top-level process rule remains visible with all fields/order preserved on unrelated edits; equivalent V2 creation and same-process shadowing cannot write without explicit promotion/confirmation. A confirmed conversion removes the old record and creates the new one atomically under a revision check, preserves process/target, maps suppress_web_ui→rule-level dnd for both boolean values, and displays ordering and higher precedence. Unknown legacy fields lacking a semantic mapping block conversion without changing either array. Assert that dnd is never an action. Queue caps/expiry and latch clear/rebase tests belong only to their deferred implementations.

The original spike performed source tracing and document/example validation only. Stage 1 implementation/testing is recorded separately from these future acceptance criteria; passing ABI/factory tests does not establish later Automation runtime behavior or real hardware/CS2 acceptance. Frozen historical binaries must not be rebuilt. **NOT VERIFIED ON REAL HARDWARE.**

## 18. Risks/open questions

Owner decisions are summarized in [automation_v2_review_summary.md](../../automation_v2_review_summary.md). Highest risks are user-visible semantics and lifetime safety, not adding a new JSON array.

- Initial scope is fixed by Owner: no event/rising→profile; latch_until_scope_exit is future-only. New missing/stale/initial-edge semantics and event-batch co-occurrence remain documented; observations are not a lossless CS2 gameplay log.
- Validate the approved full-coverage meaning of Replace and additive equivalence, RGB black transparency limitation, fixed class order and mixed legacy group. Exclusive Replace would be a larger, different design.
- Initial retrigger scope is fixed to restart/default and ignore_while_active. Validate the initial global instance budget/watchdog defaults during lifecycle acceptance; deferred stack/queue caps and queue age need later scope review, not initial implementation.
- Stage 1 implements the approved optional lifecycle export revision and narrower unknown-version acceptance policy. Its report records alias/collision and retained-generation tests; later runtime use still requires lifecycle acceptance.
- Owner selected automation_freshness_ms=3000 for new decisions, separately from approximately 10 s status freshness. Validate monotonic receipt-time accounting, independent-OR eligibility and recovery seeding; no silent change to legacy behavior. Default masking of continuous Effect rendering input remains a separate future product decision, not part of this threshold change.
- Native code remains trusted in-process; watchdog cannot meet an absolute no-crash guarantee for hostile/buggy DLLs. Decide whether stronger isolation is a later product requirement.
- Ensure unsupported legacy editor saves cannot erase new rules, names are transactionally renamed, all writers preserve revision checks after Phase 4 merges, and promotion/shadowing confirmation is enforced by both future authoring UI and API.
- Stable source links describe the approved merged Phase 4 baseline. Stage 0 verified that starting commit; subsequent implementation facts and test limits are recorded in the Stage 1 report rather than silently rewriting the historical audit.
