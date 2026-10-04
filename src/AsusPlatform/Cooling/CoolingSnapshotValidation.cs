namespace AceHFX.AsusPlatform.Cooling;

public static class CoolingSnapshotValidation
{
    public static bool IsValid(CoolingRuntimeSnapshot snapshot)
    {
        if (snapshot.SchemaVersion != 1 || snapshot.Capabilities == null || snapshot.Channels == null || snapshot.Sensors == null ||
            snapshot.WorkerIdentity is not { IsSystem: true, SessionId: 0, Sid: "S-1-5-18" } ||
            !Enum.IsDefined(snapshot.Error) || snapshot.Channels.Count > FanReadProtocol.MaximumChannels || snapshot.Sensors.Count > 64 ||
            snapshot.Capabilities.WriteContractValidated != FanObservationState.NotAttempted) return false;
        // This ABI does not yet supply verified per-sensor telemetry. Reject invented sensor counts.
        if (snapshot.Sensors.Count != 0 || snapshot.Capabilities.RpmSensorCount != null || snapshot.Capabilities.TemperatureSourceCount != null) return false;
        if (snapshot.Capabilities.Enumeration == FanObservationState.Succeeded &&
            snapshot.Capabilities.ControlChannelCount != snapshot.Channels.Count) return false;
        if (snapshot.Capabilities.Enumeration != FanObservationState.Succeeded && snapshot.Capabilities.ControlChannelCount != null) return false;
        if (snapshot.Channels.Any(x => x == null) ||
            snapshot.Channels.Select(x => x.StableId).Distinct(StringComparer.Ordinal).Count() != snapshot.Channels.Count) return false;
        foreach (var fan in snapshot.Channels)
        {
            if (fan.Name == null || fan.VendorId == null || fan.CurrentRpm == null || fan.DutyRaw == null ||
                fan.RpmMode == null || fan.FanStopState == null || fan.SourceIndex == null || fan.CurrentProfile == null || fan.MinimumDutyRaw == null ||
                fan.StableId == null || fan.StableId.Length > 128 || fan.Name.Value?.Length > 128 || fan.CollectionIndex is < 0 or >= FanReadProtocol.MaximumChannels ||
                !ValueValid(fan.Name.State, fan.Name.Value) || !ValueValid(fan.VendorId.State, fan.VendorId.Value) ||
                !ValueValid(fan.CurrentRpm.State, fan.CurrentRpm.Value) || !ValueValid(fan.DutyRaw.State, fan.DutyRaw.Value) ||
                !ValueValid(fan.RpmMode.State, fan.RpmMode.Value) || !ValueValid(fan.FanStopState.State, fan.FanStopState.Value) ||
                !ValueValid(fan.SourceIndex.State, fan.SourceIndex.Value) || !ValueValid(fan.CurrentProfile.State, fan.CurrentProfile.Value) ||
                !ValueValid(fan.MinimumDutyRaw.State, fan.MinimumDutyRaw.Value) ||
                fan.ManualMode == null || fan.NameId == null ||
                !ValueValid(fan.ManualMode.State, fan.ManualMode.Value) || !ValueValid(fan.NameId.State, fan.NameId.Value) ||
                !Enum.IsDefined(fan.SafetyClass) || !Enum.IsDefined(fan.Confidence)) return false;
            if (fan.CurrentCurve is { } curve)
            {
                if (curve.PointCount == null || curve.Points == null || !ValueValid(curve.PointCount.State, curve.PointCount.Value) ||
                    curve.PointCount.Value is < 0 or > FanReadProtocol.MaximumCurvePoints ||
                    curve.Points.Count > FanReadProtocol.MaximumCurvePoints ||
                    curve.PointCount.State == FanObservationState.Succeeded && curve.PointCount.Value != curve.Points.Count ||
                    curve.PointCount.State != FanObservationState.Succeeded && curve.Points.Count != 0) return false;
                if (curve.Points.Any(x => x == null || x.Index < 0 || x.Index >= FanReadProtocol.MaximumCurvePoints || x.Temperature == null || x.Speed == null ||
                    !ValueValid(x.Temperature.State, x.Temperature.Value) || !ValueValid(x.Speed.State, x.Speed.Value))) return false;
                if (curve.Points.Select(x => x.Index).Distinct().Count() != curve.Points.Count) return false;
            }
        }
        return new[] { snapshot.Registered, snapshot.ActivationStarted, snapshot.Activation, snapshot.InterfaceAcquired,
            snapshot.InvocationStarted, snapshot.Invocation, snapshot.Capabilities.WorkerReachable, snapshot.Capabilities.ComActivated,
            snapshot.Capabilities.Enumeration, snapshot.Capabilities.CurveReadSupported }.All(x => Enum.IsDefined(x));
    }
    private static bool ValueValid<T>(FanObservationState state, T value) => Enum.IsDefined(state) &&
        (state == FanObservationState.Succeeded ? value != null : value == null);
}
