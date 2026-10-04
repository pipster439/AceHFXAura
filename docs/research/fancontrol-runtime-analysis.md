# Cooling read-only runtime — M3.0

**Host runtime acceptance is blocked.** Installed AceHFXService is stopped/manual, and this medium-integrity session cannot run an authenticated SYSTEM worker. M3.0 does not authorize service replacement/restart. The new candidate is built locally, not deployed or run as SYSTEM from a writable workspace. AsusFanControlService is already running and was left untouched.

Implemented transaction: medium WinUI -> authenticated fixed AceHFXPlatform command 9 -> LocalSystem broker -> fixed, adjacent AceHFXFanWorker -> fixed FanControlManager local-server activation -> reduced getter DTO -> process exit. Vendor COM stays on a single STA thread in the child. The broker shares only the existing job/stdIO process transport from the Aura read worker; it does not activate AuraSdk or use Aura enumeration.

The worker requires SYSTEM, Session0, OS-derived parent PID equal to the running SCM AceHFXService PID, and exact protected installation directories for parent and worker. It accepts no command-line flags; stdin is one typed ReadFanSnapshot request. No arbitrary path/CLSID/member/argument forwarding exists. Medium startup rejection was exercised and prevented activation.

Contract validation precedes activation. Exact activation HRESULT and CoInitializeEx HRESULT are preserved; getter RCWs do not expose successful HRESULTs, so those fields remain null with TypedAutomationCompleted evidence, while exceptions preserve HRESULTs. Channel count is bounded at 32, curve points at 16, names at 128 and wire frames at the existing protocol limit. A failed read has null count/value, not fake 0 RPM. No per-channel RPM/current manual-mode/general temperature getter is invented.

Timeout is external: 3 seconds for this read worker inside the existing 5-second client request budget. One worker is allowed across clients; successful cache interval is 1 second, failure backoff 30 seconds. Cancellation or timeout disposes/kills only the child via its job; the vendor service and broker stay running. Lost child progress is Unknown, not presumed NotAttempted/success. This is an acceptable read-only transport, **not a future write-owner lifetime model**.

Structured broker logs include observation time, state/error, counts and worker PID; no token data. Existing IPC authentication/ACL protection is unchanged. Settings adds read-only status/RPM/source/raw RPM-mode/curve-point diagnostics, 1Hz on success and backoff on failure, cancelled on page unload. No sliders, mode selectors, editable curve or Apply action exists.

Actual privileged activation, channels, live RPM, live temperature, mode/curve reads and 10x1Hz cycles were **NotAttempted**, not passed. Software fixtures test transport failures, authorization, bounds, caching/recycle and DTO safety; their synthetic SYSTEM fields are not host evidence. Compiled UI is not live broker/UI acceptance.

Machine observations: fan-runtime-enumeration.json, rpm-observations.json, temperature-observations.json and fan-curve-observations.json. Current installed/config hashes are input-provenance.json. The service candidate manifest records new binaries without installing them. See ASUS_PLATFORM_PHASE3_M3_0_REPORT.md for final tests and blockers.
