# alpha.9 integrated candidate acceptance

This matrix carries forward the stabilization and firmware hardening candidates.
It does not replace or alter their frozen evidence. No VERSION change or release
authorization is implied. Software evidence is never physical acceptance.

| Area | Evidence / acceptance status |
|---|---|
| Firmware Compatibility Gate | VERIFIED_SOFTWARE scope: current-session admission; combined CI and test synchronization history in `audit_artifacts/alpha9_lighting_compatibility/CI_RESULTS.md` |
| M605 lifecycle, reconnect and session safety | VERIFIED_SOFTWARE scope: fake transport/session regressions; physical reconnect BLOCKED_PHYSICAL |
| Blockly 13 Workspace Safety | Inherited candidate; frontend and .NET combined CI results as above |
| Copilot event consistency | Inherited candidate; native combined CI results as above |
| Profile Automation | VERIFIED_SOFTWARE scope: coordinator/manual/rapid ownership transitions |
| HostManaged + AuraDirectRgb | VERIFIED_SOFTWARE; physical regression BLOCKED_PHYSICAL |
| HardwareSlot + FirmwareBank | VERIFIED_SOFTWARE for legacy defaults and suppression; restoration BLOCKED_PHYSICAL |
| HardwareSlot + AuraDirectRgb | VERIFIED_SOFTWARE for selector/RGB/failure/concurrency policy; physical coexistence BLOCKED_PHYSICAL |
| Startup/crash recovery and shutdown ownership | VERIFIED_SOFTWARE for admission and structural ordering guards; actual first/black frame BLOCKED_PHYSICAL |
| HostManaged + FirmwareBank | PARTIAL: model and suppression supported, firmware restoration unverified |
| Hosted CI | NOT_RUN in this task |
| Final concentrated Owner acceptance | Go/no-go in current task FINAL_REPORT.md; Owner review pending, physical execution NOT_RUN |
| VERIFIED_PHYSICAL | No new physical evidence in this task |

Current task evidence: `G:\Aura\audit_artifacts\alpha9_lighting_compatibility\`.
Inherited evidence: `G:\Aura\audit_artifacts\alpha9_stabilization\` and
`G:\Aura\audit_artifacts\alpha9_firmware_hardening\`.

Owner acceptance must include actual supported firmware identity, all three
supported combinations, RT/DKS/Actuation/SpeedTap retention under continuous RGB,
startup and shutdown, rapid/manual/automatic switches, external slot drift,
unplug/replug, and Aura-to-firmware restoration. No invented HID release/stop
command is authorized. `FirmwareLightingRestoreUnverified` stays unresolved until
there is physical evidence. Keep alpha.9 as a candidate awaiting Owner review.


## Automation UI final closure (offline)

Evidence: `G:\Aura\audit_artifacts\alpha9_automation_ui_closure\`.
This extends the inherited lighting candidate without changing its acceptance.
The latest canonical run, counts, warnings and skip details are recorded in
`audit_artifacts/alpha9_automation_ui_closure/CI_RESULTS.md`.

| Area | Status and scope |
|---|---|
| Target labels (HostManaged, slots 1–5, Aura/FirmwareBank, duplicate slot IDs, missing target) | VERIFIED_SOFTWARE: Fake API/model regressions |
| In-page slot selection, explicit Profile create, rule/fallback binding | VERIFIED_SOFTWARE: native WinUI controls with in-process Fake API; separate create/draft/save confirmations |
| Create rejection, ambiguous/interrupted response, revision conflict, saved rule preservation | VERIFIED_SOFTWARE: injected failures; no automatic create replay |
| Existing automatic activation of newly created Profile IDs | VERIFIED_SOFTWARE: real Runtime/coordinator with FakeTransport; HostManaged → slot 3 → slot 5, selector only, ManualHold |
| Old daemon / old Profile compatibility | VERIFIED_SOFTWARE: capability checks, legacy defaults/create payload, unchanged Profile and automation schema 1 |
| Open picker/editor state during polling | VERIFIED_SOFTWARE: native control fixture and model regression; no physical input injection |
| Firmware, selector, Stage/Apply, RGB ownership protection | Existing production admission/synchronization retained; integrated offline regressions in current CI report |
| Visual layout, 125% DPI, human keyboard/screen reader acceptance | MANUAL_NOT_RUN |
| Actual automatic slot switching, magnetic retention and RGB coexistence | BLOCKED_PHYSICAL; Owner deferred |
| New physical evidence | VERIFIED_PHYSICAL: none |
| Hosted CI / release / deployment | NOT_RUN; alpha.9 candidate only |

All requested automation UI software functionality can be completed offline.
Owner physical acceptance remains required for the integrated product, including
`FirmwareLightingRestoreUnverified`; it does not block this UI implementation.
