using System.Net;
using System.Text.Json;

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

    public IReadOnlyList<DeviceProfile> Profiles => _snapshot?.Profiles ?? [];
    public ProfileApiResponse? Snapshot => _snapshot;
    public DeviceProfile? Draft => _draft;
    public Guid? EditingId => _draft?.Id;
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
    public bool IsBusy { get; private set; }
    public bool HasUnsavedChanges => _draft is not null && _savedJson != DraftJson(_draft);
    public bool HasConflict { get; private set; }
    public ProfilePageState State { get; private set; } = ProfilePageState.Loading;
    public string Notice { get; private set; } = "";
    public ProfileNoticeKind NoticeKind { get; private set; }
    public string? ApplyDetails { get; private set; }
    public string CurrentName => Profiles.FirstOrDefault(p => p.Id == _snapshot?.SelectedProfileId)?.Name ?? "—";
    public string EditingName => _draft?.Name ?? "—";
    public int OverrideCount => _draft?.Magnetic.Keys.Count(k => HasOverride(k)) ?? 0;
    public bool CanDeleteEditing => _draft is not null && Profiles.Count > 1 &&
        _draft.Id != _snapshot?.SelectedProfileId && _draft.Id != _snapshot?.ActiveProfileId && !IsBusy;
    public IReadOnlyList<ProfileValidationIssue> RapidTriggerIssues => _draft is null ? [] :
        ProfileValidator.RapidTriggerIssues(_draft.Magnetic).Select(issue => issue.LogicalId is ushort id ?
            issue with { Message = issue.Message.Replace($"按键 {id}",
                $"按键 {MagneticKeyLayout.Keys.FirstOrDefault(key => key.LogicalId == id)?.Label ?? id.ToString()}") } : issue).ToList();
    public string RapidTriggerValidationText => string.Join("\n", RapidTriggerIssues.Select(issue => issue.Message));
    public string DraftValidationText {
        get {
            if (_draft is null) return "";
            var messages = RapidTriggerIssues.Select(issue => issue.Message).ToList();
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
        IsBusy = true;
        try {
            var result = await client.ListAsync(token);
            var preserve = HasConflict || HasUnsavedChanges;
            var changedBehindDraft = preserve && _snapshot is not null &&
                _snapshot.DocumentRevision != result.DocumentRevision;
            AcceptList(result, preserveDraft: preserve);
            if (changedBehindDraft) {
                HasConflict = true;
                Notice = "配置已在其他位置发生变化。草稿已保留，请检查后决定是否重新保存。";
                NoticeKind = ProfileNoticeKind.Warning;
            } else if (!HasConflict) {
                Notice = ""; NoticeKind = ProfileNoticeKind.None;
            }
        }
        catch (ProfileApiException ex) { HandleApiError(ex); }
        finally { IsBusy = false; }
    }

    public bool Edit(Guid id)
    {
        if (HasUnsavedChanges || HasConflict) return false;
        var profile = Profiles.FirstOrDefault(p => p.Id == id);
        if (profile is null) return false;
        _draft = profile.Clone();
        _savedJson = DraftJson(_draft);
        return true;
    }

    public void KeepDraftAfterConflict()
    {
        if (!HasConflict) return;
        HasConflict = false;
        Notice = "已保留草稿。请检查最新配置后，再决定是否保存。";
        NoticeKind = ProfileNoticeKind.Warning;
    }

    public void DiscardDraft()
    {
        if (_draft is null) return;
        var current = Profiles.FirstOrDefault(p => p.Id == _draft.Id);
        _draft = current?.Clone();
        _savedJson = _draft is null ? null : DraftJson(_draft);
        HasConflict = false;
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
            var result = await client.UpdateAsync(_draft.Clone(), _snapshot.DocumentRevision, token);
            AcceptMutation(result);
            _savedJson = DraftJson(_draft);
            Notice = "已保存配置；尚未应用到键盘。";
            NoticeKind = ProfileNoticeKind.Success;
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
        if (HasUnsavedChanges && !await SaveAsync(token)) return false;
        if (_snapshot is null) return false;
        var target = id ?? _draft?.Id ?? _snapshot.SelectedProfileId;
        try {
            IsBusy = true;
            var result = await client.ActivateAsync(target, ProfileActivationReason.Manual,
                _snapshot.DocumentRevision, token: token);
            AcceptRuntime(result);
            if (result.Outcome == "succeeded") {
                Notice = "配置已应用到键盘。"; NoticeKind = ProfileNoticeKind.Success;
                return true;
            }
            if (result.Outcome == "deferred") {
                Notice = "配置已保存；键盘可用后请手动重试应用。";
                NoticeKind = ProfileNoticeKind.Information;
            } else {
                Notice = IsQuarantine(result.Error) ?
                    "磁轴写入处于安全隔离状态，应用已停止。" :
                    result.Error?.Contains("Device global baseline unknown", StringComparison.OrdinalIgnoreCase) == true ?
                    "设备全局基础设置尚未确认。请先在磁轴设置页应用全局值，再重试配置文件。" :
                    result.Error?.Contains("Legacy global RT", StringComparison.OrdinalIgnoreCase) == true ?
                    "配置中包含尚不支持应用的旧版快速触发参数。请检查旧版参数或改用逐键设置。" :
                    result.Error?.Contains("RT prior state unknown", StringComparison.OrdinalIgnoreCase) == true ?
                    "无法确认取消管理前的快速触发设置，未写入键盘。请恢复可信的逐键基础设置后重试。" :
                    result.Error?.Contains("RT needs known Standard DKS", StringComparison.OrdinalIgnoreCase) == true ?
                    "无法确认所选按键为标准模式。请明确设置为标准模式后再启用快速触发。" :
                    result.Error?.Contains("DKS and RT conflict", StringComparison.OrdinalIgnoreCase) == true ?
                    "所选按键存在 DKS 与快速触发冲突，未改写 DKS。" :
                    "配置已保存，但应用未完成。";
                NoticeKind = ProfileNoticeKind.Error;
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
            Notice = "配置文件已更新。"; NoticeKind = ProfileNoticeKind.Success;
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
            HasConflict = true;
            try {
                var latest = await client.ListAsync(token);
                AcceptList(latest, preserveDraft: true);
                Notice = "配置已在其他位置发生变化，已重新加载最新版本。草稿已保留，请检查后决定是否重新保存。";
            } catch (ProfileApiException reloadError) {
                Notice = "配置发生冲突，且暂时无法重新加载：" + reloadError.Message;
            }
            NoticeKind = ProfileNoticeKind.Warning;
        } else HandleApiError(ex);
    }

    private void HandleApiError(ProfileApiException ex)
    {
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
        ApplyDetails = result.Operations is { Count: > 0 } ? string.Join("\n", result.Operations.Select(op =>
            $"{op.Kind} · {op.Identity}：{(op.Succeeded ? "成功" : "失败")}" +
            (string.IsNullOrWhiteSpace(op.Error) ? "" : $" · {op.Error}"))) : result.Error;
        State = StateFrom(_snapshot);
    }

    private void AcceptList(ProfileApiResponse result, bool preserveDraft)
    {
        var editId = _draft?.Id ?? result.SelectedProfileId;
        var prior = preserveDraft ? _draft : null;
        _snapshot = result;
        State = StateFrom(result);
        if (prior is not null) _draft = prior;
        else {
            _draft = result.Profiles?.FirstOrDefault(p => p.Id == editId)?.Clone() ??
                result.Profiles?.FirstOrDefault(p => p.Id == result.SelectedProfileId)?.Clone();
            _savedJson = _draft is null ? null : DraftJson(_draft);
            HasConflict = false;
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
