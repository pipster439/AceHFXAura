# Device Profile RT product model — corrected alpha.7 interpretation

## RT production cutover update

The current explicit per-key planner, prior-state restoration, `51 54` typed
selector contract and `51 53` rejection are documented in
[PROFILE_RT_PRODUCTION_MODEL.md](PROFILE_RT_PRODUCTION_MODEL.md).
Earlier production proposals below are historical evidence/context, not the
current apply contract. No global/common RT inheritance is implemented.


## Product state versus protocol primitive

Armoury Crate edits an enabled key set, press/release sensitivities for those
keys and continuous mode. The physical switch selects RT trigger mode. Shared
UI sensitivity and separate-mode controls are editing conveniences; they are
not evidence of a firmware common/base RT value with inheritance.

51 53 is a static-confirmed **all-key/bulk RT primitive**, not a demonstrated
user-facing global setting. No captured UI action invoked it. 51 54 is the
captured per-key state/parameter operation with an explicit enable flag. All
Select still used 51 54, including extra bank identities not authorized for
new Aura production use.

## Existing schema assessment

| Existing field | Assessment | Current compatibility behavior |
|---|---|---|
| `keys[].rapid_trigger.enabled` | Real per-key RT enable intent; captured byte 8 | Preserve and edit explicitly |
| Per-key `press_mm/release_mm` | Real independent sensitivity fields | Batch editor stores an independent object on each selected key |
| Profile/root `global_rapid_trigger` | Early host model, not proven firmware common/base | Preserve as legacy data; compatibility expander; daemon apply blocker stays |
| Global object's `enabled` | Early validation gate, not physical switch or key-set master | Keep validation only for that legacy object |
| `separate_mode` | UI grouping preference; no separate firmware bit established | Legacy rule unchanged; per-key runtime already writes two sensitivities independently |
| Global RT `top_mm/bottom_mm` | Historical assumed protocol fields contradicted by static HAL layout | Preserve document meaning; no translation into actual Deadzone or new writer |
| Enabled key set | Real product concept | Derive summary from explicit draft objects, never hardware state |
| Overall management master | Redundant for per-key objects | Do not add a persisted master; object presence manages a key |
| HostProfile shared parameters/list | Saved host editing intent | Not firmware common/base or readback; current safety requirements remain |
| SessionApplied | Successful host submissions in this session | Not hardware truth; unchanged |

Schema v1 allows disabled per-key RT with valid configured numbers. It rejects
a configured global legacy object with enabled=false. That rule must not be
reinterpreted as a required master for per-key RT. No schema predicate changes.

## Implemented WinUI adapter

* Normal section presents **配置中已启用按键**, managed count and selection
  buttons. Selection changes neither document nor device. All Select is not
  labelled Global RT enabled.
* Reuse keyboard selection. **管理所选按键** controls per-key object presence;
  **启用所选按键的快速触发** controls enable flags. Press/release are independent.
  Multi-selection explicitly shows first-key values and warns edits update all.
* No overall RT master is introduced. Selected-key edits never create root or
  Profile `global_rapid_trigger`.
* Old data remains under **旧版快速触发参数**, with **启用快速触发设置（旧版参数）**.
  This control only affects the legacy object. The expander is hidden when
  there is no legacy data. Removal is explicit draft editing, not migration.
* Numeric, old enable and per-key DKS errors are specific and appear inline
  and in summary. Save/conflict/switching refresh the same derived messages.

**Removal is not inheritance:** enabled=false saves explicit disable intent.
Removing a per-key object instead invokes the existing runtime's attempt to
restore trusted prior HostProfile/manual values and enable membership, failing
closed when unknown. This task changes UI explanation, not that runtime policy.

## Future schema proposal (not implemented)

Prefer v2 explicit per-key RT states and separate optional editor preferences.
Derive enabled set and management summaries. Do not add common/base inheritance
without evidence. Continuous mode needs physical gates before a new typed
property/writer. Physical-switch state remains observational, not a software
enable operation.

Do not automatically expand legacy global RT to all keys: old documents do not
identify the intended enabled key set, top/bottom fields have questionable
protocol provenance, and global objects were blocked by the planner. Preserve
the old object in a migration envelope, retain unknown data, require user review
of keys/values, and migrate idempotently via daemon revisioned atomic writes.
Root RT metadata likewise remains archived host editing intent unless a
reviewed mapping exists. No v2 or automatic migration occurs here. Existing
valid v1 documents remain readable without silently reinterpreting them.

## Next production proposal gates

Keep the verified per-key path and DKS/HostProfile safeguards. Review a semantic
selected-key coordinator separately from an optional all-key primitive. Resolve
the historical 51 53 builder field mismatch before wider use. Future all-key
typing, if accepted, must represent selector + one value, continuous mode +
key enable and fixed verified scope, with preservation tests; it is not a
`SetGlobalRt(press, release, top, bottom)` product setting.

No new HID command or automatic HID write was introduced. Automation decisions
do not call DeviceProfileRuntime::Activate. Phase 4B remains disabled.
