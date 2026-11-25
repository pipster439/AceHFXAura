using System.Net;
using System.Text;
using System.Text.Json;
using Aura_WinUI.Services;

namespace Aura.Tests;

[TestClass]
public sealed class HallLightingOverlayClientTests
{
    private sealed class StubHandler(Func<HttpRequestMessage, HttpResponseMessage> responder) : HttpMessageHandler
    {
        public HttpRequestMessage? LastRequest { get; private set; }
        public string? LastBody { get; private set; }

        protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            LastRequest = request;
            if (request.Content != null)
            {
                LastBody = await request.Content.ReadAsStringAsync(cancellationToken);
            }
            return responder(request);
        }
    }

    private static HttpResponseMessage JsonResponse(HttpStatusCode code, object payload) => new(code)
    {
        Content = new StringContent(JsonSerializer.Serialize(payload), Encoding.UTF8, "application/json")
    };

    [TestMethod]
    public async Task GetHallLightingOverlay_ParsesValidPayload()
    {
        var handler = new StubHandler(_ => JsonResponse(HttpStatusCode.OK, new
        {
            status = "ok",
            api_version = 1,
            revision = "rev-1",
            hall_lighting = new
            {
                enabled = true,
                mode = "halo",
                color = new[] { 0, 220, 255 },
                secondary_color = new[] { 255, 80, 180 },
                intensity = 0.85,
                min_depth_mm = 0.2
            }
        }));
        var client = new AuraControlClient(new HttpClient(handler));

        var res = await client.GetHallLightingOverlayAsync();
        Assert.IsTrue(res.IsSuccess);
        Assert.AreEqual("rev-1", res.Revision);
        Assert.IsNotNull(res.HallLighting);
        Assert.IsTrue(res.HallLighting.Enabled);
        Assert.AreEqual("halo", res.HallLighting.Mode);
        CollectionAssert.AreEqual(new[] { 0, 220, 255 }, res.HallLighting.Color);
        Assert.AreEqual(0.85, res.HallLighting.Intensity, 1e-6);
        Assert.AreEqual(0.2, res.HallLighting.MinDepthMm, 1e-6);
    }

    [TestMethod]
    public async Task UpdateHallLightingOverlay_HandlesSuccessConflictAndValidationError()
    {
        var handler = new StubHandler(_ => JsonResponse(HttpStatusCode.OK, new
        {
            status = "ok",
            api_version = 1,
            revision = "rev-2",
            hall_lighting = new
            {
                enabled = true,
                mode = "gradient",
                color = new[] { 255, 190, 40 },
                secondary_color = new[] { 255, 80, 180 },
                intensity = 0.7,
                min_depth_mm = 0.1
            }
        }));
        var client = new AuraControlClient(new HttpClient(handler));

        var ok = await client.UpdateHallLightingOverlayAsync(new HallLightingOverlayPatchDto
        {
            ExpectedRevision = "rev-1",
            Enabled = true,
            Mode = "gradient",
            Color = [255, 190, 40],
            Intensity = 0.7,
            MinDepthMm = 0.1
        });
        Assert.IsTrue(ok.IsSuccess);
        Assert.AreEqual("rev-2", ok.NewRevision);
        Assert.AreEqual("gradient", ok.HallLighting?.Mode);
        Assert.IsNotNull(handler.LastBody);
        Assert.IsFalse(handler.LastBody.Contains("secondary_color", StringComparison.Ordinal));

        var conflictClient = new AuraControlClient(new HttpClient(new StubHandler(_ => JsonResponse(HttpStatusCode.Conflict, new
        {
            status = "error",
            current_revision = "rev-9",
            message = "revision mismatch"
        }))));
        var conflict = await conflictClient.UpdateHallLightingOverlayAsync(new HallLightingOverlayPatchDto
        {
            ExpectedRevision = "rev-1",
            Enabled = false
        });
        Assert.IsTrue(conflict.IsConflict);
        Assert.AreEqual("rev-9", conflict.CurrentRevision);

        var badClient = new AuraControlClient(new HttpClient(new StubHandler(_ => JsonResponse(HttpStatusCode.BadRequest, new
        {
            status = "error",
            message = "Invalid mode"
        }))));
        var bad = await badClient.UpdateHallLightingOverlayAsync(new HallLightingOverlayPatchDto
        {
            ExpectedRevision = "rev-9",
            Mode = "invalid"
        });
        Assert.AreEqual(UpdateProfileStatus.ValidationError, bad.Status);
        StringAssert.Contains(bad.ErrorMessage, "Invalid mode");
    }
}
