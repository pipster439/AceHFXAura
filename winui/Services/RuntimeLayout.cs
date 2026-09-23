using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Aura_WinUI.Services;

public sealed record RuntimeLayout(string DaemonExecutablePath, string WorkingDirectory,
    string ConfigPath, string KeymapPath, string RuntimeDirectory, string TemplatePath,
    string? PayloadDirectory = null);
public sealed record RuntimeFile(string Role, string Path, string Sha256);
public sealed record RuntimeManifest(int SchemaVersion, string Version, string BuildId, List<RuntimeFile> Files);

public static class RuntimeLayoutResolver
{
    internal static readonly JsonSerializerOptions JsonOptions = new() { PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower };
    public static string DataRoot => Path.GetFullPath(Environment.GetEnvironmentVariable("AURA_DATA_ROOT")
        ?? (File.Exists(Path.Combine(AppContext.BaseDirectory, "portable.marker")) ? AppContext.BaseDirectory
        : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Aura")));

    // Pure resolution: no ancestor/CWD search, copying, or GUI-as-daemon fallback.
    public static RuntimeLayout Resolve() => Resolve(AppContext.BaseDirectory, DataRoot,
        Environment.GetEnvironmentVariable("AURA_DEV_ROOT"), Environment.GetEnvironmentVariable("AURA_DEV_BIN"));

    public static RuntimeLayout Resolve(string appRoot, string dataRoot, string? devRoot = null, string? devBin = null)
    {
        dataRoot = Path.GetFullPath(dataRoot);
        if (!string.IsNullOrWhiteSpace(devRoot))
        {
            devRoot = Path.GetFullPath(devRoot);
            var bin = Path.GetFullPath(devBin ?? Path.Combine(devRoot, "build", "Release"));
            return new(Path.Combine(bin, "aura_daemon.exe"), dataRoot, Path.Combine(dataRoot, "config.json"),
                Path.Combine(devRoot, "calibrated_keymap.json"), bin, Path.Combine(devRoot, "config.example.json"));
        }
        var payload = Path.Combine(Path.GetFullPath(appRoot), "runtime-payload");
        var manifest = ReadManifest(payload);
        var runtime = Path.Combine(dataRoot, "runtime", manifest.Version + "-" + manifest.BuildId);
        return new(Path.Combine(runtime, "aura_daemon.exe"), dataRoot, Path.Combine(dataRoot, "config.json"),
            Path.Combine(runtime, "calibrated_keymap.json"), runtime, Path.Combine(runtime, "config.example.json"), payload);
    }

    public static RuntimeManifest ReadManifest(string root)
    {
        var manifest = JsonSerializer.Deserialize<RuntimeManifest>(File.ReadAllText(Path.Combine(root, "runtime-manifest.json")), JsonOptions)
            ?? throw new InvalidDataException("Missing runtime manifest");
        if (manifest.SchemaVersion != 1 || !Regex.IsMatch(manifest.Version ?? "", @"^\d+\.\d+\.\d+(?:-[a-zA-Z0-9.]+)?$")
            || !Regex.IsMatch(manifest.BuildId ?? "", "^[a-f0-9]{16,64}$") || manifest.Files is null)
            throw new InvalidDataException("Unsupported runtime manifest");
        var required = new Dictionary<string, string> {
            ["daemon"] = "aura_daemon.exe", ["web"] = "aura_web_ui.exe", ["keymap"] = "calibrated_keymap.json",
            ["template"] = "config.example.json", ["studio"] = "web/index.html",
            ["sdk_effect"] = "include/engine/effect.h", ["sdk_plugin"] = "include/engine/plugin_interface.h",
            ["sdk_types"] = "include/aura/aura_types.h", ["sdk_keymap"] = "include/aura/keymap.h" };
        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var file in manifest.Files)
        {
            SafePath(root, file.Path);
            if (!paths.Add(file.Path) || !Regex.IsMatch(file.Sha256 ?? "", "^[a-fA-F0-9]{64}$"))
                throw new InvalidDataException("Duplicate path or invalid runtime hash");
        }
        foreach (var (role, path) in required)
            if (manifest.Files.Count(f => f.Role == role && f.Path == path) != 1 || manifest.Files.Count(f => f.Role == role) != 1)
                throw new InvalidDataException($"Missing or ambiguous runtime role: {role}");
        return manifest;
    }

    internal static string SafePath(string root, string relative)
    {
        if (string.IsNullOrWhiteSpace(relative) || relative.Contains('\\') || relative.Contains(':') ||
            relative.Split('/').Any(p => p is "" or "." or "..") || Path.IsPathRooted(relative))
            throw new InvalidDataException("Unsafe runtime path");
        var path = Path.GetFullPath(Path.Combine(root, relative));
        for (FileSystemInfo? entry = new FileInfo(path); entry != null; entry = entry is FileInfo f ? f.Directory : ((DirectoryInfo)entry).Parent)
            if (entry.Exists && (entry.Attributes & FileAttributes.ReparsePoint) != 0)
                throw new InvalidDataException("Runtime paths cannot contain reparse points");
        return path;
    }
}

public static class RuntimePreparer
{
    public static void Prepare(RuntimeLayout layout)
    {
        if (layout.PayloadDirectory is { } payload)
        {
            var manifest = RuntimeLayoutResolver.ReadManifest(payload);
            Verify(payload, manifest);
            if (!Directory.Exists(layout.RuntimeDirectory))
            {
                var stage = layout.RuntimeDirectory + ".staging-" + Guid.NewGuid().ToString("N");
                try
                {
                    Directory.CreateDirectory(stage);
                    foreach (var file in manifest.Files)
                    {
                        var dest = RuntimeLayoutResolver.SafePath(stage, file.Path);
                        Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
                        File.Copy(RuntimeLayoutResolver.SafePath(payload, file.Path), dest);
                    }
                    Verify(stage, manifest);
                    File.Copy(Path.Combine(payload, "runtime-manifest.json"), Path.Combine(stage, "runtime-manifest.json"));
                    Directory.Move(stage, layout.RuntimeDirectory);
                }
                finally { if (Directory.Exists(stage)) Directory.Delete(stage, true); }
            }
            Verify(layout.RuntimeDirectory, manifest);
        }
        foreach (var path in new[] { layout.DaemonExecutablePath, layout.KeymapPath, layout.TemplatePath,
            Path.Combine(layout.RuntimeDirectory, "aura_web_ui.exe") })
            if (!File.Exists(path)) throw new FileNotFoundException("Required runtime asset missing", path);
        if (!string.Equals(Path.GetFileName(layout.DaemonExecutablePath), "aura_daemon.exe", StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("Executable is not the declared daemon");
        Directory.CreateDirectory(layout.WorkingDirectory);
        if (!File.Exists(layout.ConfigPath))
        {
            try { File.Copy(layout.TemplatePath, layout.ConfigPath, false); }
            catch (IOException) when (File.Exists(layout.ConfigPath)) { }
        }
    }
    public static void Verify(string root, RuntimeManifest manifest)
    {
        foreach (var file in manifest.Files)
        {
            using var stream = File.OpenRead(RuntimeLayoutResolver.SafePath(root, file.Path));
            if (!Convert.ToHexString(SHA256.HashData(stream)).Equals(file.Sha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException($"Runtime hash mismatch: {file.Path}");
        }
    }
}
