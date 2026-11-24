using System.Text.Encodings.Web;
using System.Text.Json;

namespace Aura_WinUI.Services;

public static class AuraMcpConfig
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping
    };

    public static string ResolveScriptPath(string? overridePath = null)
    {
        if (!string.IsNullOrWhiteSpace(overridePath))
        {
            return Path.GetFullPath(overridePath.Trim());
        }

        try
        {
            var layout = RuntimeLayoutResolver.Resolve();
            var workingCandidate = Path.Combine(layout.WorkingDirectory, "plugins", "mcp", "aura_mcp_server.py");
            if (File.Exists(workingCandidate))
            {
                return workingCandidate;
            }
        }
        catch
        {
            // Ignore runtime layout resolution failures in design-time or minimal test hosts.
        }

        var devRoot = Environment.GetEnvironmentVariable("AURA_DEV_ROOT");
        if (!string.IsNullOrWhiteSpace(devRoot))
        {
            var devCandidate = Path.Combine(Path.GetFullPath(devRoot), "plugins", "mcp", "aura_mcp_server.py");
            if (File.Exists(devCandidate))
            {
                return devCandidate;
            }
        }

        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (var i = 0; i < 8 && dir != null; i++, dir = dir.Parent)
        {
            var candidate = Path.Combine(dir.FullName, "plugins", "mcp", "aura_mcp_server.py");
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }

        return Path.Combine("plugins", "mcp", "aura_mcp_server.py");
    }

    public static string BuildClientConfigJson(
        string scriptPath,
        string daemonUrl = "http://127.0.0.1:19897",
        string studioUrl = "http://127.0.0.1:19898")
    {
        var payload = new
        {
            mcpServers = new
            {
                aura = new
                {
                    command = "python",
                    args = new[] { scriptPath },
                    env = new
                    {
                        AURA_DAEMON_URL = daemonUrl,
                        AURA_STUDIO_URL = studioUrl
                    }
                }
            }
        };
        return JsonSerializer.Serialize(payload, JsonOptions);
    }
}
