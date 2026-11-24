using System.Text.Json;
using System.Text.Json.Serialization;

namespace Aura_WinUI.Services;

// Transport/draft models only. No evaluator, runtime truth, file I/O or activation.
public sealed class DeviceProfileAutomationConfig
{
    public int SchemaVersion { get; set; } = 1;
    public bool Enabled { get; set; }
    public Guid? FallbackProfileId { get; set; }
    public List<DeviceProfileProcessBinding> Bindings { get; set; } = [];
    [JsonExtensionData] public Dictionary<string, JsonElement>? Extensions { get; set; }
    public DeviceProfileAutomationConfig Clone() => JsonSerializer.Deserialize<DeviceProfileAutomationConfig>(
        JsonSerializer.Serialize(this, ProfileJson.Options), ProfileJson.Options)!;
}
public sealed class DeviceProfileProcessBinding
{
    public Guid RuleId { get; set; } = Guid.NewGuid();
    public bool Enabled { get; set; } = true;
    public string ProcessName { get; set; } = "";
    public Guid ProfileId { get; set; }
    public int Priority { get; set; } = 100;
    [JsonExtensionData] public Dictionary<string, JsonElement>? Extensions { get; set; }
}
public sealed class DeviceProfileAutomationDecision
{
    public bool Enabled { get; set; }
    public bool ConfigurationAvailable { get; set; } = true;
    public string? ForegroundProcess { get; set; }
    public bool DebouncePending { get; set; }
    public bool ManualHold { get; set; }
    public string? ManualHoldForeground { get; set; }
    public Guid? ResolvedProfileId { get; set; }
    public Guid? MatchedRuleId { get; set; }
    public string DecisionKind { get; set; } = "NoDecision";
    public string DecisionReason { get; set; } = "NotInitialized";
    public ulong DecisionSequence { get; set; }
    public bool HardwareActivationAllowed { get; set; }
    public string HardwareBlockReason { get; set; } = "PhaseHardwareActivationDisabled";
    public List<string> HardwareBlockReasons { get; set; } = [];
    public string CoordinatorState { get; set; } = "WaitingForDecision";
    public string ActivationState { get; set; } = "Idle";
    public Guid? ActivationTarget { get; set; }
    public string? ActivationOutcome { get; set; }
    public string? ActivationError { get; set; }
    public ulong ActivationAttempt { get; set; }
    public long ConfigurationDocumentRevision { get; set; }
}
public sealed record DeviceProfileTargetChoice(Guid? Id, string Label);
public sealed record DeviceProfileRuleRow(Guid RuleId, string ProcessName, string Description)
{
    public string EditId => $"DeviceAutomationEdit{RuleId:D}";
    public string DeleteId => $"DeviceAutomationDelete{RuleId:D}";
    public string EditName => $"编辑 {ProcessName} 的规则";
    public string DeleteName => $"删除 {ProcessName} 的规则";
}

