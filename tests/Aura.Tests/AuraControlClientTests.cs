using System.Net;
using System.Text;
using Aura_WinUI.Services;

namespace Aura.Tests;

[TestClass]
public sealed class AuraControlClientTests
{
    private sealed class FakeHttpMessageHandler : HttpMessageHandler
    {
        private readonly Func<HttpRequestMessage, HttpResponseMessage> _handler;

        public FakeHttpMessageHandler(Func<HttpRequestMessage, HttpResponseMessage> handler)
        {
            _handler = handler;
        }

        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            return Task.FromResult(_handler(request));
        }
    }

    [TestMethod]
    public async Task GetRuntimeStatusAsync_WhenDaemonOffline_ReturnsOfflineGracefullyWithoutThrowing()
    {
        // 模拟未开放端口 (例如 19999)，网络完全不可达
        using var client = new HttpClient { Timeout = TimeSpan.FromMilliseconds(500) };
        var controlClient = new AuraControlClient(new HttpClient(new FakeHttpMessageHandler(_ =>
            throw new HttpRequestException("Connection refused (127.0.0.1:19897)"))));

        var result = await controlClient.GetRuntimeStatusAsync();

        Assert.IsNotNull(result);
        Assert.IsFalse(result.IsOnline);
        StringAssert.Contains(result.ErrorMessage, "Connection refused");
        Assert.AreEqual("Offline", result.CoreStatusDisplayName);
        Assert.AreEqual("Disconnected", result.DeviceStatusDisplayName);
        Assert.AreEqual("Unknown", result.BackendDisplayName);
        Assert.AreEqual("Idle", result.GsiStatusDisplayName);
        Assert.AreEqual("--", result.ActiveProfileDisplayName);
        Assert.AreEqual("--", result.FpsDisplayName);
    }

    [TestMethod]
    public async Task GetRuntimeStatusAsync_WhenVersionMismatched_ExplicitlyDegradesToOffline()
    {
        // Constraint 6 契约测试：版本不匹配时不能当作正常状态
        var json = """
        {
          "status": "ok",
          "api_version": 99,
          "hardware": { "connected": true, "state": "connected" },
          "runtime": { "active_profile": "desktop", "fps": 25 },
          "gsi": { "active": false }
        }
        """;

        var fakeHandler = new FakeHttpMessageHandler(_ => new HttpResponseMessage(HttpStatusCode.OK)
        {
            Content = new StringContent(json, Encoding.UTF8, "application/json")
        });

        var controlClient = new AuraControlClient(new HttpClient(fakeHandler));
        var result = await controlClient.GetRuntimeStatusAsync();

        Assert.IsNotNull(result);
        Assert.IsFalse(result.IsOnline);
        StringAssert.Contains(result.ErrorMessage, "Unsupported API version");
        Assert.AreEqual("Offline", result.CoreStatusDisplayName);
    }

    [TestMethod]
    public async Task GetRuntimeStatusAsync_WhenStatusNotOk_DegradesToOffline()
    {
        var json = """
        {
          "status": "error",
          "api_version": 1,
          "error": "Service Unavailable"
        }
        """;

        var fakeHandler = new FakeHttpMessageHandler(_ => new HttpResponseMessage(HttpStatusCode.OK)
        {
            Content = new StringContent(json, Encoding.UTF8, "application/json")
        });

        var controlClient = new AuraControlClient(new HttpClient(fakeHandler));
        var result = await controlClient.GetRuntimeStatusAsync();

        Assert.IsNotNull(result);
        Assert.IsFalse(result.IsOnline);
        StringAssert.Contains(result.ErrorMessage, "Daemon reported non-ok status");
    }

    [TestMethod]
    public async Task GetRuntimeStatusAsync_WhenValidV1Payload_ParsesAllFieldsCorrectly()
    {
        var json = """
        {
          "status": "ok",
          "api_version": 1,
          "hardware": {
            "connected": true,
            "state": "connected",
            "configured_backend": "auto",
            "active_backend": "native_hid",
            "device_path": "HID\\VID_0B05&PID_1A38",
            "last_error": ""
          },
          "runtime": {
            "active_profile": "cs2",
            "fps": 25,
            "dry_run": false,
            "foreground_process": "cs2.exe"
          },
          "gsi": {
            "active": true
          }
        }
        """;

        var fakeHandler = new FakeHttpMessageHandler(_ => new HttpResponseMessage(HttpStatusCode.OK)
        {
            Content = new StringContent(json, Encoding.UTF8, "application/json")
        });

        var controlClient = new AuraControlClient(new HttpClient(fakeHandler));
        var result = await controlClient.GetRuntimeStatusAsync();

        Assert.IsNotNull(result);
        Assert.IsTrue(result.IsOnline);
        Assert.AreEqual("Connected", result.DeviceStatusDisplayName);
        Assert.AreEqual("Native HID", result.BackendDisplayName);
        Assert.AreEqual("Running", result.CoreStatusDisplayName);
        Assert.AreEqual("cs2", result.ActiveProfileDisplayName);
        Assert.AreEqual("25 FPS", result.FpsDisplayName);
        Assert.AreEqual("Active", result.GsiStatusDisplayName);
        Assert.AreEqual("HID\\VID_0B05&PID_1A38", result.Data?.Hardware.DevicePath);
    }

    [TestMethod]
    public async Task GetRuntimeStatusAsync_WhenDryRun_ShowsDryRunPriority()
    {
        // Constraint 3 契约测试：
        // dry-run 下 hardware.connected 必须为 false，active_backend 为 "dry_run"
        // WinUI Device 状态优先显示 "Dry-Run"，不得显示物理设备 Connected
        var json = """
        {
          "status": "ok",
          "api_version": 1,
          "hardware": {
            "connected": false,
            "state": "connected",
            "configured_backend": "auto",
            "active_backend": "dry_run",
            "device_path": "",
            "last_error": ""
          },
          "runtime": {
            "active_profile": "desktop",
            "fps": 25,
            "dry_run": true,
            "foreground_process": "explorer.exe"
          },
          "gsi": {
            "active": false
          }
        }
        """;

        var fakeHandler = new FakeHttpMessageHandler(_ => new HttpResponseMessage(HttpStatusCode.OK)
        {
            Content = new StringContent(json, Encoding.UTF8, "application/json")
        });

        var controlClient = new AuraControlClient(new HttpClient(fakeHandler));
        var result = await controlClient.GetRuntimeStatusAsync();

        Assert.IsNotNull(result);
        Assert.IsTrue(result.IsOnline);
        Assert.IsTrue(result.IsDryRun);
        Assert.AreEqual("Dry-Run", result.DeviceStatusDisplayName);
        Assert.AreEqual("Dry-Run", result.BackendDisplayName);
        Assert.AreEqual("Running", result.CoreStatusDisplayName);
        Assert.AreEqual("desktop", result.ActiveProfileDisplayName);
        Assert.AreEqual("Idle", result.GsiStatusDisplayName);
    }
}
