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

    [TestMethod]
    public async Task GetProfilesAsync_WhenSuccess_ParsesProfilesAndRevision()
    {
        var json = """
        {
          "status": "ok",
          "api_version": 1,
          "revision": "0123456789abcdef",
          "profiles": [
            { "name": "desktop", "type": "breathing" },
            { "name": "coding", "type": "static" }
          ]
        }
        """;

        var fakeHandler = new FakeHttpMessageHandler(req =>
        {
            Assert.AreEqual("http://127.0.0.1:19897/api/lighting/profiles", req.RequestUri?.ToString());
            return new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent(json, Encoding.UTF8, "application/json")
            };
        });

        var client = new AuraControlClient(new HttpClient(fakeHandler));
        var result = await client.GetProfilesAsync();

        Assert.IsTrue(result.IsSuccess);
        Assert.AreEqual("0123456789abcdef", result.Revision);
        Assert.AreEqual(2, result.Profiles.Count);
        Assert.AreEqual("desktop", result.Profiles[0].Name);
        Assert.AreEqual("breathing", result.Profiles[0].Type);
        Assert.AreEqual("coding", result.Profiles[1].Name);
        Assert.AreEqual("static", result.Profiles[1].Type);
    }

    [TestMethod]
    public async Task GetProfilesAsync_WhenVersionMismatched_ReturnsFailure()
    {
        var json = """
        {
          "status": "ok",
          "api_version": 2,
          "profiles": []
        }
        """;

        var fakeHandler = new FakeHttpMessageHandler(_ => new HttpResponseMessage(HttpStatusCode.OK)
        {
            Content = new StringContent(json, Encoding.UTF8, "application/json")
        });

        var client = new AuraControlClient(new HttpClient(fakeHandler));
        var result = await client.GetProfilesAsync();

        Assert.IsFalse(result.IsSuccess);
        StringAssert.Contains(result.ErrorMessage, "Unsupported API version");
    }

    [TestMethod]
    public async Task GetProfileAsync_WhenSuccess_ParsesDetailWithSupportsPeriodAndInheritance()
    {
        var json = """
        {
          "status": "ok",
          "api_version": 1,
          "revision": "abcdef0123456789",
          "profile": {
            "name": "desktop",
            "type": "breathing",
            "brightness": 0.85,
            "fps": 25,
            "fps_inherited": true,
            "supports_period": true,
            "period_ms": 3200
          }
        }
        """;

        var fakeHandler = new FakeHttpMessageHandler(req =>
        {
            Assert.AreEqual("http://127.0.0.1:19897/api/lighting/profiles/desktop", req.RequestUri?.ToString());
            return new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent(json, Encoding.UTF8, "application/json")
            };
        });

        var client = new AuraControlClient(new HttpClient(fakeHandler));
        var result = await client.GetProfileAsync("desktop");

        Assert.IsTrue(result.IsSuccess);
        Assert.AreEqual("abcdef0123456789", result.Revision);
        Assert.IsNotNull(result.Profile);
        Assert.AreEqual("desktop", result.Profile.Name);
        Assert.AreEqual("breathing", result.Profile.Type);
        Assert.AreEqual(0.85, result.Profile.Brightness);
        Assert.AreEqual(25, result.Profile.Fps);
        Assert.IsTrue(result.Profile.FpsInherited);
        Assert.IsTrue(result.Profile.SupportsPeriod);
        Assert.AreEqual(3200, result.Profile.PeriodMs);
    }

    [TestMethod]
    public async Task GetProfileAsync_WhenNotFound_ReturnsFailure()
    {
        var fakeHandler = new FakeHttpMessageHandler(_ => new HttpResponseMessage(HttpStatusCode.NotFound)
        {
            Content = new StringContent("{\"status\":\"error\",\"message\":\"Profile not found\"}", Encoding.UTF8, "application/json")
        });

        var client = new AuraControlClient(new HttpClient(fakeHandler));
        var result = await client.GetProfileAsync("ghost");

        Assert.IsFalse(result.IsSuccess);
        StringAssert.Contains(result.ErrorMessage, "404");
    }

    [TestMethod]
    public async Task UpdateProfileAsync_WhenSuccess_ReturnsSuccessWithNewRevision()
    {
        var responseJson = """
        {
          "status": "ok",
          "api_version": 1,
          "message": "Profile updated",
          "revision": "new_rev_987654321"
        }
        """;

        var fakeHandler = new FakeHttpMessageHandler(req =>
        {
            Assert.AreEqual(new HttpMethod("PATCH"), req.Method);
            Assert.AreEqual("http://127.0.0.1:19897/api/lighting/profiles/desktop", req.RequestUri?.ToString());
            return new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent(responseJson, Encoding.UTF8, "application/json")
            };
        });

        var client = new AuraControlClient(new HttpClient(fakeHandler));
        var patch = new ProfilePatchDto
        {
            ExpectedRevision = "old_rev_12345",
            Brightness = 0.5
        };

        var result = await client.UpdateProfileAsync("desktop", patch);

        Assert.IsTrue(result.IsSuccess);
        Assert.AreEqual(UpdateProfileStatus.Success, result.Status);
        Assert.AreEqual("new_rev_987654321", result.NewRevision);
    }

    [TestMethod]
    public async Task UpdateProfileAsync_SparseSerialization_OmitsNullFields()
    {
        string capturedBody = "";
        var fakeHandler = new FakeHttpMessageHandler(req =>
        {
            capturedBody = req.Content?.ReadAsStringAsync().Result ?? "";
            return new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent("{\"status\":\"ok\",\"api_version\":1,\"revision\":\"rev2\"}", Encoding.UTF8, "application/json")
            };
        });

        var client = new AuraControlClient(new HttpClient(fakeHandler));
        var patch = new ProfilePatchDto
        {
            ExpectedRevision = "rev1",
            Brightness = 0.75
            // PeriodMs and Fps are null!
        };

        var result = await client.UpdateProfileAsync("desktop", patch);
        Assert.IsTrue(result.IsSuccess);

        // Verify null fields are not serialized, preserving sparse patch semantics
        StringAssert.Contains(capturedBody, "\"brightness\":0.75");
        StringAssert.Contains(capturedBody, "\"expected_revision\":\"rev1\"");
        Assert.IsFalse(capturedBody.Contains("fps"), "Null fps must not be serialized");
        Assert.IsFalse(capturedBody.Contains("period_ms"), "Null period_ms must not be serialized");
    }

    [TestMethod]
    public async Task UpdateProfileAsync_WhenConflict409_ReturnsConflictWithCurrentRevision()
    {
        var conflictJson = """
        {
          "status": "error",
          "error": "Conflict",
          "message": "Configuration has been modified externally",
          "current_revision": "studio_modified_rev_888"
        }
        """;

        var fakeHandler = new FakeHttpMessageHandler(_ => new HttpResponseMessage(HttpStatusCode.Conflict)
        {
            Content = new StringContent(conflictJson, Encoding.UTF8, "application/json")
        });

        var client = new AuraControlClient(new HttpClient(fakeHandler));
        var patch = new ProfilePatchDto
        {
            ExpectedRevision = "stale_rev_111",
            Brightness = 0.3
        };

        var result = await client.UpdateProfileAsync("desktop", patch);

        Assert.IsFalse(result.IsSuccess);
        Assert.IsTrue(result.IsConflict);
        Assert.AreEqual(UpdateProfileStatus.Conflict, result.Status);
        Assert.AreEqual("studio_modified_rev_888", result.CurrentRevision);
        StringAssert.Contains(result.ErrorMessage, "modified externally");
    }

    [TestMethod]
    public async Task UpdateProfileAsync_WhenValidationError400_ReturnsValidationError()
    {
        var errJson = """
        {
          "status": "error",
          "error": "Validation Error",
          "message": "Field 'brightness' must be a valid number in [0.0, 1.0]"
        }
        """;

        var fakeHandler = new FakeHttpMessageHandler(_ => new HttpResponseMessage(HttpStatusCode.BadRequest)
        {
            Content = new StringContent(errJson, Encoding.UTF8, "application/json")
        });

        var client = new AuraControlClient(new HttpClient(fakeHandler));
        var patch = new ProfilePatchDto
        {
            ExpectedRevision = "rev1",
            Brightness = 1.5
        };

        var result = await client.UpdateProfileAsync("desktop", patch);

        Assert.IsFalse(result.IsSuccess);
        Assert.AreEqual(UpdateProfileStatus.ValidationError, result.Status);
        StringAssert.Contains(result.ErrorMessage, "Field 'brightness'");
    }

    [TestMethod]
    public async Task UpdateProfileAsync_WhenMalformedJson_HandlesGracefullyWithoutThrowing()
    {
        var fakeHandler = new FakeHttpMessageHandler(_ => new HttpResponseMessage(HttpStatusCode.InternalServerError)
        {
            Content = new StringContent("<!DOCTYPE html><html><body>500 Error</body></html>", Encoding.UTF8, "text/html")
        });

        var client = new AuraControlClient(new HttpClient(fakeHandler));
        var patch = new ProfilePatchDto
        {
            ExpectedRevision = "rev1",
            Brightness = 0.5
        };

        var result = await client.UpdateProfileAsync("desktop", patch);

        Assert.IsFalse(result.IsSuccess);
        Assert.AreEqual(UpdateProfileStatus.Failure, result.Status);
    }
}
