# M605 Stage B passive observer

Independent research executable; not linked into Aura. This is a **structural
candidate probe**, not a physical key provider. Synthetic reports do not prove
the actual USB layout or Press/Release semantics.

## Build and offline verification

From the repository root, using CMake and a Windows C++ compiler:

```powershell
cmake -S tools/research/physical-key-stage-b -B audit_artifacts/physical-key-preflight-20261009/build -G "Visual Studio 18 2026" -A x64
cmake --build audit_artifacts/physical-key-preflight-20261009/build --config Release
ctest --test-dir audit_artifacts/physical-key-preflight-20261009/build -C Release -V
```

The 58 tests construct synthetic fixtures inside `offline_tests.cpp` and
`delivery_tests.cpp` and exercise the same `InputDelivery`, `Observation` and
`Subscription` logic used by the Windows adapter. They
exercise framing, filtering, monotonic timing, bounded counts, reset, duplicate
accounting and fake subscription cleanup. They do not simulate an actual HID
device, Windows lock notifications, native Raw Input delivery or HAL callbacks.

## Read-only preflight (does not listen)

```powershell
$observer = '.\audit_artifacts\physical-key-preflight-20261009\build\Release\stage_b_observer.exe'
& $observer --preflight --collection notifications
& $observer --preflight --collection control
```

Preflight reads Windows session information, Raw Input device metadata and
cached preparsed capabilities. It never registers Raw Input or reads a report.
Exit 0 requires an unlocked local console, exactly one matching collection, and
an empty Raw Input registration table in this process;
exit 2 means blocked or invalid arguments. No device paths or serials are printed.

## Owner-operated test session only

Do not launch capture from automation or a remote/locked desktop. The owner must
explicitly enter a dedicated test session, close sensitive input contexts and
launch one selected case. Keep the observer foreground. No raw report or live
input is to be sent to an AI API, uploaded or pasted into an agent conversation.

```powershell
# Run only after entering the dedicated local test session.
& $observer --collection notifications --case A --timeout 10
# After closing that process, run the other channel separately.
& $observer --collection control --case A --timeout 10
```

Click **Start trial**, press only the displayed key, hold and release once within
the timeout. Click Start again for each of three independent repeats. Registration
is removed between trials. A process binds to one device and one collection only;
it never auto-reconnects or auto-restarts capture. The third trial closes the
window automatically. Idle windows close after 60 seconds. Timeout accepts 5..30 seconds.

Cases: `A`, `W`, `S`, `D`, `LWIN`, `LSHIFT`, `RSHIFT`, `COPILOT`,
`COPILOT_LSHIFT`, `COPILOT_LWIN`, `AWSD`, `RTGATE`. `RTGATE` requires notifications;
its physical positive control is planned in `NEXT_DAY_RT_TEST.md` and was NOT RUN
overnight. Start with A on both collections;
continue W/S/D only if safe. Cases invoking OS UI may lose foreground and abort;
such a trial is incomplete, never negative protocol evidence. Do not enable DKS,
remap keys, switch profiles or change any hardware configuration for this tool.
Testing an already configured DKS/remap scenario needs a separate owner-reviewed
plan after ordinary-key observations warrant it. All 68 keys remain untested.

The local console prints aggregate summaries only. If evidence is retained by the
owner, save only these summaries and a manual checklist: case, trial, whether the
requested action was completed, stop reason and observation completeness. There
is no built-in raw export, file writer or full event history. Do not use a transcript
that captures unrelated terminals or application content. Keep incomplete trials.

## Contracts and meaning of output

| Selection | Strict cached binding | Assumed RAW buffer shape, not captured proof |
|---|---|---|
| `notifications` | VID 0B05/PID 1B7E, MI_02 Col03, FFC0/1 | 21 bytes, ID 3 |
| `control` | Same VID/PID, MI_01, FF00/1 | 65 bytes, ID 0 prefix |

The collection names do **not** establish the route of 73/74. Unknown framing
stops as malformed instead of moving offsets, scanning payloads or issuing queries.
Summaries distinguish cached `expected_length/expected_report_id` from the last
target batch's observed `dwSizeHid/dwCount`; these dimensions contain no payload.
The zero prefix for the unnumbered MI_01 report remains a Windows-buffer hypothesis
until `RAWHID.dwSizeHid` is physically observed. A format mismatch is not proof
that the firmware lacks these events.

