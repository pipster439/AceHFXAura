# Fan topology — M3.0

Current passive FanInfo.data parsing yields **7 configured controls, 8 tach sensors, 6 temperature sensors**. These are configuration counts, not a successful live enumeration. The parser used the verified Fan6.proto descriptor; it did not execute old generated probe logic or read EC/SIO registers. Config paths/hashes are in input-provenance.json.

| Config index | Name | Name ID | SIO/EC bank metadata | RPM support | Stop support | Runtime COM | FanStore key/current curve | Confidence |
|---:|---|---|---|---|---|---|---|---|
| 0 | CPU Fan | CPUFAN | 2/0 | True | False | NotAttempted | Unknown | ConfigExact |
| 1 | Chassis Fan 1 | ChASSISFAN3 | 1/1 | True | True | NotAttempted | Unknown | ConfigExact |
| 2 | Chassis Fan 2 | AIOPUMPFAN | 3/2 | True | True | NotAttempted | Unknown | ConfigExact |
| 3 | Chassis Fan 3 | HAMPFAN | 8/3 | True | True | NotAttempted | Unknown | ConfigExact |
| 4 | Chassis Fan 4 | CPUOPTFAN | 9/4 | True | True | NotAttempted | Unknown | ConfigExact |
| 5 | Water Pump+ | W_PUMP_1 | 10/5 | True | False | NotAttempted | Unknown | ConfigExact |
| 6 | AIO Pump | W_PUMP_2 | 11/6 | True | False | NotAttempted | Unknown | ConfigExact |

FanStore has keys 0,2,4,6,8,10,12. Neither ordinal arithmetic nor similar names establish their runtime Id mapping. All rows remain StoredOnly/Unknown join. The full comparison, register metadata from config, tach IDs and temperature types are in fan-topology-map.json; no register was read.

CPU Optional Fan is tach Sensor_Id=1, NameID=ChASSISFAN1, SIOBank=255, ECBank=0, registers 176/177, ControlSupport=false. It has no matching FanCtrls entry. The proto comment describes ControlSupport as RPM-mode support. Model this config entry as NonControllableSensor; **physical mirroring/slave PWM coupling to CPU_FAN remains UNKNOWN**. Chassis Fan4 has a different misleading NameID CPUOPTFAN; it must not be merged with the optional tach. UI must not invent an independent CPU_OPT curve.

| Config source ID | Config name | Raw type | Config HighLimit | Live temperature |
|---:|---|---:|---:|---|
| 0 | CPU | 0 | 85 | NotAttempted |
| 1 | CPU Package | 0 | 150 | NotAttempted |
| 2 | Motherboard | 0 | 60 | NotAttempted |
| 3 | Chipset | 4 | 100 | NotAttempted |
| 4 | VRM | 1 | 110 | NotAttempted |
| 5 | T Sensor | 0 | 100 | NotAttempted |

HighLimit is not a validated thermal abort threshold. IDs/source selection remain raw; FanStore source_0=100859922 is not a proven simple temperature ID. No general source enumerator has been validated. Old asCom sensor metadata needs current ABI and getter validation before use.

FanStore main curves each contain eight points, plus eight extra entries including -1 speed sentinels. The protobuf representation includes ninth/tenth fields with zero defaults; this is not proof of a 10-point hardware LUT. Native getNumberOfCurvePoint and IsSupport_10_point are prepared read getters, not yet observed. No resampling or unit conversion is implemented. CurrentFanCurve clones service cache and can fall back to a selected profile; it is not established as live register readback. Stored manualmode/rpmmode/255 speeds are stored state only.
