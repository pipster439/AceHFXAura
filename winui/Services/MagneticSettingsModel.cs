namespace Aura_WinUI.Services;

public sealed class MagneticKeyDraft
{
    public double? ActuationMm { get; set; }
    public bool? RapidTriggerEnabled { get; set; }
    public double? PressMm { get; set; }
    public double? ReleaseMm { get; set; }
    public double? TopMm { get; set; }
    public double? BottomMm { get; set; }
    public double? DksStartMm { get; set; }
    public double? DksEndMm { get; set; }
    public List<MagneticDksSlot> DksSlots { get; } = Enumerable.Range(0, 4).Select(_ => new MagneticDksSlot()).ToList();
    public bool DksDirty { get; set; }
}

public sealed class MagneticGlobalDraft
{
    public double? ActuationMm { get; set; }
    public double? PressMm { get; set; }
    public double? ReleaseMm { get; set; }
    public bool? SeparateMode { get; set; }
    public double? TopMm { get; set; }
    public double? BottomMm { get; set; }
}

public sealed class MagneticSettingsModel
{
    private readonly IMagneticControlClient _client;
    private readonly Func<ushort, (double PressMm, double ReleaseMm)?>? _inheritedRapidTrigger;
    private readonly Dictionary<ushort, MagneticKeyDraft> _drafts = [];
    private static readonly HashSet<string> TriggerStates = ["Inactive", "Tap", "Release", "Hold"];

    public MagneticSettingsModel(IMagneticControlClient client,
        Func<ushort, (double PressMm, double ReleaseMm)?>? inheritedRapidTrigger = null)
    {
        _client = client;
        _inheritedRapidTrigger = inheritedRapidTrigger;
    }

    public ushort? SelectedLogicalId { get; private set; }
    public bool IsGlobalMode { get; private set; }
    public MagneticVisualKey? SelectedKey => SelectedLogicalId is ushort id ? MagneticKeyLayout.Find(id) : null;
    public MagneticKeyDraft? Draft => SelectedLogicalId is ushort id && _drafts.TryGetValue(id, out var draft) ? draft : null;
    public MagneticGlobalDraft GlobalDraft { get; } = new();
    public MagneticStatus? Status { get; private set; }
    public bool Busy { get; private set; }
    public bool Refreshing { get; private set; }
    public string LastMessage { get; private set; } = "尚未读取配置来源；请选择按键并设置本地草稿。";
    public bool Quarantined => Status?.PersistentSafetyQuarantine == true ||
        Status?.Health is "IndeterminateStagedState" or "PersistentSafetyQuarantine" or "Stopped";
    public bool CanWrite => !Busy && !Refreshing && Status is {
        ApiVersion: 1, Available: true, Health: "Clean", PersistentSafetyQuarantine: false };
    public bool CanWriteSelected => CanWrite && !IsGlobalMode && SelectedKey != null;
    public bool CanWriteGlobal => CanWrite && IsGlobalMode;
    public bool CanDisableRapidTrigger => SelectedLogicalId is ushort id && InheritedRt(id) is not null;
    public string RapidTriggerMasterText =>
        Status?.RapidTriggerMaster is { Known: true } master ?
            (master.Value ? "硬件总开关：开启" : "硬件总开关：关闭") :
            "硬件总开关：未知";
    public const string RapidTriggerMasterExplanation = "快速触发总开关由键盘物理开关控制。";
    public bool CanResetAllDeadzone => CanWrite && Status?.HostProfile is { GlobalDeadzoneTop.Known: true,
        GlobalDeadzoneBottom.Known: true };
    public bool DksConflictPossible
    {
        get
        {
            if (SelectedLogicalId is not ushort id) return false;
            if (Status is null) return true;
            var applied = Status.RapidTrigger.FirstOrDefault(v => v.LogicalId == id && v.Source == "SessionApplied");
            if (applied != null) return applied.Enabled;
            if (Status.HostProfile.PerKeyRtListKnown)
                return Status.RapidTrigger.Any(v => v.LogicalId == id && v.Source == "HostProfile" && v.Enabled);
            return true;
        }
    }
    public bool RtConflictPossible => SelectedLogicalId is ushort id &&
        Status?.Dks.FirstOrDefault(v => v.LogicalId == id)?.StandardRuntimeConfiguration != true;

