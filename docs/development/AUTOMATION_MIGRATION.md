# DEVELOPMENT / PRE-RELEASE: one-time Automation migration

This tool is owner/development assistance, not a public compatibility contract and never a daemon startup step.

```powershell
python tools/legacy/migrate_automation_v2.py old-config.json --daemon build/Release/aura_daemon.exe --dry-run
python tools/legacy/migrate_automation_v2.py old-config.json --daemon build/Release/aura_daemon.exe --output config-v2.json
```

The source is never overwritten. The destination must not exist. Dry-run prints a unified diff; stderr reports structured mappings/refusals. The candidate must pass the current daemon `--validate-config` path before any destination is created. Review the result and select it explicitly; the tool does not install it.

Profiles, effects and hardware/general configuration are preserved. Stable IDs are deterministic; existing V2 IDs/order remain. Straightforward canonical process rules become state Activate Profile; GSI bindings become scoped state Activate Profile; equivalent builtin state/event overlays become while_true/one_shot typed profile_effect actions. Event duration/fade/attack is explicit LegacyEnvelope metadata. Historical 10-second GSI freshness is explicitly recorded where needed.

Unknown Automation fields, ambiguous process normalization, coercion/missing-field differences, incompatible freshness, same-priority activation ordering, interleaved persistent/transient ordering and unproven plugin lifecycle mappings refuse the whole conversion. There is no force-lossy option. Resolve refused records manually with the production V2 contract; no old executor remains.

Plugin ABI v1, no-lifecycle plugins, LegacyEnvelope and legacy_hal are supported and are not retired by this utility.