public sealed class DeviceProfileAutomationModel(IProfileControlClient client)
{
    private DeviceProfileAutomationConfig? _saved;
    private string? _savedJson;
    public DeviceProfileAutomationConfig? Draft { get; private set; }
    public IReadOnlyList<DeviceProfile> Profiles { get; private set; } = [];
    public DeviceProfileAutomationDecision? Decision { get; private set; }
    public long DocumentRevision { get; private set; }
    public bool IsBusy { get; private set; }
    public bool IsAvailable { get; private set; }
    public bool NeedsConfigurationRefresh => Decision is not null && Decision.ConfigurationDocumentRevision != 0 &&
        Decision.ConfigurationDocumentRevision != DocumentRevision;
    public bool HasConflict { get; private set; }
    public bool CanCreateHardware => IsAvailable && !IsBusy && !HasConflict && HardwareCreateSupported;
    public bool HardwareCreateSupported { get; private set; }
    public bool SupportsIndependentLighting { get; private set; }
    public string LightingCompatibilityNotice => "当前后台未声明独立灯光控制支持。板载磁轴 + Aura 灯光等独立组合不能保存为自动切换目标，请更新并重启匹配版本。";
    public string HardwareCreateHint => HardwareCreateSupported ?
        "创建只保存 Profile 引用，不改写板载磁轴参数。规则仍需单独保存。" :
        "当前后台不支持原子创建板载配置或独立灯光控制，请更新并重启匹配版本。";
    public string Notice { get; private set; } = "";
    public ProfileNoticeKind NoticeKind { get; private set; }
    public bool HasUnsavedChanges => Draft is not null && Serialize(Draft) != _savedJson;
    public bool CanSave => IsAvailable && Draft is not null && !IsBusy && !HasConflict && HasUnsavedChanges;
    public string Foreground => Decision?.ForegroundProcess ?? "尚未检测到前台程序";
    public string WouldSelect => Decision?.ResolvedProfileId is { } id ? ProfileName(id) : "不选择配置文件";
    public string MatchedRule => Decision?.MatchedRuleId is { } id ?
        _saved?.Bindings.FirstOrDefault(r => r.RuleId == id)?.ProcessName ?? "规则已不存在" : "无匹配规则";
    public string ActivationStatus => !IsAvailable ? "无法读取自动应用状态" : Decision switch {
        { HardwareActivationAllowed: false } => "当前后台尚未启用自动应用到键盘",
        { Enabled: false } => "自动切换已关闭，当前键盘配置保持不变",
        { ManualHold: true } => "自动应用已暂停：你刚刚手动选择了配置文件",
        { DebouncePending: true } => "等待前台程序稳定",
        { DecisionKind: "InvalidDecision" } => "无法自动应用：引用的配置文件已不存在",
        { CoordinatorState: "Deferred" } => "等待键盘可用后重试应用",
        { CoordinatorState: "Activating" or "Preflight" } => $"正在应用：{ActivationTargetName}",
        { ActivationOutcome: "failed" } => "自动应用失败。请在配置文件页查看错误并处理后重试。",
        { CoordinatorState: "Blocked", HardwareBlockReasons: var reasons } when reasons.Contains("SafetyQuarantined") =>
            "自动应用已暂停：键盘处于安全隔离，请使用现有恢复流程处理。",
        { CoordinatorState: "Blocked", HardwareBlockReasons: var reasons } when reasons.Contains("M605Unhealthy") =>
            "自动应用已暂停：键盘连接状态尚未恢复正常。",
        { DecisionKind: "NoDecision" } => "当前没有自动应用目标，键盘配置保持不变",
        { ActivationOutcome: "succeeded" or "no-op" } => $"已提交到键盘：{ActivationTargetName}",
        { ActivationOutcome: "stale" or "cancelled" } => "匹配条件已变化，等待新的稳定匹配",
        _ => "自动应用已启用，等待稳定匹配"
    };
    private string ActivationTargetName => Decision?.ActivationTarget is { } id ? ProfileName(id) : WouldSelect;
    public string PreviewStatus => Decision is { ConfigurationAvailable: false } ? "自动切换配置暂不可用，请更新后台服务。" :
        !IsAvailable ? "无法读取自动切换状态" : Decision switch {
        { ConfigurationAvailable: false } => "自动切换配置暂不可用，请更新后台服务。",
        { Enabled: false } => "自动切换已关闭",
        { ManualHold: true, DecisionReason: "ManualHold" } => "已暂停自动匹配。你刚刚手动选择了配置文件；切换到其他程序（不包括 Aura）并保持前台后，自动匹配将恢复。",
        { DebouncePending: true } => "等待前台程序稳定…",
        { DecisionReason: "InvalidTargetProfile" } => "此规则或后备选项引用的配置文件已不存在",
        { DecisionKind: "Fallback" } => $"无匹配规则，将使用 {WouldSelect}",
        { DecisionKind: "Match" } => $"自动匹配：{WouldSelect}",
        { DecisionReason: "NoMatchingRule" } => "当前程序没有匹配规则",
        { DecisionReason: "NoForeground" } => "尚未检测到前台程序",
        { DecisionReason: "AutomationConfigurationUnavailable" } => "自动切换配置暂不可用，请更新后台服务。",
        { DecisionReason: "ProfileDocumentUnavailable" } => "配置文件暂时无法读取",
        _ => "正在读取匹配结果…"
    };
    public static string TargetLabel(DeviceProfile profile) =>
        $"{(profile.ActivationBackend == "hardware_slot" ? $"板载槽位 {profile.HardwareSlot}" : "Aura 管理")} · {profile.Name} · {(profile.EffectiveLightingOwner == "FirmwareBank" ? "板载灯光" : "Aura 动态灯光")}";
    public string ProfileName(Guid id) => Profiles.FirstOrDefault(p => p.Id == id) is { } profile ?
        TargetLabel(profile) : "配置文件已不存在";
    public IReadOnlyList<DeviceProfileRuleRow> Rules => Draft?.Bindings
        .OrderByDescending(r => r.Priority).ThenBy(r => r.RuleId.ToString("D"), StringComparer.Ordinal)
        .Select(r => new DeviceProfileRuleRow(r.RuleId, r.ProcessName,
            $"配置文件：{ProfileName(r.ProfileId)} · 优先级：{r.Priority} · {(r.Enabled ? "开启" : "关闭")}"))
        .ToList() ?? [];
    public IReadOnlyList<DeviceProfileTargetChoice> TargetChoices(Guid? current, bool includeNone)
    {
        var choices = Profiles.Select(p => new DeviceProfileTargetChoice(p.Id, TargetLabel(p))).ToList();
        if (current is { } id && !Profiles.Any(p => p.Id == id)) choices.Insert(0, new(id, "配置文件已不存在"));
        if (includeNone) choices.Insert(0, new(null, "不执行任何操作"));
        return choices;
    }
    public IReadOnlyList<DeviceProfileTargetChoice> SlotChoices(int slot) => Profiles
        .Where(p => p.ActivationBackend == "hardware_slot" && p.HardwareSlot == slot)
        .Select(p => new DeviceProfileTargetChoice(p.Id, TargetLabel(p))).ToList();