    public ushort? SpeedTapKey1 { get; private set; }
    public ushort? SpeedTapKey2 { get; private set; }
    public bool CanEnableSpeedTapPair => CanWrite && SpeedTapKey1 is ushort a && SpeedTapKey2 is ushort b &&
        a != b && Status?.SpeedTap.SavedProfilePairsKnown == true;
    public bool CanDisableSpeedTapPair => CanWrite && SpeedTapKey1 is ushort a && SpeedTapKey2 is ushort b && a != b;
    public bool? SpeedTapMasterDraft { get; private set; }
    public bool? StaticAnalogDraft { get; private set; }

    public bool Select(ushort logicalId)
    {
        if (Busy || MagneticKeyLayout.Find(logicalId) == null) return false;
        SelectedLogicalId = logicalId;
        IsGlobalMode = false;
        _ = GetDraft(); // clone known session values into local editable state only
        return true;
    }

    public bool SelectGlobal()
    {
        if (Busy) return false;
        SelectedLogicalId = null;
        IsGlobalMode = true;
        return true;
    }

    public void EditGlobalActuation(double value)
    {
        if (!Busy && IsGlobalMode && Valid(value, 0.1, 4.0)) GlobalDraft.ActuationMm = Math.Round(value, 1);
    }

    public void EditGlobalRapidTrigger(double? pressMm, double? releaseMm, bool? separateMode)
    {
        if (Busy || !IsGlobalMode) return;
        if (pressMm is double press && Valid(press, 0.1, 2.5)) GlobalDraft.PressMm = Math.Round(press, 1);
        if (releaseMm is double release && Valid(release, 0.1, 2.5)) GlobalDraft.ReleaseMm = Math.Round(release, 1);
        if (separateMode is bool separate) GlobalDraft.SeparateMode = separate;
    }

    public void EditGlobalDeadzone(double? topMm, double? bottomMm)
    {
        if (Busy || !IsGlobalMode) return;
        if (topMm is double top && Valid(top, 0, 0.5)) GlobalDraft.TopMm = Math.Round(top, 1);
        if (bottomMm is double bottom && Valid(bottom, 0, 0.5)) GlobalDraft.BottomMm = Math.Round(bottom, 1);
    }

    public void EditActuation(double value)
    {
        if (!Busy && SelectedKey != null && Valid(value, 0.1, 4.0)) GetDraft().ActuationMm = Math.Round(value, 1);
    }

    public void EditRapidTrigger(bool enabled, double? pressMm, double? releaseMm)
    {
        if (Busy || SelectedKey == null) return;
        var draft = GetDraft();
        draft.RapidTriggerEnabled = enabled;
        if (pressMm is double press && Valid(press, 0.1, 2.5)) draft.PressMm = Math.Round(press, 1);
        if (releaseMm is double release && Valid(release, 0.1, 2.5)) draft.ReleaseMm = Math.Round(release, 1);
    }

    public void EditDeadzone(double? topMm, double? bottomMm)
    {
        if (Busy || SelectedKey == null) return;
        var draft = GetDraft();
        if (topMm is double top && Valid(top, 0, 0.5)) draft.TopMm = Math.Round(top, 1);
        if (bottomMm is double bottom && Valid(bottom, 0, 0.5)) draft.BottomMm = Math.Round(bottom, 1);
    }

