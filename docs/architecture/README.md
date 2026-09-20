# Current architecture

The Phase 3.6 development line contains native Lighting controls driven by backend effect parameter schemas. This describes source behavior, not a claim of physical acceptance.

```text
WinUI Home / Lighting ── HTTP 19897 ── aura_daemon
WinUI Studio (WebView2) / browser ── HTTP 19898 ── aura_web_ui
                                                   │ proxy / preview / publish
                                                   └── HTTP 19897 ── aura_daemon
                                                                         │
                                                rules + effects + plugins + overlays
                                                                         │
                                                         Native HID first; gated HAL fallback in auto
```

- [AuraControlClient](../../winui/Services/AuraControlClient.cs) exposes status and Lighting API calls. [LightingPage](../../winui/Pages/LightingPage.xaml.cs) builds color, boolean, enum and numeric controls from schemas and submits base-lighting changes with revision handling.
- [LightingControlService](../../src/config/lighting_service.cpp) owns Lighting configuration operations. Native Lighting is no longer the old Profile-centric Web editor; compatibility profile APIs still exist.
- [RuntimeLayout](../../winui/Services/RuntimeLayout.cs) resolves source-checkout, portable and canonical application-data layouts. [DaemonSupervisor](../../winui/Services/DaemonSupervisor.cs) manages startup and graceful lifecycle.
- [StudioPage](../../winui/Pages/StudioPage.xaml.cs) uses the existing local Web runtime. [Vite](../../frontend/vite.config.js) builds `web/index.html`; do not retire frontend or the Web server while this dependency exists.
- [AuraAdapter](../../src/aura/aura_adapter.cpp) selects Native HID or legacy HAL compatibility. The configured default is `auto`, which tries Native HID then gated legacy HAL. Explicit `native_hid` does not fall back.
- Automation/game/settings native pages must be assessed independently; the presence of a page is not proof all planned functionality is implemented.

[Studio semantics](../studio/STUDIO_WORKFLOW.md), [hardware evidence](../hardware/README.md), and [packaging limitations](../development/PACKAGING.md) are maintained separately.
