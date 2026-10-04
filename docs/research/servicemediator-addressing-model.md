# ServiceMediator addressing reconstruction — M2.9

Observed on 2026-10-02. **No control/ownership/lighting call was executed.** Current TypeLib was
re-extracted through ITypeLib/ITypeInfo from installed LightingService.exe 3.10.12.0,
SHA256 `a711b9776ac0279256d22389478d44e3a51a1909c75a3bf03158cf9d5b91d3db`.
Machine evidence: `audit_artifacts/phase3-m2.9/addressing-model.json` and `servicemediator-typelib.json`.

## Four addressing namespaces

1. **Runtime logical record**: QueryAllDevice returns type/lightingname/model/manufacture/count/
   width/height/index. It returns no serial, USB interface, PCI identity, unique write handle or key
   attribute. Record position and index distinguish evidence records, not a control API address.
2. **Profile XML**: status/configuration uses `device key="Group"`, `Mainboard`, `HUE`, nested scene/
   mode/LED keys, and `DeviceName` family strings. These are sections and profile entries. They must
   not be substituted for one physical controller without tracing the corresponding parser.
3. **Matrix software-mode selection**: DISPID 102 accepts `typename`, `modelname`, signed status;
   there is no index argument. The current service converts the type name and selects internal type/
   model maps. The ASUS client forwards stored device-record strings; one confirmed path explicitly
   uses `MATRIX_Laptop`, not the motherboard's two metadata lights.
4. **Matrix application label**: DISPID 68 accepts PTR(SAFEARRAY(UI4)) and APname. APname is compared
   exactly against HKLM32 `SOFTWARE\ASUS\LedMatrixConfig\AP`, after checking a service-global enable
   flag. It is a label for the matrix path, not deviceId, zone index, authenticated SID or PID.

The ASUS AuraPlugin native wrapper at RVA `0x24A5E0` passes two BSTRs through IServiceMediator slot7
(offset `0x38`, DISPID1). Callers `0x13A550`/`0x13A6B0` construct **deviceId="1"** and a complete XML
profile. A captured static example is `<device key="Mainboard"><resetall/></device>` with funcid8.
This establishes an actual caller convention, **not authorization or a safe temporary reset**.
The first BSTR does not identify any one of the nine records in these callers. XML selection is
essential; its complete isolation/persistence semantics are not established.

## Nine-record mapping

Associations below use exact `lightingname`/`DeviceName` strings and status/configuration keys.
They are **STRONG_INFERENCE for the association**, not validated per-device write routing.
`connecteddevice` codes are profile-family metadata; they are not automatically Aura devType values.

| Runtime record | Exact family / connected code | Profile section references | Potential API namespace | Unique write address |
|---|---|---|---|---|
| TUF GAMING X870-PLUS WIFI, Mainboard_Master, index0 | Mainboard_Master / 0 | Group LEDs0–1; Mainboard LEDs0–1 | profile XML; type/model selector | UNKNOWN; physical zones unvalidated |
| Vga, VGA0, index0 (historical Vga 1) | Vga / 2 | Group LEDs2–24 | profile XML; type/model selector | UNKNOWN; no PCI/serial identity or reviewed per-device transaction |
| ROG STRIX LC III SERIES, index0 | WaterCooler / 18 | Group LED44 | profile XML; type/model selector | UNKNOWN; compressed profile record differs from four runtime slots |
| EneTech/GSkillDram, index0 | GSkillDram / 5 | Group LEDs25–40 shared family | profile XML; type/model selector | UNKNOWN; no independent DIMM key |
| EneTech/GSkillDram, index1 | GSkillDram / 5 | same shared family | same | UNKNOWN; index not an argument to102 |
| ARGB HEADER index0 | AddressableStrip / 8 | Group LEDs41–43 shared family | profile XML; type/model selector | UNKNOWN; no confirmed header-to-LED-key map/attached length |
| ARGB HEADER index1 | AddressableStrip / 8 | same | same | UNKNOWN |
| ARGB HEADER index2 | AddressableStrip / 8 | same | same | UNKNOWN |
| WDL_Keyboard/Falchion HFX index0 | WDL_Keyboard / 47 | connected family; empty capability section, no matching profile LED | separate WDL/interface namespace | UNKNOWN; no shared interface/container ID in mediator |

