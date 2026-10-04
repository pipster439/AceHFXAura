using System.Net;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Aura_WinUI.Services;

public enum ProfilePageState { Loading, Ready, NeedsApply, Deferred, ApplyFailed, DaemonUnavailable, IncompatibleDaemon, ApiProtocolError, DocumentUnavailable }
public enum ProfileNoticeKind { None, Success, Information, Warning, Error }

// A short-lived editor snapshot, never the source of runtime truth. Only the
// daemon client can mutate a document or activate a device Profile.
public sealed class ProfilePageModel(IProfileControlClient client)
{
    private ProfileApiResponse? _snapshot;
    private DeviceProfile? _draft;
    private string? _savedJson;
    private string? _baseProfileJson;
    private string? _baseDefaultsJson;
    public long BaseDocumentRevision { get; private set; }
    public Guid? ConflictedProfileId { get; private set; }
    public bool EditingProfileDeleted => _draft is not null && _snapshot is not null &&
        !Profiles.Any(p => p.Id == _draft.Id);

    public IReadOnlyList<DeviceProfile> Profiles => _snapshot?.Profiles ?? [];
    public ProfileApiResponse? Snapshot => _snapshot;
    public DeviceProfile? Draft => _draft;
    public Guid? EditingId => _draft?.Id;
    public bool IsHardwareSlotDraft => _draft?.ActivationBackend == "hardware_slot";
    public string HardwareSlotText => $"目标槽位：{_draft?.HardwareSlot?.ToString() ?? "未选择"} · 键盘当前槽位：{_snapshot?.HardwareSlotStatus?.ObservedHardwareSlot?.ToString() ?? "未确认"}";
    public bool HardwareSlotMismatch => IsHardwareSlotDraft && _snapshot?.HardwareSlotStatus?.HardwareSlotMatch == false;
    public void SetActivationBackend(string backend)
    {
        if (_draft is null || IsBusy) return;
        if (backend is not ("host_managed" or "hardware_slot")) throw new ArgumentException("Unknown backend");
        _draft.ActivationBackend = backend;
        if (backend == "host_managed") _draft.HardwareSlot = null;
        ReconcileDraftValidationNotice();
    }
    public async Task RefreshHardwareSlotAsync(CancellationToken token = default)
    {
        if (_snapshot is null || IsBusy) return;
        IsBusy = true;
        try {
            var result = await client.RefreshHardwareSlotAsync(token);
            token.ThrowIfCancellationRequested();
            // Only canonical list responses can rebase document/draft revision.
            AcceptList(await client.ListAsync(token), preserveDraft: true);
            if (_snapshot.RuntimeRevision <= result.RuntimeRevision) {
                _snapshot.HardwareSlotStatus = result.HardwareSlotStatus;
                _snapshot.ActiveProfileId = result.ActiveProfileId; _snapshot.Dirty = result.Dirty;
                _snapshot.M605SessionGeneration = result.M605SessionGeneration;
                _snapshot.RuntimeRevision = result.RuntimeRevision;
                State = StateFrom(_snapshot);
            }
        }
        catch (ProfileApiException ex) { PublishNotice("无法确认键盘当前板载槽位：" + ex.Message, ProfileNoticeKind.Error); }
        finally { IsBusy = false; }
    }
    // Draft summary and observed gate are independent; neither is firmware RT table readback.
    public int ManagedRapidTriggerKeyCount => _draft?.Magnetic.Keys
        .Where(k => k.RapidTrigger is not null).Select(k => k.LogicalId).Distinct().Count() ?? 0;
    public int EnabledRapidTriggerKeyCount => _draft?.Magnetic.Keys
        .Where(k => k.RapidTrigger?.Enabled == true).Select(k => k.LogicalId).Distinct().Count() ?? 0;
    public string RapidTriggerSelectionSummary => ManagedRapidTriggerKeyCount == 0 ?
        "未设置逐键快速触发 · 配置中已启用按键：0 个" :
        $"配置中已启用按键：{EnabledRapidTriggerKeyCount} 个 · 已管理：{ManagedRapidTriggerKeyCount} 个";
    public string HardwareRtGateState => _snapshot?.HardwareRtGate?.State is "on" or "off" ?
        _snapshot.HardwareRtGate.State : "unknown";
    public string HardwareRtGateText => HardwareRtGateState switch {
        "off" => "键盘硬件快速触发开关已关闭，当前快速触发不会生效。已保存的按键配置仍然保留。",
        "on" => "键盘硬件快速触发开关已打开。实际生效的按键仍取决于已应用的逐键配置；草稿不会自动应用。",
        _ => "无法读取键盘快速触发开关状态。等待键盘状态通知；已保存的按键配置不受影响。"
    };
    // Only refresh device observation. Never consume a newer document revision
    // against an older editing snapshot or replace a user's unsaved draft.
    public async Task RefreshHardwareRtGateAsync(CancellationToken token = default)
    {
        if (_snapshot is null || IsBusy) return;
        try {
            var result = await client.GetHardwareRtGateAsync(token);
            token.ThrowIfCancellationRequested();
            _snapshot.HardwareRtGate = result.HardwareRtGate;
        }
        catch (ProfileApiException) { _snapshot.HardwareRtGate = null; }
    }

