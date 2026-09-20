using System.Net;
using System.Text;
using System.Text.Json;
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

    [TestMethod]
    public async Task GetLightingPresetsAsync_WhenValidPayload_ParsesAllPresetsCorrectly()
    {
        var json = """
        {
          "status": "ok",
          "api_version": 1,
          "presets": [
            {
              "id": "static",
              "display_name": "Static",
              "effect": "static",
              "supports_period": false,
              "default_period_ms": null
            },
            {
              "id": "breathing",
              "display_name": "Breathing",
              "effect": "breathing",
              "supports_period": true,
              "default_period_ms": 3000
            }
          ]
        }
        """;

        var fakeHandler = new FakeHttpMessageHandler(req =>
        {
            Assert.AreEqual("http://127.0.0.1:19897/api/lighting/presets", req.RequestUri?.ToString());
            return new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent(json, Encoding.UTF8, "application/json")
            };
        });

        var client = new AuraControlClient(new HttpClient(fakeHandler));
        var result = await client.GetLightingPresetsAsync();

        Assert.IsTrue(result.IsSuccess);
        Assert.AreEqual(2, result.Presets.Count);
        Assert.AreEqual("static", result.Presets[0].Id);
        Assert.AreEqual("Static", result.Presets[0].DisplayName);
        Assert.IsFalse(result.Presets[0].SupportsPeriod);
        Assert.IsNull(result.Presets[0].DefaultPeriodMs);

        Assert.AreEqual("breathing", result.Presets[1].Id);
        Assert.AreEqual("Breathing", result.Presets[1].DisplayName);
        Assert.IsTrue(result.Presets[1].SupportsPeriod);
        Assert.AreEqual(3000, result.Presets[1].DefaultPeriodMs);
    }

    [TestMethod]
    public async Task GetBaseLightingAsync_WhenSuccess_ParsesBaseLightingCorrectly()
    {
        var json = """
        {
          "status": "ok",
          "api_version": 1,
          "revision": "base_rev_12345",
          "lighting": {
            "profile_name": "desktop",
            "effect": "breathing",
            "preset_id": "breathing",
            "is_builtin_preset": true,
            "brightness": 0.85,
            "supports_period": true,
            "period_ms": 3200
          }
        }
        """;

        var fakeHandler = new FakeHttpMessageHandler(req =>
        {
            Assert.AreEqual("http://127.0.0.1:19897/api/lighting/base", req.RequestUri?.ToString());
            return new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent(json, Encoding.UTF8, "application/json")
            };
        });

        var client = new AuraControlClient(new HttpClient(fakeHandler));
        var result = await client.GetBaseLightingAsync();

        Assert.IsTrue(result.IsSuccess);
        Assert.AreEqual("base_rev_12345", result.Revision);
        Assert.IsNotNull(result.Lighting);
        Assert.AreEqual("desktop", result.Lighting.ProfileName);
        Assert.AreEqual("breathing", result.Lighting.Effect);
        Assert.AreEqual("breathing", result.Lighting.PresetId);
        Assert.IsTrue(result.Lighting.IsBuiltinPreset);
        Assert.AreEqual(0.85, result.Lighting.Brightness);
        Assert.IsTrue(result.Lighting.SupportsPeriod);
        Assert.AreEqual(3200, result.Lighting.PeriodMs);
    }

    [TestMethod]
    public async Task GetBaseLightingAsync_WhenAdvancedEffect_ParsesNonBuiltinPresetGracefully()
    {
        var json = """
        {
          "status": "ok",
          "api_version": 1,
          "revision": "studio_rev_999",
          "lighting": {
            "profile_name": "custom_studio_prof",
            "effect": "custom_keymap",
            "preset_id": "",
            "is_builtin_preset": false,
            "brightness": 1.0,
            "supports_period": false,
            "period_ms": null
          }
        }
        """;

        var fakeHandler = new FakeHttpMessageHandler(_ => new HttpResponseMessage(HttpStatusCode.OK)
        {
            Content = new StringContent(json, Encoding.UTF8, "application/json")
        });

        var client = new AuraControlClient(new HttpClient(fakeHandler));
        var result = await client.GetBaseLightingAsync();

        Assert.IsTrue(result.IsSuccess);
        Assert.IsNotNull(result.Lighting);
        Assert.AreEqual("custom_studio_prof", result.Lighting.ProfileName);
        Assert.AreEqual("custom_keymap", result.Lighting.Effect);
        Assert.IsFalse(result.Lighting.IsBuiltinPreset);
        Assert.AreEqual("", result.Lighting.PresetId);
        Assert.IsFalse(result.Lighting.SupportsPeriod);
        Assert.IsNull(result.Lighting.PeriodMs);
    }

    [TestMethod]
    public async Task UpdateBaseLightingAsync_WhenSuccess_ReturnsSuccessWithNewRevision()
    {
        var responseJson = """
        {
          "status": "ok",
          "api_version": 1,
          "message": "Base lighting updated",
          "revision": "new_base_rev_8888"
        }
        """;

        var fakeHandler = new FakeHttpMessageHandler(req =>
        {
            Assert.AreEqual(new HttpMethod("PATCH"), req.Method);
            Assert.AreEqual("http://127.0.0.1:19897/api/lighting/base", req.RequestUri?.ToString());
            return new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent(responseJson, Encoding.UTF8, "application/json")
            };
        });

        var client = new AuraControlClient(new HttpClient(fakeHandler));
        var patch = new BaseLightingPatchDto
        {
            ExpectedRevision = "old_rev",
            Preset = "wave",
            Brightness = 0.9,
            PeriodMs = 3500
        };

        var result = await client.UpdateBaseLightingAsync(patch);

        Assert.IsTrue(result.IsSuccess);
        Assert.AreEqual(UpdateProfileStatus.Success, result.Status);
        Assert.AreEqual("new_base_rev_8888", result.NewRevision);
    }

    [TestMethod]
    public async Task UpdateBaseLightingAsync_SparseSerialization_OmitsNullFields()
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
        var patch = new BaseLightingPatchDto
        {
            ExpectedRevision = "rev1",
            Preset = "breathing"
            // Brightness and PeriodMs are null
        };

        var result = await client.UpdateBaseLightingAsync(patch);
        Assert.IsTrue(result.IsSuccess);

        StringAssert.Contains(capturedBody, "\"preset\":\"breathing\"");
        StringAssert.Contains(capturedBody, "\"expected_revision\":\"rev1\"");
        Assert.IsFalse(capturedBody.Contains("brightness"), "Null brightness must not be serialized");
        Assert.IsFalse(capturedBody.Contains("period_ms"), "Null period_ms must not be serialized");
    }

    [TestMethod]
    public async Task UpdateBaseLightingAsync_WhenConflict409_ReturnsConflictWithCurrentRevision()
    {
        var conflictJson = """
        {
          "status": "error",
          "error": "Conflict",
          "message": "Configuration has been modified externally",
          "current_revision": "latest_rev_777"
        }
        """;

        var fakeHandler = new FakeHttpMessageHandler(_ => new HttpResponseMessage(HttpStatusCode.Conflict)
        {
            Content = new StringContent(conflictJson, Encoding.UTF8, "application/json")
        });

        var client = new AuraControlClient(new HttpClient(fakeHandler));
        var patch = new BaseLightingPatchDto
        {
            ExpectedRevision = "stale_rev_1",
            Brightness = 0.5
        };

        var result = await client.UpdateBaseLightingAsync(patch);

        Assert.IsFalse(result.IsSuccess);
        Assert.IsTrue(result.IsConflict);
        Assert.AreEqual("latest_rev_777", result.CurrentRevision);
        StringAssert.Contains(result.ErrorMessage, "modified externally");
    }

    [TestMethod]
    public async Task UpdateBaseLightingAsync_WhenValidationError400_ReturnsValidationError()
    {
        var errJson = """
        {
          "status": "error",
          "error": "Validation Error",
          "message": "Preset 'ghost' is not a supported builtin preset"
        }
        """;

        var fakeHandler = new FakeHttpMessageHandler(_ => new HttpResponseMessage(HttpStatusCode.BadRequest)
        {
            Content = new StringContent(errJson, Encoding.UTF8, "application/json")
        });

        var client = new AuraControlClient(new HttpClient(fakeHandler));
        var patch = new BaseLightingPatchDto
        {
            ExpectedRevision = "rev1",
            Preset = "ghost"
        };

        var result = await client.UpdateBaseLightingAsync(patch);

        Assert.IsFalse(result.IsSuccess);
        Assert.AreEqual(UpdateProfileStatus.ValidationError, result.Status);
        StringAssert.Contains(result.ErrorMessage, "not a supported builtin preset");
    }

    [TestMethod]
    public async Task GetLightingPresetsAsync_ParsesParameterSchemaCorrectly()
    {
        var json = """
        {
          "status": "ok",
          "api_version": 1,
          "presets": [
            {
              "id": "wave",
              "display_name": "Wave",
              "effect": "wave",
              "supports_period": true,
              "default_period_ms": 3500,
              "parameter_schema": [
                {
                  "key": "direction",
                  "display_name": "波浪方向",
                  "type": "enum",
                  "default_value": "diag_dl",
                  "options": [
                    { "value": "diag_dl", "label": "左下对角" },
                    { "value": "spread", "label": "居中扩散" }
                  ]
                },
                {
                  "key": "thickness",
                  "display_name": "波浪粗细",
                  "type": "number",
                  "min": 0.1,
                  "max": 5.0,
                  "step": 0.1,
                  "default_value": 1.0
                }
              ]
            }
          ]
        }
        """;

        var fakeHandler = new FakeHttpMessageHandler(_ => new HttpResponseMessage(HttpStatusCode.OK)
        {
            Content = new StringContent(json, Encoding.UTF8, "application/json")
        });

        var client = new AuraControlClient(new HttpClient(fakeHandler));
        var result = await client.GetLightingPresetsAsync();

        Assert.IsTrue(result.IsSuccess);
        Assert.AreEqual(1, result.Presets.Count);
        var p = result.Presets[0];
        Assert.AreEqual("wave", p.Id);
        Assert.AreEqual(2, p.ParameterSchema.Count);

        var s0 = p.ParameterSchema[0];
        Assert.AreEqual("direction", s0.Key);
        Assert.AreEqual(EffectParamType.Enum, s0.Type);
        Assert.AreEqual(2, s0.Options.Count);
        Assert.AreEqual("diag_dl", s0.Options[0].Value);

        var s1 = p.ParameterSchema[1];
        Assert.AreEqual("thickness", s1.Key);
        Assert.AreEqual(EffectParamType.Number, s1.Type);
        Assert.AreEqual(0.1, s1.Min);
        Assert.AreEqual(5.0, s1.Max);
        Assert.AreEqual(0.1, s1.Step);
    }

    [TestMethod]
    public async Task GetBaseLightingAsync_ParsesParametersCorrectly()
    {
        var json = """
        {
          "status": "ok",
          "api_version": 1,
          "revision": "base_rev_999",
          "lighting": {
            "profile_name": "desktop",
            "effect": "quicksand",
            "preset_id": "quicksand",
            "is_builtin_preset": true,
            "brightness": 0.9,
            "supports_period": true,
            "period_ms": 3500,
            "parameters": {
              "color1": [255, 0, 0],
              "direction": "spread",
              "thickness": 1.8
            }
          }
        }
        """;

        var fakeHandler = new FakeHttpMessageHandler(_ => new HttpResponseMessage(HttpStatusCode.OK)
        {
            Content = new StringContent(json, Encoding.UTF8, "application/json")
        });

        var client = new AuraControlClient(new HttpClient(fakeHandler));
        var result = await client.GetBaseLightingAsync();

        Assert.IsTrue(result.IsSuccess);
        Assert.IsNotNull(result.Lighting);
        Assert.IsNotNull(result.Lighting.Parameters);
        Assert.IsTrue(result.Lighting.Parameters.ContainsKey("direction"));
        Assert.AreEqual("spread", result.Lighting.Parameters["direction"].GetString());
        Assert.IsTrue(result.Lighting.Parameters.ContainsKey("thickness"));
        Assert.AreEqual(1.8, result.Lighting.Parameters["thickness"].GetDouble());
    }

    [TestMethod]
    public async Task UpdateBaseLightingAsync_SerializesSparseParametersCorrectly()
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
        var patch = new BaseLightingPatchDto
        {
            ExpectedRevision = "rev1",
            Parameters = new Dictionary<string, object>
            {
                { "direction", "spread" },
                { "thickness", 2.5 }
            }
        };

        var result = await client.UpdateBaseLightingAsync(patch);
        Assert.IsTrue(result.IsSuccess);

        StringAssert.Contains(capturedBody, "\"expected_revision\":\"rev1\"");
        StringAssert.Contains(capturedBody, "\"parameters\":{");
        StringAssert.Contains(capturedBody, "\"direction\":\"spread\"");
        StringAssert.Contains(capturedBody, "\"thickness\":2.5");
        Assert.IsFalse(capturedBody.Contains("preset"), "Null preset omitted");
        Assert.IsFalse(capturedBody.Contains("brightness"), "Null brightness omitted");
        Assert.IsFalse(capturedBody.Contains("period_ms"), "Null period_ms omitted");
    }

    [TestMethod]
    public void EffectParamValueComparer_ColorDeepEquality_MatchesJsonAndArrays()
    {
        var draft = new int[] { 255, 0, 128 };
        using var doc1 = JsonDocument.Parse("[255, 0, 128]");
        using var doc2 = JsonDocument.Parse("[255, 0, 129]");

        Assert.IsTrue(EffectParamValueComparer.AreValuesEqual(EffectParamType.Color, draft, doc1.RootElement));
        Assert.IsFalse(EffectParamValueComparer.AreValuesEqual(EffectParamType.Color, draft, doc2.RootElement));

        var list = new List<int> { 255, 0, 128 };
        Assert.IsTrue(EffectParamValueComparer.AreValuesEqual(EffectParamType.Color, draft, list));
        Assert.IsFalse(EffectParamValueComparer.AreValuesEqual(EffectParamType.Color, draft, null));
    }

    [TestMethod]
    public void EffectParamValueComparer_BooleanEquality_MatchesJsonAndBools()
    {
        using var docTrue = JsonDocument.Parse("true");
        using var docFalse = JsonDocument.Parse("false");

        Assert.IsTrue(EffectParamValueComparer.AreValuesEqual(EffectParamType.Boolean, true, docTrue.RootElement));
        Assert.IsTrue(EffectParamValueComparer.AreValuesEqual(EffectParamType.Boolean, false, docFalse.RootElement));
        Assert.IsFalse(EffectParamValueComparer.AreValuesEqual(EffectParamType.Boolean, true, docFalse.RootElement));
        Assert.IsFalse(EffectParamValueComparer.AreValuesEqual(EffectParamType.Boolean, false, docTrue.RootElement));
        Assert.IsTrue(EffectParamValueComparer.AreValuesEqual(EffectParamType.Boolean, true, true));
        Assert.IsFalse(EffectParamValueComparer.AreValuesEqual(EffectParamType.Boolean, true, null));
    }

    [TestMethod]
    public void EffectParamValueComparer_EnumEquality_MatchesOrdinalStrings()
    {
        using var doc = JsonDocument.Parse("\"diag_dl\"");

        Assert.IsTrue(EffectParamValueComparer.AreValuesEqual(EffectParamType.Enum, "diag_dl", doc.RootElement));
        Assert.IsFalse(EffectParamValueComparer.AreValuesEqual(EffectParamType.Enum, "spread", doc.RootElement));
        Assert.IsTrue(EffectParamValueComparer.AreValuesEqual(EffectParamType.Enum, "diag_dl", "diag_dl"));
        Assert.IsFalse(EffectParamValueComparer.AreValuesEqual(EffectParamType.Enum, "diag_dl", null));
    }

    [TestMethod]
    public void EffectParamValueComparer_NumberEquality_HandlesEpsilonAndMixedTypes()
    {
        using var docExact = JsonDocument.Parse("1.5");
        using var docClose = JsonDocument.Parse("1.50005");
        using var docDifferent = JsonDocument.Parse("1.6");

        Assert.IsTrue(EffectParamValueComparer.AreValuesEqual(EffectParamType.Number, 1.5, docExact.RootElement));
        Assert.IsTrue(EffectParamValueComparer.AreValuesEqual(EffectParamType.Number, 1.5, docClose.RootElement));
        Assert.IsFalse(EffectParamValueComparer.AreValuesEqual(EffectParamType.Number, 1.5, docDifferent.RootElement));
        Assert.IsTrue(EffectParamValueComparer.AreValuesEqual(EffectParamType.Number, 2, 2.0));
        Assert.IsFalse(EffectParamValueComparer.AreValuesEqual(EffectParamType.Number, 1.5, null));
    }
}
