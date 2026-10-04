using System.Buffers.Binary;
using System.Diagnostics;
using System.IO.Pipes;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using System.Text.Json;
using AceHFX.AsusPlatform;
using AceHFX.AsusPlatform.Ipc;
using AceHFX.AsusPlatform.Runtime;
using AceHFX.AsusPlatform.Cooling;
using AceHFX.Service;
using Microsoft.Win32;

[assembly: DoNotParallelize]
namespace AsusPlatform.Tests;

internal sealed class Inventory : IRuntimeInventory
{
    public bool MissingService, StoppedService, MissingCom, Denied, OutOfProc32Only;
    public ComponentStatus Service(string name) => new(name,
        Denied ? Evidence.Denied("FixtureAccessDenied") : Evidence.Detected(!MissingService, "FixtureSCM"),
        Evidence.Detected(!MissingService, "FixtureSCM"),
        Denied ? Evidence.Denied("FixtureAccessDenied") : MissingService ? Evidence.Detected(false, "Missing") : StoppedService ?
            new(CapabilityState.InstalledButStopped, ExecutionState.Succeeded, "Stopped", false) : Evidence.Detected(true, "Running"),
        Evidence.NotAttempted("M1"), Evidence.NotAttempted("M1"), new("999.0.0", ExecutionState.Succeeded, "Unknown", "FutureVersion"));
    public ComponentStatus ComClass(string name, string classId, RegistryView view) => Service(name) with
        { Registered = Denied ? Evidence.Denied("FixtureAccessDenied") :
            Evidence.Detected(!MissingCom && !(OutOfProc32Only && view == RegistryView.Registry64 && name.StartsWith("FanControlManager")), "FixtureCLSID") };
    public Evidence DynamicLighting() => Evidence.NotAttempted("M1");
    public SecurityContext SecurityContext() => NativeSecurity.Current();
}

