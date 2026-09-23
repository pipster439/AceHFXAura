using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

namespace Aura_WinUI.Services;

public interface IOwnedDaemonProcess : IDisposable
{
    int Id { get; }
    string InstanceId { get; }
    bool HasExited { get; }
    Task StopAsync();
}

// Only processes created suspended by this object can enter this job. Never attach a PID to it.
public sealed class OwnedDaemonProcess : IOwnedDaemonProcess
{
    private IntPtr _job;
    private Process? _process;
    private readonly EventWaitHandle _shutdown;
    public string InstanceId { get; } = Guid.NewGuid().ToString("N");
    public int Id => _process!.Id;
    public bool HasExited => _process?.HasExited != false;

    public OwnedDaemonProcess(RuntimeLayout layout, bool dryRun = false, IReadOnlyDictionary<string, string>? environment = null)
    {
        var eventName = @"Local\Aura_WinUI_Stop_" + InstanceId;
        _shutdown = new EventWaitHandle(false, EventResetMode.ManualReset, eventName);
        PROCESS_INFORMATION pi = default;
        IntPtr environmentBlock = IntPtr.Zero;
        try
        {
            _job = CreateJobObject(IntPtr.Zero, null);
            if (_job == IntPtr.Zero) throw new Win32Exception();
            var limits = new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
            limits.BasicLimitInformation.LimitFlags = 0x2000; // KILL_ON_JOB_CLOSE
            if (!SetInformationJobObject(_job, 9, ref limits, (uint)Marshal.SizeOf(limits))) throw new Win32Exception();
            var args = new List<string> { layout.DaemonExecutablePath, "--config", layout.ConfigPath, "--keymap", layout.KeymapPath,
                "--instance-id", InstanceId, "--shutdown-event", eventName };
            if (dryRun) args.Add("--dry-run");
            if (layout.PayloadDirectory != null) { args.Add("--runtime-root"); args.Add(layout.RuntimeDirectory); }
            // Explicit development layout is also passed to the web sidecar; no inherited checkout discovery.
            else { args.Add("--web-root"); args.Add(Path.Combine(Path.GetDirectoryName(layout.TemplatePath)!, "web"));
                args.Add("--sdk-include"); args.Add(Path.Combine(Path.GetDirectoryName(layout.TemplatePath)!, "include")); }
            var startup = new STARTUPINFO { cb = Marshal.SizeOf<STARTUPINFO>() };
            if (environment != null)
            {
                var values = Environment.GetEnvironmentVariables().Cast<System.Collections.DictionaryEntry>()
                    .ToDictionary(x => (string)x.Key, x => (string)x.Value!, StringComparer.OrdinalIgnoreCase);
                foreach (var (key, value) in environment) values[key] = value;
                environmentBlock = Marshal.StringToHGlobalUni(string.Join('\0', values.OrderBy(x => x.Key, StringComparer.OrdinalIgnoreCase).Select(x => x.Key + "=" + x.Value)) + "\0\0");
            }
            if (!CreateProcess(layout.DaemonExecutablePath, new StringBuilder(string.Join(" ", args.Select(Quote))),
                IntPtr.Zero, IntPtr.Zero, false, 0x08000404, environmentBlock, layout.WorkingDirectory, ref startup, out pi)) throw new Win32Exception();
            if (!AssignProcessToJobObject(_job, pi.hProcess)) throw new Win32Exception();
            _process = Process.GetProcessById((int)pi.dwProcessId);
            _ = _process.Handle; // Acquire while still suspended: later exit checks/kill cannot reopen a reused PID.
            if (ResumeThread(pi.hThread) == uint.MaxValue) throw new Win32Exception();
        }
        catch
        {
            if (pi.hProcess != IntPtr.Zero) TerminateProcess(pi.hProcess, 1);
            Dispose();
            throw;
        }
        finally
        {
            if (environmentBlock != IntPtr.Zero) Marshal.FreeHGlobal(environmentBlock);
            if (pi.hThread != IntPtr.Zero) CloseHandle(pi.hThread);
            if (pi.hProcess != IntPtr.Zero) CloseHandle(pi.hProcess);
        }
    }

    internal static string Quote(string value)
    {
        var result = new StringBuilder("\""); int slashes = 0;
        foreach (var ch in value)
        {
            if (ch == '\\') { slashes++; continue; }
            result.Append('\\', ch == '\"' ? slashes * 2 + 1 : slashes);
            result.Append(ch); slashes = 0;
        }
        return result.Append('\\', slashes * 2).Append('\"').ToString();
    }

    public async Task StopAsync()
    {
        _shutdown.Set();
        if (_process is null || _process.HasExited) return;
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(4));
        try { await _process.WaitForExitAsync(timeout.Token); }
        catch (OperationCanceledException)
        {
            if (!_process.HasExited) _process.Kill(); // Retained child handle, never a discovered external PID.
            await _process.WaitForExitAsync();
        }
    }
    public void Dispose()
    {
        if (_job != IntPtr.Zero) { CloseHandle(_job); _job = IntPtr.Zero; }
        _process?.Dispose(); _process = null;
        _shutdown.Dispose();
    }

    [StructLayout(LayoutKind.Sequential)] private struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public uint dwProcessId, dwThreadId; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] private struct STARTUPINFO
    {
        public int cb; public string? reserved, desktop, title;
        public uint x, y, xSize, ySize, xChars, yChars, fill, flags;
        public ushort show, reservedSize; public IntPtr reservedPtr, input, output, error;
    }
    [StructLayout(LayoutKind.Sequential)] private struct BASIC_LIMIT
    {
        public long ProcessTime, JobTime; public uint LimitFlags;
        public UIntPtr MinWorkingSet, MaxWorkingSet; public uint ActiveProcessLimit;
        public UIntPtr Affinity; public uint PriorityClass, SchedulingClass;
    }
    [StructLayout(LayoutKind.Sequential)] private struct IO_COUNTERS { public ulong ReadOps, WriteOps, OtherOps, ReadBytes, WriteBytes, OtherBytes; }
    [StructLayout(LayoutKind.Sequential)] private struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION
    {
        public BASIC_LIMIT BasicLimitInformation; public IO_COUNTERS IoInfo;
        public UIntPtr ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemoryUsed, PeakJobMemoryUsed;
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern IntPtr CreateJobObject(IntPtr attributes, string? name);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool SetInformationJobObject(IntPtr job, int infoClass, ref JOBOBJECT_EXTENDED_LIMIT_INFORMATION info, uint length);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern bool CreateProcess(string app, StringBuilder args, IntPtr pa, IntPtr ta, bool inherit, uint flags, IntPtr env, string cwd, ref STARTUPINFO startup, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern uint ResumeThread(IntPtr thread);
    [DllImport("kernel32.dll")] private static extern bool TerminateProcess(IntPtr process, uint code);
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);
}