    // Batch editing writes one independent state per selected logical key.
    // This is not a runtime planner, shared hardware default or API mutation.
    public void SetRapidTriggerDraftForKeys(IEnumerable<ushort> logicalIds, ProfileRapidTrigger? value)
    {
        if (_draft is null || IsBusy) return;
        var ids = logicalIds.Distinct().Order().ToArray();
        if (ids.Any(id => !MagneticKeyLayout.Keys.Any(key => key.LogicalId == id)))
            throw new ArgumentException("Unsupported logical key", nameof(logicalIds));
        foreach (var id in ids) {
            var key = _draft.Magnetic.Keys.FirstOrDefault(k => k.LogicalId == id);
            if (key is null) {
                if (value is null) continue;
                key = new ProfileKey { LogicalId = id }; _draft.Magnetic.Keys.Add(key);
            }
            // Each key retains its own unknown protocol/extension metadata.
            // A batch parameter edit must not copy one key's extensions to others.
            key.RapidTrigger = value is null ? null : value with {
                Extensions = key.RapidTrigger?.Extensions is { } existing ?
                    new Dictionary<string, System.Text.Json.JsonElement>(existing) : value.Extensions is { } incoming ?
                    new Dictionary<string, System.Text.Json.JsonElement>(incoming) : null
            };
        }
    }
    // Starting configuration is a one-way authoring action, never a toggle
    // that relinquishes ownership. Existing states keep every field intact.
    public void ConfigureRapidTriggerDraftForKeys(IEnumerable<ushort> logicalIds, ProfileRapidTrigger initialValue)
    {
        if (_draft is null || IsBusy) return;
        var ids = logicalIds.Distinct().Order().ToArray();
        if (ids.Any(id => MagneticKeyLayout.Find(id) is null))
            throw new ArgumentException("Unsupported logical key", nameof(logicalIds));
        foreach (var id in ids) {
            if (_draft.Magnetic.Keys.Any(k => k.LogicalId == id && k.RapidTrigger is not null)) continue;
            // Recover an accidentally removed draft object only from the same
            // canonical Profile. No SessionApplied/device-state inference.
            var saved = Profiles.FirstOrDefault(p => p.Id == _draft.Id)?.Magnetic.Keys
                .FirstOrDefault(k => k.LogicalId == id)?.RapidTrigger;
            SetRapidTriggerDraftForKeys([id], saved ?? initialValue with { Enabled = false });
        }
        ReconcileDraftValidationNotice();
    }
    public void SetRapidTriggerEnabledForKeys(IEnumerable<ushort> logicalIds, bool enabled)
    {
        if (_draft is null || IsBusy) return;
        var ids = logicalIds.ToHashSet();
        foreach (var key in _draft.Magnetic.Keys.Where(k => ids.Contains(k.LogicalId)))
            if (key.RapidTrigger is { } rt) key.RapidTrigger = rt with { Enabled = enabled };
        ReconcileDraftValidationNotice();
    }
    public bool IsBusy { get; private set; }
    // Authoring knowledge is explicit document intent, not device readback.
    // The runtime may also know Standard from a current-session submission; the
    // editor cannot infer that from an absent DKS object or a physical RT gate.
    public ProfileDks? DraftDksForKey(ushort id) =>
        _draft?.Magnetic.Keys.FirstOrDefault(k => k.LogicalId == id)?.Dks ??
        (_snapshot?.EffectiveGlobalDefaults ?? _snapshot?.GlobalDefaults)?.Keys
            .FirstOrDefault(k => k.LogicalId == id)?.Dks;
    public IReadOnlyList<ushort> RtUnknownDksKeys => _draft?.Magnetic.Keys
        .Where(k => !IsHardwareSlotDraft && k.RapidTrigger?.Enabled == true && DraftDksForKey(k.LogicalId) is null)
        .Select(k => k.LogicalId).Distinct().Order().ToArray() ?? [];
    public IReadOnlyList<ushort> RtConflictingDksKeys => _draft?.Magnetic.Keys
        .Where(k => !IsHardwareSlotDraft && k.RapidTrigger?.Enabled == true && DraftDksForKey(k.LogicalId) is { Standard: false })
        .Select(k => k.LogicalId).Distinct().Order().ToArray() ?? [];
    public string RtDksAuthoringText(IEnumerable<ushort>? selection = null)
    {
        var ids = selection?.ToHashSet();
        var unknown = RtUnknownDksKeys.Count(id => ids is null || ids.Contains(id));
        var conflict = RtConflictingDksKeys.Count(id => ids is null || ids.Contains(id));
        return string.Join("\n", new[] {
            unknown > 0 ? $"{unknown} 个按键尚未在此配置文件或基础设置中明确设为标准模式。请明确设置后再启用快速触发。" : "",
            conflict > 0 ? $"{conflict} 个按键已配置 DKS。快速触发与 DKS 不能同时启用。" : ""
        }.Where(message => message.Length > 0));
    }
    public void SetSelectedKeysStandard(IEnumerable<ushort> logicalIds)
    {
        if (_draft is null || IsBusy) return;
        var ids = logicalIds.Distinct().Order().ToArray();
        if (ids.Any(id => MagneticKeyLayout.Find(id) is null))
            throw new ArgumentException("Unsupported logical key", nameof(logicalIds));
        foreach (var id in ids) {
            var key = _draft.Magnetic.Keys.FirstOrDefault(k => k.LogicalId == id);
            if (key is null) { key = new ProfileKey { LogicalId = id }; _draft.Magnetic.Keys.Add(key); }
            key.Dks = new ProfileDks(1, 3.6, Enumerable.Range(0, 4).Select(_ => new ProfileDksSlot()).ToList(), true);
        }
        ReconcileDraftValidationNotice();
    }
    public void KeepDksAndDisableRt(IEnumerable<ushort> logicalIds)
    {
        if (_draft is null || IsBusy) return;
        var ids = logicalIds.ToHashSet();
        foreach (var key in _draft.Magnetic.Keys.Where(k => ids.Contains(k.LogicalId)))
            if (DraftDksForKey(key.LogicalId) is { Standard: false } && key.RapidTrigger is { } rt)
                key.RapidTrigger = rt with { Enabled = false }; // keep managed object and parameters
        ReconcileDraftValidationNotice();
    }
    public bool HasUnsavedChanges => _draft is not null && _savedJson != DraftJson(_draft);
    public bool HasConflict => ConflictedProfileId is not null;
    public ProfilePageState State { get; private set; } = ProfilePageState.Loading;
    private string _notice = "";
    public long NoticeSequence { get; private set; }
    public string Notice {
        get => _notice;
        private set { if (_notice != value) { _notice = value; NoticeSequence++; } }
    }
    public ProfileNoticeKind NoticeKind { get; private set; }
    private bool _rtValidationNotice;
    private void PublishNotice(string message, ProfileNoticeKind kind)
    {
        _rtValidationNotice = false;
        _notice = message; NoticeKind = kind; NoticeSequence++;
    }
    private void ShowRtDksBlocker()
    {
        var message = RtDksAuthoringText();
        if (Notice != message || NoticeKind != ProfileNoticeKind.Error) {
            Notice = message; NoticeKind = ProfileNoticeKind.Error;
        }
        _rtValidationNotice = true;
    }
    public void ReconcileDraftValidationNotice()
    {
        if (!_rtValidationNotice) return;
        if (RtDksAuthoringText().Length > 0) ShowRtDksBlocker();
        else { _rtValidationNotice = false; Notice = ""; NoticeKind = ProfileNoticeKind.None; }
    }
    public string? ApplyDetails { get; private set; }
    public string CurrentName => Profiles.FirstOrDefault(p => p.Id == _snapshot?.SelectedProfileId)?.Name ?? "—";
    public string EditingName => _draft?.Name ?? "—";
    public int OverrideCount => _draft?.Magnetic.Keys.Count(k => HasOverride(k)) ?? 0;
    public bool CanDeleteEditing => _draft is not null && Profiles.Count > 1 &&
        _draft.Id != _snapshot?.SelectedProfileId && _draft.Id != _snapshot?.ActiveProfileId && !IsBusy;
    public IReadOnlyList<ProfileValidationIssue> RapidTriggerIssues => _draft is null || IsHardwareSlotDraft ? [] :
        ProfileValidator.RapidTriggerIssues(_draft.Magnetic).Select(issue => issue.LogicalId is ushort id ?
            issue with { Message = issue.Message.Replace($"按键 {id}",
                $"按键 {MagneticKeyLayout.Keys.FirstOrDefault(key => key.LogicalId == id)?.Label ?? id.ToString()}") } : issue).ToList();
    public string RapidTriggerValidationText => string.Join("\n", RapidTriggerIssues.Select(issue => issue.Message));
    public string DraftValidationText {
        get {
            if (_draft is null) return "";
            var messages = RapidTriggerIssues.Select(issue => issue.Message).ToList();
            if (RtDksAuthoringText() is { Length: > 0 } safety) messages.Add(safety);
            try { ValidateDraft(_draft); }
            catch (InvalidDataException ex) {
                if (messages.Count == 0 || !messages.Contains(ex.Message) &&
                    !ex.Message.Contains("DKS and rapid", StringComparison.OrdinalIgnoreCase))
                    messages.Add(EditorValidationMessage(ex.Message));
            }
            return string.Join("\n", messages);
        }
    }
    private static string EditorValidationMessage(string error) => error switch {
        "Invalid global actuation." or "Invalid key actuation." => "触发点须为 0.1–4.0 mm，步长为 0.1 mm。",
        "Invalid deadzone." => "顶部、底部死区须为 0.0–0.5 mm，步长为 0.1 mm。",
        "DKS and rapid trigger conflict." => "同一按键不能同时启用自定义 DKS 和快速触发。",
        "Invalid DKS thresholds or slots." => "请检查 DKS 触发点顺序和四段设置。",
        "Invalid DKS target or trigger." => "请检查 DKS 目标按键和触发动作。",
        _ when error.Contains("快速触发", StringComparison.Ordinal) || error.Contains("同步模式", StringComparison.Ordinal) => error,
        _ => "草稿未通过配置检查，请检查配置文件名称、数值和逐键设置。"
    };
    public bool CanApply {
        get {
            if (_snapshot is null || _draft is null || IsBusy || HasConflict ||
                State is ProfilePageState.Loading or ProfilePageState.DaemonUnavailable or
                    ProfilePageState.IncompatibleDaemon or ProfilePageState.ApiProtocolError or
                    ProfilePageState.DocumentUnavailable) return false;
            try { ValidateDraft(_draft); }
            catch (InvalidDataException) { return false; }
            if (RtUnknownDksKeys.Count > 0 || RtConflictingDksKeys.Count > 0) return false;
            return HasUnsavedChanges || _snapshot.Dirty ||
                _snapshot.ActiveProfileId != _draft.Id ||
                _snapshot.SelectedProfileId != _draft.Id;
        }
    }

