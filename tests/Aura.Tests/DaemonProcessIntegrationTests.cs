using Aura_WinUI.Services;
using System.Net.Sockets;
using System.Diagnostics;

namespace Aura.Tests;

[TestClass, DoNotParallelize]
public sealed class DaemonProcessIntegrationTests
{
    [TestMethod, TestCategory("DesktopSmoke")]
    public async Task PackagedGuiLoadsXamlRedirectsSecondaryAndPreservesExternalDaemon()
    {
        var package = Environment.GetEnvironmentVariable("AURA_PACKAGE_DIR");
        if (string.IsNullOrEmpty(package)) { Assert.Inconclusive("Set AURA_PACKAGE_DIR for desktop-session package smoke"); return; }
        var existing = Process.GetProcessesByName("Aura");
        var guiExists = existing.Length != 0;
        foreach (var process in existing) process.Dispose();
        if (guiExists || DaemonSupervisor.CheckMutexExists()) { Assert.Inconclusive("Existing user GUI/core; never redirect or stop it for tests"); return; }
        var root = Path.Combine(Path.GetTempPath(), "Aura desktop 用户-" + Guid.NewGuid());
        var layout = RuntimeLayoutResolver.Resolve(package, root);
        RuntimePreparer.Prepare(layout);
        File.WriteAllText(layout.ConfigPath, """{"default_profile":"base","profiles":{"base":{"type":"static"}}}""");
        var original = File.ReadAllText(layout.ConfigPath);
        File.WriteAllText(Path.Combine(root,"client-settings.json"), """{"MinimizeToTray":false,"Theme":"Default"}""");
        using var child = new OwnedDaemonProcess(layout, true, new Dictionary<string,string>{{"LOCALAPPDATA",root}});
        Process? gui = null;
        try
        {
            var core = new AuraControlClient();
            var deadline = DateTime.UtcNow.AddSeconds(12);
            while (!(await core.GetRuntimeStatusAsync()).IsOnline)
            { Assert.IsFalse(child.HasExited); Assert.IsTrue(DateTime.UtcNow < deadline); await Task.Delay(100); }
            ProcessStartInfo StartInfo()
            {
                var info = new ProcessStartInfo(Path.Combine(package,"Aura.exe")) { WorkingDirectory=root, UseShellExecute=false, WindowStyle=ProcessWindowStyle.Hidden };
                info.Environment["AURA_DATA_ROOT"] = root; info.Environment["LOCALAPPDATA"] = root;
                info.Environment.Remove("AURA_DEV_ROOT"); info.Environment.Remove("AURA_DEV_BIN");
                return info;
            }
            gui = Process.Start(StartInfo())!;
            deadline = DateTime.UtcNow.AddSeconds(20);
            while (gui.MainWindowHandle == IntPtr.Zero)
            {
                Assert.IsFalse(gui.HasExited, "Packaged GUI exited during XAML initialization");
                Assert.IsTrue(DateTime.UtcNow < deadline, "No packaged GUI window");
                await Task.Delay(100); gui.Refresh();
            }
            await Task.Delay(500);
            using (var secondary = Process.Start(StartInfo())!)
            {
                using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
                await secondary.WaitForExitAsync(timeout.Token);
                Assert.AreEqual(0,secondary.ExitCode,"Secondary activation failed");
            }
            Assert.IsFalse(gui.HasExited);
            Assert.IsTrue(gui.CloseMainWindow(), "GUI did not accept normal window close");
            using (var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10))) await gui.WaitForExitAsync(timeout.Token);
            Assert.AreEqual(0,gui.ExitCode);
            Assert.IsFalse(child.HasExited,"GUI stopped an externally started daemon");
            Assert.AreEqual(original,File.ReadAllText(layout.ConfigPath));
        }
        finally
        {
            if (gui != null) { if (!gui.HasExited) { gui.Kill(); await gui.WaitForExitAsync(); } gui.Dispose(); }
            await child.StopAsync();
            Directory.Delete(root,true);
        }
    }

    [TestMethod]
    public async Task RealSuspendedChildPrivateShutdownAndExternalAttachment()
    {
        var bin = Environment.GetEnvironmentVariable("AURA_INTEGRATION_BIN");
        if (string.IsNullOrEmpty(bin)) { Assert.Inconclusive("Set AURA_INTEGRATION_BIN to a freshly built runtime directory"); return; }
        if (DaemonSupervisor.CheckMutexExists()) { Assert.Inconclusive("Existing user daemon; never stop it for tests"); return; }
        foreach (var port in new[] {19897,19898})
        {
            try { var listener = new TcpListener(System.Net.IPAddress.Loopback,port); listener.Start(); listener.Stop(); }
            catch (SocketException) { Assert.Inconclusive("User port occupied"); return; }
        }
        var repo = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory,"../../../../../"));
        var root = Path.Combine(Path.GetTempPath(),"Aura 生命周期 🧪-"+Guid.NewGuid()); Directory.CreateDirectory(root);
        var config = Path.Combine(root,"config.json");
        File.WriteAllText(config,"""{"default_profile":"base","profiles":{"base":{"type":"static"}}}""");
        var layout = new RuntimeLayout(Path.Combine(bin,"aura_daemon.exe"),root,config,Path.Combine(repo,"calibrated_keymap.json"),bin,Path.Combine(repo,"config.example.json"));
        var core = new AuraControlClient(); IOwnedDaemonProcess? child = null;
        var supervisor = new DaemonSupervisor(core, prepare:()=>layout,
            start:l => child = new OwnedDaemonProcess(l, true, new Dictionary<string,string>{{"LOCALAPPDATA",root}}));
        try
        {
            await supervisor.EnsureStartedAsync();
            Assert.IsTrue(supervisor.CoreReady, supervisor.StatusDescription);
            Assert.AreEqual(DaemonOwnership.SpawnedByWinUI,supervisor.Ownership);
            Assert.AreEqual(child!.Id,supervisor.Identity!.ProcessId);
            var attached = new DaemonSupervisor(core);
            await attached.EnsureStartedAsync(); Assert.AreEqual(DaemonOwnership.AttachedPreExisting,attached.Ownership);
            await attached.StopAsync(); Assert.IsFalse(child.HasExited,"Attached GUI stopped external daemon");
            var deadline = DateTime.UtcNow.AddSeconds(8);
            while (!await supervisor.ProbeWebServerAsync() && DateTime.UtcNow < deadline) await Task.Delay(100);
            Assert.IsTrue(supervisor.StudioWebReady,"Web identity did not match core");
            await supervisor.StopAsync();
            Assert.IsFalse(DaemonSupervisor.CheckMutexExists(),"Owned daemon did not release mutex");
        }
        finally { await supervisor.StopAsync(); Directory.Delete(root,true); }
    }
}
