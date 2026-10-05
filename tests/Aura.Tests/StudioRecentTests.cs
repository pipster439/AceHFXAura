using System.Text.Json;
using Aura_WinUI.Services;

namespace Aura.Tests;

[TestClass]
public sealed class StudioRecentTests
{
    private sealed class Fixture : IDisposable {
        public string Root { get; } = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "aura-studio-recent-test-" + Guid.NewGuid());
        public string Path => System.IO.Path.Combine(Root, "Studio", "recent.json");
        public void Dispose() { if (Directory.Exists(Root)) Directory.Delete(Root, true); }
    }
    [TestMethod]
    public void RecentsSurviveRestartAreBoundedAndUseLogicalIdentity() {
        using var f = new Fixture(); var catalog = Enumerable.Range(0, 12).Select(i => "effect_" + i).ToArray();
        var store = new StudioRecentStore(f.Root); foreach (var id in catalog) store.Order(catalog, id);
        var ordered = new StudioRecentStore(f.Root).Order(catalog); Assert.AreEqual("effect_11", ordered[0]); Assert.AreEqual("effect_2", ordered[9]);
        using var doc = JsonDocument.Parse(File.ReadAllText(f.Path)); Assert.AreEqual(10, doc.RootElement.GetProperty("projects").GetArrayLength());
        Assert.DoesNotContain(f.Root, File.ReadAllText(f.Path)); Assert.DoesNotContain("api_key", File.ReadAllText(f.Path));
    }
    [TestMethod]
    public void DeletedProjectsArePrunedAndClearDoesNotRemoveProjectCatalog() {
        using var f = new Fixture(); var store = new StudioRecentStore(f.Root); store.Order(["first", "deleted"], "deleted"); store.Order(["first", "deleted"], "first");
        CollectionAssert.AreEqual(new[] { "first" }, store.Order(["first"]).ToArray()); Assert.DoesNotContain("deleted", File.ReadAllText(f.Path));
        store.Clear(); CollectionAssert.AreEqual(new[] { "second", "first" }, new StudioRecentStore(f.Root).Order(["second", "first"]).ToArray());
    }
    [TestMethod]
    public void RecentAtomicFailurePreservesOldFileAndCleansOnlyOwnTemporaryFile() {
        using var f = new Fixture(); var store = new StudioRecentStore(f.Root); store.Order(["first", "second"], "first"); var before = File.ReadAllBytes(f.Path);
        Assert.ThrowsExactly<IOException>(() => new StudioRecentStore(f.Root, _ => throw new IOException("fixture")).Order(["first", "second"], "second"));
        CollectionAssert.AreEqual(before, File.ReadAllBytes(f.Path)); Assert.IsEmpty(Directory.GetFiles(System.IO.Path.GetDirectoryName(f.Path)!, "*.tmp"));
    }
    [TestMethod]
    public void CorruptionAndUnsafeIdentityNeverOverwriteOrLeakPaths() {
        using var f = new Fixture(); var store = new StudioRecentStore(f.Root); store.Order(["first"], "first");
        Assert.ThrowsExactly<StudioPersistenceException>(() => store.Order([@"C:\private\key"], @"C:\private\key"));
        File.WriteAllText(f.Path, "{broken"); Assert.ThrowsExactly<StudioPersistenceException>(() => store.Order(["first"])); Assert.AreEqual("{broken", File.ReadAllText(f.Path));
        store.Clear(); Assert.IsNotNull(new StudioRecentStore(f.Root).Order(["first"]));
    }
    [TestMethod]
    public void RepeatedStatusDoesNotRewriteFileOrChangeOrder() {
        using var f = new Fixture(); var store = new StudioRecentStore(f.Root); store.Order(["first", "second"], "second"); var before = File.ReadAllBytes(f.Path);
        var ordered = new StudioRecentStore(f.Root, _ => Assert.Fail("Unnecessary write")).Order(["first", "second"]);
        Assert.AreEqual("second", ordered[0]); CollectionAssert.AreEqual(before, File.ReadAllBytes(f.Path));
    }
}
