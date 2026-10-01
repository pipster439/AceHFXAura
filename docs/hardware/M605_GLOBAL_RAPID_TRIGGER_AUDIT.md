# M605 All-key / Per-key Rapid Trigger audit — alpha.7

## Correction of initial Global RT assumption

The original USB captures remain valid. The correction is to product semantics,
not packet bytes. Armoury Crate edits an enabled key set and parameters for that
set. A shared UI sensitivity is a bulk editor convenience. It is not evidence of
a firmware common/base RT value or inheritance. The historical filename of this
document and original `global_rt_*.pcap` files is retained without changing hashes.
Corrected scenario labels and scopes are in `scenario_summary.csv`.

## Scope and evidence boundary

This audit changes Profile RT validation presentation only. It adds no writer,
builder, allowlist entry, Profile operation or automatic activation. Existing
hardware/configuration source files are compared byte-for-byte with the working
tree snapshot taken at the beginning of the task.

Static firmware: **1.00.58**, readonly core2 image, SHA-256
`8fe68a13d7a0cd3bf8b4fd0dbe0575c0b7e67c1d4373e8d63970cf248684e668`.
Physical capture: **1.00.59**, VID `0B05`, PID `1B7E`, bcdDevice `0159`,
USBPcap4, captured address 10, MI_01 OUT `0D` / IN `85`.
An identical echo is submission evidence, never configuration readback.

Artifacts: `audit_artifacts/alpha7_global_rt`. Full raw captures are preserved
under `F:/USBPcap/global_rt_<scenario>.pcap`. Derived captures retain original
record order and full target-interface traffic plus target descriptors.

## Important correction: the official UI does not use an RT software master

The official `7038/index.js` RT component displays the physical switch state.
Chinese resource `row_2145` instructs users to turn RT on/off using the keyboard
switch. The separate-mode toggle means independent down/up sensitivity.
The user confirmed editing works while the physical switch is OFF.

The enabled-key dialog chooses the RT key set; it does not expose a different
sensitivity for each individual key. The frontend passes that key set to each
`/rapidTrigger` mutation. Unified sensitivity, separate sensitivity and key-set
changes in the captured scenarios use **51 54**, including the All Select UI.
An All Select button is not evidence that the SDK AllKey operation was invoked.

Scenario D All Select generated 135 distinct wire identities (68 with high byte
`00`, 67 with high byte `9F`), not 135 physical keys. One separate-mode group
contained 270 writes, and unified groups contained 135 writes. The static PreKey
handler maps high byte `9F` to its second bank. This captures official extra-bank
traffic; it does **not** authorize extending Aura's existing verified layer-0
logical mapping or adding a bank parameter. Exact identities are retained in
`D/rt_apply_groups.json`. W disable/re-enable uses vendor byte 8 values 0/1.

## Static call chain

Official source identities/hashes: `input_identities.json`.

* Frontend `7038/index.js`, component `an`: unified callback `b`, press `S`,
  release `E`, separate `P`, key selection `C`, continuous mode `_`.
  `5744-bundle-f78f7.js` thunk issues PUT `/rapidTrigger`, carrying enabled-key
  list, sensitivity, separate/continue state and switchStatus.
* `ArmouryKbSDK.dll` ExecuteFunction selector **0xD1** dispatches
  `FUN_10043ba0` (AllKey); **0xD2** dispatches `FUN_10043de0` (PreKey).
* SDK XML parser `FUN_1004c8f0` reads `source_key`, `rapid_trigger`,
  `continue_mode`, `switch`, `layer`, `type`. `DAT_101843f4` was checked by
  readonly memory access: its literal is `type`.
* AllKey wrapper calls HAL function **0x2D**; PreKey calls **0x2E**.
* `AacKbHal_x64.dll`, `FUN_18008bf70`, constructs the 64-byte vendor payload.
* Firmware dispatcher starts near `180024E4`. AllKey branch
  `18002EFA..18002FE2`; PreKey branch `18002FE4` onwards.

Full source excerpts, readonly MCP outputs, HAL assembly and Capstone firmware
assembly are retained. Ghidra did not define a function at the raw firmware
dispatcher; that failed decompilation is retained and is not cited as successful
decompilation. ARM Thumb disassembly came directly from the readonly image.

## 51 53 static packet contract (not physical capture)

| Vendor offset | Static meaning |
|---|---|
| 0..1 | `51 53` |
| 2..3 | little-endian selector/type: 0 unified, 1 press, 2 release |
| 4 | one sensitivity raw value, 0.1 mm unit |
| 5 | continuous-mode value; firmware writes `(value + 1)` to mode bits |
| 6 | RT enable for the affected key records, **not physical switch state** |
| 7 | layer/bank; static branch accepts 0 and 1 |
| 8..63 | zero in official HAL construction |

