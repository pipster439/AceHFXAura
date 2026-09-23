# alpha.4 release checklist — WinUI desktop entry

Version source: [`VERSION`](../../VERSION). A generated ZIP is a candidate, not approval to publish. Record the candidate SHA-256, commit/patch, Windows version and test date. Never substitute dry-run tests for physical results.

## Automated gates

- [ ] C++ Release ALL_BUILD and CTest pass (including original Native HID/Automation/GSI/Plugin tests).
- [ ] Frontend `npm test` and `npm run build` pass; explain any skips.
- [ ] `dotnet test tests/Aura.Tests/Aura.Tests.csproj -c Release` passes with `AURA_INTEGRATION_BIN` set to the freshly built native Release directory; no lifecycle test skipped for occupied ports.
- [ ] `dotnet build winui/Aura.WinUI.csproj -c Release -p:Platform=x64` succeeds.
- [ ] Selected Python runtime/authoring/effect/reload/retrigger tests pass with `AURA_BIN_DIR` set to the same native directory.
- [ ] `test_winui_productization.TestGsiConfigurationContract` passes: typed cfg state, whitelist, Origin rejection, atomic replacement, revision conflict and failed-write preservation.
- [ ] Default package script succeeds, includes GUI PRI/XBF/assets/dependencies, native x64 sidecars, Studio web asset, four SDK headers and manifest hashes. No ASUS DLL/user config/development marker.
- [ ] `AURA_PACKAGE_DIR` points to the final extracted candidate; `test_winui_productization.TestPackagedRuntime` passes outside checkout assets.
- [ ] VERSION, GUI About/assembly, native product versions, runtime manifest, archive name and notes agree. CI Windows/frontend jobs are green for the exact release commit.

## Configuration acceptance (record against the exact candidate)

- [ ] Fresh isolated data root creates the small canonical config once; restart preserves Lighting, profiles, Automation and Studio edits.
- [ ] Existing malformed/invalid config is not overwritten; the failure and path are visible to the user.
- [ ] Global FPS 25 default, 10/100 bounds and profile override/inheritance match runtime behavior after reload.
- [ ] Automation and Studio publication saves preserve unrelated profiles, Lighting and opaque root fields; failed publication retains the old reference.
- [ ] Stale revisions surface a conflict; failed temp/replace keeps the previous config bytes.
- [ ] Rapid consecutive valid saves reload the latest config; test a data-root path with Unicode and spaces.
- [ ] Candidate ZIP contains no user/private config; upgrade preserves config, plugins and client preferences.

## Manual desktop and lifecycle gates (Windows 11 x64)

- [ ] Start complete extracted ZIP from a clean-machine-like location without source checkout, Node, CMake or .NET SDK; core starts automatically and no GUI recursion occurs.
- [ ] Repeat under a path containing spaces/non-ASCII characters and with a fresh explicit data root. Config is created once; reopen/upgrade preserves modified config, plugins, theme and tray preference.
- [ ] Test canonical and explicitly portable layouts, missing/corrupt payload diagnostics and an incompatible external core. Never stop external processes to recover automatically.
- [ ] Launch GUI twice during startup and from tray: one window, one tray icon, no duplicate daemon. Restore/close/Exit are consistent.
- [ ] GUI-owned exit stops only its core/web tree; externally started daemon survives GUI exit. Restart/reconnect and abrupt GUI termination leave no owned orphan process.
- [ ] Home reports actual hardware/backend/profile/FPS and REAL/SIMULATION correctly; unplugged hardware is not reported as connected.
- [ ] Lighting preset/schema fields, sparse save, advanced plugin notice, dirty navigation, concurrent Studio revision conflict and cancelled in-flight save behave correctly.
- [ ] Test 600 DIP minimum width, 500 DIP minimum height, 100/150/200% DPI, light/dark/system themes. No clipped Settings controls, slider value jumps, notification-driven page jumps or stale page notifications.
- [ ] Rapid navigation and back/Settings/titlebar selection remain synchronized; background page work stops. Repeated Studio navigation retains one WebView/editor and its draft.
- [ ] WebView2 absent: actionable install link/error; installed: Studio loads. Suppress web: core/Home/Lighting/GSI continue; Studio reports web unavailability and recovers without discarding a retained draft automatically.

