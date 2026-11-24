using System.Text.Json;
using Aura_WinUI.Services;

namespace Aura.Tests;

[TestClass]
public class AuraMcpConfigTests
{
    [TestMethod]
    public void BuildClientConfigJsonProducesValidStdioMcpSchema()
    {
        var json = AuraMcpConfig.BuildClientConfigJson(@"G:\Aura\plugins\mcp\aura_mcp_server.py");
        using var doc = JsonDocument.Parse(json);
        var aura = doc.RootElement.GetProperty("mcpServers").GetProperty("aura");

        Assert.AreEqual("python", aura.GetProperty("command").GetString());
        Assert.AreEqual(@"G:\Aura\plugins\mcp\aura_mcp_server.py", aura.GetProperty("args")[0].GetString());
        var env = aura.GetProperty("env");
        Assert.AreEqual("http://127.0.0.1:19897", env.GetProperty("AURA_DAEMON_URL").GetString());
        Assert.AreEqual("http://127.0.0.1:19898", env.GetProperty("AURA_STUDIO_URL").GetString());
    }

    [TestMethod]
    public void ResolveScriptPathFindsWorkspaceMcpScript()
    {
        var resolved = AuraMcpConfig.ResolveScriptPath();
        Assert.EndsWith(Path.Combine("plugins", "mcp", "aura_mcp_server.py"), resolved, StringComparison.OrdinalIgnoreCase);
    }
}