The probes assume byte 1 = marker, bytes 2..3 = little-endian configuration Wire
ID, byte 4 = opaque parameter. These are hypotheses borrowed from HAL dispatch
and firmware builders, not a verified USB parser. `field_hypothesis_selected`
compares only preselected configuration IDs; it cannot prove physical identity.
Parameter bins are 0,1,2,3,4,other and cannot prove DKS attribution. No numeric ID
or parameter outside those bins is emitted. A marker byte at another payload
position is not treated as an event.

`repeated_73/74`, `marker_74_before_73` and exact consecutive-report duplicates
are aggregate structural observations, not per-key sequence validation. Timing
is host receipt in monotonic milliseconds; it is neither switch latency nor
firmware sampling rate. No timing-based pairing is attempted. Aggregates cannot
establish per-key ordering, a missing release, physical coverage or logical/physical
independence in a combination. Discovery of candidates needs a separately reviewed,
bounded semantic validation step before any provider or Reactive/Ripple migration.

Each capture run gets a random session ID, an in-memory exact `hDevice` binding,
an alias `M605-target-1`, and a connection generation. Device removal ends the
run and advances its generation; reconnect requires fresh preflight and explicit
start in a new process. No serial, fingerprint or stable machine identifier is
persisted. `physical_identity` and `press_release` always remain `UNKNOWN`.

The 2026-10-09 hardware validation added local registration snapshots, safe timer
ticks, observed trial duration, WM_INPUT/header-filter/payload-batch counters, and
end-of-trial target revalidation. Zero reports can now be distinguished from
messages discarded by the target filter. Registration snapshots check the exact
page, usage, foreground flags and this process's window; removal must leave zero registrations.
These diagnostics are counts/booleans, not input data. A zero-message trial still
cannot prove the device sent nothing on USB, because no positive vendor-delivery
control is produced. The window title names the scope/case and its active status
shows the trial number and countdown. Launch this interactive window visibly:
`Start-Process -WindowStyle Normal` (Hidden may hide its initial ShowWindow).
Target identity is also revalidated from cached metadata on each active timer tick.

`--registration-audit --collection notifications|control` performs a reversible
Windows registration metadata check without a message pump or `Observation::arm`;
it reads no payload and immediately removes the registration. This is not a
physical test. On the measured Windows host, GetRegisteredRawInputDevices returns
the correct page/usage/window but flags=0 after a successful DEVNOTIFY request.
Snapshot flags 0 and 0x2000 are accepted; all other flags remain rejected. Requested
flags are always exactly DEVNOTIFY. The missing echo is not proof of device-change
notification delivery, which still needs separate physical validation. Four new
offline regression cases cover this distinction and reject errors, background
flags, mixed collections and window mismatches. The earlier NULL sizing-query
mistake and overly strict flag comparison caused safe stops before capture;
those attempts must be retained as tool failures, not keyboard failures.

## Safety and privacy boundary

- No `CreateFile` HID handle, hook, Output/Feature/Input query, driver install,
  vendor HAL execution, network call or Aura configuration/production mutation.
- One vendor usage pair per independent process. No `INPUTSINK`, `NOLEGACY`,
  `PAGEONLY` or keyboard usage registration. Existing registrations in this
  process block startup. A named local mutex prevents parallel observer processes.
- `RID_HEADER` type/handle filtering precedes `RID_INPUT`; other devices' payloads
  are never fetched. Same-usage notifications may reach the window, then are
  discarded by header. Collection path and cached report capabilities must match.
- Local active unlocked console/default input desktop, WTS notifications and
  foreground checks gate each capture. Lock/disconnect/focus loss aborts; timers
  and input handlers both enforce timeout. No fallback to background capture.
- Batch buffer at most 65536 bytes, budget 200000 reports per trial; only one
  previous report (at most 65 bytes) is retained for comparison. Buffers are
  explicitly erased; stop clears temporary state and unregisters input. Native
  WTS/timer/window resources are released on exit. Unregister failures abort and
  are reported; process exit is the final resource boundary, not a fake success.
- Cross-process Raw Input registrations are documented independently; physical
  coexistence with HardwareRtGateMonitor is **NOT RUN**. Any interference ends
  the experiment. Do not stop Aura/ASUS software or change its safety gates.

