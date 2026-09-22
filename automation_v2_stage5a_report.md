> HISTORICAL: pre-retirement implementation evidence. Current Automation supports V2 only; see docs/architecture/AUTOMATION_V2.md.

# Automation v2 Stage 5A — authoring and migration contract

Status: implemented for Owner review; stopped after Stage 5A. The accepted Stage 4 checkpoint was committed separately as `a604b09` before these edits. Stage 3 and earlier checkpoints were not amended. This report and the staged binary patch are against that checkpoint.

## API surface

The authoritative API runs in the daemon on loopback port 19897. The web server on 19898 exposes a same-origin proxy to these routes; it does not implement its own V2 schema or mutate configs for these operations.

| Method / path | Contract |
| --- | --- |
| GET `/api/automation/v2/capabilities` | Version 2 authoring capabilities, accepted pairings, AST limits/operators/examples, canonical event catalog, effect kinds, composition/blend/retrigger choices, scope/witness constraints, ordering, DND and conversion policy |
| GET `/api/automation/v2/records` | Current content revision, raw records with provenance/path/order/stable V2 ID, profile choices and precedence explanation; read only |
| POST `/api/automation/v2/rules` | Create `{expected_revision, rule, position?, acknowledge_shadowing?}` |
| PATCH `/api/automation/v2/rules` | Sparse update `{expected_revision, id, patch, position?, acknowledge_shadowing?}` |
| DELETE `/api/automation/v2/rules` | Delete `{expected_revision, id}` |
| POST `/api/automation/v2/validate` | Validate the same create or sparse-update body, without publishing; includes shadow acknowledgement requirements |
| POST `/api/automation/v2/promotions/propose` | Prepare `{expected_revision, source_index, id, position?}`; returns concrete proposal without writing |
| POST `/api/automation/v2/promotions/commit` | Commit `{expected_revision, proposal, confirm:true}` after recomputing/verifying the complete proposal |

V2 IDs are nonempty opaque strings supplied by the caller (the client uses UUIDs). They are unique among V2 records, immutable on update, and passed in JSON rather than interpreted as array offsets or URL fragments. Legacy IDs remain in their provenance namespace. An omitted create position appends; an omitted update position retains the original position. An explicit update position is the insertion offset **after removing that same ID** from the orchestration array. Position is revision-bound ordering information, never identity or a new profile priority.

Every operation that could write requires the content revision returned by list. Validation and proposals also require a revision so their results cannot silently refer to a different config. All authoring reads/mutations use the existing cross-process `NamedConfigLock`. Mutations reread under the lock, check the revision, edit ordered JSON, validate the candidate V2 plan, and use the existing `AtomicWriteConfigFile`/MoveFileEx discipline once. Concurrent writes against one revision yield one success and one conflict.

Typical create request:

```json
{
  "expected_revision": "revision_from_records",
  "position": 0,
  "rule": {
    "id": "low-health-warning",
    "model": "automation_v2",
    "when": {
      "mode": "rising",
      "condition": {"field": "player.state.health", "op": "<", "value": 15}
    },
    "action": {
      "type": "trigger_effect", "lifetime": "one_shot",
      "effect": {"kind": "profile_effect", "name": "warning"},
      "retrigger": "ignore_while_active"
    }
  }
}
```

Updates use JSON Merge Patch: omitted fields survive; explicit null removes a field; arrays replace only when explicitly supplied. Unknown root/orchestration/rule metadata and unrelated legacy arrays survive. Successful writes may reformat JSON whitespace; list, validation, rejected writes and proposals leave file bytes unchanged. New V2 authoring never canonicalizes or migrates the whole config.

Validation directly uses `RuleEngine::ParseAutomationRule` and the shared `ValidateAutomationReferences`. The latter is the factored existing candidate-profile check, still used by runtime loading against fully constructed candidate profiles. The authoring caller checks the candidate config's profile entries, without constructing effects or loading DLLs. Plugin references remain runtime-resolved; authoring does not claim that a DLL is presently available. Accepted runtime trigger/AST/lifecycle semantics are unchanged. The canonical event capability list comes from the existing detector alias table rather than a second frontend list.

Expected errors include 428 `revision_required`, 409 `revision_conflict`, 409 `shadow_acknowledgement_required`, 409 `proposal_changed`, 404 `rule_not_found`/`source_not_found`, 422 `invalid_schema`/`invalid_reference`/`duplicate_id`/`invalid_position`/`immutable_identity`/`conversion_blocked`, and 500 `atomic_write_failed`. Validation responses contain `errors:[{path,rule_id,code,message}]`, current revision where available, and conflict-specific data. Reference errors identify `/rule/action/profile` or `/rule/action/effect/name`; general AST/schema errors identify the offending rule subtree and include the parser's human-readable reason. These are JSON paths/public concepts, not C++ type names. Existing loopback Host/Origin/Referer and JSON write protections apply at both daemon and proxy.

