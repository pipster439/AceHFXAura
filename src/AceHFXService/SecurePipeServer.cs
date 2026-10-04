using System.ComponentModel;
using System.IO.Pipes;
using AceHFX.AsusPlatform;
using AceHFX.AsusPlatform.Ipc;

namespace AceHFX.Service;

public sealed class SecurePipeServer(string pipeName, InteractiveSessionResolver sessions,
    IAsusCapabilityProvider capabilities, ServerHello hello, StructuredLog log,
    AceHFX.AsusPlatform.Cooling.ICoolingReadOnlyProvider? cooling = null)
{
    private readonly ClientAuthenticator _authenticator = new();
    public async Task RunAsync(CancellationToken stop)
    {
        log.Write("PipeListenerStartup", new { pipeName, protocolVersion = Protocol.Version });
        while (!stop.IsCancellationRequested)
        {
            try
            {
                var user = sessions.Resolve();
                if (user == null) { await Task.Delay(1000, stop); continue; }
                using var generation = CancellationTokenSource.CreateLinkedTokenSource(stop);
                using var pipe = SecurePipe.CreateServer(pipeName, user);
                var monitor = WatchInteractiveSessionAsync(user, generation);
                try
                {
                    await pipe.WaitForConnectionAsync(generation.Token);
                    // Re-resolve after accept to close the login/logoff race before authentication.
                    if (sessions.Resolve() != user) continue;
                    await HandleConnectionAsync(pipe, user, generation.Token);
                }
                finally
                {
                    generation.Cancel();
                    await monitor;
                }
            }
            catch (OperationCanceledException) { }
            catch (Exception e) when (e is IOException or Win32Exception or UnauthorizedAccessException)
            {
                log.Write("PipeListenerFailure", new { error = e.GetType().Name }, true);
                // FIRST_PIPE_INSTANCE conflicts fail closed; never join an attacker-created pipe.
                try { await Task.Delay(1000, stop); } catch (OperationCanceledException) { }
            }
        }
    }
    private async Task WatchInteractiveSessionAsync(InteractiveUser expected, CancellationTokenSource generation)
    {
        try
        {
            while (!generation.IsCancellationRequested)
            {
                await Task.Delay(500, generation.Token);
                if (sessions.Resolve() != expected) { log.Write("InteractiveSessionChanged"); generation.Cancel(); }
            }
        }
        catch (OperationCanceledException) { }
    }
    private async Task HandleConnectionAsync(NamedPipeServerStream pipe, InteractiveUser user, CancellationToken stop)
    {
        using var sessionDeadline = CancellationTokenSource.CreateLinkedTokenSource(stop);
        sessionDeadline.CancelAfter(Protocol.SessionTimeout);
        var token = sessionDeadline.Token;
        var firstRequestId = Guid.Empty;
        AuthenticatedClient client;
        try
        {
            // Pipe impersonation requires a message to have been read. Bound that first
            // frame, but dispatch absolutely nothing until OS authentication succeeds.
            using var firstDeadline = CancellationTokenSource.CreateLinkedTokenSource(token);
            firstDeadline.CancelAfter(Protocol.RequestTimeout);
            var first = await FrameProtocol.ReadAsync<PlatformRequest>(pipe, firstDeadline.Token);
            firstRequestId = first.RequestId;
            client = _authenticator.Authenticate(pipe, user);
            using (client)
            using (var lifecycle = new SessionLifecycle(client.Identity))
            {
                log.Write("ClientAuthenticated", new { lifecycle.ConnectionId, client.Identity });
                var dispatcher = new RequestDispatcher(capabilities, hello, cooling);
                using var connection = CancellationTokenSource.CreateLinkedTokenSource(token);
                var watchdog = WatchClientAsync(client, connection);
                try
                {
                    await ReplyAsync(first, connection.Token);
                    for (var i = 1; i < Protocol.MaximumRequestsPerSession; i++)
                    {
                        using var requestDeadline = CancellationTokenSource.CreateLinkedTokenSource(connection.Token);
                        requestDeadline.CancelAfter(Protocol.RequestTimeout);
                        var request = await FrameProtocol.ReadAsync<PlatformRequest>(pipe, requestDeadline.Token);
                        if (!client.IsAlive || sessions.Resolve() != user) throw new UnauthorizedAccessException("SessionExpired");
                        await ReplyAsync(request, requestDeadline.Token);
                    }
                }
                finally
                {
                    connection.Cancel();
                    await watchdog;
                    log.Write("ClientDisconnected", new { lifecycle.ConnectionId });
                }
                async Task ReplyAsync(PlatformRequest request, CancellationToken replyToken)
                {
                    var response = await dispatcher.DispatchAsync(request, replyToken);
                    if (!response.Success) log.Write("IpcRequestRejected", new { request.RequestId, response.Error }, true);
                    await FrameProtocol.WriteAsync(pipe, response, replyToken);
                }
            }
        }
        catch (UnauthorizedAccessException)
        {
            log.Write("ClientAuthenticationRejected", new { error = PlatformError.UnauthorizedClient }, true);
            await BestEffortErrorAsync(PlatformError.UnauthorizedClient);
        }
        catch (ProtocolException e)
        {
            // Truncation is ordinary disconnect. Payloads, token data and raw JSON are never logged.
            if (e.Error != PlatformError.TruncatedFrame) log.Write("IpcFrameRejected", new { e.Error }, true);
            await BestEffortErrorAsync(e.Error);
        }
        catch (OperationCanceledException) { if (!stop.IsCancellationRequested) { log.Write("IpcTimeout", null, true); await BestEffortErrorAsync(PlatformError.Timeout); } }
        catch (Exception e)
        {
            log.Write("IpcConnectionFailure", new { error = e.GetType().Name }, true);
        }
        async Task BestEffortErrorAsync(PlatformError error)
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromMilliseconds(200));
            try { await FrameProtocol.WriteAsync(pipe, new PlatformResponse(Protocol.Version, firstRequestId, false, error), deadline.Token); }
            catch (Exception e) when (e is IOException or OperationCanceledException or ObjectDisposedException) { }
        }
    }
    private static async Task WatchClientAsync(AuthenticatedClient client, CancellationTokenSource connection)
    {
        try
        {
            while (!connection.IsCancellationRequested)
            {
                await Task.Delay(250, connection.Token);
                if (!client.IsAlive) connection.Cancel();
            }
        }
        catch (OperationCanceledException) { }
    }
}
