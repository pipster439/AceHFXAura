using System.ComponentModel;
using System.Diagnostics;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Security.Principal;
using Microsoft.Win32.SafeHandles;

namespace AceHFX.AsusPlatform.Ipc;

internal static class NativeSecurity
{
    internal const uint QueryLimitedInformation = 0x1000, Synchronize = 0x100000;
    internal const uint ClientRights = 0x12019b; // Read/write data, attributes, EA, READ_CONTROL, SYNCHRONIZE; no CreatePipeInstance.
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern SafeProcessHandle OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern uint WaitForSingleObject(SafeProcessHandle process, uint timeout);
    [DllImport("advapi32.dll", SetLastError = true)] internal static extern bool OpenProcessToken(SafeProcessHandle process, uint access, out SafeAccessTokenHandle token);
    [DllImport("advapi32.dll", SetLastError = true)] private static extern bool GetTokenInformation(SafeAccessTokenHandle token, int infoClass, out int session, int length, out int needed);
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern bool GetNamedPipeClientProcessId(SafePipeHandle pipe, out uint pid);
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern bool GetNamedPipeClientSessionId(SafePipeHandle pipe, out uint session);
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern bool GetNamedPipeServerProcessId(SafePipeHandle pipe, out uint pid);
    [DllImport("kernel32.dll", SetLastError = true)] internal static extern bool GetNamedPipeServerSessionId(SafePipeHandle pipe, out uint session);
    [DllImport("kernel32.dll")] internal static extern uint WTSGetActiveConsoleSessionId();
    [DllImport("wtsapi32.dll", SetLastError = true)] internal static extern bool WTSQueryUserToken(uint sessionId, out SafeAccessTokenHandle token);
    [DllImport("wtsapi32.dll", SetLastError = true)] internal static extern bool WTSEnumerateSessions(IntPtr server, int reserved, int version, out IntPtr sessions, out int count);
    [DllImport("wtsapi32.dll")] internal static extern void WTSFreeMemory(IntPtr memory);
    [StructLayout(LayoutKind.Sequential)] internal struct WtsSession { public int SessionId; public IntPtr Name; public int State; }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] internal static extern SafePipeHandle CreateNamedPipe(string name, uint mode, uint pipeMode, uint instances, uint outSize, uint inSize, uint timeout, ref SecurityAttributes security);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] internal static extern SafePipeHandle CreateFile(string name, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)] internal static extern bool ConvertStringSecurityDescriptorToSecurityDescriptor(string sddl, uint revision, out IntPtr descriptor, out uint size);
    [DllImport("kernel32.dll")] internal static extern IntPtr LocalFree(IntPtr value);
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern IntPtr OpenSCManager(string? machine, string? database, uint access);
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern IntPtr OpenService(IntPtr manager, string name, uint access);
    [DllImport("advapi32.dll", SetLastError = true)] private static extern bool QueryServiceStatusEx(IntPtr service, int level, out ServiceStatus status, int size, out int needed);
    [DllImport("advapi32.dll")] private static extern bool CloseServiceHandle(IntPtr handle);
    [StructLayout(LayoutKind.Sequential)] internal struct SecurityAttributes { public int Length; public IntPtr Descriptor; public int Inherit; }
    [StructLayout(LayoutKind.Sequential)] private struct ServiceStatus
    {
        public uint Type, State, Accepted, Win32Exit, ServiceExit, CheckPoint, WaitHint, Pid, Flags;
    }
    internal static Win32Exception LastError() => new(Marshal.GetLastWin32Error());
    internal static SecurityContext TokenContext(SafeAccessTokenHandle token, int pid)
    {
        using var identity = new WindowsIdentity(token.DangerousGetHandle());
        if (identity.User == null || !GetTokenInformation(token, 12, out var session, sizeof(int), out _)) throw LastError();
        return new(identity.User.Value, pid, session, identity.IsSystem);
    }
    public static SecurityContext Current()
    {
        using var identity = WindowsIdentity.GetCurrent();
        using var process = Process.GetCurrentProcess();
        return new(identity.User?.Value ?? throw new UnauthorizedAccessException(), process.Id, process.SessionId, identity.IsSystem);
    }
    internal static bool Alive(SafeProcessHandle process) => !process.IsInvalid && WaitForSingleObject(process, 0) == 0x102;
    internal static uint ServicePid()
    {
        var manager = OpenSCManager(null, null, 1);
        if (manager == IntPtr.Zero) throw LastError();
        try
        {
            var service = OpenService(manager, Protocol.ServiceName, 4);
            if (service == IntPtr.Zero) throw LastError();
            try
            {
                if (!QueryServiceStatusEx(service, 0, out var status, Marshal.SizeOf<ServiceStatus>(), out _)) throw LastError();
                return status.State == 4 ? status.Pid : 0;
            }
            finally { CloseServiceHandle(service); }
        }
        finally { CloseServiceHandle(manager); }
    }
}
public sealed record InteractiveUser(string Sid, int SessionId);
public sealed class InteractiveSessionResolver
{
    public InteractiveUser? Resolve()
    {
        try
        {
            var current = NativeSecurity.Current();
            if (current.IsSystem) return ResolveSystem();
            // Only used by the explicitly isolated console development host.
            return current.SessionId > 0 ? new(current.Sid, current.SessionId) : null;
        }
        catch (Win32Exception) { return null; }
    }
    internal static InteractiveUser? ResolveSystem()
    {
        var session = NativeSecurity.WTSGetActiveConsoleSessionId();
        var console = UserForSession(session);
        if (console != null) return console;
        if (!NativeSecurity.WTSEnumerateSessions(IntPtr.Zero, 0, 1, out var memory, out var count)) return null;
        try
        {
            var active = new List<InteractiveUser>();
            var disconnected = new List<InteractiveUser>();
            for (var i = 0; i < count; i++)
            {
                var entry = Marshal.PtrToStructure<NativeSecurity.WtsSession>(memory + i * Marshal.SizeOf<NativeSecurity.WtsSession>());
                if (entry.SessionId <= 0 || entry.State is not (0 or 4)) continue;
                var user = UserForSession((uint)entry.SessionId);
                if (user != null) (entry.State == 0 ? active : disconnected).Add(user);
            }
            return SelectIntendedUser(active, disconnected);
        }
        finally { NativeSecurity.WTSFreeMemory(memory); }
    }
    internal static InteractiveUser? SelectIntendedUser(IReadOnlyList<InteractiveUser> active, IReadOnlyList<InteractiveUser> disconnected) =>
        active.Count == 1 ? active[0] : active.Count == 0 && disconnected.Count == 1 ? disconnected[0] : null;
    private static InteractiveUser? UserForSession(uint session)
    {
        if (session == uint.MaxValue || session == 0) return null;
        if (!NativeSecurity.WTSQueryUserToken(session, out var token)) { token?.Dispose(); return null; }
        using (token)
        {
            var context = NativeSecurity.TokenContext(token, 0);
            return context.SessionId == session && !context.IsSystem ? new(context.Sid, context.SessionId) : null;
        }
    }
}
public sealed class AuthenticatedClient(SecurityContext identity, SafeProcessHandle process) : IDisposable
{
    public SecurityContext Identity { get; } = identity;
    public bool IsAlive => NativeSecurity.Alive(process);
    public void Dispose() => process.Dispose();
}
public static class ClientAuthenticationPolicy
{
    public static bool Accept(SecurityContext client, InteractiveUser expected, bool processAlive, uint pipeSession) =>
        processAlive && client.ProcessId > 0 && client.SessionId > 0 && client.SessionId == expected.SessionId &&
        pipeSession == expected.SessionId && StringComparer.Ordinal.Equals(client.Sid, expected.Sid);
}
public sealed class ClientAuthenticator
{
    public AuthenticatedClient Authenticate(NamedPipeServerStream pipe, InteractiveUser expected)
    {
        if (!NativeSecurity.GetNamedPipeClientProcessId(pipe.SafePipeHandle, out var pid) ||
            !NativeSecurity.GetNamedPipeClientSessionId(pipe.SafePipeHandle, out var session)) throw new UnauthorizedAccessException("PipeClientIdentityUnavailable");
        var process = NativeSecurity.OpenProcess(NativeSecurity.QueryLimitedInformation | NativeSecurity.Synchronize, false, pid);
        try
        {
            if (process.IsInvalid || !NativeSecurity.OpenProcessToken(process, 8, out var token)) throw new UnauthorizedAccessException("ClientTokenUnavailable");
            SecurityContext identity;
            using (token) identity = NativeSecurity.TokenContext(token, checked((int)pid));
            if (!ClientAuthenticationPolicy.Accept(identity, expected, NativeSecurity.Alive(process), session))
                throw new UnauthorizedAccessException("ClientPolicyRejected");
            // Cross-check the pipe's OS token, so a duplicated handle cannot substitute another token.
            string? pipeSid = null;
            pipe.RunAsClient(() => { using var peer = WindowsIdentity.GetCurrent(true); pipeSid = peer?.User?.Value; });
            if (!StringComparer.Ordinal.Equals(pipeSid, identity.Sid) || !NativeSecurity.Alive(process))
                throw new UnauthorizedAccessException("PipeTokenMismatchOrClientExited");
            return new(identity, process);
        }
        catch { process.Dispose(); throw; }
    }
}