## Provenance and coexistence

List returns original record data, not a reconstruction:

| Provenance | Source / profile arbitration |
| --- | --- |
| `automation_v2` | `orchestration.rules`; profile actions use tier 1 at written config order; effect actions do not arbitrate profiles |
| `legacy_orchestration` | Same orchestration array/tier, original order and fields retained; read only in the new client |
| `legacy_gsi_binding` | `gsi_bindings`, tier 2 |
| `application` | Top-level `rules`, tier 3; still edited through existing Phase 4 API |
| `legacy_event_overlay` | `orchestration.event_overlays`, no profile tier; legacy executor unchanged |
| `unsupported_application` / `unsupported_container` | Raw content remains visible; no automatic repair or deletion |

Within each source array, position and path reflect the original array. A flattened list also reports the global profile precedence explanation; event overlays are not presented as profile arbitration candidates. Top-level Application Rules remain usable through the unchanged `/api/automation/rules` CRUD contract, including its revision-bound indices. V2 editing never adopts those indices as persistent IDs.

## Server-side shadow protection

For state → Activate Profile rules whose condition and scope are process-only, the server checks every top-level Application Rule. It normalizes the Application process with the accepted Phase 4 canonicalizer, then evaluates the proposed process AST/scope using the existing V2 ConditionNode evaluator on that process snapshot. Thus compound AND/OR/NOT process expressions also receive protection without a new business evaluator.

Conflict entries distinguish `equivalent_same_profile` from `same_process_different_profile`, include the original record/index and canonical process, and disclose orchestration precedence above GSI bindings and Application Rules. Both reject create/update unless `acknowledge_shadowing:true` is supplied at the checked revision. This applies even to currently disabled proposed records, conservatively requiring a decision before enabling later. The client displays conflicts and an explicit acknowledgement checkbox, and clears acknowledgement when the draft/position changes. Direct HTTP calls are tested; protection is not UI-only.

## Promotion transaction and loss awareness

A proposal contains the revision, exact source/index, proposed stable-ID V2 record, selected orchestration position, precedence change, DND mapping (including whether the original field was present), and shadowing disclosure. Preparing it changes nothing.

Only three source fields have defined conversion mappings:

| Source | Destination |
| --- | --- |
| `process` | `when.condition = {field:"process.name", op:"==", value:<original process>}` under state mode |
| `profile` | `action = {type:"activate_profile", profile:<same target>}` |
| `suppress_web_ui` | Rule-level `dnd`; true → true, false → false, omitted → explicit false |

DND is never an action or action member. All extra source fields, including display-looking names/descriptions, block conversion with an `unmapped_fields` list: no unapproved metadata mapping is assumed. The original is untouched, and confirmation cannot override this refusal. The pre-existing frozen Stage 0 `promotion_expectations.json` cases are now exercised as real conversion tests: true, false, omitted and unknown `custom_extra_tag`; historical fixture contents were not rewritten.

On confirmation the service locks/rechecks the revision, reconstructs the proposal from the current exact source, and compares the full returned proposal, including source, destination and insertion offset. It then inserts the V2 record and removes the exact original in the same candidate object and performs one atomic replacement. No committed intermediate state has neither rule or both conversion halves independently. Replaying a proposal, changing its source/destination, or refreshing only the outer revision cannot bypass verification. A simulated replace failure leaves the original bytes intact; the replace-seam test observes a complete old config and a complete two-part candidate.

**Concrete compatibility finding:** historical raw top-level rules can contain extensionless process strings, whitespace or paths. The accepted legacy runtime matcher is asymmetric for extensionless strings; V2's `.exe` equivalence is symmetric. Blind conversion could therefore broaden behavior. Promotion refuses noncanonical spellings and asks for an explicit normalization edit through the Phase 4 API first. Case differences in valid `.exe` names are safe and preserved. This is a loss-aware conversion restriction, not a change to legacy matching or a new runtime semantic. Shadow detection still conservatively recognizes `.exe`/non-`.exe` equivalent identities.

## Legacy editor safety

When any V2 record exists, `canonicalConfig` returns the original config rather than migrating legacy arrays. Old simple-list writes throw a read-only error. Blockly restore refuses before clearing its workspace; the legacy Orchestrator surface is explicitly read only and guards its save handler as well. The old condition simulator is not used to interpret V2 records. Opening the new Automation surface only calls GET/list/capabilities.

The generic web `/api/config` endpoint additionally compares the raw V2 records **and their orchestration positions** under the existing revision/writer lock. A whole-config writer that creates, deletes, changes or moves V2 records gets 409 `v2_authoring_required`. Unrelated writes preserving those records/positions still work, including Studio draft/publication metadata and Phase 4 sparse CRUD. This closes a server-side whole-list bypass of shadow acknowledgement. Explicit on-disk config editing remains a runtime compatibility input; it is not an authoring API bypass guarantee.

## Minimal functional client

