using System.Net;
using System.Text;
using System.Text.Json;
using System.Security.Cryptography;
using Aura_WinUI.Services;

namespace Aura.Tests;

[TestClass]
public sealed class ProductizationTests
{
    [TestMethod]
    public void EmbeddedStudioUrlAndThemeMessagesAreRestrictedToPresentation()
    {
        var effect = EmbeddedStudioNavigation.InitialUrl("studio", "dark");
        Assert.AreEqual("http",effect.Scheme); Assert.AreEqual("127.0.0.1",effect.Host); Assert.AreEqual(19898,effect.Port);
        Assert.AreEqual("/?host=winui&tab=studio&theme=dark",effect.PathAndQuery);
        Assert.AreEqual("/?host=winui&tab=automation&theme=light",
            EmbeddedStudioNavigation.InitialUrl("automation","light").PathAndQuery);
        Assert.Throws<ArgumentOutOfRangeException>(() => EmbeddedStudioNavigation.InitialUrl("lighting","dark"));
        Assert.Throws<ArgumentOutOfRangeException>(() => EmbeddedStudioNavigation.InitialUrl("studio","auto"));
        using var message = JsonDocument.Parse(EmbeddedStudioNavigation.ThemeMessage("light"));
        Assert.AreEqual(2,message.RootElement.EnumerateObject().Count());
        Assert.AreEqual("theme_changed",message.RootElement.GetProperty("type").GetString());
        Assert.AreEqual("light",message.RootElement.GetProperty("theme").GetString());
        Assert.Throws<ArgumentOutOfRangeException>(() => EmbeddedStudioNavigation.ThemeMessage("javascript:alert(1)"));
    }
    [TestMethod]
    public async Task GlobalFpsUsesTypedNarrowContractAndRejectsMissingVersion()
    {
        var client = new AuraControlClient(new(new Handler(async request => {
            Assert.AreEqual(19897,request.RequestUri!.Port);
            Assert.AreEqual("/api/lighting/global",request.RequestUri.AbsolutePath);
            if (request.Method == HttpMethod.Get) return Json(new {status="ok",api_version=1,revision="initial",fps=25});
            using var document = JsonDocument.Parse(await request.Content!.ReadAsStringAsync());
            Assert.AreEqual(2,document.RootElement.EnumerateObject().Count());
            Assert.AreEqual(40,document.RootElement.GetProperty("fps").GetInt32());
            Assert.AreEqual("initial",document.RootElement.GetProperty("expected_revision").GetString());
            return Json(new {status="ok",api_version=1,revision="updated"});
        })));
        Assert.AreEqual(25,(await client.GetGlobalLightingAsync()).Value!.Fps);
        Assert.AreEqual("updated",(await client.UpdateGlobalLightingAsync(40,"initial")).Value!.Revision);
        var invalid = new AuraControlClient(new(new Handler(_ => Task.FromResult(Json(new {status="ok",revision="x",fps=25})))));
        Assert.IsFalse((await invalid.GetGlobalLightingAsync()).IsSuccess);
    }
    [TestMethod]
    public async Task GlobalFpsConflictRemainsFailureAndUnknownGsiEnumRemainsVisible()
    {
        var client = new AuraControlClient(new(new Handler(_ => Task.FromResult(Json(new {message="changed"},409)))));
        var result = await client.UpdateGlobalLightingAsync(40,"old");
        Assert.IsFalse(result.IsSuccess); Assert.AreEqual(409,result.StatusCode);
        Assert.AreEqual("已安放",GsiPresentation.Label("planted"));
        Assert.AreEqual("future-value",GsiPresentation.Label("future-value"));
        Assert.AreEqual("—",GsiPresentation.Label("—"));
    }
    private sealed class Handler(Func<HttpRequestMessage, Task<HttpResponseMessage>> send) : HttpMessageHandler
    { protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token) => send(request); }
    private static HttpResponseMessage Json(object value, int status = 200) => new((HttpStatusCode)status)
    { Content = new StringContent(JsonSerializer.Serialize(value), Encoding.UTF8, "application/json") };
    private static object Status(string instance = "external", int pid = 15, bool suppressed = false) => new {
        status = "ok", api_version = 1, identity = new { service = "aura_daemon", instance_id = instance, process_id = pid, product_version = "0.1.0-alpha.4", config_path = "external.json" },
        studio_web = new { suppressed }, hardware = new { connected = false }, runtime = new { dry_run = true } };
    private sealed class Child : IOwnedDaemonProcess
    {
        public int Id => 42; public string InstanceId => "owned"; public bool HasExited { get; set; }
        public int Stops, Disposals;
        public Task StopAsync() { Stops++; HasExited = true; return Task.CompletedTask; }
        public void Dispose() => Disposals++;
    }
    [TestMethod]
    public async Task AttachedExitDoesNotPrepareSpawnOrStopExternalService()
    {
        var client = new AuraControlClient(new(new Handler(_ => Task.FromResult(Json(Status())))));
        var supervisor = new DaemonSupervisor(client, prepare: () => throw new Exception("Must not prepare"), start: _ => throw new Exception("Must not spawn"));
        await supervisor.EnsureStartedAsync();
        Assert.IsTrue(supervisor.CoreReady); Assert.AreEqual(DaemonOwnership.AttachedPreExisting, supervisor.Ownership);
        await supervisor.StopAsync();
        Assert.Contains("外部核心保持运行", supervisor.StatusDescription);
    }
    [TestMethod]
    public async Task CoreRemainsReadyWhenWebIsSuppressedOrWrongService()
    {
        var core = new AuraControlClient(new(new Handler(_ => Task.FromResult(Json(Status(suppressed: true))))));
        var web = new AuraWebClient(new(new Handler(_ => Task.FromResult(Json(new { service = "other", web_api_version = 2 })))));
        var supervisor = new DaemonSupervisor(core, web);
        await supervisor.RefreshAsync(); Assert.IsTrue(supervisor.CoreReady); Assert.IsTrue(supervisor.WebSuppressed);
        Assert.IsFalse(await supervisor.ProbeWebServerAsync()); Assert.IsTrue(supervisor.CoreReady);
        await supervisor.StopAsync();
    }
    [TestMethod]
    public async Task WebReadinessIsInvalidatedWhenCoreRestarts()
    {
        string instance = "first";
        var core = new AuraControlClient(new(new Handler(_ => Task.FromResult(Json(Status(instance))))));
        var web = new AuraWebClient(new(new Handler(_ => Task.FromResult(Json(new { service="aura_web_ui",web_api_version=2,daemon_instance_id="first" })))));
        var supervisor = new DaemonSupervisor(core,web);
        await supervisor.RefreshAsync(); Assert.IsTrue(await supervisor.ProbeWebServerAsync());
        instance = "replacement"; await supervisor.RefreshAsync();
        Assert.IsTrue(supervisor.CoreReady); Assert.IsFalse(supervisor.StudioWebReady);
        Assert.IsFalse(await supervisor.ProbeWebServerAsync());
        await supervisor.StopAsync();
    }
    [TestMethod]
    public async Task OwnedCrashReconnectDisposesOldChildAndSpawnsOnce()
    {
        Child? current = null; int starts = 0;
        var core = new AuraControlClient(new(new Handler(_ => Task.FromResult(current is { HasExited:false } ? Json(Status("owned",42)) : Json(new {},503)))));
        var supervisor = new DaemonSupervisor(core,prepare:()=>new("d","w","c","k","r","t"),start:_=>{starts++;return current=new();},mutexExists:()=>false);
        await supervisor.EnsureStartedAsync(); var old = current!; old.HasExited=true;
        await Task.WhenAll(supervisor.EnsureStartedAsync(),supervisor.EnsureStartedAsync());
        Assert.AreEqual(2,starts); Assert.AreEqual(1,old.Disposals); Assert.IsTrue(supervisor.CoreReady);
        await supervisor.StopAsync();
    }
    [TestMethod]
    public async Task OwnedStartupSerializesConcurrentCallsAndStopsOnlyItsChild()
    {
        Child? child = null; int starts = 0;
        var core = new AuraControlClient(new(new Handler(_ => Task.FromResult(child == null ? Json(new { }, 503) : Json(Status("owned",42))))));
        var supervisor = new DaemonSupervisor(core, prepare: () => new("daemon", "data", "config", "keymap", "runtime", "template"),
            start: _ => { starts++; return child = new(); }, mutexExists: () => false);
        await Task.WhenAll(supervisor.EnsureStartedAsync(), supervisor.EnsureStartedAsync());
        Assert.AreEqual(1, starts); Assert.AreEqual(DaemonOwnership.SpawnedByWinUI, supervisor.Ownership);
        await supervisor.StopAsync(); Assert.AreEqual(1, child!.Stops); Assert.AreEqual(1, child.Disposals);
        await supervisor.EnsureStartedAsync(); Assert.AreEqual(1, starts);
    }
    [TestMethod]
    public async Task MutexWithoutCoreDoesNotSpawnOrClaimOwnership()
    {
        var core = new AuraControlClient(new(new Handler(_ => Task.FromResult(Json(new { },503)))));
        var supervisor = new DaemonSupervisor(core, prepare: () => throw new Exception("no prepare"), mutexExists: () => true);
        await supervisor.EnsureStartedAsync(); Assert.IsFalse(supervisor.CoreReady); Assert.AreEqual(DaemonOwnership.None, supervisor.Ownership);
        await supervisor.StopAsync();
    }
    [TestMethod]
    public async Task ReplacedDaemonCannotInheritSpawnOwnership()
    {
        var child = new Child(); int phase = 0;
        var core = new AuraControlClient(new(new Handler(_ => Task.FromResult(phase++ == 0 ? Json(new {},503) : Json(Status(phase == 2 ? "owned" : "external", phase == 2 ? 42 : 99))))));
        var supervisor = new DaemonSupervisor(core, prepare: () => new("d","w","c","k","r","t"), start: _ => child, mutexExists: () => false);
        await supervisor.EnsureStartedAsync(); child.HasExited = true;
        await supervisor.RefreshAsync(); Assert.AreEqual(DaemonOwnership.AttachedPreExisting, supervisor.Ownership);
        await supervisor.StopAsync(); // Fake child receives cleanup, but no discovered external object exists to stop.
        Assert.AreEqual(1, child.Disposals);
    }
    [TestMethod]
    public async Task SimulationWaitsForAppliedSequenceAndNeverRetriesPost()
    {
        int posts = 0, gets = 0;
        var core = new AuraControlClient(new(new Handler(request => {
            Assert.AreEqual(19897, request.RequestUri!.Port);
            if (request.Method == HttpMethod.Post) { posts++; return Task.FromResult(Json(new { status = "queued", sequence = 4, instance_id = "one" },202)); }
            return Task.FromResult(Json(new { gsi_api_version = 1, instance_id = "one", applied_sequence = ++gets < 2 ? 3 : 4, enabled = true, source = "simulation" }));
        })));
        var result = await core.UpdateSimulationAsync(new() { IncrementKill = true });
        Assert.IsTrue(result.IsSuccess); Assert.AreEqual(1, posts); Assert.AreEqual(2, gets);
    }
    [TestMethod]
    public async Task SimulationRestartDuringAcknowledgementIsNotSuccess()
    {
        var core = new AuraControlClient(new(new Handler(request => Task.FromResult(request.Method == HttpMethod.Post ?
            Json(new { status = "queued", sequence = 1, instance_id = "before" },202) :
            Json(new { gsi_api_version = 1, instance_id = "after", source = "simulation", applied_sequence = 100 })))));
        var result = await core.UpdateSimulationAsync(new() { Enabled = true }); Assert.IsFalse(result.IsSuccess);
    }
    [TestMethod]
    public async Task GsiMissingFieldsStayUnknownAndMissingContractIsRejected()
    {
        var core = new AuraControlClient(new(new Handler(_ => Task.FromResult(Json(new { gsi_api_version = 1, instance_id = "one", source = "real", data = new { } })))));
        var result = await core.GetGsiAsync(); Assert.IsTrue(result.IsSuccess); Assert.AreEqual("—", result.Value!.Field("player.state.health"));
        var old = new AuraControlClient(new(new Handler(_ => Task.FromResult(Json(new { connected = true })))));
        Assert.IsFalse((await old.GetGsiAsync()).IsSuccess);
    }
    [TestMethod]
    public async Task InstallationUsesWebPortSelectedPathAndExpectedFileRevision()
    {
        var web = new AuraWebClient(new(new Handler(async request => {
            Assert.AreEqual(19898, request.RequestUri!.Port); Assert.AreEqual("/api/gsi/install-cfg", request.RequestUri.AbsolutePath);
            using var body = JsonDocument.Parse(await request.Content!.ReadAsStringAsync());
            Assert.AreEqual("second", body.RootElement.GetProperty("target_dir").GetString());
            Assert.AreEqual("missing", body.RootElement.GetProperty("expected_cfg_revision").GetString());
            return Json(new { status = "ok", path = "second/file" });
        })));
        Assert.IsTrue((await web.InstallCfgAsync(new() { Path = "second", Revision = "missing" })).IsSuccess);
    }
    [TestMethod]
    public async Task InstallationConflictIsNotSuccessful()
    {
        var web = new AuraWebClient(new(new Handler(_ => Task.FromResult(Json(new { message = "changed" },409)))));
        var result = await web.InstallCfgAsync(new() { Path = "target", Revision = "old" });
        Assert.IsFalse(result.IsSuccess); Assert.AreEqual(409, result.StatusCode);
    }
    private static readonly Dictionary<string,string> Assets = new() {
        ["daemon"]="aura_daemon.exe", ["web"]="aura_web_ui.exe", ["keymap"]="calibrated_keymap.json", ["template"]="config.example.json",
        ["studio"]="web/index.html", ["sdk_effect"]="include/engine/effect.h", ["sdk_plugin"]="include/engine/plugin_interface.h",
        ["sdk_types"]="include/aura/aura_types.h", ["sdk_keymap"]="include/aura/keymap.h" };
    private static void Payload(string app)
    {
        var root = Path.Combine(app,"runtime-payload"); var files = new List<RuntimeFile>();
        foreach (var (role,path) in Assets)
        {
            var full = Path.Combine(root,path); Directory.CreateDirectory(Path.GetDirectoryName(full)!); File.WriteAllText(full,role);
            files.Add(new(role,path,Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(full))).ToLowerInvariant()));
        }
        File.WriteAllText(Path.Combine(root,"runtime-manifest.json"),JsonSerializer.Serialize(new RuntimeManifest(1,"0.1.0-alpha.4","0123456789abcdef",files),RuntimeLayoutResolver.JsonOptions));
    }
    [TestMethod]
    public void PrepareIsolatedBundlePreservesConfigurationAndPlugins()
    {
        var root = Path.Combine(Path.GetTempPath(),"Aura-layout-"+Guid.NewGuid()); Directory.CreateDirectory(root);
        try
        {
            Payload(root); var data = Path.Combine(root,"用户 data"); Directory.CreateDirectory(Path.Combine(data,"plugins"));
            File.WriteAllText(Path.Combine(data,"config.json"),"user-data"); File.WriteAllText(Path.Combine(data,"plugins","custom.dll"),"user-plugin");
            var layout = RuntimeLayoutResolver.Resolve(root,data);
            Assert.IsFalse(Directory.Exists(layout.RuntimeDirectory)); // Resolve is pure.
            RuntimePreparer.Prepare(layout); RuntimePreparer.Prepare(layout);
            Assert.AreEqual("user-data",File.ReadAllText(layout.ConfigPath));
            Assert.AreEqual("user-plugin",File.ReadAllText(Path.Combine(data,"plugins","custom.dll")));
            Assert.AreEqual("daemon",File.ReadAllText(layout.DaemonExecutablePath));
            File.WriteAllText(layout.DaemonExecutablePath,"damaged");
            Assert.ThrowsExactly<InvalidDataException>(()=>RuntimePreparer.Prepare(layout));
        }
        finally { Directory.Delete(root,true); }
    }
    [TestMethod]
    public void MissingDaemonCannotFallBackToGuiAndTraversalIsRejected()
    {
        var root = Path.Combine(Path.GetTempPath(),"Aura-manifest-"+Guid.NewGuid()); Directory.CreateDirectory(root);
        try
        {
            File.WriteAllText(Path.Combine(root,"Aura.exe"),"gui");
            Assert.ThrowsExactly<DirectoryNotFoundException>(()=>RuntimeLayoutResolver.Resolve(root,root));
            Payload(root); var payload = Path.Combine(root,"runtime-payload");
            var manifest = RuntimeLayoutResolver.ReadManifest(payload); manifest.Files[0]=new("daemon","../Aura.exe",new string('0',64));
            File.WriteAllText(Path.Combine(payload,"runtime-manifest.json"),JsonSerializer.Serialize(manifest,RuntimeLayoutResolver.JsonOptions));
            Assert.ThrowsExactly<InvalidDataException>(()=>RuntimeLayoutResolver.Resolve(root,root));
        }
        finally { Directory.Delete(root,true); }
    }
    [TestMethod]
    public void DevModeUsesExplicitBuildOutputWithoutCreatingConfig()
    {
        var root = Path.Combine(Path.GetTempPath(),"Aura-dev-"+Guid.NewGuid());
        var layout = RuntimeLayoutResolver.Resolve("ignored",Path.Combine(root,"data"),root,Path.Combine(root,"chosen-bin"));
        Assert.AreEqual(Path.Combine(root,"chosen-bin","aura_daemon.exe"),layout.DaemonExecutablePath);
        Assert.IsFalse(Directory.Exists(root));
    }
}
