# FanControl current control surface — M3.0

The current installed TypeLib was extracted with ITypeLib/ITypeInfo and REGKIND_NONE. No vendor COM object was activated by extraction. Raw types, inheritance, ordered parameters, PARAMFLAGS, pointer/SAFEARRAY types and VARIANT_BOOL are in `audit_artifacts/phase3-m3.0/fancontrol-typelib.json` and the software fixture. Library {DF5522FB-119C-428D-A62E-8BFE4A2FBC9B}, version 1.0.

Classification combines current INVOKEKIND with the Phase2 getter audit and narrowly inspected getter/collection wrappers. PROPERTYGET alone is insufficient approval for untraced members. READ_ONLY_CONFIRMED denotes the reduced metadata/getter surface, **not an executed host getter or proof of hardware state**. Lazy initialization and service-cache behavior remain distinct from a physical readback. PROPERTYPUT/PUTREF is prohibited. Untraced functions/getters stay UNKNOWN and are absent from the compiled worker.

The worker uses sparse IDispatch getter-only interfaces, not an invented sparse dual-interface vtable. A startup contract check matches current lib GUID, interface IIDs, DISPIDs, INVOKEKIND, return/parameter VARTYPE and returned interface IIDs. Unknown component versions do not automatically fail; an actual ABI mismatch does.

Current IFanControl has no RPM getter and no ManualMode getter. IsRpmMode is independent of manual mode. EcFanStop getter is a readable flag, not permission to stop a fan or a capability assertion. Manager GetAic2Rpm has no channel argument. AI CPU temperature can return S_FALSE with no output; it is excluded rather than converted to a fabricated zero by an RCW. Old hardware-monitor Markdown has the wrong IacpiHmData IID and simplified method list; none was copied into product code.

Static attribution uses the corrected manager vtable at VA 0x59bf1c (this offset 16); the earlier offset-4 connection-point table is not the IFanControlManager implementation. Only corrected offsets are curated. Full proprietary decompilation stays local and is excluded from the delivery archive.