A Web Automation navigation entry loads server capabilities/provenance and supplies create, stable-ID edit/delete, explicit position, validate-only, shadow acknowledgement, proposal review/confirmation and existing Phase 4 sparse editing. New-rule choices come from the daemon's four allowed pairings: state/profile, state/while_true, rising/one_shot, event/one_shot. Advanced scope/conditions/action options are editable as JSON. This deliberately functional JSON editor avoids a second validation engine; all authoritative validation is server-side. Draft edits retain their captured revision rather than silently inheriting a refreshed list revision.

No rendering blocks were copied into Automation. Studio remains HOW TO RENDER. No business trigger moved into Effect Studio. The client does not offer stack, queue, latch or event/rising profile activation. The WinUI client/API v1 code is untouched; .NET tests were nevertheless run. No visual redesign was attempted.

## Tests and verification

| Test | Final result |
| --- | --- |
| Full Release CTest | **13/13 passed**, 2.37 s |
| Stage 1 ABI/factory/lifetime | **212 assertions passed**, plus real PluginManager runtime and frozen fixture integrity suites |
| Stage 2 conformance | **257 assertions passed** |
| Stage 3 lifecycle/composition | **115 assertions passed** |
| Stage 4 reconciliation/publication | **83 assertions passed** |
| New Stage 5A authoring | **79 assertions passed** |
| Existing Phase 4 CRUD C++ tests | Unchanged and passed in full CTest |
| Actual daemon/web entrypoints, Stage 2–4 integration and new authoring HTTP test | **31/31 passed**, 17.855 s |
| Frontend tests | **39 total: 36 passed, 3 existing Windows/g++ environment skips, 0 failed** |
| Frontend production build | Passed; tracked `web/index.html` regenerated |
| .NET `Aura.Tests`, Release | **36/36 passed**, no skips; WinUI source untouched |
| Staged diff / patch | Whitespace check and reverse-apply check performed for delivered patch |

New tests cover provenance/order and unsupported records, no-write reads/proposals/validation, create/update/delete by ID, unknown data preservation, duplicate/invalid schema/reference/AST/retrigger, revision requirements/conflicts, normalized and compound shadowing, explicit acknowledgements, all four captured conversion fixtures, proposal/source tampering, real external source changes, atomic replace failure, exact once insertion/removal, concurrent writers, and legacy editor protections. The real-process test exercises daemon routes through the web proxy, Phase 4 PATCH, full-config guard and unrelated saves, conversion, and CSRF rejection on both ports. Existing tests were not weakened or rewritten.

Reproduction:

```powershell
cmake -S . -B build/automation-v2-stage2 -A x64
cmake --build build/automation-v2-stage2 --config Release --parallel 4
ctest --test-dir build/automation-v2-stage2 -C Release --output-on-failure
$env:AURA_BIN_DIR='G:\Aura\build\automation-v2-stage2\Release'
$env:PYTHONPATH='tests'
python -m unittest test_runtime_entrypoints.TestDaemonEntrypoint test_runtime_entrypoints.TestWebUiEntrypoint test_automation_v2_daemon test_automation_effect_daemon test_automation_reload_daemon test_automation_authoring_daemon -v
dotnet test tests/Aura.Tests/Aura.Tests.csproj --configuration Release
cd frontend
npm test
npm run build
```

Local logs: `build/stage5a-build.log`, `build/stage5a-ctest.log`, `build/stage5a-entrypoints.log`, `build/stage5a-frontend-tests.log`, `build/stage5a-frontend-build.log`, `build/stage5a-dotnet.log`. Native generated Studio DLL tests still exercise the real MSVC compiler despite portable g++ skips. No physical LED/live-CS2 acceptance or visual/browser interaction acceptance is claimed.

## Gemini handoff, limits and deviations

The subsequent UI pass can improve Chinese labels, provenance/precedence presentation, structured error placement, accessible forms/focus, responsive layout and proposal diff presentation. It can replace the functional JSON textarea with AST/action controls driven by this capability contract, while keeping server validation authoritative. It should preserve explicit acknowledgement, captured draft revisions, immutable IDs, proposal confirmation, unknown fields, and legacy read-only boundaries. WinUI integration beyond its existing Phase 4 API is not part of this patch.

Profile arbitration remains array-ordered; layer priority is not repurposed for profile selection. The API does not load plugin factories, guarantee telemetry field availability, persist runtime temporal memory, or provide new runtime introspection. Successful authoring means atomic config persistence; the existing daemon frame-boundary reload remains responsible for activation. Reformatting on successful writes is allowed; raw field/value preservation and failed-operation byte preservation are tested.

The noncanonical legacy process conversion refusal above is the only additional compatibility restriction discovered during integration. Shared reference-validation extraction and event-catalog exposure change no accepted runtime semantics. No deferred stack/queue/latch work, lifecycle/ABI/composition changes, broad Lighting/Studio refactor, or Stage 5B work was started.