Windows host framing uses the existing dummy ReportID prefix to form 65 bytes;
that prefix is absent from the actual 64-byte USB vendor payload.
No 51 53 packet was captured in scenarios A–D. Do not replay this static layout.
No new public type/layer/raw sender is authorized by this audit.

In firmware 1.00.58, selector 0 writes both seven-bit sensitivity fields in
the RT word at key-record offset `+0xA`; selector 1 writes bits 7..13 and
selector 2 writes bits 0..6. Enable/continue affect bits 14..15. The AllKey branch
loops over key records (bank stride `0xD84`, record stride `0x20`). It is not
shown as an Actuation-style common value plus a separately cleared inheritance
flag. A disabled write clears the two high mode bits but still carries sensitivity.
These are static observations, not 1.00.59 physical behavior conclusions.

## 51 54 official captured contract

| Vendor offset | Meaning supported by HAL + firmware + capture |
|---|---|
| 0..1 | `51 54` |
| 2..3 | selector 0 unified, 1 press/down, 2 release/up |
| 4..5 | little-endian wire key; bank encoded through the firmware key mapping |
| 6 | sensitivity raw |
| 7 | continuous mode |
| 8 | enabled key flag |
| 9..63 | zero in captured writes |

Scenario B cross-checks both `press=0.5, release=1.5` and reversed values.
For W (wire `0012`), frames 265899/265905 carry selector 1/raw 5 and selector
2/raw 15; frames 288305/288309 carry selector 1/raw 15 and selector 2/raw 5.
Unified writes use selector 0. Each official group is followed by `50 55` and
identical IN payloads are retained with timing. This proves USB fields, not
independent physical sensitivity perception (not yet explicitly reported).

## Inheritance and OFF cautions

The current official UI stores an enabled-key set and fans the current common
UI sensitivities over it. Removing a key sends 51 54 with enable zero; readding
it sends the current values with enable one. This is not proof of a firmware
per-key sensitivity inheritance table or a pure inheritance-reset primitive.

Scenario C physical OFF captured only status/query commands, with no 51 53,
51 54, reset or Apply. The user confirmed sensitivity values remain visible.
Do not translate the physical switch action into a fabricated software master
write. Physical OFF and clearing a key's RT enable flag are different actions.

The current `BuildGlobalRapidTrigger(press, release, top, bottom, separate)`
already existed before this task. Its historical field interpretation does not
agree with the static official HAL/firmware layout above: vendor byte 5 is not
a second sensitivity and bytes 6/7 are not RT Deadzone fields in that handler.
**Do not expand use of that primitive.** It remains unchanged in this audit;
existing byte tests prove construction, not this newly investigated semantics.
Any next production proposal must explicitly resolve this mismatch.

## DKS and Deadzone

The RT handler writes the RT word and notification state; its shown branches
do not write DKS payload records or Deadzone byte fields. That is static
preservation evidence only, not a complete physical preservation test.
The official frontend excludes nonstandard trigger types from RT selection.
The page-level overall Reset uses resetType 3; it must not be mistaken for a
dedicated per-key RT inheritance reset or used in these captures.

Existing Aura safety remains unchanged: enabling per-key RT with active/unknown
DKS requires explicit resolution and a Standard DKS transaction first; enabling
DKS with active/unknown RT disables per-key RT first using trusted HostProfile
press/release values. A failure of the second operation remains partial state.
Disabling RT alone does not imply a new global Standard rewrite requirement.
Global Profile RT remains blocked; Profile-wide top/bottom/separate/master
semantics have not been productionized.

## Remaining gates

1. Find an actual official user operation that invokes SDK 0xD1 / HAL 0x2D;
   current visible global sensitivity + All Select actions captured 51 54 only.
2. Capture 51 53 and its complete Apply/echo/companion sequence on 1.00.59.
3. Verify its interaction with previously different per-key RT values and
   identify a real inheritance operation, if firmware offers that concept.
4. User confirmed M is disabled in the RT selector after assigning DKS and
   restored it. E contains explicit DKS setup/restoration traffic and no RT write.
   F contains four 51 54 writes, one Apply, and no Deadzone/reset command; user
   reported unchanged Deadzone UI values. Independent physical Deadzone and
   asymmetric sensitivity perception remain unreported / NOT VERIFIED.
5. Keep physical-switch master separate from document intent and key-enable
   flags. No automatic Device Profile hardware activation is enabled.

All six captures are complete. Physical conclusions are limited to the user's
reported observations; zero captured 51 53 packets cannot be called a 51 53 PASS.