## alpha.4 UI polish gates

- [ ] Global FPS GET/PATCH tests cover default 25, boundaries 10/100, invalid integers/types/unknown fields, revision 409, atomic failure, unrelated fields and profile overrides, and actual daemon hot reload.
- [ ] FPS dialog resolves dirty lighting via save/discard/cancel. Failed draft save prevents the global write; global conflict rereads configuration and remains visible. Runtime FPS is labeled a target, never a measurement.
- [ ] Opt-in `test_ui_layout.py` records sibling left/width and viewport height within 1 DIP during expansion and overlay notification open/close, no horizontal scrolling, and navigation pane toggles. Record actual DPI and distinguish XAML-rendered captures from desktop compositor screenshots.
- [ ] Inspect 600x500, 800x600, 1060x720 DIP and maximized, long paths, slider dragging, theme selection, scroll bottom, and light/dark at 100/150/200% DPI. Passing one host scale is not acceptance of the others.
- [ ] GSI empty states distinguish missing setup, installed/waiting, stale data and unavailable core. Simulation is clearly visible even with advanced diagnostics collapsed; player fields retain missing/unknown semantics.
- [ ] Settings keeps product/version and core/Studio status visible, technical details collapsed; unknown external runtime paths are not inferred from the client's layout.

## Embedded Studio gates

- [ ] UI-005: standalone `/` retains the full Web shell and stored theme; embedded Studio has no second product sidebar or external page scrolling.
- [ ] Embedded `studio` and `automation` URL entry, WinUI initial/dynamic theme, Blockly resize, 600×500 through maximized layout, and 50 native navigation reentries pass on the final package.
- [ ] Effect draft survives native navigation; Effect/Automation switching does not navigate the WebView. Web suppression preserves the editor until explicit retry.
- [ ] Verify actual user interaction: Blockly editing, Studio publication and Automation revision-bound save on the exact candidate; keep real keyboard and CS2 acceptance separate.

## GSI / runtime / hardware gates

- [ ] Native HID real keyboard connection, Lighting change, continuous output, preview/apply/exit behavior and USB reconnect work as in alpha.3.
- [ ] Native Game Integration detects actual CS2 library/cfg candidates, displays selected path and matching/different/missing state; reinstall requires explicit confirmation. Cancel writes nothing; permission failure and revision conflict remain visible.
- [ ] SIMULATION enable/edit/heartbeat/+kill/exit runs daemon evaluation; distinguish queue acknowledgement from application, REAL from SIMULATION, Online from freshness. No duplicate POST retry or stale event replay.
- [ ] Real CS2 sends GSI after cfg install/restart; health/armor/bomb/round/kills match received data. First real packet after simulation seeds event baseline without replay.
- [ ] Automation v2 state/rising/event and existing retrigger behavior remain unchanged on real CS2 and keyboard.
- [ ] Packaged Studio saves drafts and publishes/loads a native plugin using packaged SDK on a machine with MSVC + Windows SDK, without checkout headers. Reload failure preserves old publication/config.
- [ ] Owner records all physical/GUI results and limitations against the exact candidate, not an older alpha.3 package.

## Publication gates

- [ ] CHANGELOG candidate marked released only after the above gates. Tag `v<VERSION>`, checksum and notes match the validated bits.
- [ ] Notes state Windows 11 x64, unsigned/SmartScreen reality, WebView2 dependency and local compiler requirement for native publishing.
- [ ] No independent Light Bar, Hall controls, new HID commands, Automation extensions, ABI rewrite or plugin crash-isolation claims.
- [ ] Publishing/commit/push/tag creation is separately authorized; packaging does not perform them.