| Member | DISPID | INVOKEKIND | Classification | Runtime policy |
|---|---:|---:|---|---|
| IFanControlCollection._NewEnum | -4 | 2 | UNKNOWN | NotAttempted |
| IFanControlCollection.Count | 2 | 2 | READ_ONLY_CONFIRMED | getter allowlist |
| IFanControlCollection.Item | 0 | 2 | READ_ONLY_CONFIRMED | getter allowlist |
| IFanControl.Id | 1 | 2 | READ_ONLY_CONFIRMED | getter allowlist |
| IFanControl.Name | 2 | 2 | READ_ONLY_CONFIRMED | getter allowlist |
| IFanControl.DutyCycle | 3 | 2 | READ_ONLY_CONFIRMED | getter allowlist |
| IFanControl.DutyCycle | 3 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.Profiles | 4 | 2 | READ_ONLY_CONFIRMED | getter allowlist |
| IFanControl.ApplyFanCurve | 5 | 1 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.NormalizeFanCurve | 6 | 1 | LIKELY_STATE_CHANGING | NotAttempted |
| IFanControl.StartupProfileIndex | 7 | 2 | UNKNOWN | NotAttempted |
| IFanControl.StartupProfileIndex | 7 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.ApplyIndex | 8 | 1 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.CalculatedDuty | 9 | 2 | UNKNOWN | NotAttempted |
| IFanControl.MinimalDuty | 10 | 2 | READ_ONLY_CONFIRMED | getter allowlist |
| IFanControl.EcMode | 11 | 2 | UNKNOWN | NotAttempted |
| IFanControl.EcMode | 11 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.ThermalWeights | 12 | 2 | UNKNOWN | NotAttempted |
| IFanControl.ThermalWeights | 12 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.CurrentFanCurve | 13 | 2 | READ_ONLY_CONFIRMED | getter allowlist |
| IFanControl.DisplayName | 14 | 2 | UNKNOWN | NotAttempted |
| IFanControl.DisplayName | 14 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.StepUpTime | 15 | 2 | UNKNOWN | NotAttempted |
| IFanControl.StepUpTime | 15 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.StepDownTime | 16 | 2 | UNKNOWN | NotAttempted |
| IFanControl.StepDownTime | 16 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.TempTolerance | 17 | 2 | UNKNOWN | NotAttempted |
| IFanControl.TempTolerance | 17 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.EnableRpmMode | 18 | 1 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.IsRpmMode | 19 | 2 | READ_ONLY_CONFIRMED | getter allowlist |
| IFanControl.RpmTolerance | 20 | 2 | UNKNOWN | NotAttempted |
| IFanControl.RpmTolerance | 20 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.EcFanStop | 21 | 2 | READ_ONLY_CONFIRMED | getter allowlist |
| IFanControl.EcFanStop | 21 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.StepUnit | 22 | 2 | UNKNOWN | NotAttempted |
| IFanControl.StepUnit | 22 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.T1DelayTime | 23 | 2 | UNKNOWN | NotAttempted |
| IFanControl.T1DelayTime | 23 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.StepUnitSupported | 24 | 2 | UNKNOWN | NotAttempted |
| IFanControl.WeightsSupported | 25 | 2 | UNKNOWN | NotAttempted |
| IFanControl.StepUnitButNotSave | 26 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.StepUpTimeButNotSave | 27 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.StepDownTimeButNotSave | 28 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.T1DelayTimeButNotSave | 29 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.RpmToleranceButNotSave | 31 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.EnableRpmModeButNotSave | 32 | 1 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.ApplyFanCurveButNotSave | 33 | 1 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.IsSencitiveSupported | 34 | 2 | UNKNOWN | NotAttempted |
| IFanControl.SetFanDuty | 35 | 1 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.RefreshFanCurve | 36 | 1 | UNKNOWN | NotAttempted |
| IFanControl.EnableManualMode | 37 | 1 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.FanSourceIndex | 38 | 2 | READ_ONLY_CONFIRMED | getter allowlist |
| IFanControl.FanSourceIndex | 38 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControl.MultiFanSource | 39 | 2 | UNKNOWN | NotAttempted |
| IFanControl.SaveRpmTargetDuty | 40 | 1 | LIKELY_STATE_CHANGING | NotAttempted |
| IFanControl.AIFanECRegister | 41 | 2 | UNKNOWN | NotAttempted |
| IFanControl.ECBank | 42 | 2 | UNKNOWN | NotAttempted |
| IFanControl.IsEQModeSupported | 43 | 2 | UNKNOWN | NotAttempted |
| IFanControl.Bank | 44 | 2 | UNKNOWN | NotAttempted |
| IFanProfileCollection._NewEnum | -4 | 2 | UNKNOWN | NotAttempted |
| IFanProfileCollection.Count | 2 | 2 | UNKNOWN | NotAttempted |
| IFanProfileCollection.Item | 0 | 2 | UNKNOWN | NotAttempted |
| IFanProfileCollection.Current | 4 | 2 | READ_ONLY_CONFIRMED | getter allowlist |
| IFanProfileCollection.SaveUserProfile | 5 | 1 | LIKELY_STATE_CHANGING | NotAttempted |
| IFanProfile.Name | 1 | 2 | UNKNOWN | NotAttempted |
| IFanProfile.Type | 2 | 2 | UNKNOWN | NotAttempted |
| IFanProfile.Id | 3 | 2 | UNKNOWN | NotAttempted |
| IFanProfile.FanCurve | 4 | 2 | UNKNOWN | NotAttempted |
| IFanCurve._NewEnum | -4 | 2 | UNKNOWN | NotAttempted |
| IFanCurve.Count | 2 | 2 | READ_ONLY_CONFIRMED | getter allowlist |
| IFanCurve.Item | 0 | 2 | READ_ONLY_CONFIRMED | getter allowlist |
| IFanCurve.AddPoint | 3 | 1 | LIKELY_STATE_CHANGING | NotAttempted |
| IFanCurvePoint.Temperature | 1 | 2 | READ_ONLY_CONFIRMED | getter allowlist |
| IFanCurvePoint.Temperature | 1 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanCurvePoint.Speed | 2 | 2 | READ_ONLY_CONFIRMED | getter allowlist |
| IFanCurvePoint.Speed | 2 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IThermalWeights._NewEnum | -4 | 2 | UNKNOWN | NotAttempted |
| IThermalWeights.Item | 0 | 2 | UNKNOWN | NotAttempted |
| IThermalWeights.Item | 0 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IThermalWeights.Count | 1 | 2 | UNKNOWN | NotAttempted |
| IThermalWeights.Save | 2 | 1 | LIKELY_STATE_CHANGING | NotAttempted |
| IThermalWeights.ApplyButNotSave | 3 | 1 | LIKELY_STATE_CHANGING | NotAttempted |
| IMultiFanSource._NewEnum | -4 | 2 | UNKNOWN | NotAttempted |
| IMultiFanSource.Item | 0 | 2 | UNKNOWN | NotAttempted |
| IMultiFanSource.Item | 0 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IMultiFanSource.Count | 1 | 2 | UNKNOWN | NotAttempted |
| IMultiFanSource.Save | 2 | 1 | LIKELY_STATE_CHANGING | NotAttempted |
| IMultiFanSource.ApplyButNotSave | 3 | 1 | LIKELY_STATE_CHANGING | NotAttempted |
| IMultiFanSource.Clear | 4 | 1 | LIKELY_STATE_CHANGING | NotAttempted |
| IMultiFanSource.Add | 5 | 1 | LIKELY_STATE_CHANGING | NotAttempted |
| IDualModeFanCurve._NewEnum | -4 | 2 | UNKNOWN | NotAttempted |
| IDualModeFanCurve.Count | 2 | 2 | UNKNOWN | NotAttempted |
| IDualModeFanCurve.Item | 0 | 2 | UNKNOWN | NotAttempted |
| IDualModeFanCurve.AddPoint | 3 | 1 | LIKELY_STATE_CHANGING | NotAttempted |
| IDualModeFanCurve.FanCurve | 4 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.Controls | 2 | 2 | READ_ONLY_CONFIRMED | getter allowlist |
| IFanControlManager.ConstraintType | 3 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.MaxStepTime | 4 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.MaxTolerance | 5 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.IsSIOSupportedRPM | 6 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.RpmPerstep | 7 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.RpmSmoothFreq | 8 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.OneSencitiveSupported | 9 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.IsSupportFanStop | 10 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.FanServiceVersion | 11 | 2 | READ_ONLY_CONFIRMED | getter allowlist |
| IFanControlManager.AIFanCpuTemperature | 12 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.AIFanCpuTempIn | 13 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.AIFanMinDuty | 14 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControlManager.FanCount | 15 | 2 | READ_ONLY_CONFIRMED | getter allowlist |
| IFanControlManager.AIFanEntryTemp | 16 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.SetCriticalPoint | 17 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControlManager.IsCPUZSupport | 18 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.IsCPUZ_GPU | 19 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.IsCPUZ_DDR | 20 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.IsCPUZ_M2 | 21 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.getGPUMaxTemp | 22 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.getDDRMaxTemp | 23 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.getM2MaxTemp | 24 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.IsCPUZInitialized | 25 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.GetAic2Temp | 26 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.SetAic2FanDuty | 27 | 4 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControlManager.SetAiSuiteRequestDDR5 | 28 | 1 | STATE_CHANGING_CONFIRMED | NotAttempted |
| IFanControlManager.GetAic2Rpm | 29 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.IsMBIFSupportAC | 30 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.IsSupport_10_point | 31 | 2 | READ_ONLY_CONFIRMED | getter allowlist |
| IFanControlManager.IsRYUJIN3plugin | 32 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.Additional_Func | 33 | 2 | UNKNOWN | NotAttempted |
| IFanControlManager.getNumberOfCurvePoint | 34 | 2 | READ_ONLY_CONFIRMED | getter allowlist |

For each member the machine inventory also retains raw parameters/types, evidence, RVA where inspected, runtimeAllowed, execution=NotAttempted and open questions. Relevant inherited IUnknown/IDispatch methods are recorded in the raw TypeLib, excluded from this vendor-member table. No getter was exercised under SYSTEM in this milestone.
