using System.ComponentModel;
using System.IO.Pipes;

namespace AceHFX.AsusPlatform.Ipc;

public sealed class AceHfxServiceClient : IAceHfxServiceClient
{
    public async Task<PlatformResponse> SendAsync(Command command, CancellationToken cancellationToken = default)
    {
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        deadline.CancelAfter(Protocol.RequestTimeout);
        var id = Guid.NewGuid();
        try
        {
            if (NativeSecurity.ServicePid() == 0) return Fail(PlatformError.ServiceUnavailable, "BrokerNotRunning");
            using var pipe = await SecurePipe.ConnectAsync(Protocol.PipeName, deadline.Token);
            ValidateServer(pipe);
            var helloId = Guid.NewGuid();
            await FrameProtocol.WriteAsync(pipe, new PlatformRequest(Protocol.Version, helloId, Command.ClientHello), deadline.Token);
            var hello = await FrameProtocol.ReadAsync<PlatformResponse>(pipe, deadline.Token);
            ValidateResponse(hello, helloId);
            if (!hello.Success) return hello with { RequestId = id };
            if (hello.Hello?.ProtocolVersion != Protocol.Version) throw new ProtocolException(PlatformError.ProtocolMismatch, "InvalidServerHello");
            await FrameProtocol.WriteAsync(pipe, new PlatformRequest(Protocol.Version, id, command), deadline.Token);
            var response = await FrameProtocol.ReadAsync<PlatformResponse>(pipe, deadline.Token);
            ValidateResponse(response, id);
            return response;
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested) { return Fail(PlatformError.Timeout, "BrokerRequestTimeout"); }
        catch (UnauthorizedAccessException) { return Fail(PlatformError.UnauthorizedClient, "BrokerIdentityRejected"); }
        catch (ProtocolException e) { return Fail(e.Error, e.Message); }
        catch (Win32Exception e) { return Fail(e.NativeErrorCode == 5 ? PlatformError.AccessDenied : PlatformError.ServiceUnavailable, "BrokerConnectionFailed"); }
        catch (IOException) { return Fail(PlatformError.ServiceUnavailable, "BrokerDisconnected"); }
        PlatformResponse Fail(PlatformError error, string reason) => new(Protocol.Version, id, false, error, reason);
    }
    private static void ValidateResponse(PlatformResponse response, Guid id)
    {
        if (response.ProtocolVersion != Protocol.Version) throw new ProtocolException(PlatformError.ProtocolMismatch, "ResponseProtocolMismatch");
        if (response.RequestId != id || !Enum.IsDefined(response.Error) || response.Success != (response.Error == PlatformError.None))
            throw new ProtocolException(PlatformError.MalformedRequest, "InvalidResponseEnvelope");
    }
    private static void ValidateServer(NamedPipeClientStream pipe)
    {
        if (!NativeSecurity.GetNamedPipeServerProcessId(pipe.SafePipeHandle, out var pid) || pid == 0 || pid != NativeSecurity.ServicePid())
            throw new UnauthorizedAccessException();
        // A medium-integrity client cannot inspect a LocalSystem process token. Bind the
        // OS pipe PID to the running SCM service and its administrator-owned account config.
        if (!NativeSecurity.GetNamedPipeServerSessionId(pipe.SafePipeHandle, out var session) || session != 0)
            throw new UnauthorizedAccessException();
        using var machine = Microsoft.Win32.RegistryKey.OpenBaseKey(Microsoft.Win32.RegistryHive.LocalMachine, Microsoft.Win32.RegistryView.Registry64);
        using var service = machine.OpenSubKey(@"SYSTEM\CurrentControlSet\Services\" + Protocol.ServiceName);
        if (!string.Equals(service?.GetValue("ObjectName") as string, "LocalSystem", StringComparison.OrdinalIgnoreCase))
            throw new UnauthorizedAccessException();
    }
}
