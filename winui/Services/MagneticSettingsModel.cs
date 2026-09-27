namespace Aura_WinUI.Services;

public enum MagneticSelectionContext { Global, Single, Multi }
public enum MagneticDksState { Standard, SessionConfigured, Unknown }
public enum MagneticBatchRtAction { Unchanged, Enable, Disable }
public enum MagneticAggregateKind { Unknown, UniformKnown, MixedKnown, ContainsUnknown }
public enum MagneticOverlaySource { Unknown, SessionPerKey, HostPerKey, SessionGlobalBaseline, HostGlobalBaseline }
public sealed record MagneticAggregate(MagneticAggregateKind Kind, string? Value, int KnownCount,
    int UnknownCount, string? Source);
public sealed record MagneticKeyOverlay(string ActuationText, string RtPressText, string RtReleaseText,
    bool ActuationKnown, bool RtPressKnown, bool RtReleaseKnown, string AccessibilityText,
    MagneticOverlaySource ActuationSource, MagneticOverlaySource RtPressSource,
    MagneticOverlaySource RtReleaseSource);

public sealed class MagneticBatchDraft
{
    public double? ActuationMm { get; internal set; }
    public double? TopMm { get; internal set; }
    public double? BottomMm { get; internal set; }
    public double? PressMm { get; internal set; }
    public double? ReleaseMm { get; internal set; }
    public MagneticBatchRtAction RtAction { get; internal set; }
    internal void Clear()
    {
        ActuationMm = TopMm = BottomMm = PressMm = ReleaseMm = null;
        RtAction = MagneticBatchRtAction.Unchanged;
    }
}

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
    private readonly HashSet<ushort> _multiIds = [];
    private static readonly HashSet<string> TriggerStates = ["Inactive", "Tap", "Release", "Hold"];

    public MagneticSettingsModel(IMagneticControlClient client,
        Func<ushort, (double PressMm, double ReleaseMm)?>? inheritedRapidTrigger = null)
    {
        _client = client;
        _inheritedRapidTrigger = inheritedRapidTrigger;
    }

    public MagneticSelectionContext SelectionContext { get; private set; } = MagneticSelectionContext.Global;
    public ushort? SelectedLogicalId { get; private set; }
    public bool IsGlobalMode => SelectionContext == MagneticSelectionContext.Global;
    public bool IsMultiMode => SelectionContext == MagneticSelectionContext.Multi;
    public IReadOnlyList<ushort> SelectedLogicalIds => MagneticKeyLayout.Keys
        .Where(key => _multiIds.Contains(key.LogicalId)).Select(key => key.LogicalId).ToArray();
    public int SelectedCount => _multiIds.Count;
    public string MultiSelectionSummary => SelectedCount == 0 ? "尚未选择按键" :
        SelectedCount <= 6 ? string.Join(" · ", MagneticKeyLayout.Keys
            .Where(key => _multiIds.Contains(key.LogicalId)).Select(key => key.Label)) :
        $"已选择 {SelectedCount} 个按键";
    public bool DksEditorOpen { get; private set; }
    public MagneticBatchDraft BatchDraft { get; } = new();
    public MagneticBatchResult? LastBatchResult { get; private set; }
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
    public bool CanWriteSelected => CanWrite && SelectionContext == MagneticSelectionContext.Single && SelectedKey != null;
    public bool CanWriteGlobal => CanWrite && IsGlobalMode;
    public bool CanBatchActuation => CanWrite && IsMultiMode && SelectedCount > 0 && BatchDraft.ActuationMm is not null;
    public bool CanBatchDeadzone => CanWrite && IsMultiMode && SelectedCount > 0 &&
        BatchDraft.TopMm is not null && BatchDraft.BottomMm is not null;
    public bool CanBatchRapidTrigger => CanWrite && IsMultiMode && SelectedCount > 0 &&
        (BatchDraft.RtAction == MagneticBatchRtAction.Enable && BatchDraft.PressMm is not null &&
            BatchDraft.ReleaseMm is not null ||
         BatchDraft.RtAction == MagneticBatchRtAction.Disable &&
            (Status?.GlobalRapidTrigger.Known == true || Status?.HostProfile is {
                GlobalRtPress.Known: true, GlobalRtRelease.Known: true }));
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
    public MagneticDksState DksState
    {
        get
        {
            if (SelectedLogicalId is not ushort id) return MagneticDksState.Unknown;
            var applied = Status?.Dks.FirstOrDefault(v => v.LogicalId == id && v.Source == "SessionApplied");
            return applied == null ? MagneticDksState.Unknown :
                applied.StandardRuntimeConfiguration ? MagneticDksState.Standard : MagneticDksState.SessionConfigured;
        }
    }
    public string DksStateText => DksState switch {
        MagneticDksState.Standard => "标准按键行为",
        MagneticDksState.SessionConfigured => "本次会话已配置 DKS",
        _ => "当前 DKS 状态未知"
    };
    public bool RtConflictPossible => SelectedLogicalId is not null && DksState != MagneticDksState.Standard;
    public string RtDksConflictCopy => DksState == MagneticDksState.SessionConfigured ?
        "此键在本次会话中已配置 DKS。继续后，Aura 会先将此键恢复为标准按键行为，再应用快速触发。两次操作分别提交。" :
        "无法确认此键当前的 DKS 状态。快速触发与自定义 DKS 不应同时使用。继续后，Aura 会先将此键恢复为标准按键行为，再应用快速触发。两次操作分别提交。";
    public MagneticAggregate MultiActuation => AggregateMulti(id => Status?.Actuation
        .FirstOrDefault(v => v.LogicalId == id && KnownSource(v.Source)) is { } v ?
            ($"{v.Raw / 10.0:F1} mm", v.Source) : null);
    // HostProfile only knows per-key RT membership; its sensitivity is not a full known value.
    public MagneticAggregate MultiRapidTrigger => AggregateMulti(id => Status?.RapidTrigger
        .FirstOrDefault(v => v.LogicalId == id && v.Source == "SessionApplied") is { } v ?
            ($"{(v.Enabled ? "开启" : "关闭")} · 按下 {v.PressRaw / 10.0:F1} / 抬起 {v.ReleaseRaw / 10.0:F1} mm",
                v.Source) : null);
    public MagneticAggregate MultiDeadzone => AggregateMulti(id => Status?.Deadzone
        .FirstOrDefault(v => v.LogicalId == id && KnownSource(v.Source)) is { } v ?
            ($"顶部 {v.TopRaw / 10.0:F1} / 底部 {v.BottomRaw / 10.0:F1} mm", v.Source) : null);
    public string MultiActuationText => FormatAggregate(MultiActuation);
    public string MultiRapidTriggerText => FormatAggregate(MultiRapidTrigger);
    public string MultiDeadzoneText => FormatAggregate(MultiDeadzone);

    public MagneticKeyOverlay KeyOverlay(ushort logicalId)
    {
        // Session/per-key values win. A known global value is a labeled baseline:
        // this status model cannot prove absence of an unparsed device override.
        var actuation = Status?.Actuation.FirstOrDefault(value => value.LogicalId == logicalId &&
            value.Source == "SessionApplied") ?? Status?.Actuation.FirstOrDefault(value =>
            value.LogicalId == logicalId && value.Source == "HostProfile");
        var rt = Status?.RapidTrigger.FirstOrDefault(value => value.LogicalId == logicalId &&
            value.Source == "SessionApplied");
        var global = Status?.GlobalActuation;
        MagneticOverlaySource actuationSource = actuation?.Source switch {
            "SessionApplied" => MagneticOverlaySource.SessionPerKey,
            "HostProfile" => MagneticOverlaySource.HostPerKey,
            _ => global is { Known: true, Source: "SessionApplied" } ? MagneticOverlaySource.SessionGlobalBaseline :
                global is { Known: true, Source: "HostProfile" } ? MagneticOverlaySource.HostGlobalBaseline :
                MagneticOverlaySource.Unknown
        };
        bool actuationKnown = actuationSource != MagneticOverlaySource.Unknown;
        bool rtKnown = rt is { Enabled: true };
        byte actuationRaw = actuation?.Raw ?? global?.Raw ?? 0;
        string actuationText = actuationKnown ? $"{actuationRaw / 10.0:F1}" : "—";
        string pressText = rtKnown ? $"↓{rt!.PressRaw / 10.0:F1}" : "—";
        string releaseText = rtKnown ? $"↑{rt!.ReleaseRaw / 10.0:F1}" : "—";
        string actuationAccessible = actuationSource switch {
            MagneticOverlaySource.SessionPerKey => $"触发点 {actuationText} mm，本次会话逐键设置",
            MagneticOverlaySource.HostPerKey => $"触发点 {actuationText} mm，已保存逐键设置",
            MagneticOverlaySource.SessionGlobalBaseline or MagneticOverlaySource.HostGlobalBaseline =>
                $"触发点全局基线 {actuationText} mm，设备逐键覆盖状态未确认",
            _ => "触发点未知"
        };
        string accessibility = actuationAccessible + "，" +
            (rtKnown ? $"RT 按下 {rt!.PressRaw / 10.0:F1} mm，RT 抬起 {rt.ReleaseRaw / 10.0:F1} mm" :
                rt is { Enabled: false } ? "RT 已禁用，按下和抬起灵敏度不适用" : "RT 按下未知，RT 抬起未知");
        return new(actuationText, pressText, releaseText, actuationKnown, rtKnown, rtKnown, accessibility,
            actuationSource, rtKnown ? MagneticOverlaySource.SessionPerKey : MagneticOverlaySource.Unknown,
            rtKnown ? MagneticOverlaySource.SessionPerKey : MagneticOverlaySource.Unknown);
    }

    public ushort? SpeedTapKey1 { get; private set; }
    public ushort? SpeedTapKey2 { get; private set; }
    public bool CanEnableSpeedTapPair => CanWrite && SpeedTapKey1 is ushort a && SpeedTapKey2 is ushort b &&
        a != b && Status?.SpeedTap.SavedProfilePairsKnown == true;
    public bool CanDisableSpeedTapPair => CanWrite && SpeedTapKey1 is ushort a && SpeedTapKey2 is ushort b && a != b;
    public bool? SpeedTapMasterDraft { get; private set; }
    public bool? StaticAnalogDraft { get; private set; }
    public const string HardwareAnalogDescription =
        "该功能由键盘固件根据磁轴行程直接渲染。目前仅验证其在键盘已处于固件恒亮模式时可正常工作。Aura 软件灯效启用后，当前版本无法安全地自动切回固件灯光模式。";
    public const string HardwareAnalogBlockedReason =
        "当前由 Aura 软件灯效控制键盘灯光，无法安全切换到固件压感灯效。";
    // This UI has no trusted firmware-lighting-mode state. Keep the verified
    // typed runtime operation, but do not offer an unsafe normal Apply path.
    public bool CanApplyStaticAnalog => false;

    public bool Select(ushort logicalId)
    {
        if (Busy || MagneticKeyLayout.Find(logicalId) == null) return false;
        SelectedLogicalId = logicalId;
        SelectionContext = MagneticSelectionContext.Single;
        _multiIds.Clear();
        BatchDraft.Clear(); LastBatchResult = null;
        DksEditorOpen = false;
        _ = GetDraft(); // clone known session values into local editable state only
        return true;
    }

    public bool SelectGlobal()
    {
        if (Busy) return false;
        SelectedLogicalId = null;
        SelectionContext = MagneticSelectionContext.Global;
        _multiIds.Clear();
        BatchDraft.Clear(); LastBatchResult = null;
        DksEditorOpen = false;
        return true;
    }

    public bool EnterMulti()
    {
        if (Busy) return false;
        if (IsMultiMode) return true;
        _multiIds.Clear();
        BatchDraft.Clear(); LastBatchResult = null;
        if (SelectedLogicalId is ushort id) _multiIds.Add(id);
        SelectedLogicalId = null;
        SelectionContext = MagneticSelectionContext.Multi;
        DksEditorOpen = false;
        return true;
    }

    public bool ToggleMulti(ushort logicalId)
    {
        if (Busy || MagneticKeyLayout.Find(logicalId) == null) return false;
        if (!IsMultiMode && !EnterMulti()) return false;
        if (!_multiIds.Add(logicalId)) _multiIds.Remove(logicalId);
        BatchDraft.Clear(); LastBatchResult = null;
        return true;
    }

    public bool ClearMulti()
    {
        if (Busy || !IsMultiMode) return false;
        _multiIds.Clear();
        BatchDraft.Clear(); LastBatchResult = null;
        return true;
    }

    public bool ConfigureDksEditor()
    {
        if (Busy || SelectionContext != MagneticSelectionContext.Single) return false;
        DksEditorOpen = true;
        return true;
    }

    public void EditBatchActuation(double value)
    {
        if (!Busy && IsMultiMode && Valid(value, 0.1, 4.0)) BatchDraft.ActuationMm = Math.Round(value, 1);
    }

    public void EditBatchDeadzone(double? top, double? bottom)
    {
        if (Busy || !IsMultiMode) return;
        if (top is double t && Valid(t, 0, 0.5)) BatchDraft.TopMm = Math.Round(t, 1);
        if (bottom is double b && Valid(b, 0, 0.5)) BatchDraft.BottomMm = Math.Round(b, 1);
    }

    public void EditBatchRapidTrigger(MagneticBatchRtAction action, double? press = null, double? release = null)
    {
        if (Busy || !IsMultiMode) return;
        BatchDraft.RtAction = action;
        if (press is double p && Valid(p, 0.1, 2.5)) BatchDraft.PressMm = Math.Round(p, 1);
        if (release is double r && Valid(r, 0.1, 2.5)) BatchDraft.ReleaseMm = Math.Round(r, 1);
    }

    public Task<bool> ApplyBatchActuationAsync() => !CanBatchActuation ? Task.FromResult(false) :
        SubmitBatchAsync(ids => _client.SetBatchActuationAsync(ids, BatchDraft.ActuationMm!.Value),
            () => BatchDraft.ActuationMm = null);

    public Task<bool> ApplyBatchDeadzoneAsync() => !CanBatchDeadzone ? Task.FromResult(false) :
        SubmitBatchAsync(ids => _client.SetBatchDeadzoneAsync(ids, BatchDraft.TopMm!.Value,
            BatchDraft.BottomMm!.Value), () => { BatchDraft.TopMm = null; BatchDraft.BottomMm = null; });

    public Task<bool> ApplyBatchRapidTriggerAsync(bool resolveDks = false) => !CanBatchRapidTrigger ?
        Task.FromResult(false) : SubmitBatchAsync(ids => _client.SetBatchRapidTriggerAsync(ids,
            BatchDraft.RtAction == MagneticBatchRtAction.Enable, BatchDraft.PressMm,
            BatchDraft.ReleaseMm, resolveDks), () => {
                BatchDraft.RtAction = MagneticBatchRtAction.Unchanged;
                BatchDraft.PressMm = BatchDraft.ReleaseMm = null;
            });

    private async Task<bool> SubmitBatchAsync(Func<IReadOnlyList<ushort>, Task<MagneticStatus>> submit,
        Action clearDraft)
    {
        var ids = SelectedLogicalIds.ToArray(); // Freeze canonical order before the first await.
        Busy = true;
        LastBatchResult = null;
        LastMessage = $"正在逐个安全应用 {ids.Length} 个按键…";
        try
        {
            var response = await submit(ids);
            LastBatchResult = response.BatchResult;
            // A partial batch changes only successful SessionApplied entries. Refresh explicitly.
            try { Status = await _client.GetStatusAsync(); }
            catch (Exception ex) {
                Status = null;
                LastMessage = $"批量结果已收到，但状态刷新失败：{ex.Message}。请刷新状态。";
                return false;
            }
            if (response.BatchResult?.CompletedFully == true) {
                clearDraft();
                LastMessage = $"已应用到 {ids.Length} 个按键；本次会话记录不是设备读回。";
                return true;
            }
            LastMessage = response.BatchResult is { } result ?
                $"批量应用未完整完成：已应用 {result.AppliedCount}，失败 {result.FailedCount}，未执行 {result.NotExecutedCount}。" :
                $"无法确认批量应用结果：{response.LastError}。请刷新状态。";
            return false;
        }
        catch (Exception ex)
        {
            Status = null;
            LastMessage = $"无法确认批量应用结果：{ex.Message}。请刷新状态。";
            return false;
        }
        finally { Busy = false; }
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
    public Task<bool> ApplyStaticAnalogAsync() => CanApplyStaticAnalog && CanWrite && StaticAnalogDraft is bool enabled ?
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

    private static bool KnownSource(string source) => source is "SessionApplied" or "HostProfile";

    private MagneticAggregate AggregateMulti(Func<ushort, (string Value, string Source)?> read)
    {
        if (!IsMultiMode || SelectedCount == 0) return new(MagneticAggregateKind.Unknown, null, 0, 0, null);
        var known = SelectedLogicalIds.Select(read).Where(value => value.HasValue)
            .Select(value => value!.Value).ToArray();
        if (known.Length == 0) return new(MagneticAggregateKind.Unknown, null, 0, SelectedCount, null);
        if (known.Length != SelectedCount) return new(MagneticAggregateKind.ContainsUnknown, null,
            known.Length, SelectedCount - known.Length, null);
        if (known.Select(value => value.Value).Distinct().Count() != 1)
            return new(MagneticAggregateKind.MixedKnown, null, known.Length, 0, null);
        string source = known.Select(value => value.Source).Distinct().Count() == 1 ?
            (known[0].Source == "SessionApplied" ? "本次会话" : "已保存配置") : "来源混合";
        return new(MagneticAggregateKind.UniformKnown, known[0].Value, known.Length, 0, source);
    }

    private static string FormatAggregate(MagneticAggregate aggregate) => aggregate.Kind switch {
        MagneticAggregateKind.UniformKnown => $"{aggregate.Source}：{aggregate.Value}",
        MagneticAggregateKind.MixedKnown => "混合值",
        MagneticAggregateKind.ContainsUnknown => $"{aggregate.KnownCount} 个已知 · {aggregate.UnknownCount} 个未知",
        _ => "未知"
    };

    private static bool Valid(double value, double min, double max) =>
        double.IsFinite(value) && value >= min - 1e-9 && value <= max + 1e-9 &&
        Math.Abs(value * 10 - Math.Round(value * 10)) < 1e-7;
}
