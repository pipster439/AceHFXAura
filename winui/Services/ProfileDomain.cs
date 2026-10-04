using System.Text.Json;
using System.Text.Json.Serialization;

namespace Aura_WinUI.Services;

// Device profiles are distinct from config.json's existing lighting effect recipes.
// All values are desired state; none is a firmware readback.
public sealed class DeviceProfile
{
    public int SchemaVersion { get; set; } = 1;
    public Guid Id { get; set; } = Guid.NewGuid();
    public string Name { get; set; } = "New profile";
    public string ActivationBackend { get; set; } = "host_managed";
    public int? HardwareSlot { get; set; }
    public ProfileMagnetic Magnetic { get; set; } = new();
    public ProfileLighting Lighting { get; set; } = new();
    public ProfileAutomationBinding? Automation { get; set; }
    [JsonExtensionData] public Dictionary<string, JsonElement>? Extensions { get; set; }

    public DeviceProfile Clone() => JsonSerializer.Deserialize<DeviceProfile>(JsonSerializer.Serialize(this, ProfileJson.Options), ProfileJson.Options)!;
}

public sealed class ProfileMagnetic
{
    public double? GlobalActuationMm { get; set; }
    public ProfileDeadzone? GlobalDeadzone { get; set; }
    // Compatibility-only. The daemon migrates recognized legacy intent;
    // fresh explicit-per-key saves must not recreate even a null legacy field.
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public ProfileRapidTrigger? GlobalRapidTrigger { get; set; }
    public List<ProfileKey> Keys { get; set; } = [];
    [JsonExtensionData] public Dictionary<string, JsonElement>? Extensions { get; set; }
}

public sealed class ProfileKey
{
    public ushort LogicalId { get; set; }
    public double? ActuationMm { get; set; }
    public ProfileDeadzone? Deadzone { get; set; }
    public ProfileRapidTrigger? RapidTrigger { get; set; }
    public ProfileDks? Dks { get; set; }
    [JsonExtensionData] public Dictionary<string, JsonElement>? Extensions { get; set; }
}

public sealed record ProfileDeadzone(double TopMm, double BottomMm);
public sealed record ProfileRapidTrigger(bool Enabled, double PressMm, double ReleaseMm,
    bool SeparateMode = false, double? TopMm = null, double? BottomMm = null)
{
    [JsonExtensionData] public Dictionary<string, JsonElement>? Extensions { get; set; }
}
public sealed record ProfileDks(double StartMm, double EndMm, List<ProfileDksSlot> Slots, bool Standard = false);
public sealed record ProfileDksTarget(string Kind = "DefaultSentinel", ushort? LogicalId = null);
public sealed class ProfileDksSlot
{
    public ProfileDksTarget Target { get; set; } = new();
    public string DownStart { get; set; } = "Inactive";
    public string DownEnd { get; set; } = "Inactive";
    public string UpStart { get; set; } = "Inactive";
    public string UpEnd { get; set; } = "Inactive";
}
public sealed class ProfileLighting
{
    // The existing daemon/Automation owns lighting until the ownership phase.
    public string? LegacyEffectReference { get; set; }
    public string Ownership { get; set; } = "LegacyUnmanaged";
    [JsonExtensionData] public Dictionary<string, JsonElement>? Extensions { get; set; }
}
public sealed record ProfileAutomationBinding(string? RuleId = null);

public sealed class ProfileDocument
{
    public int SchemaVersion { get; set; } = 1;
    public long Revision { get; set; }
    public Guid SelectedProfileId { get; set; }
    public ProfileMagnetic GlobalDefaults { get; set; } = new();
    public List<DeviceProfile> Profiles { get; set; } = [];
    [JsonExtensionData] public Dictionary<string, JsonElement>? Extensions { get; set; }
}