    public void EditDksThresholds(double? startMm, double? endMm)
    {
        if (Busy || SelectedKey == null) return;
        var draft = GetDraft();
        if (startMm is double start && Valid(start, 0.1, 4.0)) { draft.DksStartMm = Math.Round(start, 1); draft.DksDirty = true; }
        if (endMm is double end && Valid(end, 0.1, 4.0)) { draft.DksEndMm = Math.Round(end, 1); draft.DksDirty = true; }
    }

    public bool EditDksSlot(int index, ushort? targetId, string downStart, string downEnd,
        string upStart, string upEnd)
    {
        if (Busy || SelectedKey == null || index is < 0 or > 3 ||
            targetId is ushort id && !MagneticKeyLayout.IsValidDksActionTarget(id) ||
            !TriggerStates.Contains(downStart) || !TriggerStates.Contains(downEnd) ||
            !TriggerStates.Contains(upStart) || !TriggerStates.Contains(upEnd)) return false;
        var draft = GetDraft();
        draft.DksSlots[index] = new MagneticDksSlot
        {
            Target = targetId is ushort logical ?
                new MagneticDksTarget { Kind = "LogicalKey", LogicalId = logical } :
                new MagneticDksTarget { Kind = "DefaultSentinel" },
            DownStart = downStart, DownEnd = downEnd, UpStart = upStart, UpEnd = upEnd
        };
        draft.DksDirty = true;
        return true;
    }

    public void EditSpeedTapPair(ushort? key1, ushort? key2)
    {
        if (Busy || key1 is ushort a && MagneticKeyLayout.Find(a) == null ||
            key2 is ushort b && MagneticKeyLayout.Find(b) == null) return;
        SpeedTapKey1 = key1;
        SpeedTapKey2 = key2;
    }
    public void EditSpeedTapMaster(bool enabled) { if (!Busy) SpeedTapMasterDraft = enabled; }
    public void EditStaticAnalog(bool enabled) { if (!Busy) StaticAnalogDraft = enabled; }

    public async Task RefreshAsync()
    {
        if (Busy || Refreshing) return;
        Refreshing = true;
        try
        {
            Status = await _client.GetStatusAsync();
            foreach (var (id, draft) in _drafts)
                if (!draft.DksDirty) HydrateDks(id, draft);
            LastMessage = Status.Health switch
            {
                "PersistentSafetyQuarantine" => "存在跨重启安全隔离：上次磁轴事务未确认完成。请在外部完成重新同步后执行恢复。",
                "IndeterminateStagedState" => "磁轴配置状态未知，本次会话已停止写入；需要外部重新同步。",
                "Stopped" => "磁轴运行时已停止，无法继续修改。",
                _ when Status.PersistentSafetyQuarantine => "跨重启安全隔离中；禁止继续写入。请在外部完成重新同步后执行恢复。",
                _ when Status.ApiVersion != 1 => "磁轴服务状态无法验证，禁止写入。",
                _ when !Status.Available => "原生 HID 设备当前不可用，或核心正在模拟/使用其他后端。",
                _ => "配置文件保存值与本次会话已应用值均不是设备读回。"
            };
        }
        catch (Exception ex)
        {
            Status = null;
            LastMessage = $"无法读取磁轴运行状态：{ex.Message}";
        }
        finally { Refreshing = false; }
    }

    public Task<bool> ApplyActuationAsync()
    {
        if (!CanWriteSelected || Draft?.ActuationMm is not double mm || SelectedLogicalId is not ushort key)
            return Task.FromResult(false);
        return SubmitAsync(() => _client.SetActuationAsync(key, mm), () => GetDraft().ActuationMm = null);
    }