    public string StateTitle => State switch {
        ProfilePageState.Loading => "正在加载配置文件",
        ProfilePageState.Ready => "已应用到键盘",
        ProfilePageState.NeedsApply => "配置需要重新应用",
        ProfilePageState.Deferred => "应用已延后",
        ProfilePageState.ApplyFailed => "应用未完成",
        ProfilePageState.DaemonUnavailable => "Aura 后台服务不可用",
        ProfilePageState.IncompatibleDaemon => "Aura 后台版本不兼容",
        ProfilePageState.ApiProtocolError => "配置文件服务响应异常",
        ProfilePageState.DocumentUnavailable => "配置文件暂时无法读取",
        _ => "配置状态未知"
    };
    public string StateDescription => State switch {
        ProfilePageState.Loading => "正在从 Aura 后台读取配置…",
        ProfilePageState.Ready when _snapshot?.HardwareSlotStatus?.ProfileActivationBackend == "hardware_slot" =>
            $"已确认键盘当前板载槽位：{_snapshot.HardwareSlotStatus.ObservedHardwareSlot?.ToString() ?? "未知"}。槽位内容由 ASUS 软件管理。",
        ProfilePageState.Ready => "当前配置已在本次连接中提交。",
        ProfilePageState.NeedsApply => "保存的配置尚未在本次连接中完整应用，请手动重试。",
        ProfilePageState.Deferred => "上次应用时键盘不可用。配置已保存，请点击“重试应用”再次尝试。",
        ProfilePageState.ApplyFailed => "配置已保存，但未完整应用。可查看详情并重试。",
        ProfilePageState.DaemonUnavailable => "请检查 Aura 后台服务，然后重试连接。",
        ProfilePageState.IncompatibleDaemon => "当前后台服务不支持配置文件功能。请退出旧后台服务并重启 Aura。",
        ProfilePageState.ApiProtocolError => "后台返回了无法识别的配置文件响应。请重试连接；若持续出现，请检查后台版本。",
        ProfilePageState.DocumentUnavailable => "Aura 后台无法读取配置文件，请检查详细错误并重试。",
        _ => ""
    };

