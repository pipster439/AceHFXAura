# Legacy Global RT Migration — alpha.7

## Ownership and protocol boundary

DeviceProfileRuntime is the single document writer. Its existing loader,
validation, CommitLocked revision check, named writer mutex and atomic file
replacement own migration. Coordinator and WinUI do not migrate files.
Migration submits **zero M605/HID operations**, changes no physical RT gate,
and never acknowledges quarantine. Normal RT stays explicit per-key `51 54`.
`51 53` remains blocked. Resolver/planner legacy preflight remains intact.

## Serialized fields and old intent

| Layer | Legacy | Current |
|---|---|---|
| Durable device baseline | `global_defaults.global_rapid_trigger` | `global_defaults.keys[].rapid_trigger` |
| Profile desired | `profiles[].magnetic.global_rapid_trigger` | `profiles[].magnetic.keys[].rapid_trigger` |
| Temporary override | `global_rapid_trigger` compatibility-only | explicit per-key objects; legacy still blocked |

Old v1 object: `enabled`, `press_mm`, `release_mm`, `separate_mode`, `top_mm`,
`bottom_mm`, optional `continuous` and extension data. Old validator required
enabled=true, legal sensitivities, legal top/bottom and equal sensitivities in
non-separate mode. Older writer incorrectly treated this as a global hardware
setting. The object is saved **host intent**, not an actual firmware getter.

New object: enabled + press/release + editor separate_mode + continuous OFF.
Object absent/null is unmanaged; enabled=false is an explicit managed OFF.
There is no RT global inheritance and no firmware separate-mode bit.

## Contract: NeutralV1AllAuditedKeys

Document schema remains v1; one additive archive records conversion provenance.
This migration explicitly defines old complete neutral global host intent as
**all 68 audited logical keys use the same saved pair**. It is not a claim that
Armoury uses `51 53`, or that arbitrary enabled=true legacy data implies all
keys. This exact recognized subset is the only automatic expansion allowed.

| Input | Document operation |
|---|---|
| Absent/null | no migration, no revision increment |
| Empty object, or object with only null-valued fields | remove legacy, preserve original in archive, no managed keys generated |
| Complete old valid enabled=true, press/release0.1–2.5 in0.1 steps, top=bottom=0, continuous absent/false | fill explicit RT objects in audited68-key scope, then remove legacy |
| Existing explicit RT including enabled=false | preserve entire object and extensions; never overwrite |
| Original explicit root RT vs Profile legacy | root explicit state wins; no Profile object generated for that key |
| Unmanaged/null key in convertible all-key scope | becomes managed under the explicit all-key migration contract |
| Unmanaged key without recognized all-key intent | remains unmanaged |
| Nonzero/missing/invalid top/bottom, disabled master, invalid sensitivity, continuous ON, malformed legacy | retain verbatim; MigrationRequired; unchanged planner guard blocks target |
| Would add enabled RT beside existing custom DKS | retain that legacy layer; no silent Standard conversion |
| Unknown archive extension namespace | retain; no overwrite |

Root original explicit identity is captured before any expansion. Profile-local
legacy can overwrite root-generated legacy intent, but not original explicit
root keys; explicit Profile state remains highest priority. JSON key arrays are
sorted by logical ID after conversion. All unrelated fields/GUIDs/automation
configuration/extensions remain. Legacy unknown fields are retained in the
archive and original backup, not reinterpreted as RT parameters.

**DKS/RT authority is not weakened:** conversion does not make DKS known. A
converted all-key RT Profile may subsequently fail ordinary preflight for
unknown Standard DKS or another safety requirement. User must review its broad
managed footprint and set trusted Standard intent explicitly where appropriate.
Migration must never add Standard DKS simply to make activation pass.

## Persistence and failure

On valid document load, compute migration without I/O to hardware. Under named
writer lock, compare disk revision/content, create exclusive
`device-profiles.json.legacy-rt-v1.r<old revision>.bak`, verify byte equality and
flush backup, then reuse CommitLocked for one revision increment and atomic
replacement. Existing backup is reusable only if exact original bytes match.

Archive root extension `legacy_rt_migration_v1` contains contract + archived
original objects, scope/profile GUID, source revision and generated-key count.
The archive is never a live legacy setting or a second desired RT truth.
On repeated load, converted fields are gone: no further write/revision/backup.
Null fields are not treated as effective legacy and do not repeatedly migrate.

Failure preserves the prior document and legacy guard; valid core Profiles
stay loaded with a Failed migration diagnostic. Unsupported layers can remain
while eligible independent layers migrate in one commit; report explicitly
states CompletedWithBlockedLegacy. No destructive schema-v2 reset.

For persistence retry, revisioned POST `/api/device-profiles/migrate-legacy-rt`
reuses the same loader migration method. It does not synchronize/probe device
presence; response is cached state. A stale expected_revision returns409.
New create/update/defaults/duplicate cannot introduce/change nonnull legacy.
Unchanged unsupported legacy can round-trip; removal is allowed. Manual global
baseline persistence accepts actuation/deadzone only, not RT.

## UI and diagnostics

Magnetic global context labels old RT controls read-only and disables them;
single/multi-key verified controls remain. Profile legacy inspector allows
removal only, never enabling/editing old parameters. Fresh DTO serialization
omits null `global_rapid_trigger`; nonnull compatibility data round-trips.
Client Save also rejects creation/modification; daemon is authoritative.

Read-only diagnostic `legacy_rt_migration` exposes status/contract/counts, never
archive contents or backup path. `rapid_trigger.legacy_global_rt_present` is
computed from current selected Profile + root document, never hardcoded.
`last_plan.rapid_trigger_management` describes the last actual plan; historical
failed plans are not fabricated as current successful submissions.

P4B does not migrate. Normal new configuration revision invalidates prior
decision admission; unchanged coordinator can reevaluate the same semantic
foreground target using the new document revision after migration.
No physical Phase4B acceptance is performed as part of document conversion.
