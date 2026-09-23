# alpha.4 configuration contract

The daemon is the runtime authority. WinUI uses the daemon's typed Lighting and Automation APIs; Studio uses the existing Web authoring routes. `config.json` is user data, not a package asset. Current code is the source of truth for the exact preset catalog and Automation condition/action schema.

## Location and first run

WinUI resolves the data root from `AURA_DATA_ROOT`, then a sibling `portable.marker`, then `%LOCALAPPDATA%\Aura`. It creates `config.json` only when absent, by copying the packaged `config.example.json` once. The daemon and Web standalone entrypoints can also copy that template when their implicit config is absent. The small template contains a `desktop` static default, one optional wave sample, global FPS 25, `auto` hardware backend and empty Automation rules. Existing config, plugins and separate client settings survive package updates. Missing explicit `--config` fails. An existing malformed or invalid file is never reset: daemon startup fails closed and logs the path and validation reason. WinUI shows the failure and config path from this launch's appended daemon log when available. On hot reload failure, the previous valid runtime state remains active until another file change; `/api/runtime/status` exposes `config.healthy: false` and `config.last_error` until a valid reload clears it.

## Root fields

| Field | Persisted type | Omission/default | Owner and effect |
|---|---|---|---|
| `profiles` | nonempty object keyed by profile name | required | Daemon profile recipes; each value must be an object. |
| `default_profile` | nonempty string naming a profile | optional: `desktop`, which must exist | Base lighting fallback and active default. |
| `fps` | integer 10–100 | optional: 25 | Daemon target FPS; a profile's explicit `fps` overrides it. |
| `hardware_backend` | `auto`, `native_hid`, `legacy_hal` | optional: `auto` | Daemon backend selection. |
| `orchestration` | object | optional: no rules | Daemon Automation v2. `rules` is an array, empty by default. Optional `fallback_profile` names an existing profile; optional `automation_freshness_ms` is a positive integer, default 3000. |
| `blockly_effects` | object keyed by Studio effect name | optional: empty | Studio draft/source and publication metadata; daemon uses the referenced published plugin profile. |

Unknown root keys are preserved by narrow writers for forward compatibility. They have no daemon semantics. Empty legacy root `rules`/`gsi_bindings` and `orchestration.event_overlays` may still be read; nonempty legacy containers are rejected with `migration_required`. No new writer emits them. Hardware aliases `native`/`hid` and `legacy`/`hal` remain parser compatibility only; new examples and clients use canonical names. No `config_version` is required.

## Profiles

A profile object defaults to `type: static` for old files; new writers should specify `type`. Supported types are `static`, `breathing`, `color_cycle`, `wave`, `custom_keymap`, `reactive`, `ripple`, `starry_night`, `quicksand`, `current`, `raindrop` and `plugin`. The daemon's `/api/lighting/presets` catalog supplies effect specific defaults, accepted parameters and enum options. `color`, `color1`, `color2`, `bg` and `keys` colors are RGB integer triples in 0–255. New writes use `brightness` as a finite ratio 0–1, default 1.0; the runtime still reads old 0–255 values. `period_ms` is the canonical period, integer at least 33 for effects that support it. Old `speed_index` is read only when `period_ms` is absent; new writes do not create it. `fps` is an optional integer 10–100; omission inherits root FPS. `plugin` uses `plugin_name` as its identity. Old `plugin`, `effect`, `effect_name` and `plugin_path` references remain parser compatibility; new publication uses `plugin_name`. Unknown effect types fail loading. Defensive runtime clamps for some old profile values remain a last resort; official writes reject invalid known fields before replacement.

The typed Lighting base/profile PATCH routes accept sparse fields, require `expected_revision`, and write only the selected profile while preserving unrelated root fields, Automation rules and explicit profile FPS overrides. Preset changes use the existing catalog; they do not rewrite an unchanged profile into another equivalent representation. Global FPS GET/PATCH uses the same 25 and 10–100 contract as the daemon, and PATCH changes only root `fps`.

## Automation and Studio ownership

`orchestration.rules` is the sole Automation rule list. Each record has a stable `id`, `model: automation_v2`, `scope`, `when.mode`, `when.condition` and `action`; the existing daemon validator defines lifetime, retrigger, composition and blend. Empty rules run the default/fallback profile. The v2 authoring API validates the whole candidate plan and references before an atomic revision-bound write. Invalid records leave the old file intact. Standalone Web and embedded Studio share these routes; neither keeps an independent rule source.

`blockly_effects[name]` stores the Studio Blockly workspace (`blockly_json`), publication settings, timestamps and last applied publication metadata (`applied_plugin_name`, revision and source). The editor may retain an unsaved transient draft in browser `sessionStorage`. Save persists the draft in the existing config; publishing stages source, compiles an immutable plugin, confirms daemon reload, then updates the config's plugin profile reference. Failure preserves the prior published reference. WinUI does not parse or store Blockly workspace JSON.

Theme, tray behavior, navigation/window state and WebView2 cache belong to client settings/storage, not `config.json`.

## Writes, validation and reload

Official writers acquire the shared named configuration lock, read the latest bytes, compare the content revision, apply the narrow mutation, validate known fields and Automation containers, flush a temporary file and atomically replace the original. A stale revision returns 409; typed request fields outside their whitelist return 400. The Web full-document compatibility route also requires `If-Match` and prevents Automation v2 changes through it. Its save coordinator discards a queued old snapshot if an earlier save changed the revision, then refreshes and reports the conflict. Failed validation or replacement leaves the original bytes intact. Unknown root fields survive typed edits. The daemon checks file timestamp and identity at frame boundaries, so consecutive atomic replacements with the same timestamp still reload; failure retains the last valid state.

## Release boundary

v0.1.0-alpha.4 is frozen to WinUI productization, configuration/release hygiene and correctness/stability fixes. Hardware feature expansion begins in v0.1.0-alpha.5.
