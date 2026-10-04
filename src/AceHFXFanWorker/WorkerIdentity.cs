using System.Diagnostics;
using System.Runtime.InteropServices;
using AceHFX.AsusPlatform.Ipc;

namespace AceHFX.FanWorker;

internal static class WorkerIdentity
{
    internal static void Validate()
    {
        var identity = NativeSecurity.Current();
        if (!identity.IsSystem || identity.SessionId != 0) throw new UnauthorizedAccessException("SystemSessionZeroRequired");
        using var current = Process.GetCurrentProcess();
        if (NtQueryInformationProcess(current.Handle, 0, out var info, Marshal.SizeOf<ProcessInfo>(), out _) != 0)
            throw new UnauthorizedAccessException("ParentQueryFailed");
        var parentPid = checked((uint)info.Parent.ToInt64());
        if (parentPid == 0 || parentPid != NativeSecurity.ServicePid()) throw new UnauthorizedAccessException("BrokerParentRequired");
        using var parent = Process.GetProcessById(checked((int)parentPid));
        var directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "AceHFXAura", "AceHFXService-M1");
        if (!string.Equals(parent.MainModule?.FileName, Path.Combine(directory, "AceHFXService.exe"), StringComparison.OrdinalIgnoreCase) ||
            !string.Equals(AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar), directory, StringComparison.OrdinalIgnoreCase))
            throw new UnauthorizedAccessException("ProtectedBrokerInstallationRequired");
    }
    [StructLayout(LayoutKind.Sequential)] private struct ProcessInfo
    { public IntPtr Reserved1, Peb, Reserved2, Reserved3, Pid, Parent; }
    [DllImport("ntdll.dll")] private static extern int NtQueryInformationProcess(IntPtr process, int type,
        out ProcessInfo info, int size, out int returned);
}