Current acceptance and known limits: `docs/research/physical-key-state/STAGE_B_REPORT.md`.

## Overnight offline audit (2026-10-09)

No observer mode was executed overnight, including preflight or registration
audit. Only the offline test executable ran. Actual Windows delivery, lock,
unplug and new UI behavior are BLOCKED_PHYSICAL. Earlier evidence is preserved.

`InputSource` isolates RID_HEADER, size-probe and payload-copy operations. The
native source performs GetRawInputData; mock sources exercise its result
contracts without OS input. `InputDelivery` filters source and session before
fetching payload, validates copied header/length/count, strips only RAWINPUT
trailing padding and wipes an aligned temporary buffer. It never searches for
markers at alternate offsets. The MI_01 64-versus-65 RAW buffer question stays
UNKNOWN until a real target batch is received. Core uses the existing pure
`m605_rt_gate.h` parser; no production monitor or backend is linked or edited.

`wm_input_total` counts handler entries before safety/active filtering, across
the process; `wm_input` counts active-trial entries. `wm_input_inactive`, guard
and deadline rejections distinguish discarded messages. Cumulative device-change
counts, per-trial header/size/copy failure counts, snapshot checks and window/
thread booleans are metadata only. Actual HWND/thread/device numeric identities
are kept in RAM; output uses validity booleans and the target alias. Histogram
arrays emit only nonzero `[bin,count]` pairs, never report bytes or key sequences.
`report_id_histogram` counts IDs inspected during framing validation (including
an invalid batch prefix); `report_length_batch_histogram` counts core batch
attempts, bins 0..65 and 66=larger. These are not counts of accepted key events.

`read_failures` bins: 0 header read, 1 size read, 2 size bounds, 3 copy read,
4 changed header, 5 HID count/length bounds. Invalid report ID and core framing
have separate counters. Native registration add/removal errors and message-loop
errors are recorded separately; expected final WM_QUIT preserves the trial stop
reason. Unexpected WM_QUIT or GetMessage failure stops and cleans the listener.
The snapshot is rechecked each active timer tick. Foreground-only registration,
manual Start, strict collection scope and three-trial lifetime remain required.

`rt_on/off/invalid/changes/repeats` summarize only recognized RT switch reports.
The active display shows the current known RT state; it starts UNKNOWN, becomes
ON/OFF only on valid 03 76, becomes UNKNOWN on malformed RT, and STALE at stop.
`rt_last_valid` is the last valid switch value retained as one aggregate datum;
it is not fresh after stop. No silence-based OFF or key release is invented.
No 73/74 state machine, physical identity mapping or provider is implemented.
See `OVERNIGHT_OFFLINE_REPORT.md` and `NEXT_DAY_RT_TEST.md` for limits and the
owner-operated positive-control plan. Do not run this observer unattended.

## Startup preflight regression fix (2026-10-09)

All modes now use the same session/collection/process-registration gate. Output
includes `blocked_stage`, `reason`, `api`, `win32_error`, and the registration
API's `registration_returned` / `registration_capacity_out`. Empty enumeration
succeeds with returned=0 even if the supplied capacity remains 1. Existing
entries and API failures still block. Capacity is not a success-path entry count.
The previous `returned == 0 && capacity == 0` check rejected a clean process
before creating any window; the old preflight mode skipped that check.

Nine new offline regression cases cover empty-table semantics, actual entries,
API errors, common startup gates, and fail-closed session/collection results.
On the local host, both collection preflights and metadata-only registration
audits passed. Audits read zero payloads and immediately unregister; this does
not establish WM_INPUT delivery or RT/73/74 semantics. All physical tests remain
NOT RUN for this fix. Foreground and explicit Start guards are unchanged.

```powershell
& $observer --registration-audit --collection notifications
& $observer --registration-audit --collection control
# RT command below is for the owner in a dedicated safe local session only.
# It was not executed in this validation. Click Start separately for each trial.
& $observer --collection notifications --case RTGATE --timeout 10
```

See `docs/research/physical-key-state/PREFLIGHT_STARTUP_FIX_REPORT.md` for the
root-cause reproduction, guard audit, validation evidence and remaining limits.
