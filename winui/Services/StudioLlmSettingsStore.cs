using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace Aura_WinUI.Services;

public interface IStudioCredentialStore
{
    string? Read(string target);
    void Set(string target, string key);
    void Remove(string target);
}
// Windows Credential Manager; no plaintext credential ever enters JSON/settings/log exports.
public sealed class StudioCredentialStore : IStudioCredentialStore
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct Credential
    {
        public uint Flags, Type; public string TargetName; public string? Comment;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastWritten;
        public uint BlobSize; public IntPtr Blob; public uint Persist, AttributeCount; public IntPtr Attributes;
        public string? TargetAlias, UserName;
    }
    [DllImport("advapi32.dll", EntryPoint = "CredWriteW", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)] private static extern bool Write(ref Credential credential, uint flags);
    [DllImport("advapi32.dll", EntryPoint = "CredReadW", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)] private static extern bool ReadNative(string target, uint type, uint flags, out IntPtr credential);
    [DllImport("advapi32.dll", EntryPoint = "CredDeleteW", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)] private static extern bool Delete(string target, uint type, uint flags);
    [DllImport("advapi32.dll")] private static extern void CredFree(IntPtr buffer);
    private static void CheckTarget(string target) {
        if (!OperatingSystem.IsWindows() || !target.StartsWith("Aura/StudioLLM/", StringComparison.Ordinal) || target.Length > 256)
            throw new StudioLlmException("credential", "安全凭据存储不可用。");
    }
    public string? Read(string target)
    {
        CheckTarget(target);
        if (!ReadNative(target, 1, 0, out var pointer)) {
            if (Marshal.GetLastWin32Error() == 1168) return null;
            throw new StudioLlmException("credential", "无法读取安全凭据。");
        }
        try { var c = Marshal.PtrToStructure<Credential>(pointer);
            if (c.BlobSize > 5120 || c.BlobSize % 2 != 0) throw new StudioLlmException("credential", "安全凭据无效。");
            return Marshal.PtrToStringUni(c.Blob, (int)c.BlobSize / 2); }
        finally { CredFree(pointer); }
    }
    public void Set(string target, string key)
    {
        CheckTarget(target);
        if (string.IsNullOrWhiteSpace(key) || Encoding.Unicode.GetByteCount(key) > 5120 || key.Any(char.IsControl))
            throw new StudioLlmException("credential", "密钥无效或过长。");
        var bytes = Encoding.Unicode.GetBytes(key); var pointer = Marshal.AllocHGlobal(bytes.Length);
        try {
            Marshal.Copy(bytes, 0, pointer, bytes.Length);
            var c = new Credential { Type = 1, TargetName = target, BlobSize = (uint)bytes.Length, Blob = pointer, Persist = 2, UserName = "Aura Studio" };
            if (!Write(ref c, 0)) throw new StudioLlmException("credential", "无法保存安全凭据。");
        } finally { CryptographicOperations.ZeroMemory(bytes); for (var i = 0; i < bytes.Length; i++) Marshal.WriteByte(pointer, i, 0); Marshal.FreeHGlobal(pointer); }
    }
    public void Remove(string target) {
        CheckTarget(target);
        if (!Delete(target, 1, 0) && Marshal.GetLastWin32Error() != 1168) throw new StudioLlmException("credential", "无法删除安全凭据。");
    }
}
public sealed class StudioLlmSettingsStore(string path, IStudioCredentialStore credentials)
{
    public StudioLlmSettings Settings { get; private set; } = new();
    // Bind key to exact normalized service base; changing endpoint never silently reuses a key.
    public static string Target(StudioLlmSettings settings) => "Aura/StudioLLM/" + Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(settings.Endpoint().AbsoluteUri)));
    public void Load() {
        if (!File.Exists(path)) return;
        try { Settings = JsonSerializer.Deserialize<StudioLlmSettings>(File.ReadAllText(path)) ?? new(); }
        catch (JsonException) { throw new StudioLlmException("settings", "服务设置格式无效，请重新保存。"); }
    }
    public void Save(StudioLlmSettings settings, string? newKey = null) {
        var target = Target(settings);
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        if (newKey != null) credentials.Set(target, newKey);
        File.WriteAllText(path + ".tmp", JsonSerializer.Serialize(settings)); File.Move(path + ".tmp", path, true); Settings = settings;
    }
    public bool HasKey => credentials.Read(Target(Settings)) != null;
    public string Key() => credentials.Read(Target(Settings)) ?? throw new StudioLlmException("credential", "请先设置 API 密钥。");
    public void RemoveKey() => credentials.Remove(Target(Settings));
}