    public static bool HasOverride(ProfileKey key) => key.ActuationMm is not null ||
        key.Deadzone is not null || key.RapidTrigger is not null || key.Dks is not null;

    public async Task LoadAsync(CancellationToken token = default)
    {
        // Reads and local mutations share this UI admission boundary. A poll may
        // not land an older response over a locally acknowledged mutation.
        if (IsBusy) return;
        IsBusy = true;
        try {
            var result = await client.ListAsync(token);
            token.ThrowIfCancellationRequested();
            AcceptList(result, preserveDraft: true);
            if (HasConflict) ShowConflict();
            else ReconcileDraftValidationNotice(); // polling is state, not error dismissal
        }
        catch (ProfileApiException ex) { HandleApiError(ex); }
        finally { IsBusy = false; }
    }

    public bool Edit(Guid id)
    {
        // One draft at a time; the page asks before discarding it. Conflict
        // metadata belongs to its GUID and cannot follow a new editor target.
        if (HasUnsavedChanges && !HasConflict) return false;
        var profile = Profiles.FirstOrDefault(p => p.Id == id);
        if (profile is null) return false;
        _draft = profile.Clone();
        Rebase(profile);
        _rtValidationNotice = false;
        Notice = ""; NoticeKind = ProfileNoticeKind.None;
        return true;
    }

