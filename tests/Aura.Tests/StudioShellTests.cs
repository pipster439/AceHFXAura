using System.Text.Json;
using Aura_WinUI.Services;

namespace Aura.Tests;

[TestClass]
public class StudioShellTests
{
    [TestMethod]
    public void CommandsAreTypedAndOriginBounded()
    {
        Assert.ThrowsExactly<ArgumentOutOfRangeException>(() => StudioShellModel.Command("shell"));
        Assert.IsTrue(StudioShellModel.TrustedSource("http://127.0.0.1:19898/?host=winui"));
        Assert.IsFalse(StudioShellModel.TrustedSource("https://127.0.0.1:19898/"));
        Assert.IsFalse(StudioShellModel.TrustedSource("http://example.org:19898/"));
        var command = JsonDocument.Parse(StudioShellModel.Command("select", "example")).RootElement;
        Assert.AreEqual("select", command.GetProperty("command").GetString());
    }
    [TestMethod]
    public void MalformedStatusCannotPartiallyReplaceShellState()
    {
        var model = new StudioShellModel();
        Assert.IsTrue(model.Receive(JsonSerializer.Serialize(new { type = "studio_state", name = "example", workType = "effect",
            validation = "验证通过", build = "成功", lifecycle = "草稿", plugin = "未发布", diagnostics = "", projects = new[] { "example" }, playing = true, busy = false })));
        Assert.IsFalse(model.Receive("{\"type\":\"studio_state\",\"name\":\"bad\"}"));
        Assert.AreEqual("example", model.Name);
        Assert.IsFalse(model.Receive(new string('x', 65537)));
    }
}
