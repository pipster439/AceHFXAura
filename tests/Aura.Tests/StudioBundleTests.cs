using System.IO.Compression;
using System.Text;
using System.Text.Json;
using Aura_WinUI.Services;

namespace Aura.Tests;

[TestClass]
public sealed class StudioBundleTests
{
    private static JsonElement Payload() => JsonSerializer.SerializeToElement(new {
        manifest = new { schema_version = 1, name = "fixture", description = "share", tags = new[] { "local" }, created_with_version = "alpha.8",
            capabilities = new { schema_version = 1, inputs = Array.Empty<string>(), outputs = new[] { "keyboard_rgb" }, features = new[] { "simulation_supported" }, gsi_fields = Array.Empty<string>(), diagnostics = Array.Empty<string>() } },
        project = new { name = "fixture", publication = new { mode = "continuous", fade_out_ms = 0 }, json = new { blocks = new { blocks = Array.Empty<object>(), languageVersion = 0 } } }
    });
    private static byte[] Zip(params (string name, string content, int attributes)[] entries) {
        using var bytes = new MemoryStream(); using (var zip = new ZipArchive(bytes, ZipArchiveMode.Create, true)) foreach (var e in entries) {
            var entry = zip.CreateEntry(e.name); entry.ExternalAttributes = e.attributes; using var stream = entry.Open(); stream.Write(Encoding.UTF8.GetBytes(e.content));
        } return bytes.ToArray();
    }
    private static (string name, string content, int attributes) Manifest() => ("manifest.json", Payload().GetProperty("manifest").GetRawText(), 0);
    private static (string name, string content, int attributes) Project() => ("project.json", Payload().GetProperty("project").GetRawText(), 0);
    [TestMethod] public void SourceOnlyRoundtripAndVersionStamp() {
        var bytes = StudioProjectBundle.Export(Payload(), "2.0.0-alpha.8"); var parsed = StudioProjectBundle.Parse(bytes);
        Assert.AreEqual("2.0.0-alpha.8", parsed.GetProperty("manifest").GetProperty("created_with_version").GetString());
        Assert.AreEqual(Payload().GetProperty("project").GetRawText(), parsed.GetProperty("project").GetRawText());
        using var zip = new ZipArchive(new MemoryStream(bytes)); CollectionAssert.AreEquivalent(new[] { "manifest.json", "project.json" }, zip.Entries.Select(e=>e.FullName).ToArray());
    }
    [DataTestMethod]
    [DataRow("../project.json")][DataRow("/project.json")][DataRow("C:/project.json")][DataRow("project/../project.json")][DataRow("evil.dll")][DataRow("evil.exe")][DataRow("evil.js")][DataRow("PROJECT.JSON")]
    public void TraversalAbsoluteExecutablesAndNoncanonicalMembersRejected(string member) {
        var p=Project(); Assert.ThrowsExactly<StudioPersistenceException>(()=>StudioProjectBundle.Parse(Zip(Manifest(), (member,p.content,0))));
    }
    [TestMethod] public void DuplicateCaseInsensitiveCountAndLinksRejected() {
        Assert.ThrowsExactly<StudioPersistenceException>(()=>StudioProjectBundle.Parse(Zip(Project(), ("PROJECT.JSON",Project().content,0))));
        Assert.ThrowsExactly<StudioPersistenceException>(()=>StudioProjectBundle.Parse(Zip(Manifest(),Project(),("extra.json","{}",0))));
        foreach(var attrs in new[]{unchecked((int)0xa0000000), 0x400, 0x10}) Assert.ThrowsExactly<StudioPersistenceException>(()=>StudioProjectBundle.Parse(Zip(Manifest(),("project.json",Project().content,attrs))));
    }
    [TestMethod] public void CompressedBombArchiveSizeAndMalformedJsonRejected() {
        Assert.ThrowsExactly<StudioPersistenceException>(()=>StudioProjectBundle.Parse(Zip(Manifest(),("project.json",new string('x', StudioProjectBundle.MaxProjectBytes+1),0))));
        Assert.ThrowsExactly<StudioPersistenceException>(()=>StudioProjectBundle.Parse(new byte[StudioProjectBundle.MaxArchiveBytes+1]));
        Assert.ThrowsExactly<StudioPersistenceException>(()=>StudioProjectBundle.Parse(Zip(("manifest.json","{}",0),Project())));
        Assert.ThrowsExactly<StudioPersistenceException>(()=>StudioProjectBundle.Parse(Zip(("manifest.json",Manifest().content.Replace("\"schema_version\":1", "\"schema_version\":1,\"schema_version\":1"),0),Project())));
        Assert.ThrowsExactly<StudioPersistenceException>(()=>StudioProjectBundle.Parse([1,2,3]));
    }
    [DataTestMethod]
    [DataRow("sk-fixtureprivateabcdef")][DataRow("Bearer privatevalue")][DataRow("api_key=private")][DataRow("C:/Users/owner/private")][DataRow("/home/owner/private")]
    public void MetadataCannotSmuggleSecretsOrLocalPaths(string secret) {
        var m=Manifest(); var text=m.content.Replace("share",secret); Assert.ThrowsExactly<StudioPersistenceException>(()=>StudioProjectBundle.Parse(Zip((m.name,text,0),Project())));
    }
    [TestMethod] public void GraphSecretsScriptsAndRuntimeFieldsRejected() {
        foreach(var field in new[]{"provider_api_key","credentials","script","exe","applied_plugin_name","diagnostics"}) {
            var p=Project(); var text=p.content.Replace("\"blocks\":{", "\""+field+"\":\"private\",\"blocks\":{");
            Assert.ThrowsExactly<StudioPersistenceException>(()=>StudioProjectBundle.Parse(Zip(Manifest(),(p.name,text,0))));
        }
    }
    [TestMethod] public void PaletteCommandsUseExistingTypedTransport() {
        foreach(var id in new[]{"palette","run_scenario","restore_snapshot","export_bundle","import_bundle","ask_ai","diagnostics"}) {
            using var doc=JsonDocument.Parse(StudioShellModel.Command(id)); Assert.AreEqual(id,doc.RootElement.GetProperty("command").GetString());
        }
    }
}
