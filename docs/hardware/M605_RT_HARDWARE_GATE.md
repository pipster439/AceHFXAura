# M605 RT hardware gate — alpha.7

## Evidence and independent state

Physical keyboard/capture: firmware **1.00.59**. Firmware static audit: **1.00.58**.
The physical switch is observed device status, not Profile desired state and not
SessionApplied per-key firmware readback. A Profile owns explicit per-key RT
objects submitted through the existing `51 54` path. No Profile controls the
physical switch. `51 53` remains blocked. Phase 4B remains disabled.

## Exact status notification

Observed: MI_02 / EP **0x8C IN**, 21 bytes, actual Report ID **03**:

| Offset | Confirmed meaning |
|---|---|
| 0 | Report ID 03 |
| 1 | Event 76 |
| 2..3 | zero in captured notification |
| 4 | entire state byte: 00 OFF, 01 ON (not mask 0x20) |
| 5..20 | zero in captured notification |

ON: `03 76 00 00 01` + 16 zero bytes.
OFF: `03 76 00 00 00` + 16 zero bytes.
No dummy Report ID 00 prefix belongs to this input report. MI_01 command packets
remain 64-byte USB payloads with existing 65-byte Windows transport framing.

Raw Input collection: VID 0B05/PID 1B7E, MI_02 Col03, usage page **FFC0**, usage 1;
Windows cached HID descriptor confirms input length 21 and button-cap Report ID 3.
The previously proposed MI_01/0x85 is the command/echo channel, not this unsolicited
notification. Official HAL additionally has read-query `25 00` and status reply
byte 4. This change sends neither query nor a switch-control OUT command.

## Static chain

- HAL `FUN_18001fc00`: event 0x76, stripped report byte 3 (USB byte 4), switch callback.
- HAL `FUN_18008d250`, selector 0x2F: `GetRapidTriggerInfo_SwitchStatus`, query 25 00,
  returns cached RT switch status. No physical-switch control primitive established.
- Firmware 1.00.58 assembly starting **0x1800687e**: GPIO 0x47, debounce counter,
  active-bank bit 5 (mask 0x20) is updated; 0x76 notification emitted.
- Firmware `FUN_18004498`: zeroed 21-byte notification with event/parameters at 1..4.
- Firmware `FUN_18004a7e`: RT scan requires per-key enabled/mode bits and bank gate bit 5.
- No per-key `51 54` table clearing is present in the audited switch branch.

Static debounce counter is confirmed; its milliseconds cannot be established
from this capture because physical toggle instants were not externally timestamped.
Notifications are event-driven; duplicates occurred. No periodic refresh guarantee.
No onboard-bank or NVM persistence guarantee is inferred.

## Production observation boundary

`HardwareRtGateMonitor` registers only the verified vendor Raw Input collection,
checks VID/PID/interface/collection/cached report capabilities, and listens to
WM_INPUT + WM_INPUT_DEVICE_CHANGE. It uses no HID file handle, ReadFile, WriteFile,
feature report, query, M605 command or Profile mutation. Non-target reports ignored.
Recognized malformed RT notification -> Unknown. Start without notification ->
Unknown. Removal/ambiguous collection -> Unknown. Arrival -> Unknown until valid
notification. Multiple identical keyboards are never merged.

Observation includes UTC timestamp, sequence, connected collection and separate
**input_session_generation**. This describes the MI_02 input collection lifetime,
not M605 write transport generation. M605 reconnection still clears its submission
shadow using existing preflight. Gate notification does not change M605 health,
latch, selected/active/dirty, mutation revision or any per-key RT values.

Pure GET `/api/device-profiles/hardware-rt-gate` reads only the observation cache.
Profile page polls it while loaded, cancels when unloaded, updates only its status
InfoBar. It does not call runtime synchronization/transport presence preflight.
Diagnostics GET also remains pure cached read.

## Three sources

- Profile counts: selected Profile desired objects (`configured_rt_key_count`,
  `enabled_rt_key_count`), not firmware state or editing draft.
- `hardware_rt_gate`: observed USB notification, with input identity lifetime.
- `effective_rt_active`: derived gate + clean current-session submitted managed
  RT keys, nullable, not readback, not claim about unmanaged keys/external writes.

Gate OFF -> effective inactive, while Profile objects and host submission remain.
Gate ON -> effective depends on submitted key enabled state. Unknown + enabled
intent -> unknown. Unknown + known disabled intent -> inactive. WinUI does not
claim an unapplied draft is active. Save and explicit manual Apply stay independent.

Diagnostic schema 1 receives additive fields; existing export accepts them. No
new file format, schema migration, global RT interpretation or protocol write.

## Physical evidence / limits

One combined official/Aura capture has 12 switch notifications, 8 per-key RT OUT
commands, identical RT echoes, 7 Apply OUT gates and zero 51 53. Addresses were
11 then 12, confirmed by descriptors; do not filter this capture by stale addr10.
User reports official UI always synced, Aura disable normal, hardware-OFF editing
allowed, switch-ON restoration and unplug configuration retained. This is scoped
W/firmware1.00.59 acceptance, not full keyboard, continuous ON, banks or NVM proof.
The original failure was not reproduced; it remains NOT VERIFIED historically.
New passive monitor registration and mock/live-cache behavior are tested separately;
new daemon listener + physical toggle UI latency has not been measured.