    public void KeepDraftAfterConflict()
    {
        if (!HasConflict || _draft is null) return;
        var current = Profiles.FirstOrDefault(p => p.Id == _draft.Id);
        if (current is null) { ShowConflict(); return; }
        Rebase(current); // Accept latest canonical base, never the draft as saved.
        Notice = "已保留草稿，并以最新版本为基础。检查后可保存或应用。";
        NoticeKind = ProfileNoticeKind.Information;
    }

    public void DiscardDraft()
    {
        if (_draft is null) return;
        var current = Profiles.FirstOrDefault(p => p.Id == _draft.Id);
        _draft = current?.Clone();
        Rebase(current);
        _rtValidationNotice = false;
        Notice = ""; NoticeKind = ProfileNoticeKind.None;
    }

    private static string DefaultsJson(ProfileApiResponse snapshot) => JsonSerializer.Serialize(
        new { snapshot.GlobalDefaults, snapshot.EffectiveGlobalDefaults }, ProfileJson.Options);
    private static bool SameContent(string? left, string? right) => left == right ||
        left is not null && right is not null && JsonNode.DeepEquals(JsonNode.Parse(left), JsonNode.Parse(right));
    private void Rebase(DeviceProfile? canonical)
    {
        _baseProfileJson = canonical is null ? null : DraftJson(canonical);
        _savedJson = _baseProfileJson;
        _baseDefaultsJson = _snapshot is null ? null : DefaultsJson(_snapshot);
        BaseDocumentRevision = _snapshot?.DocumentRevision ?? 0;
        ConflictedProfileId = null;
    }
    private void ShowConflict()
    {
        _rtValidationNotice = false; // document conflict owns its independent resolution
        Notice = EditingProfileDeleted ? "正在编辑的配置文件已被删除。草稿已保留，请放弃草稿或选择其他配置文件。" :
            "正在编辑的配置或基础设置已在其他位置发生变化，已重新加载最新版本。草稿已保留，请决定是否继续使用。";
        NoticeKind = ProfileNoticeKind.Warning;
    }
    private async Task<bool> ReconcileBeforeWriteAsync(CancellationToken token)
    {
        var latest = await client.ListAsync(token);
        token.ThrowIfCancellationRequested();
        AcceptList(latest, preserveDraft: true);
        if (HasConflict) { ShowConflict(); return false; }
        return true;
    }

