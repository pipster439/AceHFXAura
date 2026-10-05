using System.Text.Json;
using Aura_WinUI.Services;

namespace Aura.Tests;

[TestClass]
public sealed class StudioPersistenceTests
{
    private sealed class Fixture : IDisposable {
        public string Root { get; } = Path.Combine(Path.GetTempPath(), "aura-studio-journal-test-" + Guid.NewGuid());
        public Fixture() { Directory.CreateDirectory(Root); }
        public void Dispose() { Directory.Delete(Root, true); }
    }
    private static StudioDraft Draft(double value = 3, string id = "fixture_id") {
        using var json = JsonDocument.Parse(JsonSerializer.Serialize(new { name = "fixture", json = new { blocks = new { languageVersion = 0, blocks = new[] { new { type = "math_number", id, x = 20, y = 30, fields = new { NUM = value } } } } }, publication = new { mode = "continuous", fade_out_ms = 0 } }));
        return StudioDraft.Parse(json.RootElement);
    }
    [TestMethod]
    public void AtomicFailurePreservesPreviousCompleteJournalAndRemovesTemporaryFile() {
        using var f = new Fixture(); var good = new StudioDraftStore(f.Root); good.ObserveSaved(Draft()); good.Autosave(Draft(4));
        var bytes = File.ReadAllBytes(good.JournalPath("fixture"));
        var failed = new StudioDraftStore(f.Root, _ => throw new IOException("fixture interrupted before rename")); failed.ObserveSaved(Draft(4));
        Assert.ThrowsExactly<IOException>(() => failed.Autosave(Draft(5)));
        CollectionAssert.AreEqual(bytes, File.ReadAllBytes(good.JournalPath("fixture")));
        Assert.IsEmpty(Directory.GetFiles(Path.GetDirectoryName(good.JournalPath("fixture"))!, "*.tmp"));
        good.ObserveSaved(Draft(4)); good.Autosave(Draft(5)); Assert.AreEqual(Draft(5).Fingerprint(), new StudioDraftStore(f.Root).ObserveSaved(Draft(4))!.DraftHash);
    }
    [TestMethod]
    public async Task DebouncePersistsOnlyLatestEditWithoutSleep() {
        using var f = new Fixture(); var store = new StudioDraftStore(f.Root); store.ObserveSaved(Draft());
        var gates = new List<TaskCompletionSource>(); var status = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var autosave = new StudioAutosaveCoordinator(store, (duration, ct) => {
            Assert.IsTrue(duration.TotalMilliseconds > 700 && duration.TotalMilliseconds <= 800); var gate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously); gates.Add(gate); return gate.Task.WaitAsync(ct);
        });
        autosave.Status += (_, _) => status.TrySetResult(); autosave.Queue(Draft(4)); autosave.Queue(Draft(5));
        Assert.IsFalse(File.Exists(store.JournalPath("fixture"))); gates[^1].SetResult(); await status.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.AreEqual(Draft(5).Fingerprint(), new StudioDraftStore(f.Root).ObserveSaved(Draft())!.DraftHash);
    }
    private sealed class Clock : TimeProvider {
        public long Milliseconds; public override long TimestampFrequency => 1000;
        public override long GetTimestamp() => Milliseconds;
    }
    [TestMethod]
    public void ContinuousEditsHaveFiveSecondMaximumWait() {
        using var f = new Fixture(); var store = new StudioDraftStore(f.Root); store.ObserveSaved(Draft());
        var clock = new Clock(); var waits = new List<TimeSpan>();
        var auto = new StudioAutosaveCoordinator(store, (duration, ct) => { waits.Add(duration); return Task.Delay(Timeout.InfiniteTimeSpan, ct); }, clock);
        auto.Queue(Draft(4)); clock.Milliseconds = 4600; auto.Queue(Draft(5)); clock.Milliseconds = 5001; auto.Queue(Draft(6));
        Assert.AreEqual(TimeSpan.FromMilliseconds(800), waits[0]); Assert.AreEqual(TimeSpan.FromMilliseconds(400), waits[1]); Assert.AreEqual(TimeSpan.Zero, waits[2]);
        auto.Flush(); Assert.AreEqual(Draft(6).Fingerprint(), new StudioDraftStore(f.Root).ObserveSaved(Draft())!.DraftHash);
    }
    [TestMethod]
    public void RestartOffersNewerDraftOnlyAfterAutosaveAndRestoreIsDraftOnly() {
        using var f = new Fixture(); var before = new StudioDraftStore(f.Root); before.ObserveSaved(Draft()); before.Autosave(Draft(4));
        // Discard the process-owned objects without a clean save/shutdown call.
        var restarted = new StudioDraftStore(f.Root); var recovery = restarted.ObserveSaved(Draft(id: "regenerated_id"));
        Assert.IsNotNull(recovery); Assert.IsTrue(recovery.Revision > 0); Assert.IsTrue(recovery.UpdatedUtc > DateTimeOffset.UtcNow.AddMinutes(-1));
        Assert.ThrowsExactly<StudioPersistenceException>(() => restarted.Autosave(Draft(7)));
        Assert.AreEqual(Draft(4).Fingerprint(), restarted.RestoreRecovery("fixture").Fingerprint());
        Assert.IsFalse(File.Exists(Path.Combine(f.Root, "config.json"))); restarted.Autosave(Draft(4));
    }
    [TestMethod]
    public void NewerExternallySavedDurableDraftDoesNotOfferStaleRecovery() {
        using var f = new Fixture(); var store = new StudioDraftStore(f.Root); store.ObserveSaved(Draft()); store.Autosave(Draft(4));
        Assert.IsNull(new StudioDraftStore(f.Root).ObserveSaved(Draft(6), DateTimeOffset.UtcNow.AddSeconds(1)));
        Assert.IsNull(new StudioDraftStore(f.Root).ObserveSaved(Draft(), DateTimeOffset.UtcNow.AddSeconds(1)), "A newer save of the same graph must supersede stale recovery too.");
    }
    [TestMethod]
    public void RecoveryUsesOnlyTheSelectedEffectsDurableSaveTime() {
        using var f = new Fixture(); var store = new StudioDraftStore(f.Root); store.ObserveSaved(Draft()); store.Autosave(Draft(4));
        var savedTime = DateTimeOffset.UtcNow.AddHours(-1).ToUnixTimeMilliseconds();
        using var config = JsonDocument.Parse(JsonSerializer.Serialize(new { unrelated_settings_updated_at = DateTimeOffset.UtcNow.AddDays(1).ToUnixTimeMilliseconds(), blockly_effects = new { fixture = new { source_updated_at = savedTime } } }));
        Assert.AreEqual(DateTimeOffset.FromUnixTimeMilliseconds(savedTime), StudioDraftStore.DurableTimestamp(config.RootElement, "fixture"));
        Assert.IsNotNull(new StudioDraftStore(f.Root).ObserveSaved(Draft(), StudioDraftStore.DurableTimestamp(config.RootElement, "fixture")));
        using var legacy = JsonDocument.Parse("{\"blockly_effects\":{\"fixture\":{}}}"); Assert.IsNull(StudioDraftStore.DurableTimestamp(legacy.RootElement, "fixture"));
    }
    [TestMethod]
    public void DiscardAndCleanSaveShutdownDoNotOfferFalseRecovery() {
        using var f = new Fixture(); var store = new StudioDraftStore(f.Root); store.ObserveSaved(Draft()); store.Autosave(Draft(4));
        store.Discard("fixture"); Assert.IsNull(new StudioDraftStore(f.Root).ObserveSaved(Draft()));
        var autosave = new StudioAutosaveCoordinator(store, (_, ct) => Task.Delay(Timeout.InfiniteTimeSpan, ct));
        autosave.Queue(Draft(5)); autosave.Saved(Draft(5)); autosave.Flush();
        Assert.IsNull(new StudioDraftStore(f.Root).ObserveSaved(Draft(5)));
    }
    [TestMethod]
    public void CleanShutdownFlushesPendingUnsavedDraftAndExistingConfigIsUntouched() {
        using var f = new Fixture(); var config = Path.Combine(f.Root, "config.json"); File.WriteAllText(config, "published-fixture-unchanged");
        var store = new StudioDraftStore(f.Root); store.ObserveSaved(Draft()); var auto = new StudioAutosaveCoordinator(store, (_, ct) => Task.Delay(Timeout.InfiniteTimeSpan, ct));
        auto.Queue(Draft(4)); auto.Flush(); Assert.IsNotNull(new StudioDraftStore(f.Root).ObserveSaved(Draft()));
        Assert.AreEqual("published-fixture-unchanged", File.ReadAllText(config));
    }
    [TestMethod]
    public void SnapshotRetentionAndReasonsAreBoundedAndRestoreDoesNotPublish() {
        using var f = new Fixture(); var store = new StudioDraftStore(f.Root); StudioSnapshot? oldest = null;
        for (var i = 0; i < 12; i++) { var snapshot = store.Snapshot(Draft(i), i % 2 == 0 ? "before_ai_apply" : "before_publish"); oldest ??= snapshot; }
        Assert.HasCount(10, store.Snapshots("fixture")); Assert.AreEqual(Draft(11).Fingerprint(), store.RestoreSnapshot("fixture", store.Snapshots("fixture")[0].Id).Fingerprint());
        Assert.ThrowsExactly<StudioPersistenceException>(() => store.RestoreSnapshot("fixture", oldest!.Id));
        Assert.ThrowsExactly<StudioPersistenceException>(() => store.Snapshot(Draft(), "build"));
        Assert.IsFalse(File.ReadAllText(store.JournalPath("fixture")).Contains("applied_plugin")); Assert.IsFalse(File.Exists(Path.Combine(f.Root, "config.json")));
    }
    [TestMethod]
    public void SnapshotPreAiAndPrePublishContainOriginalDraft() {
        using var f = new Fixture(); var store = new StudioDraftStore(f.Root);
        var ai = store.Snapshot(Draft(3), "before_ai_apply"); var publish = store.Snapshot(Draft(2), "before_publish");
        Assert.AreEqual(Draft(3).Fingerprint(), store.RestoreSnapshot("fixture", ai.Id).Fingerprint());
        Assert.AreEqual(Draft(2).Fingerprint(), store.RestoreSnapshot("fixture", publish.Id).Fingerprint());
    }
    [TestMethod]
    public void SecretsPathsAndPublicationAuthorityCannotBePersisted() {
        using var f = new Fixture(); var store = new StudioDraftStore(f.Root);
        foreach (var text in new[] { "sk-fixture_key_12345", "Bearer fixture-key", @"C:\local\compiler.exe", "/home/user/private" }) {
            using var json = JsonDocument.Parse(JsonSerializer.Serialize(new { blocks = new[] { new { type = "text", fields = new { TEXT = text } } } }));
            Assert.ThrowsExactly<StudioPersistenceException>(() => store.Snapshot(Draft() with { Json = json.RootElement.Clone() }, "before_ai_apply"));
        }
        using var secret = JsonDocument.Parse("{\"blocks\":[],\"api_key\":\"fixture\"}");
        Assert.ThrowsExactly<StudioPersistenceException>(() => (Draft() with { Json = secret.RootElement.Clone() }).Validate());
        using var authority = JsonDocument.Parse("{\"blocks\":[],\"applied_plugin_name\":\"fixture.dll\"}");
        Assert.ThrowsExactly<StudioPersistenceException>(() => (Draft() with { Json = authority.RootElement.Clone() }).Validate());
        Assert.ThrowsExactly<StudioPersistenceException>(() => store.JournalPath("../outside"));
        Assert.ThrowsExactly<StudioPersistenceException>(() => store.Snapshot(Draft(id: "sk-fixture_secret_12345"), "before_ai_apply"));
        Assert.ThrowsExactly<StudioPersistenceException>(() => store.JournalPath(null!));
        Assert.IsFalse(Directory.Exists(Path.Combine(f.Root, "Studio")));
    }
    [TestMethod]
    public void WindowsReservedNamesAndCaseDistinctProjectIdsKeepIndependentJournals() {
        using var f = new Fixture(); var store = new StudioDraftStore(f.Root);
        var names = new[] { "con", "nul", "PRN", "COM1", "Lpt9", "Aux", "Effect", "effect" };
        var paths = names.Select(store.JournalPath).ToArray(); Assert.AreEqual(names.Length, paths.Distinct(StringComparer.OrdinalIgnoreCase).Count());
        foreach (var name in names) { store.ObserveSaved(Draft() with { Name = name }); store.Autosave(Draft(4) with { Name = name }); }
        foreach (var name in names) Assert.AreEqual(name, new StudioDraftStore(f.Root).ObserveSaved(Draft() with { Name = name })!.Draft.Name);
    }
    [TestMethod]
    public void JournalRejectsCorruptionWithoutOverwritingAndRespectsInjectedDataRoot() {
        using var f = new Fixture(); var store = new StudioDraftStore(f.Root); store.ObserveSaved(Draft()); store.Autosave(Draft(4));
        Assert.IsTrue(store.JournalPath("fixture").StartsWith(f.Root, StringComparison.Ordinal));
        File.WriteAllText(store.JournalPath("fixture"), "{broken");
        Assert.ThrowsExactly<StudioPersistenceException>(() => store.ObserveSaved(Draft())); Assert.AreEqual("{broken", File.ReadAllText(store.JournalPath("fixture")));
    }
}