    // One atomic document mutation, followed by a separate explicit automation save.
    // Never retry a create whose response may have been lost.
    public async Task<Guid?> CreateHardwareAsync(string name, int slot, string owner, CancellationToken token = default)
    {
        if (!CanCreateHardware) { Notice = HardwareCreateHint; NoticeKind = ProfileNoticeKind.Warning; return null; }
        name = name.Trim();
        if (name.Length == 0 || System.Text.Encoding.UTF8.GetByteCount(name) > 100 || name.Any(char.IsControl) ||
            slot is < 1 or > 5 || owner is not ("AuraDirectRgb" or "FirmwareBank")) {
            Notice = "请输入有效名称（最多 100 字节）、槽位 1–5 和灯光来源。"; NoticeKind = ProfileNoticeKind.Warning; return null;
        }
        if (Profiles.Any(p => string.Equals(p.Name, name, StringComparison.OrdinalIgnoreCase))) {
            Notice = "名称已存在，请选择现有配置或使用不同名称。"; NoticeKind = ProfileNoticeKind.Warning; return null;
        }
        IsBusy = true;
        try {
            var response = await client.CreateHardwareAsync(name, slot, owner, DocumentRevision, token);
            var created = response.Profile;
            if (created is null || created.Id == Guid.Empty || Profiles.Any(p => p.Id == created.Id) ||
                created.Name != name || created.ActivationBackend != "hardware_slot" || created.HardwareSlot != slot ||
                created.Lighting?.ControlOwner != owner || response.DocumentRevision != DocumentRevision + 1)
                throw new ProfileApiException("Unexpected hardware Profile response", category: ProfileApiErrorCategory.InvalidResponse);
            Profiles = Profiles.Append(created).ToList(); DocumentRevision = response.DocumentRevision;
            Notice = "板载配置已创建；请选择并保留到草稿，再保存规则。取消编辑不会删除已创建的配置。";
            NoticeKind = ProfileNoticeKind.Information;
            return created.Id;
        } catch (ProfileApiException ex) {
            if (ex.RevisionConflict) {
                HasConflict = true;
                try { Accept(await client.GetAutomationAsync(token), preserveDraft: true); }
                catch (ProfileApiException reload) { ApiError(reload); }
                Notice = "创建发生修订冲突，未重试；草稿和输入已保留，请重新读取后确认。";
                NoticeKind = ProfileNoticeKind.Warning;
            } else if (ex.StatusCode == System.Net.HttpStatusCode.UnprocessableEntity) {
                Notice = "创建被拒绝（名称可能已存在或参数无效）。输入和草稿已保留，请检查后重试。";
                NoticeKind = ProfileNoticeKind.Warning;
            } else {
                ApiError(ex);
                Notice = "创建未确认：" + Notice + " 请重新读取并检查配置列表后再决定是否创建，勿直接重复请求。";
            }
            return null;
        } catch (OperationCanceledException) {
            IsAvailable = false; Decision = null;
            Notice = "创建请求已中断，结果未确认。输入和规则草稿已保留，请重新读取并检查配置列表；不会自动重试。";
            NoticeKind = ProfileNoticeKind.Warning; return null;
        } finally { IsBusy = false; }
    }
    public static string NormalizeProcessInput(string input)
    {
        var name = input.Trim();
        name = name[(Math.Max(name.LastIndexOf('\\'), name.LastIndexOf('/')) + 1)..];
        int bytes;
        try { bytes = new System.Text.UTF8Encoding(false, true).GetByteCount(name); }
        catch (System.Text.EncoderFallbackException) { throw new InvalidDataException("程序名称包含无效字符，请重新输入。"); }
        if (name.Length == 0 || bytes > 255 || name is "." or ".." ||
            name.EndsWith('.') || name.StartsWith(' ') || name.EndsWith(' ') ||
            name.Any(c => char.IsControl(c) || "<>:\"/\\|?*".Contains(c)))
            throw new InvalidDataException("请输入有效的程序名称，例如 cs2.exe。可粘贴路径，保存时只保留程序名。");
        return name.ToLowerInvariant(); // daemon remains authoritative for Windows normalization
    }
    public void SetEnabled(bool enabled) { if (Draft is not null) Draft.Enabled = enabled; }
    public void SetFallback(Guid? id) { if (Draft is not null) Draft.FallbackProfileId = id; }
    public void UpsertRule(Guid? ruleId, string input, Guid? target, double priority, bool enabled)
    {
        if (Draft is null) throw new InvalidDataException("自动切换配置暂不可用。");
        var process = NormalizeProcessInput(input);
        if (target is not { } id || id == Guid.Empty || !Profiles.Any(p => p.Id == id))
            throw new InvalidDataException("请选择一个仍然存在的配置文件。");
        if (!double.IsFinite(priority) || priority != Math.Truncate(priority) || priority < int.MinValue || priority > int.MaxValue)
            throw new InvalidDataException("优先级必须是有效整数。");
        var rule = ruleId is { } ruleGuid ? Draft.Bindings.FirstOrDefault(r => r.RuleId == ruleGuid) : null;
        if (ruleId is not null && rule is null) throw new InvalidDataException("规则已不存在，请重新读取配置。");
        if (rule is null) { rule = new(); Draft.Bindings.Add(rule); }
        rule.ProcessName = process; rule.ProfileId = id; rule.Priority = (int)priority; rule.Enabled = enabled;
    }
    public void DeleteRule(Guid id) => Draft?.Bindings.RemoveAll(r => r.RuleId == id);
    public void KeepDraftAfterConflict() { HasConflict = false; Notice = "草稿已保留，请检查最新配置后再保存。"; }
    public void DiscardDraft() { Draft = _saved?.Clone(); _savedJson = Draft is null ? null : Serialize(Draft); HasConflict = false; Notice = ""; }