    public async Task<bool> SaveAsync(CancellationToken token = default)
    {
        if (_draft is null || _snapshot is null || HasConflict || IsBusy) return false;
        try {
            ValidateDraft(_draft);
            var priorLegacy = Profiles.FirstOrDefault(p => p.Id == _draft.Id)?.Magnetic.GlobalRapidTrigger;
            if (_draft.Magnetic.GlobalRapidTrigger is { } legacy &&
                JsonSerializer.Serialize(legacy, ProfileJson.Options) != JsonSerializer.Serialize(priorLegacy, ProfileJson.Options)) {
                Notice = "不能新增或修改旧版快速触发参数。请使用逐键设置；已有旧版参数可保留或移除。";
                NoticeKind = ProfileNoticeKind.Warning;
                return false;
            }
            IsBusy = true;
            if (!await ReconcileBeforeWriteAsync(token)) return false;
            var result = await client.UpdateAsync(_draft.Clone(), _snapshot.DocumentRevision, token);
            AcceptMutation(result);
            // Successful Save acknowledges the server's canonical representation
            // as both draft and base, including normalization/extension metadata.
            if (result.Profile is not null) _draft = result.Profile.Clone();
            Rebase(Profiles.FirstOrDefault(p => p.Id == _draft.Id));
            PublishNotice("已保存配置；尚未应用到键盘。", ProfileNoticeKind.Success);
            return true;
        }
        catch (ProfileApiException ex) { await HandleMutationErrorAsync(ex, token); return false; }
        catch (InvalidDataException ex) {
            // Live editor validation owns draft errors; this notice cannot linger
            // after a toggle/value change or after selecting another Profile.
            Notice = "";
            ApplyDetails = ex.Message;
            NoticeKind = ProfileNoticeKind.Warning; return false;
        }
        finally { IsBusy = false; }
    }

    public async Task<bool> ApplyAsync(Guid? id = null, CancellationToken token = default)
    {
        if (_snapshot is null || HasConflict || IsBusy) return false;
        if ((id is null || id == EditingId) && RtDksAuthoringText().Length > 0) {
            ShowRtDksBlocker(); return false;
        }
        if (HasUnsavedChanges && !await SaveAsync(token)) return false;
        if (_snapshot is null) return false;
        var target = id ?? _draft?.Id ?? _snapshot.SelectedProfileId;
        try {
            IsBusy = true;
            if (!await ReconcileBeforeWriteAsync(token)) return false;
            var result = await client.ActivateAsync(target, ProfileActivationReason.Manual,
                _snapshot.DocumentRevision, token: token);
            AcceptRuntime(result);
            if (result.Outcome == "succeeded") {
                // Event identity belongs to this accepted manual request, never
                // to selected/active snapshots or a polling render.
                PublishNotice("配置已应用到键盘。", ProfileNoticeKind.Success);
                return true;
            }
            if (result.Outcome == "deferred") {
                PublishNotice("配置已保存；键盘可用后请手动重试应用。", ProfileNoticeKind.Information);
            } else {
                var message = IsQuarantine(result.Error) ?
                    "磁轴写入处于安全隔离状态，应用已停止。" :
                    result.Error?.Contains("Device global baseline unknown", StringComparison.OrdinalIgnoreCase) == true ?
                    "设备全局基础设置尚未确认。请先在磁轴设置页应用全局值，再重试配置文件。" :
                    result.Error?.Contains("Legacy global RT", StringComparison.OrdinalIgnoreCase) == true ?
                    "配置中包含尚不支持应用的旧版快速触发参数。请检查旧版参数或改用逐键设置。" :
                    result.Error?.Contains("RT prior state unknown", StringComparison.OrdinalIgnoreCase) == true ?
                    "无法停止管理该按键的快速触发，因为接管前状态未知。请恢复可信的逐键设置后重试。" :
                    result.Error?.Contains("RT needs known Standard DKS", StringComparison.OrdinalIgnoreCase) == true ?
                    "无法确认所选按键为标准模式。请明确设置为标准模式后再启用快速触发。" :
                    result.Error?.Contains("DKS and RT conflict", StringComparison.OrdinalIgnoreCase) == true ?
                    "所选按键存在 DKS 与快速触发冲突，未改写 DKS。" :
                    "配置已保存，但应用未完成。";
                PublishNotice(message, ProfileNoticeKind.Error);
                _rtValidationNotice = (result.Error?.Contains("RT needs known Standard DKS") == true ||
                    result.Error?.Contains("DKS and RT conflict") == true) && RtDksAuthoringText().Length > 0;
            }
            return false;
        }
        catch (ProfileApiException ex) { await HandleMutationErrorAsync(ex, token); return false; }
        finally { IsBusy = false; }
    }