    public Task<bool> ApplyRapidTriggerAsync(bool resolveDks = false)
    {
        if (!CanWriteSelected || Draft?.RapidTriggerEnabled is not bool enabled || SelectedLogicalId is not ushort key)
            return Task.FromResult(false);
        if (enabled)
        {
            if (Draft.PressMm is not double press || Draft.ReleaseMm is not double release ||
                RtConflictPossible && !resolveDks) {
                LastMessage = "启用单键 RT 可能覆盖 DKS；请明确确认后应用。";
                return Task.FromResult(false);
            }
            return SubmitAsync(() => _client.SetRapidTriggerAsync(key, press, release, resolveDks),
                () => { GetDraft().RapidTriggerEnabled = null; GetDraft().PressMm = null; GetDraft().ReleaseMm = null; });
        }
        if (InheritedRt(key) is null) {
            LastMessage = "缺少可信来源的全局 RT Press/Release 值，不能推测禁用参数。";
            return Task.FromResult(false);
        }
        return SubmitAsync(() => _client.DisableRapidTriggerAsync(key),
            () => GetDraft().RapidTriggerEnabled = null);
    }

    public Task<bool> ApplyDeadzoneAsync()
    {
        if (!CanWriteSelected || Draft?.TopMm is not double top || Draft.BottomMm is not double bottom ||
            SelectedLogicalId is not ushort key) return Task.FromResult(false);
        return SubmitAsync(() => _client.SetDeadzoneAsync(key, top, bottom),
            () => { GetDraft().TopMm = null; GetDraft().BottomMm = null; });
    }

    public Task<bool> ResetAllDeadzoneAsync() => CanResetAllDeadzone ?
        SubmitAsync(_client.ResetAllDeadzoneAsync, () => { foreach (var draft in _drafts.Values) {
            draft.TopMm = null; draft.BottomMm = null;
        } }) : Task.FromResult(false);

    public Task<bool> ApplyDksAsync(bool resolveRt = false)
    {
        var draft = Draft;
        if (!CanWriteSelected || SelectedLogicalId is not ushort key || draft?.DksDirty != true ||
            draft.DksStartMm is not double start || draft.DksEndMm is not double end || start > end ||
            draft.DksSlots.Count != 4 ||
            draft.DksSlots.Any(s => !ValidSlot(s)) || DksConflictPossible && !resolveRt) {
            LastMessage = DksConflictPossible && !resolveRt ?
                "启用 DKS 可能关闭该键的逐键快速触发；请明确确认后应用。" :
                "DKS 需要有效的起止行程、四个动作槽，且起点不得大于终点。";
            return Task.FromResult(false);
        }
        return SubmitAsync(() => _client.SetDksAsync(key, start, end, draft.DksSlots, resolveRt),
            () => draft.DksDirty = false);
    }

    public Task<bool> RestoreDksStandardAsync() => CanWriteSelected && SelectedLogicalId is ushort key ?
        SubmitAsync(() => _client.RestoreDksStandardAsync(key), () => {
            var draft = GetDraft();
            draft.DksStartMm = null; draft.DksEndMm = null; draft.DksDirty = false;
            draft.DksSlots.Clear();
            for (int i = 0; i < 4; ++i) draft.DksSlots.Add(new MagneticDksSlot());
            HydrateDks(key, draft);
        }) :
        Task.FromResult(false);

    public Task<bool> ApplyGlobalActuationAsync()
    {
        if (!CanWriteGlobal || GlobalDraft.ActuationMm is not double mm)
            return Task.FromResult(false);
        return SubmitAsync(() => _client.SetGlobalActuationAsync(mm), () => GlobalDraft.ActuationMm = null);
    }

    public Task<bool> ApplyGlobalDeadzoneAsync()
    {
        if (!CanWriteGlobal || GlobalDraft.TopMm is not double top || GlobalDraft.BottomMm is not double bottom)
            return Task.FromResult(false);
        return SubmitAsync(() => _client.SetGlobalDeadzoneAsync(top, bottom),
            () => { GlobalDraft.TopMm = null; GlobalDraft.BottomMm = null; });
    }