    public async Task RefreshAsync(CancellationToken token = default)
    {
        if (IsBusy) return;
        IsBusy = true;
        try { Accept(await client.GetAutomationAsync(token), preserveDraft: HasUnsavedChanges || HasConflict); }
        catch (ProfileApiException ex) { ApiError(ex); }
        finally { IsBusy = false; }
    }
    public async Task RefreshDecisionAsync(CancellationToken token = default)
    {
        if (IsBusy || !IsAvailable) return;
        try {
            var response = await client.GetAutomationStatusAsync(token);
            token.ThrowIfCancellationRequested();
            Decision = response.AutomationDecision ?? throw new ProfileApiException("Missing automation status",
                category: ProfileApiErrorCategory.InvalidResponse);
            // Never consume a revision without its configuration snapshot.
        } catch (ProfileApiException ex) { ApiError(ex); }
    }
    public async Task<bool> SaveAsync(CancellationToken token = default)
    {
        if (!CanSave || Draft is null) return false;
        IsBusy = true;
        try {
            foreach (var r in Draft.Bindings) {
                r.ProcessName = NormalizeProcessInput(r.ProcessName);
                if (r.ProfileId == Guid.Empty || r.RuleId == Guid.Empty) throw new InvalidDataException("规则缺少有效的配置文件或规则标识。");
            }
            if (Draft.Bindings.Any(r => !Profiles.Any(p => p.Id == r.ProfileId)) ||
                Draft.FallbackProfileId is { } fallback && !Profiles.Any(p => p.Id == fallback))
                throw new InvalidDataException("目标配置文件已不存在，请重新选择规则和后备配置。");
            var targets = Draft.Bindings.Select(r => r.ProfileId).Append(Draft.FallbackProfileId ?? Guid.Empty);
            if (!SupportsIndependentLighting && Profiles.Any(p => targets.Contains(p.Id) &&
                p.EffectiveLightingOwner != (p.ActivationBackend == "hardware_slot" ? "FirmwareBank" : "AuraDirectRgb")))
                throw new InvalidDataException(LightingCompatibilityNotice);
            Accept(await client.UpdateAutomationAsync(Draft.Clone(), DocumentRevision, token), preserveDraft: false);
            Notice = Decision?.HardwareActivationAllowed == true && Draft?.Enabled == true ?
                "规则已保存，后台将按已保存的规则自动应用配置文件。" : "规则已保存。";
            NoticeKind = ProfileNoticeKind.Success;
            return true;
        }
        catch (ProfileApiException ex) {
            if (ex.RevisionConflict) {
                HasConflict = true;
                try { Accept(await client.GetAutomationAsync(token), preserveDraft: true); }
                catch (ProfileApiException reload) { ApiError(reload); }
                Notice = IsAvailable ? "配置已在其他位置发生变化，已重新读取。草稿已保留，请检查后决定是否重新保存。" :
                    "配置发生冲突，暂无法重新读取。草稿已保留，请恢复连接后重试。";
                NoticeKind = ProfileNoticeKind.Warning;
            } else {
                ApiError(ex);
                Notice = "规则保存未确认；草稿已保留。" + Notice + " 请重新读取并检查已保存规则后再决定是否重试。";
            }
            return false;
        }
        catch (OperationCanceledException) {
            IsAvailable = false; Decision = null;
            Notice = "规则保存请求已中断，结果未确认。草稿已保留，请重新读取并检查已保存规则；不会自动重试。";
            NoticeKind = ProfileNoticeKind.Warning; return false;
        }
        catch (InvalidDataException ex) { Notice = ex.Message; NoticeKind = ProfileNoticeKind.Warning; return false; }
        finally { IsBusy = false; }
    }
    private static string Serialize(DeviceProfileAutomationConfig config) => JsonSerializer.Serialize(config, ProfileJson.Options);
    private void Accept(ProfileApiResponse response, bool preserveDraft)
    {
        if (response.AutomationDecision is null)
            throw new ProfileApiException("Invalid automation response", category: ProfileApiErrorCategory.InvalidResponse);
        Profiles = response.Profiles ?? [];
        SupportsIndependentLighting = response.LightingControlOwnerSupported;
        HardwareCreateSupported = response.HardwareProfileCreateSupported && response.LightingControlOwnerSupported;
        Decision = response.AutomationDecision;
        if (response.AutomationConfigurationAvailable == false || !Decision.ConfigurationAvailable) {
            DocumentRevision = response.DocumentRevision; IsAvailable = false;
            Notice = "自动切换配置版本不兼容或内容无效，原数据已保留。配置文件仍可手动管理和应用。";
            NoticeKind = ProfileNoticeKind.Warning; return;
        }
        if (response.DeviceProfileAutomation is not { } raw)
            throw new ProfileApiException("Missing automation configuration", category: ProfileApiErrorCategory.InvalidResponse);
        DeviceProfileAutomationConfig config;
        try {
            config = raw.Deserialize<DeviceProfileAutomationConfig>(ProfileJson.Options) ?? throw new JsonException();
            if (config.SchemaVersion != 1) throw new JsonException();
        } catch (JsonException) { throw new ProfileApiException("Invalid automation schema", category: ProfileApiErrorCategory.InvalidResponse); }
        if (preserveDraft && DocumentRevision != response.DocumentRevision) {
            HasConflict = true; Notice = "配置已在其他位置更改。草稿已保留，请检查后再保存。"; NoticeKind = ProfileNoticeKind.Warning;
        }
        var recovering = !IsAvailable;
        _saved = config; _savedJson = Serialize(config);
        DocumentRevision = response.DocumentRevision; IsAvailable = true;
        if (!preserveDraft) { Draft = config.Clone(); _savedJson = Serialize(Draft); HasConflict = false; if (recovering) Notice = ""; }
        if (NoticeKind == ProfileNoticeKind.Error) { Notice = ""; NoticeKind = ProfileNoticeKind.None; }
    }
    private void ApiError(ProfileApiException error)
    {
        IsAvailable = false; Decision = null;
        Notice = error.DaemonUnavailable ? "无法读取自动切换状态，请恢复 Aura 后台服务后重试。" :
            error.IncompatibleDaemon ? "当前后台版本不支持设备配置文件规则，请重启 Aura 以加载匹配版本。" :
            "无法读取自动切换状态，后台响应异常或配置文件暂不可用，请重试。";
        NoticeKind = ProfileNoticeKind.Error;
    }
}
