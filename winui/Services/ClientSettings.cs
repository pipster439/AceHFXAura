using System.Reflection;
using System.Text.Json;

namespace Aura_WinUI.Services;

public sealed class ClientSettings
{
    public string Theme { get; set; } = "Default";
    public bool MinimizeToTray { get; set; } = true;
    public static ClientSettings Current { get; } = Load();
    public static string Version => typeof(ClientSettings).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion.Split('+')[0] ?? "unknown";
    private static string SettingsPath => Path.Combine(RuntimeLayoutResolver.DataRoot, "client-settings.json");
    private static ClientSettings Load()
    {
        try { return JsonSerializer.Deserialize<ClientSettings>(File.ReadAllText(SettingsPath)) ?? new(); }
        catch { return new(); }
    }
    public void Save()
    {
        try
        {
            Directory.CreateDirectory(RuntimeLayoutResolver.DataRoot);
            File.WriteAllText(SettingsPath + ".tmp", JsonSerializer.Serialize(this));
            File.Move(SettingsPath + ".tmp", SettingsPath, true);
        }
        catch (Exception ex) { Log(ex); }
    }
    public static void Log(Exception ex)
    {
        try
        {
            var directory = Path.Combine(RuntimeLayoutResolver.DataRoot, "logs");
            Directory.CreateDirectory(directory);
            File.AppendAllText(Path.Combine(directory, "winui.log"), $"{DateTimeOffset.Now:O} {ex}\n");
        }
        catch { }
    }
}