[TestClass]
public sealed class ProtocolTests
{
    [TestMethod]
    public async Task RoundTrip()
    {
        var request = new PlatformRequest(Protocol.Version, Guid.NewGuid(), Command.ClientHello);
        using var stream = new MemoryStream();
        await FrameProtocol.WriteAsync(stream, request, default);
        stream.Position = 0;
        Assert.AreEqual(request, await FrameProtocol.ReadAsync<PlatformRequest>(stream, default));
        var coolingRequest = new PlatformRequest(1, Guid.NewGuid(), Command.GetCoolingReadOnlySnapshot);
        Assert.AreEqual(coolingRequest, await FramedRoundTrip(coolingRequest));
        var json = JsonSerializer.SerializeToElement(coolingRequest, Protocol.Json);
        Assert.AreEqual(9, json.GetProperty("command").GetInt32());
        CollectionAssert.AreEquivalent(new[] { "protocolVersion", "requestId", "command" },
            json.EnumerateObject().Select(p => p.Name).ToArray());
        var snapshot = CoolingRuntimeSnapshot.Unobserved(PlatformError.ComponentMissing,
            "FixtureCoolingUnavailable", FanObservationState.Unavailable);
        var response = new PlatformResponse(1, coolingRequest.RequestId, true, PlatformError.None, Cooling: snapshot);
        var decoded = await FramedRoundTrip(response);
        Assert.AreEqual(response.RequestId, decoded.RequestId);
        Assert.IsTrue(decoded.Success);
        Assert.IsNotNull(decoded.Cooling);
        Assert.AreEqual(PlatformError.ComponentMissing, decoded.Cooling.Error);
        Assert.AreEqual(snapshot.Diagnostic, decoded.Cooling.Diagnostic);
        Assert.IsTrue(JsonSerializer.SerializeToElement(decoded, Protocol.Json).TryGetProperty("cooling", out _));
        // A v1 DTO without the additive field still reads the existing envelope.
        var legacy = JsonSerializer.Deserialize<LegacyResponse>(JsonSerializer.Serialize(decoded, Protocol.Json), Protocol.Json)!;
        Assert.AreEqual(1, legacy.ProtocolVersion);
        Assert.AreEqual(coolingRequest.RequestId, legacy.RequestId);
        Assert.IsTrue(legacy.Success);
        Assert.AreEqual(PlatformError.None, legacy.Error);
    }
    [TestMethod]
    [DataRow(0, PlatformError.MalformedRequest)]
    [DataRow(-1, PlatformError.MalformedRequest)]
    [DataRow(Protocol.MaximumMessageBytes + 1, PlatformError.RequestTooLarge)]
    [DataRow(int.MaxValue, PlatformError.RequestTooLarge)]
    public async Task RejectFrameLength(int length, PlatformError error)
    {
        var data = new byte[4]; BinaryPrimitives.WriteInt32LittleEndian(data, length);
        using var stream = new MemoryStream(data);
        var failure = await Assert.ThrowsAsync<ProtocolException>(() => FrameProtocol.ReadAsync<PlatformRequest>(stream, default));
        Assert.AreEqual(error, failure.Error);
    }
    [TestMethod]
    [DataRow("{")]
    [DataRow("null")]
    [DataRow("[]")]
    [DataRow("{\"command\":{\"$type\":\"Injected.Type\"}}")]
    [DataRow("{\"command\":1,\"Command\":2}")]
    public async Task MalformedPayload(string json)
    {
        using var stream = Payload(json);
        Assert.AreEqual(PlatformError.MalformedRequest,
            (await Assert.ThrowsAsync<ProtocolException>(() => FrameProtocol.ReadAsync<PlatformRequest>(stream, default))).Error);
    }
    [TestMethod]
    public async Task UnknownFieldTolerance()
    {
        using var stream = Payload($"{{\"protocolVersion\":1,\"requestId\":\"{Guid.NewGuid()}\",\"command\":1,\"futureField\":42}}");
        Assert.AreEqual(Command.ClientHello, (await FrameProtocol.ReadAsync<PlatformRequest>(stream, default)).Command);
    }
    [TestMethod]
    [DataRow(2)] [DataRow(7)]
    public async Task TruncatedFrame(int size)
    {
        var data = new byte[size]; if (size >= 4) BinaryPrimitives.WriteInt32LittleEndian(data, 10);
        using var stream = new MemoryStream(data);
        Assert.AreEqual(PlatformError.TruncatedFrame,
            (await Assert.ThrowsAsync<ProtocolException>(() => FrameProtocol.ReadAsync<PlatformRequest>(stream, default))).Error);
    }
    [TestMethod]
    public void StableWireEnums()
    {
        Assert.AreEqual("1,2,3,4,5,6,7,8,9", string.Join(',', Enum.GetValues<Command>().Select(v => (int)v)));
        // Pin names to IDs as well as the complete set: reassigning two existing
        // names must fail even when Enum.GetValues still returns the same numbers.
        var existing = new[] { Command.ClientHello, Command.Ping, Command.GetProtocolVersion,
            Command.GetServiceVersion, Command.GetServerIdentity, Command.GetAsusRuntimeCapabilities,
            Command.GetAsusServiceStatus, Command.GetAsusComponentVersions };
        for (int i = 0; i < existing.Length; i++) Assert.AreEqual(i + 1, (int)existing[i], existing[i].ToString());
        Assert.AreEqual(9, (int)Command.GetCoolingReadOnlySnapshot);
        Assert.AreEqual("0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16", string.Join(',', Enum.GetValues<PlatformError>().Select(v => (int)v)));
        Assert.AreEqual("0,1,2,3,4,5", string.Join(',', Enum.GetValues<CapabilityState>().Select(v => (int)v)));
        Assert.AreEqual("0,1,2,3,4,5", string.Join(',', Enum.GetValues<ExecutionState>().Select(v => (int)v)));
    }
    [TestMethod]
    public async Task DispatcherDefendsContract()
    {
        var dispatcher = Dispatcher(); var id = Guid.NewGuid();
        Assert.AreEqual(PlatformError.MalformedRequest, dispatcher.Dispatch(new(1, id, Command.Ping)).Error);
        Assert.AreEqual(PlatformError.ProtocolMismatch, dispatcher.Dispatch(new(2, Guid.NewGuid(), Command.ClientHello)).Error);
        Assert.AreEqual(PlatformError.UnsupportedCapability, dispatcher.Dispatch(new(1, Guid.NewGuid(), (Command)999)).Error);
        Assert.AreEqual(PlatformError.MalformedRequest, dispatcher.Dispatch(new(1, Guid.Empty, Command.ClientHello)).Error);
        id = Guid.NewGuid();
        Assert.IsTrue(dispatcher.Dispatch(new(1, id, Command.ClientHello)).Success);
        Assert.AreEqual(PlatformError.DuplicateRequestId, dispatcher.Dispatch(new(1, id, Command.Ping)).Error);
        Assert.IsTrue(dispatcher.Dispatch(new(1, Guid.NewGuid(), Command.GetAsusRuntimeCapabilities)).Success);
        await CoolingCommandDefendsContract();
    }
    private static async Task CoolingCommandDefendsContract()
    {
        var snapshot = CoolingRuntimeSnapshot.Unobserved(PlatformError.None, "FixtureCoolingSnapshot");
        var cooling = new RecordingCoolingProvider(snapshot);
        var dispatcher = new RequestDispatcher(new AsusCapabilityDetector(new Inventory()),
            new(1, "test", NativeSecurity.Current()), cooling);
        Assert.AreEqual(PlatformError.MalformedRequest,
            (await dispatcher.DispatchAsync(new(1, Guid.NewGuid(), Command.GetCoolingReadOnlySnapshot), default)).Error);
        Assert.AreEqual(PlatformError.ProtocolMismatch,
            (await dispatcher.DispatchAsync(new(2, Guid.NewGuid(), Command.GetCoolingReadOnlySnapshot), default)).Error);
        Assert.AreEqual(PlatformError.MalformedRequest,
            (await dispatcher.DispatchAsync(new(1, Guid.Empty, Command.GetCoolingReadOnlySnapshot), default)).Error);
        Assert.AreEqual(0, cooling.Calls, "invalid envelope or missing hello invoked cooling provider");
        var hello = await dispatcher.DispatchAsync(new(1, Guid.NewGuid(), Command.ClientHello), default);
        Assert.IsTrue(hello.Success);
        Assert.AreEqual(1, hello.Hello!.ProtocolVersion);
        var id = Guid.NewGuid();
        var accepted = await dispatcher.DispatchAsync(new(1, id, Command.GetCoolingReadOnlySnapshot), default);
        Assert.IsTrue(accepted.Success);
        Assert.AreEqual(id, accepted.RequestId);
        Assert.AreEqual(PlatformError.None, accepted.Error);
        Assert.AreSame(snapshot, accepted.Cooling);
        Assert.AreEqual(1, cooling.Calls);
        Assert.AreEqual(PlatformError.DuplicateRequestId,
            (await dispatcher.DispatchAsync(new(1, id, Command.GetCoolingReadOnlySnapshot), default)).Error);
        Assert.AreEqual(PlatformError.UnsupportedCapability,
            (await dispatcher.DispatchAsync(new(1, Guid.NewGuid(), (Command)999), default)).Error);
        Assert.AreEqual(1, cooling.Calls, "rejected request invoked cooling provider");

        var unsupported = Dispatcher(); // v1 server with no cooling provider
        Assert.IsTrue(unsupported.Dispatch(new(1, Guid.NewGuid(), Command.ClientHello)).Success);
        var rejected = await unsupported.DispatchAsync(new(1, Guid.NewGuid(), Command.GetCoolingReadOnlySnapshot), default);
        Assert.AreEqual(PlatformError.UnsupportedCapability, rejected.Error);
        Assert.IsNull(rejected.Cooling);
        cooling.Snapshot = CoolingRuntimeSnapshot.Unobserved(PlatformError.ComponentMissing,
            "FixtureCoolingUnavailable", FanObservationState.Unavailable);
        var unavailable = await dispatcher.DispatchAsync(new(1, Guid.NewGuid(), Command.GetCoolingReadOnlySnapshot), default);
        Assert.IsTrue(unavailable.Success, "request succeeded; unavailable telemetry remains explicit in Cooling");
        Assert.AreEqual(PlatformError.ComponentMissing, unavailable.Cooling!.Error);
        Assert.AreEqual(FanObservationState.Unavailable, unavailable.Cooling.Capabilities.WorkerReachable);
        // Exercise the real provider's authorization barrier without launching a worker.
        int workerStarts = 0;
        var deniedProvider = new CoolingReadOnlyProvider(() => { workerStarts++; throw new InvalidOperationException("MustNotLaunch"); },
            () => false, TimeSpan.FromSeconds(1));
        var denied = await deniedProvider.ReadAsync();
        Assert.AreEqual(PlatformError.AccessDenied, denied.Error);
        Assert.AreEqual(FanObservationState.PermissionDenied, denied.Capabilities.WorkerReachable);
        Assert.AreEqual(0, workerStarts);
    }
    private sealed class RecordingCoolingProvider(CoolingRuntimeSnapshot snapshot) : ICoolingReadOnlyProvider
    {
        public CoolingRuntimeSnapshot Snapshot = snapshot;
        public int Calls;
        public Task<CoolingRuntimeSnapshot> ReadAsync(CancellationToken token = default)
        { Calls++; return Task.FromResult(Snapshot); }
    }
    private sealed record LegacyResponse(int ProtocolVersion, Guid RequestId, bool Success, PlatformError Error);
    private static async Task<T> FramedRoundTrip<T>(T value)
    {
        using var stream = new MemoryStream();
        await FrameProtocol.WriteAsync(stream, value, default);
        stream.Position = 0;
        return await FrameProtocol.ReadAsync<T>(stream, default);
    }
    [TestMethod]
    public void BoundRequestIdStorage()
    {
        var dispatcher = Dispatcher(); dispatcher.Dispatch(new(1, Guid.NewGuid(), Command.ClientHello));
        for (int i = 1; i < Protocol.MaximumRequestsPerSession; i++) Assert.IsTrue(dispatcher.Dispatch(new(1, Guid.NewGuid(), Command.Ping)).Success);
        Assert.AreEqual(PlatformError.RequestTooLarge, dispatcher.Dispatch(new(1, Guid.NewGuid(), Command.Ping)).Error);
    }
    private static RequestDispatcher Dispatcher() => new(new AsusCapabilityDetector(new Inventory()), new(1, "test", NativeSecurity.Current()));
    internal static MemoryStream Payload(string json)
    {
        var bytes = Encoding.UTF8.GetBytes(json); var data = new byte[bytes.Length + 4];
        BinaryPrimitives.WriteInt32LittleEndian(data, bytes.Length); bytes.CopyTo(data, 4); return new(data);
    }
}
[TestClass]
public sealed class CapabilityTests
{
    [TestMethod]
    public void RealMissingServiceAndComRegistrationAreExplicit()
    {
        var missingService = new AsusServiceDetector(new()).Detect("AceHFXAura.Tests.Missing." + Guid.NewGuid().ToString("N"));
        Assert.IsFalse(missingService.Installed.Value);
        Assert.AreEqual(ExecutionState.Succeeded, missingService.Installed.Execution);
        Assert.IsFalse(missingService.Running.Value);
        var missingClass = new AsusComDetector(new()).Detect("MissingTestClass", Guid.NewGuid().ToString(), RegistryView.Registry64);
        Assert.IsFalse(missingClass.Registered.Value);
        Assert.AreEqual(ExecutionState.NotAttempted, missingClass.Accessible.Execution);
    }
    [TestMethod]
    public void VersionMetadataRejectsRemoteAndRelativeImagePaths()
    {
        var detector = new AsusVersionDetector();
        Assert.AreEqual("NonLocalImagePathRejected", detector.Read(@"\\server\share\Vendor.dll").Reason);
        Assert.AreEqual("NonLocalImagePathRejected", detector.Read(@"relative\Vendor.dll").Reason);
        Assert.IsNull(detector.Read(null).Value);
    }
    [TestMethod] public void MissingService() => Assert.AreEqual(CapabilityState.Unavailable, Discover(new() { MissingService = true }).Aura.Backend.StackDetected.State);
    [TestMethod] public void StoppedService() => Assert.AreEqual(CapabilityState.InstalledButStopped, Discover(new() { StoppedService = true }).Cooling.Backend.StackDetected.State);
    [TestMethod] public void RegisteredCom() => Assert.IsTrue(Discover(new()).Aura.AuraSdk.Registered.Value);
    [TestMethod] public void MissingCom() => Assert.AreEqual(CapabilityState.Unavailable, Discover(new() { MissingCom = true }).Cooling.Backend.StackDetected.State);
    [TestMethod] public void PermissionDenied() => Assert.AreEqual(CapabilityState.PermissionDenied, Discover(new() { Denied = true }).Aura.Backend.StackDetected.State);
    [TestMethod] public void OutOfProc32RegistrationCanBeDetectedFrom64Product() => Assert.AreEqual(CapabilityState.Available,
        Discover(new() { OutOfProc32Only = true }).Cooling.Backend.StackDetected.State);
    [TestMethod]
    public void UnknownVersionDoesNotImplyUnsupportedOrUsable()
    {
        var result = Discover(new());
        Assert.AreEqual("999.0.0", result.Aura.AuraSdk.Version.Value);
        Assert.AreEqual("Unknown", result.Aura.AuraSdk.Version.Compatibility);
        Assert.AreEqual(CapabilityState.Available, result.Aura.Backend.StackDetected.State);
        Assert.AreEqual(ExecutionState.NotAttempted, result.Aura.Backend.Usable.Execution);
        Assert.IsNull(result.Aura.Backend.Usable.Value);
        var roundTrip = JsonSerializer.Deserialize<AsusPlatformCapabilities>(JsonSerializer.Serialize(result, Protocol.Json), Protocol.Json)!;
        Assert.AreEqual(ExecutionState.NotAttempted, roundTrip.Cooling.FanControlCom.Validated.Execution);
    }
    [TestMethod]
    public void RealReadOnlyDiscovery()
    {
        var result = new AsusCapabilityDetector(new AsusRuntimeDetector()).Discover();
        Assert.HasCount(10, result.Services); Assert.HasCount(18, result.Components);
        Assert.IsTrue(result.Components.All(c => c.Validated.Execution == ExecutionState.NotAttempted));
    }
    private static AsusPlatformCapabilities Discover(Inventory inventory) => new AsusCapabilityDetector(inventory).Discover();
}
[TestClass]
public sealed class AuthenticationTests
{
    [TestMethod]
    public void SessionSelectionRejectsAmbiguityAndSupportsUniqueLoggedOnSession()
    {
        var a = new InteractiveUser("S-1-5-21-1-2-3-1001", 1);
        var b = new InteractiveUser("S-1-5-21-1-2-3-1002", 2);
        Assert.AreEqual(a, InteractiveSessionResolver.SelectIntendedUser([], [a]));
        Assert.AreEqual(b, InteractiveSessionResolver.SelectIntendedUser([b], [a]));
        Assert.IsNull(InteractiveSessionResolver.SelectIntendedUser([], [a, b]));
        Assert.IsNull(InteractiveSessionResolver.SelectIntendedUser([a, b], []));
        Assert.IsNull(InteractiveSessionResolver.SelectIntendedUser([], []));
    }
    [TestMethod]
    public void PolicyAcceptsExpectedIdentityAndRejectsMismatches()
    {
        var current = new SecurityContext("S-1-5-21-1-2-3-1001", 1234, 7, false);
        var expected = new InteractiveUser(current.Sid, current.SessionId);
        Assert.IsTrue(ClientAuthenticationPolicy.Accept(current, expected, true, (uint)current.SessionId));
        Assert.IsFalse(ClientAuthenticationPolicy.Accept(current with { Sid = "S-1-5-18" }, expected, true, (uint)current.SessionId));
        Assert.IsFalse(ClientAuthenticationPolicy.Accept(current with { ProcessId = 0 }, expected, true, (uint)current.SessionId));
        Assert.IsFalse(ClientAuthenticationPolicy.Accept(current, expected, false, (uint)current.SessionId));
        Assert.IsFalse(ClientAuthenticationPolicy.Accept(current with { SessionId = current.SessionId + 1 }, expected, true, (uint)current.SessionId));
        Assert.IsFalse(ClientAuthenticationPolicy.Accept(current, expected, true, 0));
    }
    [TestMethod]
    public void InvalidPidAndExitedProcess()
    {
        using var invalid = NativeSecurity.OpenProcess(0x1000, false, uint.MaxValue); Assert.IsTrue(invalid.IsInvalid);
        using var child = Process.Start(new ProcessStartInfo("powershell.exe", "-NoProfile -NonInteractive -Command Start-Sleep -Seconds 30") { CreateNoWindow = true, UseShellExecute = false })!;
        using var handle = NativeSecurity.OpenProcess(0x1000 | 0x100000, false, (uint)child.Id);
        Assert.IsTrue(NativeSecurity.Alive(handle)); child.Kill(); child.WaitForExit(); Assert.IsFalse(NativeSecurity.Alive(handle));
    }
    [TestMethod]
    public void PipeAclHasNoBroadUsersOrClientCreateInstance()
    {
        var intended = new InteractiveUser("S-1-5-21-1-2-3-1001", 7);
        var descriptor = new RawSecurityDescriptor(SecurePipe.SecurityDescriptor(intended));
        var acl = descriptor.DiscretionaryAcl!;
        Assert.HasCount(3, acl);
        foreach (CommonAce ace in acl)
        {
            Assert.AreNotEqual("S-1-5-11", ace.SecurityIdentifier.Value); Assert.AreNotEqual("S-1-1-0", ace.SecurityIdentifier.Value);
            if (ace.SecurityIdentifier.Value == intended.Sid) Assert.AreEqual(0, ace.AccessMask & 4);
        }
    }
    [TestMethod]
    public void FutureLeaseBoundToAuthenticatedConnection()
    {
        var current = NativeSecurity.Current(); using var session = new SessionLifecycle(current);
        Assert.IsNull(session.Lease);
        var now = DateTimeOffset.UtcNow;
        var lease = new DeviceControlLease(Guid.NewGuid(), current.ProcessId, current.Sid, now, now, now.AddSeconds(5));
        Assert.Throws<InvalidOperationException>(() => session.AttachFutureLease(lease with { OwnerPid = current.ProcessId + 1 }));
        Assert.Throws<InvalidOperationException>(() => session.AttachFutureLease(lease with { OwnerSid = current.Sid == "S-1-5-18" ? "S-1-5-19" : "S-1-5-18" }));
        session.AttachFutureLease(lease); Assert.AreEqual(lease, session.Lease);
        session.Dispose(); Assert.IsTrue(session.Closed); Assert.IsNull(session.Lease);
        Assert.Throws<InvalidOperationException>(() => session.AttachFutureLease(lease));
    }
}
[TestClass]
public sealed class NativePipeTests
{
    [TestMethod]
    [TestCategory("DesktopSmoke")]
    public async Task RealPipeTokenRejectsWrongExpectedSid()
    {
        var current = NativeSecurity.Current();
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        var name = "AceHFXPlatform.Tests." + Guid.NewGuid().ToString("N");
        using var server = SecurePipe.CreateServer(name, new(current.Sid, current.SessionId));
        var accept = server.WaitForConnectionAsync(deadline.Token);
        using var client = await SecurePipe.ConnectAsync(name, deadline.Token);
        await accept;
        await FrameProtocol.WriteAsync(client, new PlatformRequest(1, Guid.NewGuid(), Command.ClientHello), deadline.Token);
        await FrameProtocol.ReadAsync<PlatformRequest>(server, deadline.Token);
        Assert.Throws<UnauthorizedAccessException>(() => new ClientAuthenticator().Authenticate(server, new("S-1-5-18", current.SessionId)));
    }
    [TestMethod]
    [TestCategory("DesktopSmoke")]
    public async Task AuthenticatedCurrentUserReconnectAndMalformedClients()
    {
        var name = "AceHFXPlatform.Tests." + Guid.NewGuid().ToString("N");
        using var lifetime = new CancellationTokenSource(TimeSpan.FromSeconds(20));
        var server = new SecurePipeServer(name, new(), new AsusCapabilityDetector(new Inventory()),
            new(Protocol.Version, "test", NativeSecurity.Current()), new(true));
        var task = server.RunAsync(lifetime.Token);
        try
        {
            await ValidConnection();
            using (var pipe = await SecurePipe.ConnectAsync(name, lifetime.Token))
            {
                await pipe.WriteAsync(new byte[] { 255, 255, 255, 255 }, lifetime.Token);
                Assert.AreEqual(PlatformError.MalformedRequest, (await FrameProtocol.ReadAsync<PlatformResponse>(pipe, lifetime.Token)).Error);
            }
            using (var pipe = await SecurePipe.ConnectAsync(name, lifetime.Token))
                await pipe.WriteAsync(new byte[] { 20, 0, 0, 0, 123 }, lifetime.Token); // disconnect mid-frame
            await ValidConnection(); // listener survived both malformed peers and reconnect.
        }
        finally { lifetime.Cancel(); await task; }
        async Task ValidConnection()
        {
            using var pipe = await SecurePipe.ConnectAsync(name, lifetime.Token);
            var id = Guid.NewGuid();
            await FrameProtocol.WriteAsync(pipe, new PlatformRequest(1, id, Command.ClientHello), lifetime.Token);
            var hello = await FrameProtocol.ReadAsync<PlatformResponse>(pipe, lifetime.Token);
            Assert.IsTrue(hello.Success); Assert.AreEqual(id, hello.RequestId);
            id = Guid.NewGuid();
            await FrameProtocol.WriteAsync(pipe, new PlatformRequest(1, id, Command.GetAsusRuntimeCapabilities), lifetime.Token);
            Assert.IsNotNull((await FrameProtocol.ReadAsync<PlatformResponse>(pipe, lifetime.Token)).Capabilities);
        }
    }
    [TestMethod]
    [TestCategory("DesktopSmoke")]
    public async Task StalledClientTimeoutAndListenerRecovery()
    {
        var name = "AceHFXPlatform.Tests." + Guid.NewGuid().ToString("N");
        using var lifetime = new CancellationTokenSource(TimeSpan.FromSeconds(15));
        var server = new SecurePipeServer(name, new(), new AsusCapabilityDetector(new Inventory()), new(1, "test", NativeSecurity.Current()), new(true));
        var task = server.RunAsync(lifetime.Token);
        try
        {
            using var pipe = await SecurePipe.ConnectAsync(name, lifetime.Token);
            Assert.AreEqual(PlatformError.Timeout, (await FrameProtocol.ReadAsync<PlatformResponse>(pipe, lifetime.Token)).Error);
        }
        finally { lifetime.Cancel(); await task; }
    }
}