    public Task<bool> ApplyGlobalRapidTriggerAsync()
    {
        if (!CanWriteGlobal || GlobalDraft.PressMm is not double press || GlobalDraft.ReleaseMm is not double release)
            return Task.FromResult(false);

        double top;
        double bottom;
        if (Status?.GlobalDeadzone.Known == true)
        {
            top = Status.GlobalDeadzone.TopRaw / 10.0;
            bottom = Status.GlobalDeadzone.BottomRaw / 10.0;
        }
        else if (Status?.HostProfile.GlobalDeadzoneTop.Known == true &&
                 Status?.HostProfile.GlobalDeadzoneBottom.Known == true)
        {
            top = Status.HostProfile.GlobalDeadzoneTop.Raw / 10.0;
            bottom = Status.HostProfile.GlobalDeadzoneBottom.Raw / 10.0;
        }
        else
        {
            LastMessage = "无法确认当前全局死区，无法安全应用快速触发设置。";
            return Task.FromResult(false);
        }

        bool separate = GlobalDraft.SeparateMode ?? (Status?.GlobalRapidTrigger.Known == true && Status.GlobalRapidTrigger.SeparateMode.HasValue ?
            Status.GlobalRapidTrigger.SeparateMode.Value : Math.Abs(press - release) > 1e-4);

        if (!separate && Math.Abs(press - release) > 1e-4)
        {
            LastMessage = "联动灵敏度模式下，按下与释放行程必须相同。";
            return Task.FromResult(false);
        }

        return SubmitAsync(() => _client.SetGlobalRapidTriggerAsync(press, release, top, bottom, separate),
            () => { GlobalDraft.PressMm = null; GlobalDraft.ReleaseMm = null; GlobalDraft.SeparateMode = null; });
    }

    public Task<bool> ApplySpeedTapPairAsync(bool enabled)
    {
        if (!CanWrite || SpeedTapKey1 is not ushort a || SpeedTapKey2 is not ushort b || a == b)
            return Task.FromResult(false);
        if (enabled && Status?.SpeedTap.SavedProfilePairsKnown != true)
        {
            LastMessage = "活动配置保存的 SpeedTap 键对基线未知，无法排除键对冲突；禁止启用新键对。";
            return Task.FromResult(false);
        }
        return SubmitAsync(() => enabled ? _client.SetSpeedTapPairAsync(a, b) :
            _client.DisableSpeedTapPairAsync(a, b));
    }
    public Task<bool> ApplySpeedTapMasterAsync() => CanWrite && SpeedTapMasterDraft is bool enabled ?
        SubmitAsync(() => _client.SetSpeedTapMasterAsync(enabled), () => SpeedTapMasterDraft = null) :
        Task.FromResult(false);
    public Task<bool> ResetSpeedTapToProfileAsync() => CanWrite ?
        SubmitAsync(_client.ResetSpeedTapToProfileAsync) : Task.FromResult(false);
    public Task<bool> ApplyStaticAnalogAsync() => CanWrite && StaticAnalogDraft is bool enabled ?
        SubmitAsync(() => _client.SetStaticAnalogEffectAsync(enabled), () => StaticAnalogDraft = null) :
        Task.FromResult(false);

    public async Task<bool> AcknowledgeExternalResynchronizationAsync()
    {
        if (Busy || Refreshing) return false;
        Busy = true;
        try
        {
            var response = await _client.AcknowledgeExternalResynchronizationAsync();
            Status = response;
            if (response.Succeeded)
            {
                _drafts.Clear();
                LastMessage = "已确认外部重新同步；安全隔离已清除。";
            }
            else
            {
                LastMessage = string.IsNullOrWhiteSpace(response.LastError) ?
                    "确认外部重新同步失败。" : response.LastError;
            }
            return response.Succeeded;
        }
        catch (Exception ex)
        {
            Status = null;
            LastMessage = $"无法确认外部重新同步：{ex.Message}";
            return false;
        }
        finally { Busy = false; }
    }

