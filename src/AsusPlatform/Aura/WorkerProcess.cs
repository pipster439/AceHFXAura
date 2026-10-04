using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace AceHFX.AsusPlatform.Aura;

internal interface IAuraWorker : IAsyncDisposable
{
    int Pid { get; }
    Stream Input { get; }
    Stream Output { get; }
    Task WaitForExitAsync(CancellationToken token);
    int ExitCode { get; }
}
internal sealed class WorkerProcess : IAuraWorker
{
    private readonly Process _process;
    private readonly SafeFileHandle _job;
    public int Pid => _process.Id;
    public Stream Input => _process.StandardInput.BaseStream;
    public Stream Output => _process.StandardOutput.BaseStream;
    public int ExitCode => _process.ExitCode;
    public WorkerProcess(string path, string? fixtureArgument = null)
    {
        _job=CreateJobObject(IntPtr.Zero,null);
        if(_job.IsInvalid) throw new System.ComponentModel.Win32Exception();
        var info=new JobLimits(); info.Basic.LimitFlags=0x2000; // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if(!SetInformationJobObject(_job,9,ref info,Marshal.SizeOf<JobLimits>())) { _job.Dispose(); throw new System.ComponentModel.Win32Exception(); }
        var start=new ProcessStartInfo(Path.GetFullPath(path)) { UseShellExecute=false, CreateNoWindow=true,
            RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true,
            WorkingDirectory=Path.GetDirectoryName(Path.GetFullPath(path))! };
        if(fixtureArgument!=null) start.ArgumentList.Add(fixtureArgument);
        _process=new Process {StartInfo=start};
        bool started=false;
        try {
            if(!_process.Start()) throw new IOException("WorkerLaunchFailed");
            started=true;
            // Worker blocks on stdin before COM activation: job assignment precedes the discovery request.
            if(!AssignProcessToJobObject(_job,_process.SafeHandle)) throw new System.ComponentModel.Win32Exception();
            _ = DrainErrorAsync(); // bounded-memory discard; never log vendor strings or let stderr block the worker
        } catch { if(started && !_process.HasExited) _process.Kill(true); _process.Dispose(); _job.Dispose(); throw; }
    }
    private async Task DrainErrorAsync() {
        try { var buffer=new char[512]; while(await _process.StandardError.ReadAsync(buffer)>0) {} }
        catch(Exception e) when(e is IOException or ObjectDisposedException or InvalidOperationException) {}
    }
    public Task WaitForExitAsync(CancellationToken token) => _process.WaitForExitAsync(token);
    public async ValueTask DisposeAsync() {
        _job.Dispose(); // also on parent crash, OS closes its non-inherited job handle
        try { using var stop=new CancellationTokenSource(TimeSpan.FromSeconds(2)); await _process.WaitForExitAsync(stop.Token); }
        catch(OperationCanceledException) { if(!_process.HasExited) _process.Kill(true); }
        finally { _process.Dispose(); }
    }
    [StructLayout(LayoutKind.Sequential)] private struct BasicLimits {
        public long PerProcessTime,PerJobTime; public uint LimitFlags; public UIntPtr MinWorkingSet,MaxWorkingSet;
        public uint ActiveProcessLimit; public UIntPtr Affinity; public uint Priority,SchedulingClass;
    }
    [StructLayout(LayoutKind.Sequential)] private struct IoCounters { public ulong ReadOps,WriteOps,OtherOps,ReadBytes,WriteBytes,OtherBytes; }
    [StructLayout(LayoutKind.Sequential)] private struct JobLimits { public BasicLimits Basic; public IoCounters Io; public UIntPtr ProcessMemory,JobMemory,PeakProcessMemory,PeakJobMemory; }
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] private static extern SafeFileHandle CreateJobObject(IntPtr security,string? name);
    [DllImport("kernel32.dll",SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)] private static extern bool SetInformationJobObject(SafeFileHandle job,int type,ref JobLimits info,int size);
    [DllImport("kernel32.dll",SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)] private static extern bool AssignProcessToJobObject(SafeFileHandle job,SafeProcessHandle process);
}