No inference assigns ARGB index0→profile LED41 or DIMM index0→LED25–32. Similar model names are not
joined to physical devices. The historical M2.6 mapping confidence/141-slot release analysis remains
unchanged. Falchion continues to use existing M605 direct HID product control; this research never
opened its HID or LampArray control object.

## Counts and payload contradictions

| Representation | Actual evidence | Meaning / limit |
|---|---|---|
| QueryAllDevice | nine records; sum519 | declared logical record slots; ARGB120 each; not physical population |
| Group capability | ledcount1122;45 explicit LED records | capability serialization reads an internal aggregate field at controller+0x3C; unit/contributions not fully reconstructed |
| Group explicit records | 2 motherboard +23 VGA +16 DRAM +3 ARGB zones +1 cooler =45 | compressed zone and ordinary LED entries coexist |
| ARGB capability zone | max_total_led_count500/defaultlednum500 each | different capability/default capacities from runtime120; neither proves attached length |
| Cooler | four runtime slots; one Group capability entry | aggregate profile zone versus logical geometry |
| Mainboard capability | count2; Back Plate-1/2 | labels only; physical outputs and isolation unproven |
| Status XML | Group/Mainboard/HUE, scenes/modes, repeated LED keys | profile settings; not sampled live pixel buffers |

Current static path: QueryAllDeviceCap (`0x2C27C0`) initializes controller aggregates and invokes
controller capability serializers; Group serialization (`0x1F6460`) emits its aggregate count, while
per-controller serializers emit LED/zone nodes separately. **1122 is not established as a flat frame
length.** A numerical decomposition of1122 is deliberately not asserted without proving its units.
The current LastScript uses separate viewports and device type/model/index geometry. XML profile LED
keys, script viewport coordinates and SetLedMatrix array offsets are not interchangeable.

## SetLedMatrix receiver: confirmed static limits

Native wrapper RVA `0x2D52B0` → functor `0x2DF7C0` → AP/enable validation `0x2D7450` → distribution
`0x2CF6D0`. TypeLib establishes **PTR(SAFEARRAY(UI4))**, not bytes or an array of device descriptors.
The inspected AuraPlugin has control/script wrappers, but no verified SetLedMatrix payload producer
was found in that bounded scope. This is not proof that ASUS has no such caller elsewhere.

- Distribution looks for type `0xF8900` geometry; absent geometry logs and returns.
- A type `0x13000` branch traverses28×68 input positions with a special triangular remapping table;
  it is not a generic nine-device frame loop.
- The `0xF8900` branch uses a bound resembling `min(width*height, ubound-lbound)`, not necessarily
  elementCount. Vendor code queries array bounds but reads elements starting at index0. There are
  unchecked device-specific buffer accesses; arbitrary sizes/lower bounds are unsafe.
- UI4 values pass to a controller operation at vtable+0x68, followed by controller flushing. Exact
  RGB byte order, alpha/brightness semantics and a safe array length for any current device remain
  **UNKNOWN**. No conversion formula is invented from the UI4 type.
- No per-zone subset contract was established. The nine observed records do not identify one
  eligible matrix receiver through this API.

The offline validator rejects incomplete semantics even when element type/count look plausible.
It creates no write payload and performs no COM calls.

## First target decision

**No target selected.** Two motherboard entries are small but not physically prevalidated and lack
a demonstrated transient restore contract. DRAM/header families are ambiguous, ARGB attached length
is unknown, GPU scope is larger, and cooler lighting must remain separated from cooling operations.
Keyboard control remains authoritative through the existing HID implementation. A whole Group or
all-device profile would affect unrelated motherboard/GPU/DRAM/cooler/ARGB lighting and is rejected.
