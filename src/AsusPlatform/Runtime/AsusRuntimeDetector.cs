using System.ComponentModel;
using System.Diagnostics;
using System.Security;
using System.ServiceProcess;
using Microsoft.Win32;

namespace AceHFX.AsusPlatform.Runtime;

public interface IRuntimeInventory
{
    ComponentStatus Service(string name);
    ComponentStatus ComClass(string name, string classId, RegistryView view);
    Evidence DynamicLighting();
    SecurityContext SecurityContext();
}
internal static class DiscoveryEvidence
{
    public static ComponentStatus Component(string name, Evidence installed, Evidence registered, Evidence running, ComponentVersion version) =>
        new(name, installed, registered, running, Evidence.NotAttempted("VendorActivationNotAttemptedM1"),
            Evidence.NotAttempted("HardwareValidationNotAttemptedM1"), version);
    public static ComponentVersion NoVersion(string reason, ExecutionState state = ExecutionState.NotAttempted) => new(null, state, "Unknown", reason);
    public static ComponentStatus Failure(string name, Exception error) => Component(name,
        error is UnauthorizedAccessException or SecurityException || error is Win32Exception { NativeErrorCode: 5 } ?
            Evidence.Denied("MetadataAccessDenied") : Evidence.Unknown("MetadataQueryFailed"),
        Evidence.NotAttempted("QueryFailed"), Evidence.NotAttempted("QueryFailed"), NoVersion("QueryFailed"));
}
public sealed class AsusVersionDetector
{
    // These paths come only from fixed, machine registration keys, never from IPC.
    public ComponentVersion Read(string? commandLine)
    {
        if (string.IsNullOrWhiteSpace(commandLine)) return DiscoveryEvidence.NoVersion("ImagePathMissing");
        try
        {
            var path = Environment.ExpandEnvironmentVariables(commandLine.Trim());
            if (path.StartsWith('"'))
            {
                var close = path.IndexOf('"', 1);
                if (close < 0) return DiscoveryEvidence.NoVersion("InvalidImagePath", ExecutionState.Failed);
                path = path[1..close];
            }
            else
            {
                var end = path.IndexOf(".exe", StringComparison.OrdinalIgnoreCase);
                if (end >= 0) path = path[..(end + 4)];
            }
            if (path.StartsWith(@"\SystemRoot\", StringComparison.OrdinalIgnoreCase))
                path = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), path[12..]);
            if (path.StartsWith(@"\??\", StringComparison.Ordinal)) path = path[4..];
            if (!Path.IsPathFullyQualified(path) || path.StartsWith(@"\\", StringComparison.Ordinal))
                return DiscoveryEvidence.NoVersion("NonLocalImagePathRejected", ExecutionState.Failed);
            var value = FileVersionInfo.GetVersionInfo(path).FileVersion;
            return new(value, ExecutionState.Succeeded, "Unknown", value == null ? "VersionResourceMissing" : "DetectedCompatibilityUnknown");
        }
        catch (UnauthorizedAccessException) { return DiscoveryEvidence.NoVersion("VersionAccessDenied", ExecutionState.PermissionDenied); }
        catch (Exception e) when (e is IOException or ArgumentException or Win32Exception) { return DiscoveryEvidence.NoVersion("VersionUnavailable", ExecutionState.Unavailable); }
    }
}
public sealed class AsusServiceDetector(AsusVersionDetector versions)
{
    public ComponentStatus Detect(string name)
    {
        try
        {
            using var machine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64);
            using var registration = machine.OpenSubKey(@"SYSTEM\CurrentControlSet\Services\" + name);
            if (registration == null) return DiscoveryEvidence.Component(name, Evidence.Detected(false, "ScmRegistrationMissing"),
                Evidence.Detected(false, "ScmRegistrationMissing"), Evidence.Detected(false, "ServiceMissing"),
                DiscoveryEvidence.NoVersion("ServiceMissing", ExecutionState.Unavailable));
            var version = versions.Read(registration.GetValue("ImagePath") as string);
            Evidence running;
            try
            {
                using var service = new ServiceController(name);
                var state = service.Status;
                running = state == ServiceControllerStatus.Running ? Evidence.Detected(true, "ScmRunning") :
                    new(CapabilityState.InstalledButStopped, ExecutionState.Succeeded, "ScmState:" + state, false);
            }
            catch (Exception e) when (e is InvalidOperationException or Win32Exception)
            {
                running = (e.InnerException ?? e) is Win32Exception { NativeErrorCode: 5 } ?
                    Evidence.Denied("ScmQueryAccessDenied") : Evidence.Unknown("ScmQueryFailed");
            }
            return DiscoveryEvidence.Component(name, Evidence.Detected(true, "MachineServiceRegistration"),
                Evidence.Detected(true, "MachineServiceRegistration"), running, version);
        }
        catch (Exception e) when (e is UnauthorizedAccessException or SecurityException or IOException) { return DiscoveryEvidence.Failure(name, e); }
    }
}
public sealed class AsusComDetector(AsusVersionDetector versions)
{
    public ComponentStatus Detect(string name, string classId, RegistryView view)
    {
        try
        {
            using var machine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view);
            using var cls = machine.OpenSubKey(@"SOFTWARE\Classes\CLSID\{" + classId + "}");
            using var inproc = cls?.OpenSubKey("InprocServer32");
            using var local = cls?.OpenSubKey("LocalServer32");
            var path = (inproc?.GetValue(null) ?? local?.GetValue(null)) as string;
            var version = cls == null ? DiscoveryEvidence.NoVersion("ClsidMissing", ExecutionState.Unavailable) : versions.Read(path);
            var installed = version.Execution == ExecutionState.Succeeded ? Evidence.Detected(true, "RegisteredServerFileMetadataRead") :
                version.Execution == ExecutionState.PermissionDenied ? Evidence.Denied("RegisteredServerFileAccessDenied") :
                Evidence.NotAttempted("RegistrationDoesNotProveInstallation");
            return DiscoveryEvidence.Component(name,
                installed,
                Evidence.Detected(cls != null, "MachineClsid:" + view), Evidence.NotAttempted("ComNotActivatedM1"),
                version);
        }
        catch (Exception e) when (e is UnauthorizedAccessException or SecurityException or IOException)
        {
            var failure = DiscoveryEvidence.Failure(name, e);
            return failure with { Registered = failure.Installed };
        }
    }
}
public sealed class AsusRuntimeDetector : IRuntimeInventory
{
    private readonly AsusServiceDetector _services = new(new());
    private readonly AsusComDetector _com = new(new());
    public ComponentStatus Service(string name)
    {
        var result = _services.Detect(name);
        if (name != "ArmourySocketServer" || result.Installed.Value != false) return result;
        // This component is often a process, not an SCM service. Absence from SCM is
        // not evidence that the ArmourySocketServer runtime component is absent.
        var processes = Process.GetProcessesByName(name);
        try
        {
            if (processes.Length == 0) return result with { Installed = Evidence.NotAttempted("NoServiceOrProcessInstallationUnknown"),
                Running = Evidence.Detected(false, "ProcessNotObserved"), Version = DiscoveryEvidence.NoVersion("NoProcessImage", ExecutionState.Unavailable) };
            ComponentVersion version;
            try { version = new AsusVersionDetector().Read(processes[0].MainModule?.FileName); }
            catch (Win32Exception) { version = DiscoveryEvidence.NoVersion("ProcessImageAccessDenied", ExecutionState.PermissionDenied); }
            catch (InvalidOperationException) { version = DiscoveryEvidence.NoVersion("ProcessExited", ExecutionState.Unavailable); }
            return result with { Installed = Evidence.NotAttempted("ProcessDoesNotProveInstallation"),
                Running = Evidence.Detected(true, "NamedProcessObservedOnly"), Version = version };
        }
        finally { foreach (var process in processes) process.Dispose(); }
    }
    public ComponentStatus ComClass(string name, string classId, RegistryView view) => _com.Detect(name, classId, view);
    public SecurityContext SecurityContext() => Ipc.NativeSecurity.Current();
    public Evidence DynamicLighting()
    {
        try
        {
            var identity = Ipc.NativeSecurity.Current();
            var sid = identity.IsSystem ? Ipc.InteractiveSessionResolver.ResolveSystem()?.Sid : identity.Sid;
            if (sid == null) return Evidence.NotAttempted("NoInteractiveSession");
            using var users = RegistryKey.OpenBaseKey(RegistryHive.Users, RegistryView.Registry64);
            using var key = users.OpenSubKey(sid + @"\Software\Microsoft\Lighting");
            return key?.GetValue("IsLampArrayEnabled") is int value ? Evidence.Detected(value != 0, "InteractiveUserLightingSettingOnly") :
                Evidence.NotAttempted("LightingSettingMissingDoesNotProveNoLampArray");
        }
        catch (Exception e) when (e is UnauthorizedAccessException or SecurityException) { return Evidence.Denied("LightingSettingAccessDenied"); }
    }
}