public static class ProfileJson
{
    public static readonly JsonSerializerOptions Options = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        PropertyNameCaseInsensitive = false,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Skip
    };

    public static ProfileDocument NewDefault(string? legacyLightingReference = null)
    {
        var profile = new DeviceProfile { Name = "Desktop" };
        profile.Lighting.LegacyEffectReference = legacyLightingReference;
        return new ProfileDocument { SelectedProfileId = profile.Id, Profiles = [profile] };
    }

    public static ProfileDocument Deserialize(string json)
    {
        try
        {
            using var parsed = JsonDocument.Parse(json);
            var root = parsed.RootElement;
            if (root.ValueKind != JsonValueKind.Object) throw new InvalidDataException("Profile document must be an object.");
            // v0 was the prototype envelope without an explicit version. Migration is pure and idempotent.
            var version = 0;
            if (root.TryGetProperty("schema_version", out var v) &&
                (v.ValueKind != JsonValueKind.Number || !v.TryGetInt32(out version)))
                throw new InvalidDataException("Invalid profile schema version.");
            if (version is < 0 or > 1) throw new InvalidDataException($"Unsupported profile schema {version}.");
            var document = JsonSerializer.Deserialize<ProfileDocument>(json, Options)
                ?? throw new InvalidDataException("Empty profile document.");
            if (document.Profiles is null) throw new InvalidDataException("Missing profile list.");
            if (version == 0)
            {
                document.SchemaVersion = 1;
                if (document.Profiles.Any(p => p is null || p.SchemaVersion != 1))
                    throw new InvalidDataException("Unsupported nested profile schema.");
            }
            ProfileValidator.Validate(document);
            return document;
        }
        catch (JsonException ex) { throw new InvalidDataException("Malformed profile JSON.", ex); }
    }

    public static string Serialize(ProfileDocument document)
    {
        ProfileValidator.Validate(document);
        return JsonSerializer.Serialize(document, Options) + "\n";
    }
}

public sealed record ProfileValidationIssue(string Code, string Message, ushort? LogicalId = null);

public static class ProfileValidator
{
    public static void Validate(ProfileDocument document)
    {
        if (document.SchemaVersion != 1 || document.Revision < 0 || document.Profiles is null || document.Profiles.Count == 0)
            throw new InvalidDataException("Unsupported or empty profile document.");
        if (document.Profiles.Any(p => p is null || p.SchemaVersion != 1 || p.Id == Guid.Empty ||
            string.IsNullOrWhiteSpace(p.Name) || p.Name.Length > 100 || p.Magnetic is null || p.Lighting is null ||
            p.Lighting.Ownership != "LegacyUnmanaged"))
            throw new InvalidDataException("Invalid profile identity or schema.");
        if (document.Profiles.Select(p => p.Id).Distinct().Count() != document.Profiles.Count ||
            !document.Profiles.Any(p => p.Id == document.SelectedProfileId))
            throw new InvalidDataException("Duplicate profile ID or missing selected profile.");
        ValidateMagnetic(document.GlobalDefaults);
        foreach (var profile in document.Profiles) {
            if (profile.ActivationBackend is not ("host_managed" or "hardware_slot") ||
                profile.ActivationBackend == "hardware_slot" && profile.HardwareSlot is not (>= 1 and <= 5))
                throw new InvalidDataException("Invalid activation backend or hardware slot (expected 1–5).");
            ValidateMagnetic(profile.Magnetic);
        }
    }

    private static void ValidateMagnetic(ProfileMagnetic magnetic)
    {
        if (magnetic is null || magnetic.Keys is null) throw new InvalidDataException("Missing magnetic settings.");
        if (magnetic.GlobalActuationMm is double a && !Tenth(a, 0.1, 4.0))
            throw new InvalidDataException("Invalid global actuation.");
        if (magnetic.GlobalDeadzone is { } dz) ValidateDeadzone(dz);
        if (magnetic.GlobalRapidTrigger is { } rt) ValidateRapidTrigger(rt, true);
        if (magnetic.Keys.Any(k => k is null) ||
            magnetic.Keys.Select(k => k.LogicalId).Distinct().Count() != magnetic.Keys.Count)
            throw new InvalidDataException("Duplicate logical key.");
        foreach (var key in magnetic.Keys)
        {
            if (key is null) throw new InvalidDataException("Null key override.");
            if (key.LogicalId == 0) throw new InvalidDataException("Invalid logical key.");
            if (key.ActuationMm is double value && !Tenth(value, 0.1, 4.0))
                throw new InvalidDataException("Invalid key actuation.");
            if (key.Deadzone is { } keyDz) ValidateDeadzone(keyDz);
            if (key.RapidTrigger is { } keyRt) ValidateRapidTrigger(keyRt, false);
            if (key.Dks is { } dks)
            {
                if (!Tenth(dks.StartMm, 0.1, 4.0) || !Tenth(dks.EndMm, 0.1, 4.0) ||
                    dks.StartMm > dks.EndMm || dks.Slots is null || dks.Slots.Count != 4)
                    throw new InvalidDataException("Invalid DKS thresholds or slots.");
                foreach (var slot in dks.Slots)
                {
                    if (slot is null || slot.Target is null ||
                        (slot.Target.Kind == "DefaultSentinel" && slot.Target.LogicalId is not null) ||
                        (slot.Target.Kind != "DefaultSentinel" &&
                        (slot.Target.Kind != "LogicalKey" || slot.Target.LogicalId is not ushort target ||
                         target == 0 || target == 0x0508)) ||
                        !ValidTrigger(slot.DownStart) || !ValidTrigger(slot.DownEnd) ||
                        !ValidTrigger(slot.UpStart) || !ValidTrigger(slot.UpEnd))
                        throw new InvalidDataException("Invalid DKS target or trigger.");
                }
                if (key.RapidTrigger?.Enabled == true && !dks.Standard)
                    throw new InvalidDataException("DKS and rapid trigger conflict.");
            }
        }
    }