    public async Task<bool> CreateAsync(string name, CancellationToken token = default) =>
        await MutateAsync(rev => client.CreateAsync(name, rev, token), token);
    public async Task<bool> DuplicateAsync(Guid id, string name, CancellationToken token = default) =>
        await MutateAsync(rev => client.DuplicateAsync(id, name, rev, token), token);
    public async Task<bool> RenameAsync(Guid id, string name, CancellationToken token = default) =>
        await MutateAsync(rev => client.RenameAsync(id, name, rev, token), token);
    public async Task<bool> DeleteAsync(Guid id, CancellationToken token = default)
    {
        if (Profiles.Count <= 1 || id == _snapshot?.SelectedProfileId ||
            id == _snapshot?.ActiveProfileId) {
            Notice = "不能删除当前选择、正在使用或最后一个配置文件。";
            NoticeKind = ProfileNoticeKind.Warning; return false;
        }
        return await MutateAsync(rev => client.DeleteAsync(id, rev, token), token);
    }

    private async Task<bool> MutateAsync(Func<long, Task<ProfileApiResponse>> call, CancellationToken token)
    {
        if (_snapshot is null || IsBusy || HasConflict || HasUnsavedChanges) return false;
        try {
            IsBusy = true;
            var result = await call(_snapshot.DocumentRevision);
            AcceptMutation(result);
            await ReloadAfterMutationAsync(result.Profile?.Id, token);
            PublishNotice("配置文件已更新。", ProfileNoticeKind.Success);
            return true;
        }
        catch (ProfileApiException ex) { await HandleMutationErrorAsync(ex, token); return false; }
        finally { IsBusy = false; }
    }

    private async Task ReloadAfterMutationAsync(Guid? editId, CancellationToken token)
    {
        var result = await client.ListAsync(token);
        AcceptList(result, preserveDraft: false);
        if (editId is Guid id) Edit(id);
    }

    private async Task HandleMutationErrorAsync(ProfileApiException ex, CancellationToken token)
    {
        if (ex.RevisionConflict) {
            try {
                var latest = await client.ListAsync(token);
                token.ThrowIfCancellationRequested();
                AcceptList(latest, preserveDraft: true);
                if (HasConflict) ShowConflict();
                else {
                    Notice = "配置版本已更新，草稿已保留并使用最新基础版本。请重试保存或应用。";
                    NoticeKind = ProfileNoticeKind.Information;
                }
            } catch (ProfileApiException reloadError) {
                // No canonical revision is available: do not permit stale writes.
                ConflictedProfileId = _draft?.Id;
                Notice = "配置发生冲突，且暂时无法重新加载：" + reloadError.Message;
                NoticeKind = ProfileNoticeKind.Warning;
            }
        } else HandleApiError(ex);
    }

    private void HandleApiError(ProfileApiException ex)
    {
        _rtValidationNotice = false;
        State = ex.IncompatibleDaemon ? ProfilePageState.IncompatibleDaemon :
            ex.DaemonUnavailable ? ProfilePageState.DaemonUnavailable :
            ex.Category == ProfileApiErrorCategory.InvalidResponse ? ProfilePageState.ApiProtocolError :
            ex.StatusCode == HttpStatusCode.ServiceUnavailable ?
                ProfilePageState.DocumentUnavailable : ProfilePageState.ApplyFailed;
        Notice = ex.IncompatibleDaemon ? "当前 Aura 后台服务版本不支持配置文件功能。请退出旧后台服务并重启 Aura。" :
            ex.DaemonUnavailable ? "Aura 后台服务不可用，请重试连接。" :
            ex.Category == ProfileApiErrorCategory.InvalidResponse ? "配置文件服务返回了无效响应；请重试连接。" :
            ex.StatusCode == HttpStatusCode.ServiceUnavailable ? "配置文件暂时无法读取，请检查后台日志并重试。" :
            ex.StatusCode == HttpStatusCode.UnprocessableEntity ? "配置内容未通过后台检查，请检查磁轴设置。" :
            "Aura 后台处理配置时发生错误，请查看详细信息。";
        ApplyDetails = ex.Message;
        NoticeKind = ProfileNoticeKind.Error;
    }