    private async Task<bool> SubmitAsync(Func<Task<MagneticStatus>> submit, Action? clearDraft = null)
    {
        Busy = true; // Set before first await: duplicate clicks cannot queue writes.
        LastMessage = "正在应用…磁轴事务期间灯光帧可能短暂等待。";
        try
        {
            var response = await submit();
            Status = response; // Service returns a fresh aggregated state after completion.
            if (response.Succeeded) clearDraft?.Invoke();
            LastMessage = response.Health is "IndeterminateStagedState" or "PersistentSafetyQuarantine" ||
                response.PersistentSafetyQuarantine ?
                "配置状态不确定，磁轴写入已被安全隔离；需要外部重新同步。" :
                response.Succeeded ? "本次会话已应用；这是提交记录，不是设备读回。" :
                string.IsNullOrWhiteSpace(response.LastError) ? "应用失败，草稿已保留。" : response.LastError;
            return response.Succeeded;
        }
        catch (Exception ex)
        {
            Status = null; // Submission may have reached daemon. Fail closed.
            LastMessage = $"无法确认应用结果：{ex.Message}。请刷新状态。";
            return false;
        }
        finally { Busy = false; }
    }

    private (double PressMm, double ReleaseMm)? InheritedRt(ushort key)
    {
        if (Status?.GlobalRapidTrigger.Known == true)
            return (Status.GlobalRapidTrigger.PressRaw / 10.0, Status.GlobalRapidTrigger.ReleaseRaw / 10.0);
        var host = Status?.HostProfile;
        if (host is { GlobalRtPress.Known: true, GlobalRtRelease.Known: true })
            return (host.GlobalRtPress.Raw / 10.0, host.GlobalRtRelease.Raw / 10.0);
        var values = _inheritedRapidTrigger?.Invoke(key);
        return values is { } known && Valid(known.PressMm, 0.1, 2.5) &&
            Valid(known.ReleaseMm, 0.1, 2.5) ? known : null;
    }

    private MagneticKeyDraft GetDraft()
    {
        var id = SelectedLogicalId!.Value;
        if (!_drafts.TryGetValue(id, out var draft))
        {
            _drafts[id] = draft = new MagneticKeyDraft();
            HydrateDks(id, draft);
        }
        return draft;
    }

    private void HydrateDks(ushort id, MagneticKeyDraft draft)
    {
        var applied = Status?.Dks.FirstOrDefault(value => value.LogicalId == id &&
            value.Source == "SessionApplied" && value.Slots.Count == 4);
        if (applied == null) return;
        draft.DksStartMm = applied.StartRaw / 10.0;
        draft.DksEndMm = applied.EndRaw / 10.0;
        draft.DksSlots.Clear();
        foreach (var slot in applied.Slots)
            draft.DksSlots.Add(new MagneticDksSlot {
                Target = new MagneticDksTarget { Kind = slot.Target.Kind,
                    LogicalId = slot.Target.LogicalId },
                DownStart = slot.DownStart, DownEnd = slot.DownEnd,
                UpStart = slot.UpStart, UpEnd = slot.UpEnd
            });
    }

    private static bool ValidSlot(MagneticDksSlot slot) =>
        ((slot.Target.Kind == "DefaultSentinel" && slot.Target.LogicalId is null) ||
        (slot.Target.Kind == "LogicalKey" && slot.Target.LogicalId is ushort id &&
        MagneticKeyLayout.IsValidDksActionTarget(id))) &&
        TriggerStates.Contains(slot.DownStart) && TriggerStates.Contains(slot.DownEnd) &&
        TriggerStates.Contains(slot.UpStart) && TriggerStates.Contains(slot.UpEnd);

    private static bool Valid(double value, double min, double max) =>
        double.IsFinite(value) && value >= min - 1e-9 && value <= max + 1e-9 &&
        Math.Abs(value * 10 - Math.Round(value * 10)) < 1e-7;
}
