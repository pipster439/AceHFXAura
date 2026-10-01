# Current architecture

The current development line contains native Lighting controls and accepted Automation v2 through Stage 6. See [Automation v2](AUTOMATION_V2.md) for authoring, lifecycle, retrigger and reconciliation semantics. This describes source behavior, not a claim of physical acceptance.

The [alpha.7 Profile Engine core](PROFILE_ENGINE.md) documents device profile desired state, apply ordering, migration, and hardware evidence limits. Its profiles are distinct from the existing lighting recipes.

[Phase 4A / 4A.5 Device Profile Automation](DEVICE_PROFILE_AUTOMATION.md) adds daemon-owned, debounced GUID decisions, a native draft editor, accepted Manual Apply hold integration and pure-read process diagnostics. Hardware activation remains disabled; [decision/state table](DEVICE_PROFILE_AUTOMATION_STATES.md).

```text
WinUI Home / Lighting ── HTTP 19897 ── aura_daemon
WinUI Studio (WebView2) / browser ── HTTP 19898 ── aura_web_ui
                                                   │ proxy / preview / publish
                                                   └── HTTP 19897 ── aura_daemon
                                                                         │
                                                V2 RuleEngine → AutomationEffectRuntime
                                                        Base → persistent → transient
                                                                         │
                                                         Native HID first; gated HAL fallback in auto
```

- [AuraControlClient](../../winui/Services/AuraControlClient.cs) exposes status and Lighting API calls. [LightingPage](../../winui/Pages/LightingPage.xaml.cs) builds color, boolean, enum and numeric controls from schemas and submits base-lighting changes with revision handling.
- [LightingControlService](../../src/config/lighting_service.cpp) owns Lighting configuration operations. Native Lighting is no longer the old Profile-centric Web editor; compatibility profile APIs still exist.
- [RuntimeLayout](../../winui/Services/RuntimeLayout.cs) resolves source-checkout, portable and canonical application-data layouts. [DaemonSupervisor](../../winui/Services/DaemonSupervisor.cs) manages startup and graceful lifecycle.
- [StudioPage](../../winui/Pages/StudioPage.xaml.cs) uses the existing local Web runtime. [Vite](../../frontend/vite.config.js) builds `web/index.html`; do not retire frontend or the Web server while this dependency exists.
- [AuraAdapter](../../src/aura/aura_adapter.cpp) selects Native HID or legacy HAL compatibility. The configured default is `auto`, which tries Native HID then gated legacy HAL. Explicit `native_hid` does not fall back.
- Automation authoring uses the Web Studio V2 Blockly editor. The daemon owns the read-only effect catalog projection and isolated GSI simulation source; no separate native Application Rules editor remains.

[Studio semantics](../studio/STUDIO_WORKFLOW.md), [hardware evidence](../hardware/README.md), and [packaging limitations](../development/PACKAGING.md) are maintained separately.

The [RT hardware gate contract](../hardware/M605_RT_HARDWARE_GATE.md) separates
passively observed physical switch state from Profile desired per-key RT and
host-submission-only SessionApplied. Gate observation never controls the switch
or triggers writes. [Phase 4B Device Profile Automation](DEVICE_PROFILE_AUTOMATION.md)
uses a separate serialized coordinator and the existing Profile apply authority.

[Reusable software CI and local Profile automation smoke](../development/CI.md) keep
mock/dry-run regression separate from explicitly authorized hardware acceptance.