    private static bool ValidTrigger(string value) => value is "Inactive" or "Tap" or "Release" or "Hold";
    private static bool Tenth(double value, double min, double max) =>
        double.IsFinite(value) && value >= min && value <= max && Math.Abs(value * 10 - Math.Round(value * 10)) < 1e-7;
    private static void ValidateDeadzone(ProfileDeadzone value)
    {
        if (!Tenth(value.TopMm, 0, 0.5) || !Tenth(value.BottomMm, 0, 0.5))
            throw new InvalidDataException("Invalid deadzone.");
    }
    // Editor diagnostics share the same predicates as serialization validation.
    // The daemon still owns effective resolution, safety preflight and submission.
    public static IReadOnlyList<ProfileValidationIssue> RapidTriggerIssues(ProfileMagnetic magnetic)
    {
        var issues = new List<ProfileValidationIssue>();
        if (magnetic.GlobalRapidTrigger is { } global) issues.AddRange(RapidTriggerValueIssues(global, true));
        foreach (var key in magnetic.Keys) {
            if (key.RapidTrigger is not { } rt) continue;
            issues.AddRange(RapidTriggerValueIssues(rt, false, key.LogicalId));
            if (rt.Enabled && key.Dks is { Standard: false }) issues.Add(new("RtDksConflict",
                $"按键 {key.LogicalId} 不能同时启用自定义 DKS 和快速触发。", key.LogicalId));
        }
        return issues;
    }

    private static IReadOnlyList<ProfileValidationIssue> RapidTriggerValueIssues(
        ProfileRapidTrigger value, bool global, ushort? logicalId = null)
    {
        var issues = new List<ProfileValidationIssue>();
        var location = logicalId is ushort id ? $"按键 {id}：" : "";
        if (!global && value.Extensions?.TryGetValue("continuous", out var continuous) == true &&
            continuous.ValueKind != JsonValueKind.False)
            issues.Add(new("RtContinuousUnsupported", location + "当前版本不支持持续模式，不能安全应用此快速触发设置。", logicalId));
        if (!Tenth(value.PressMm, 0.1, 2.5) || !Tenth(value.ReleaseMm, 0.1, 2.5))
            issues.Add(new("RtSensitivityRange", location + "快速触发按下、抬起灵敏度须为 0.1–2.5 mm，步长为 0.1 mm。", logicalId));
        if (global && (value.TopMm is not double top || value.BottomMm is not double bottom ||
            !Tenth(top, 0, 0.5) || !Tenth(bottom, 0, 0.5)))
            issues.Add(new("RtDeadzoneRange", "快速触发顶部、底部死区须为 0.0–0.5 mm，步长为 0.1 mm。"));
        if (global && !value.SeparateMode && value.PressMm != value.ReleaseMm)
            issues.Add(new("RtSeparateMode", "同步模式的按下、抬起灵敏度必须相同；需要不同值时请开启独立模式。"));
        if (global && !value.Enabled)
            issues.Add(new("RtMasterDisabled", "此配置文件包含旧版快速触发参数，但尚未启用快速触发设置。请开启旧版参数中的‘启用快速触发设置’，或关闭‘保留旧版参数’后使用逐键设置。"));
        return issues;
    }

    private static void ValidateRapidTrigger(ProfileRapidTrigger value, bool global)
    {
        var issue = RapidTriggerValueIssues(value, global).FirstOrDefault();
        if (issue is not null) throw new InvalidDataException(issue.Message);
    }
}
