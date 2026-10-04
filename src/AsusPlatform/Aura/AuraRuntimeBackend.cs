using System.Diagnostics;
using System.Text.Json;
using AceHFX.AsusPlatform.Runtime;
using Microsoft.Win32;
using AceHFX.AsusPlatform.Ipc;

namespace AceHFX.AsusPlatform.Aura;

public sealed class AuraRuntimeBackend : IAuraBackend
{
    private readonly Func<IAuraWorker> _launch;
    private readonly Func<CancellationToken,Task<DynamicLightingObservation>> _dynamic;
    private readonly Func<LightingTopologyObservation> _topology;
    private readonly TimeSpan _total;
    private readonly TimeSpan? _fixtureStageTimeout;
    private readonly Func<SecurityContext> _owner;
    private readonly SemaphoreSlim _single = new(1,1);
    public AuraOwnershipCoordinator LogicalOwnership { get; } = new();
    public AuraRuntimeBackend() : this(()=>new WorkerProcess(Path.Combine(AppContext.BaseDirectory,"AuraWorker.exe")),
        AuraObservations.DynamicLightingAsync,AuraObservations.LightingTopology,TimeSpan.FromSeconds(90)) {}
    internal AuraRuntimeBackend(Func<IAuraWorker> launch,Func<CancellationToken,Task<DynamicLightingObservation>> dynamic,
        Func<LightingTopologyObservation> topology,TimeSpan total,TimeSpan? fixtureStageTimeout=null,Func<SecurityContext>? owner=null) {
        _launch=launch; _dynamic=dynamic; _topology=topology; _total=total; _fixtureStageTimeout=fixtureStageTimeout; _owner=owner??NativeSecurity.Current;
    }
    public async Task<AuraRuntimeState> DiscoverAsync(CancellationToken token=default) {
        await _single.WaitAsync(token);
        try { return await DiscoverCoreAsync(token); }
        finally { _single.Release(); }
    }
    private async Task<AuraRuntimeState> DiscoverCoreAsync(CancellationToken token) {
        var instance=Guid.NewGuid(); var request=Guid.NewGuid(); var timings=new Dictionary<string,double>();
        var inventory=new AsusRuntimeDetector();
        var sdk=await Task.Run(()=>inventory.ComClass("AuraSdk",AsusCapabilityDetector.AuraSdkClass,RegistryView.Registry64),token);
        var service=await Task.Run(()=>inventory.Service("LightingService"),token);
        var topology=await Task.Run(_topology,token);
        DynamicLightingObservation dynamic;
        using(var dl=CancellationTokenSource.CreateLinkedTokenSource(token)) {
            dl.CancelAfter(TimeSpan.FromSeconds(5));
            try { dynamic=await _dynamic(dl.Token); }
            catch(OperationCanceledException) when(!token.IsCancellationRequested) { dynamic=new(ExecutionState.TimedOut,new Dictionary<string,string>(),Evidence.NotAttempted("UserSettingUnknown"),[],Evidence.NotAttempted("AmbientOwnerUnknown"),Evidence.NotAttempted("AuraParticipationUnknown"),"DynamicLightingTimedOut"); }
        }
        JsonElement? snapshot=null; AuraValue<bool?>? activationAck=null; PlatformError error=PlatformError.None; string diagnostic="ReadOnlyDiscoveryComplete";
        int? pid=null; string phase="startup"; bool reachable=false, enumerationStarted=false;
        using var total=CancellationTokenSource.CreateLinkedTokenSource(token); total.CancelAfter(_total);
        try {
            await using var worker=_launch(); pid=worker.Pid;
            var owner=_owner();
            LogicalOwnership.BeginLogicalLease(owner,worker.Pid,instance,TimeSpan.FromSeconds(30));
            await WorkerProtocol.WriteAsync(worker.Input,new(1,request,"discover"),total.Token);
            int sequence=0; var stageWatch=Stopwatch.StartNew();
            while(true) {
                using var deadline=CancellationTokenSource.CreateLinkedTokenSource(total.Token);
                deadline.CancelAfter(_fixtureStageTimeout??(phase is "activation" or "enumeration" ? TimeSpan.FromSeconds(10) : phase=="property" ? TimeSpan.FromSeconds(3) : TimeSpan.FromSeconds(2)));
                WorkerMessage message;
                try { message=await WorkerProtocol.ReadAsync(worker.Output,deadline.Token); }
                catch(OperationCanceledException) when(!token.IsCancellationRequested) { throw new TimeoutException("VendorStageTimeout:"+phase); }
                if(message.ProtocolVersion!=1 || message.RequestId!=request || message.WorkerPid!=worker.Pid || message.Sequence!=sequence++ || sequence>100000)
                    throw new InvalidDataException("WorkerEnvelopeInvalid");
                reachable=true; timings[phase]=timings.GetValueOrDefault(phase)+stageWatch.Elapsed.TotalMilliseconds; stageWatch.Restart();
                LogicalOwnership.Heartbeat(LogicalOwnership.Lease!.LeaseId,owner,TimeSpan.FromSeconds(30));
                if(message.Kind=="activation") {
                    if(activationAck!=null || message.Stage!="activation" || message.Snapshot==null) throw new InvalidDataException("WorkerActivationEnvelopeInvalid");
                    activationAck=AuraTopologyMapper.Value<bool?>(message.Snapshot.Value,"activation");
                    continue;
                }
                if(message.Kind=="result") {
                    if(message.Stage!="complete") throw new InvalidDataException("WorkerResultStageInvalid");
                    snapshot=message.Snapshot??throw new InvalidDataException("WorkerSnapshotMissing"); break;
                }
                if(message.Kind!="progress" || message.Stage is not ("startup" or "activation" or "property" or "enumeration" or "shutdown")) throw new InvalidDataException("WorkerStageInvalid");
                phase=message.Stage;
                enumerationStarted |= phase=="enumeration";
            }
            using var stop=CancellationTokenSource.CreateLinkedTokenSource(total.Token); stop.CancelAfter(TimeSpan.FromSeconds(2));
            try { await worker.WaitForExitAsync(stop.Token); }
            catch(OperationCanceledException) when(!token.IsCancellationRequested) { throw new TimeoutException("WorkerShutdownTimeout"); }
            if(worker.ExitCode!=0) { snapshot=null; error=PlatformError.WorkerCrashed; diagnostic="WorkerExitedAfterResult"; }
        } catch(OperationCanceledException) when(!token.IsCancellationRequested) { error=PlatformError.VendorTimeout; diagnostic="WorkerTotalTimeout"; }
        catch(TimeoutException) { error=PlatformError.VendorTimeout; diagnostic="VendorTimeout:"+phase; }
        catch(EndOfStreamException) { error=PlatformError.WorkerCrashed; diagnostic="WorkerExitedOrDisconnected:"+phase; }
        catch(Exception e) when(e is JsonException or InvalidDataException) { error=PlatformError.MalformedRequest; diagnostic="MalformedWorkerResponse"; }
        catch(Exception e) when(e is IOException or System.ComponentModel.Win32Exception or UnauthorizedAccessException) { error=PlatformError.ServiceUnavailable; diagnostic="AuraWorkerUnavailable"; }
        finally {
            // await using has already closed the OS job and discarded every COM object before lease cleanup.
            if(LogicalOwnership.Lease!=null) {
                if(error==PlatformError.None) LogicalOwnership.ReleaseLogicalLease();
                else { new AuraRecoveryCoordinator(LogicalOwnership).WorkerFailed(error); LogicalOwnership.CompleteLogicalRecovery(true); }
            }
        }
        var activation=activationAck??new AuraValue<bool?>(ExecutionState.NotAttempted,null); var categories=(IReadOnlyList<AuraEnumeration>)Array.Empty<AuraEnumeration>();
        var api=Evidence.NotAttempted("AuraSdkVersionInterfacesNotQueried");
        var sdk2=new AuraValue<bool?>(ExecutionState.NotAttempted,null);
        var sdk3=new AuraValue<bool?>(ExecutionState.NotAttempted,null);
        if(snapshot!=null) {
            try {
                activation=AuraTopologyMapper.Value<bool?>(snapshot.Value,"activation");
                if(activationAck==null || activation!=activationAck) throw new InvalidDataException("WorkerActivationEvidenceMismatch");
                categories=AuraTopologyMapper.Map(snapshot.Value,instance,topology);
                if(activation.Execution==ExecutionState.Succeeded && (categories.Count!=AuraTopologyMapper.Categories.Length || activation.Value!=true)) throw new InvalidDataException("WorkerEnumerationIncomplete");
                sdk2=AuraTopologyMapper.Value<bool?>(snapshot.Value,"sdk2"); sdk3=AuraTopologyMapper.Value<bool?>(snapshot.Value,"sdk3");
                api=sdk2.Value==true || sdk3.Value==true ? Evidence.Detected(true,"QueryInterfaceOnlyOwnershipNotInvoked") :
                    activation.Execution==ExecutionState.Succeeded ? Evidence.Detected(true,"CanonicalSdk1SwitchModeSlotPresentReleaseSupportSeparate") :
                    sdk2.Execution==ExecutionState.PermissionDenied || sdk3.Execution==ExecutionState.PermissionDenied ? Evidence.Denied("OwnershipInterfaceQueryDenied") :
                    sdk2.Execution==ExecutionState.NotAttempted && sdk3.Execution==ExecutionState.NotAttempted ? Evidence.NotAttempted("OwnershipInterfacesNotQueried") : Evidence.Unknown("OwnershipInterfaceSupportUnknown");
            } catch(Exception e) when(e is JsonException or InvalidDataException or InvalidOperationException or KeyNotFoundException or FormatException or OverflowException) {
                error=PlatformError.MalformedRequest; diagnostic="WorkerSnapshotInvalid"; activation=activationAck??new(ExecutionState.Failed,null); categories=[]; api=Evidence.NotAttempted("InvalidWorkerSnapshot"); sdk2=new(ExecutionState.NotAttempted,null); sdk3=new(ExecutionState.NotAttempted,null);
            }
        }
        if(activationAck==null && phase=="activation" && error==PlatformError.VendorTimeout) activation=new(ExecutionState.TimedOut,null);
        if(activationAck==null && phase=="activation" && error==PlatformError.WorkerCrashed) activation=new(ExecutionState.Failed,null);
        var enumeration=categories.Count==0 ? (enumerationStarted ? error==PlatformError.VendorTimeout ? ExecutionState.TimedOut : ExecutionState.Failed : ExecutionState.NotAttempted) : categories.All(x=>x.Execution==ExecutionState.Succeeded && x.Count.Execution==ExecutionState.Succeeded) ? ExecutionState.Succeeded : ExecutionState.Failed;
        int? count=enumeration==ExecutionState.Succeeded ? categories.Sum(x=>x.Count.Value!.Value) : null;
        int? unique=count!=null && categories.SelectMany(x=>x.Devices).All(x=>x.ComIdentityIndex!=null) ? categories.SelectMany(x=>x.Devices).Select(x=>x.ComIdentityIndex).Distinct().Count() : null;
        bool anyTopology=categories.SelectMany(x=>x.Devices).Any(x=>x.LightCount?.Execution==ExecutionState.Succeeded && !x.Anomalies.Contains("SomeLightGettersFailed"));
        var capabilities=new AuraRuntimeCapabilities(sdk.Installed,sdk.Registered,service.Running,
            Evidence.Detected(reachable,"WorkerEnvelopeObserved"),new(activation.Execution==ExecutionState.Succeeded ? CapabilityState.Available : CapabilityState.Unknown,activation.Execution,"ComActivationResult",activation.Value),
            enumeration==ExecutionState.NotAttempted ? Evidence.NotAttempted("EnumerationNotAttempted") : new(enumeration==ExecutionState.Succeeded ? CapabilityState.Available : CapabilityState.Unknown,enumeration,"CategoryEnumerationAndCount",enumeration==ExecutionState.Succeeded),
            count==null ? Evidence.NotAttempted("DeviceCountUnknown") : Evidence.Detected(count>0,"EnumerationObservationCountNotUniqueHardwareCount"),
            anyTopology ? Evidence.Detected(true,"AtLeastOneTopologyCollectionRead") : Evidence.NotAttempted("NoReturnedDeviceTopology"),api,
            Evidence.NotAttempted("AURA_OWNERSHIP_APPROVAL_REQUIRED"),Evidence.NotAttempted("SharedMemoryNotOpenedM2"),Evidence.NotAttempted("RealtimeOutputNotValidatedM2"));
        if(error==PlatformError.None && activation.Execution!=ExecutionState.Succeeded) { error=activation.Execution==ExecutionState.PermissionDenied ? PlatformError.AccessDenied : PlatformError.VendorApiFailure; diagnostic="ComActivationFailed"; }
        if(error==PlatformError.None && enumeration!=ExecutionState.Succeeded) { error=PlatformError.VendorApiFailure; diagnostic="SomeEnumerationCategoriesFailed"; }
        return new(instance,DateTimeOffset.UtcNow,pid,error,diagnostic,activation.Execution,activation.HResult,enumeration,count,unique,categories,capabilities,dynamic,topology,AuraOwnershipState.Unknown,timings,sdk2,sdk3);
    }
}