    private void AcceptMutation(ProfileApiResponse result)
    {
        if (_snapshot is null) return;
        _snapshot.DocumentRevision = result.DocumentRevision;
        _snapshot.MutationRevision = result.MutationRevision;
        _snapshot.RuntimeRevision = result.RuntimeRevision;
        _snapshot.ActiveProfileId = result.ActiveProfileId;
        _snapshot.SelectedProfileId = result.SelectedProfileId;
        _snapshot.Dirty = result.Dirty;
        _snapshot.HardwareRtGate = result.HardwareRtGate;
        _snapshot.HardwareSlotStatus = result.HardwareSlotStatus;
        _snapshot.M605SessionGeneration = result.M605SessionGeneration;
        if (result.Profile is not null && _snapshot.Profiles is not null) {
            var index = _snapshot.Profiles.FindIndex(p => p.Id == result.Profile.Id);
            if (index >= 0) _snapshot.Profiles[index] = result.Profile;
            else _snapshot.Profiles.Add(result.Profile);
        }
        State = StateFrom(_snapshot);
    }

    private void AcceptRuntime(ProfileApiResponse result)
    {
        if (_snapshot is null) return;
        _snapshot.DocumentRevision = result.DocumentRevision;
        _snapshot.MutationRevision = result.MutationRevision;
        _snapshot.RuntimeRevision = result.RuntimeRevision;
        _snapshot.SelectedProfileId = result.SelectedProfileId;
        _snapshot.ActiveProfileId = result.ActiveProfileId;
        _snapshot.Dirty = result.Dirty;
        _snapshot.Outcome = result.Outcome;
        _snapshot.Error = result.Error;
        _snapshot.Operations = result.Operations;
        _snapshot.HardwareSlotStatus = result.HardwareSlotStatus;
        _snapshot.M605SessionGeneration = result.M605SessionGeneration;
        ApplyDetails = result.Operations is { Count: > 0 } ? string.Join("\n", result.Operations.Select(op =>
            $"{op.Kind} · {op.Identity}：{(op.Succeeded ? "成功" : "失败")}" +
            (string.IsNullOrWhiteSpace(op.Error) ? "" : $" · {op.Error}"))) : result.Error;
        BaseDocumentRevision = result.DocumentRevision;
        State = StateFrom(_snapshot);
    }

    private void AcceptList(ProfileApiResponse result, bool preserveDraft)
    {
        if (_snapshot is not null && result.DocumentRevision < _snapshot.DocumentRevision) return;
        var prior = _draft;
        var dirty = HasUnsavedChanges;
        var current = result.Profiles?.FirstOrDefault(p => p.Id == prior?.Id);
        var relevantChanged = prior is not null &&
            (current is null || !SameContent(_baseProfileJson, DraftJson(current)) ||
             prior.ActivationBackend != "hardware_slot" && !SameContent(_baseDefaultsJson, DefaultsJson(result)));
        _snapshot = result;
        State = StateFrom(result);
        if (prior is not null && preserveDraft) {
            if (current is null || relevantChanged && (dirty || HasConflict)) {
                ConflictedProfileId = prior.Id;
                ShowConflict();
                return;
            }
            // Unrelated revision changes transparently rebase the preserved draft.
            // A clean editor instead accepts the latest relevant content.
            if (!dirty && !HasConflict) _draft = current!.Clone();
            if (!HasConflict) Rebase(current);
        } else {
            _draft = current?.Clone() ?? result.Profiles?.FirstOrDefault(p => p.Id == result.SelectedProfileId)?.Clone();
            Rebase(_draft);
        }
    }

    private static ProfilePageState StateFrom(ProfileApiResponse result) =>
        (result.Outcome ?? result.LastApplyOutcome?.Outcome) switch {
        "deferred" => ProfilePageState.Deferred,
        "failed" => ProfilePageState.ApplyFailed,
        _ when result.Dirty || result.ActiveProfileId != result.SelectedProfileId => ProfilePageState.NeedsApply,
        _ => ProfilePageState.Ready
    };
    private static bool IsQuarantine(string? error) => error?.Contains("quarantin", StringComparison.OrdinalIgnoreCase) == true ||
        error?.Contains("unhealthy", StringComparison.OrdinalIgnoreCase) == true;
    private static string DraftJson(DeviceProfile profile) => JsonSerializer.Serialize(profile, ProfileJson.Options);
    private static void ValidateDraft(DeviceProfile draft) => ProfileValidator.Validate(new ProfileDocument {
        SelectedProfileId = draft.Id, Profiles = [draft]
    });
}
