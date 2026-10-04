using System.Diagnostics;
using System.Text.Json;
using AceHFX.AsusPlatform;
using AceHFX.AsusPlatform.Aura;
using AceHFX.AsusPlatform.Ipc;

namespace AsusPlatform.Tests;

internal sealed class AuraClock : TimeProvider
{
    public DateTimeOffset Now = new(2026,10,1,0,0,0,TimeSpan.Zero);
    public override DateTimeOffset GetUtcNow() => Now;
}
[TestClass]
public sealed class AuraRuntimeTests
{
    private static SecurityContext FixtureOwner() => new("S-1-5-21-1-2-3-1001",Environment.ProcessId,1,false);
    private static DynamicLightingObservation UnknownDynamic() => new(ExecutionState.Succeeded,new Dictionary<string,string>(),Evidence.NotAttempted("FixtureUnknown"),[],Evidence.NotAttempted("NoOwnerProof"),Evidence.NotAttempted("NoParticipationProof"),"Fixture");
    private static LightingTopologyObservation EmptyTopology() => new(ExecutionState.Succeeded,"Fixture",null,[],new Dictionary<string,string>(),new Dictionary<string,string>(),"Fixture");
    private static AuraRuntimeBackend Backend(string mode) => new(()=>new WorkerProcess(Path.Combine(AppContext.BaseDirectory,"AuraWorkerFixture.exe"),mode),
        _=>Task.FromResult(UnknownDynamic()),EmptyTopology,TimeSpan.FromSeconds(5),TimeSpan.FromMilliseconds(500),FixtureOwner);
    [TestMethod] public async Task ZeroCountRequiresSuccessfulEnumeration() {
        var result=await Backend("zero").DiscoverAsync();
        Assert.AreEqual(PlatformError.None,result.Error); Assert.AreEqual(ExecutionState.Succeeded,result.Enumeration);
        Assert.AreEqual(0,result.DeviceCount); Assert.AreEqual(0,result.UniqueComIdentityCount);
        Assert.HasCount(11,result.Categories); Assert.AreEqual(ExecutionState.NotAttempted,result.Capabilities.ReadableTopology.Execution);
        Assert.AreEqual(AuraOwnershipState.Unknown,result.Ownership);
        Assert.AreEqual(ExecutionState.NotAttempted,result.Capabilities.OwnershipValidated.Execution);
    }
    [TestMethod] public async Task DeviceAndLightGetterMetadataIsMappedWithoutMatrixAssumption() {
        var result=await Backend("devices").DiscoverAsync(); var device=result.Categories[0].Devices.Single();
        Assert.AreEqual(1,result.DeviceCount); Assert.AreEqual("Fixture Motherboard",device.Name!.Value);
        Assert.AreEqual((byte)11,device.Lights[0].Red!.Value); Assert.AreEqual((uint)0x0b1621,device.Lights[0].Color!.Value);
        Assert.IsNull(device.StableDeviceId); Assert.AreEqual(MappingConfidence.Unknown,device.Zones[0].Confidence);
        Assert.IsEmpty(device.Anomalies);
    }
    [TestMethod] public async Task DuplicatesUseWorkerComIdentityAndPreserveCategoryOccurrences() {
        var result=await Backend("duplicates").DiscoverAsync(); Assert.AreEqual(2,result.DeviceCount); Assert.AreEqual(1,result.UniqueComIdentityCount);
        CollectionAssert.AreEqual(new uint[]{0x10000},result.Categories[0].Devices[0].AlsoSeenInCategories.ToArray());
        Assert.AreNotEqual(result.Categories[0].Devices[0].RuntimeId,result.Categories[1].Devices[0].RuntimeId);
    }
    [TestMethod] public async Task TopologyMismatchIsPreserved() {
        var result=await Backend("mismatch").DiscoverAsync(); var d=result.Categories[0].Devices[0];
        CollectionAssert.Contains(d.Anomalies.ToArray(),"WidthHeightDoesNotMatchLightsCount"); Assert.AreEqual(1,d.LightCount!.Value); Assert.HasCount(1,d.Lights);
    }
    [TestMethod] public async Task EnumerationFailureNeverLooksLikeZeroDevices() {
        var result=await Backend("enum-failed").DiscoverAsync(); Assert.AreEqual(ExecutionState.Failed,result.Enumeration); Assert.IsNull(result.DeviceCount);
        Assert.IsNull(result.Categories[1].Count.Value); Assert.AreEqual(PlatformError.VendorApiFailure,result.Error);
    }
    [TestMethod] public async Task ActivationDeniedLeavesEnumerationNotAttempted() {
        var result=await Backend("denied").DiscoverAsync(); Assert.AreEqual(ExecutionState.PermissionDenied,result.Activation);
        Assert.AreEqual(PlatformError.AccessDenied,result.Error); Assert.AreEqual(ExecutionState.NotAttempted,result.Enumeration); Assert.IsNull(result.DeviceCount);
        Assert.AreEqual(ExecutionState.NotAttempted,result.Capabilities.OwnershipApiPresent.Execution);
    }
    [TestMethod] public async Task WorkerCrashDoesNotTerminateTestHostAndLogicalRecoveryRuns() {
        var backend=Backend("crash"); var result=await backend.DiscoverAsync(); Assert.AreEqual(PlatformError.WorkerCrashed,result.Error);
        Assert.IsNull(result.DeviceCount); Assert.IsNull(backend.LogicalOwnership.Lease);
        Assert.IsTrue(backend.LogicalOwnership.Events.Any(e=>e.Event==AuraWatchdogEvent.RecoverySucceeded));
        Assert.IsFalse(backend.LogicalOwnership.Events.Any(e=>e.HardwareAcquired));
    }
    [TestMethod] public async Task VendorHangIsTerminatedAndStructuredTimeoutReturned() {
        var result=await Backend("hang").DiscoverAsync(); Assert.AreEqual(PlatformError.VendorTimeout,result.Error); Assert.IsNull(result.DeviceCount);
        Assert.AreEqual(ExecutionState.TimedOut,result.Activation);
        Assert.ThrowsExactly<ArgumentException>(()=>Process.GetProcessById(result.WorkerPid!.Value));
    }
    [TestMethod] public async Task MalformedWorkerResponseIsContained() {
        var result=await Backend("malformed").DiscoverAsync(); Assert.AreEqual(PlatformError.MalformedRequest,result.Error); Assert.IsNull(result.DeviceCount);
    }
    [TestMethod] public async Task ReconnectAfterCrashOrTimeoutUsesFreshWorkerAndBackendIdentity() {
        int attempt=0; var backend=new AuraRuntimeBackend(()=>new WorkerProcess(Path.Combine(AppContext.BaseDirectory,"AuraWorkerFixture.exe"),attempt++==0?"crash":attempt==2?"hang":"zero"),
            _=>Task.FromResult(UnknownDynamic()),EmptyTopology,TimeSpan.FromSeconds(5),TimeSpan.FromMilliseconds(500),FixtureOwner);
        var a=await backend.DiscoverAsync();var b=await backend.DiscoverAsync();var c=await backend.DiscoverAsync();
        Assert.AreEqual(PlatformError.WorkerCrashed,a.Error); Assert.AreEqual(PlatformError.VendorTimeout,b.Error); Assert.AreEqual(PlatformError.None,c.Error);
        Assert.AreNotEqual(a.BackendInstanceId,c.BackendInstanceId); Assert.AreNotEqual(a.WorkerPid,c.WorkerPid);
    }
    [TestMethod] public async Task DynamicLightingUnknownDoesNotAssertCurrentOwner() {
        var result=await Backend("zero").DiscoverAsync(); Assert.IsNull(result.DynamicLighting.Enabled.Value);
        Assert.IsNull(result.DynamicLighting.WindowsAmbientControl.Value); Assert.AreEqual(AuraOwnershipState.Unknown,result.Ownership);
    }
    [TestMethod] public void LogicalLeaseDuplicateAndWrongHeartbeatAreRejected() {
        var coordinator=new AuraOwnershipCoordinator(); var owner=FixtureOwner(); var lease=coordinator.BeginLogicalLease(owner,123,Guid.NewGuid(),TimeSpan.FromSeconds(10));
        Assert.ThrowsExactly<InvalidOperationException>(()=>coordinator.BeginLogicalLease(owner,123,Guid.NewGuid(),TimeSpan.FromSeconds(10)));
        Assert.ThrowsExactly<InvalidOperationException>(()=>coordinator.Heartbeat(lease.LeaseId,owner with{Sid="Wrong"},TimeSpan.FromSeconds(10)));
        coordinator.Heartbeat(lease.LeaseId,owner,TimeSpan.FromSeconds(10)); Assert.AreEqual(PlatformError.AuraWriteApprovalRequired,coordinator.RequestHardwareOwnership());
        coordinator.ReleaseLogicalLease(); Assert.IsNull(coordinator.Lease); Assert.AreEqual(AuraOwnershipState.Unknown,coordinator.ObservedHardwareOwner);
    }
    [TestMethod] public void HeartbeatExpiryAndRecoveryFailurePreservePendingMetadata() {
        var clock=new AuraClock(); var coordinator=new AuraOwnershipCoordinator(clock);var owner=FixtureOwner();
        coordinator.BeginLogicalLease(owner,123,Guid.NewGuid(),TimeSpan.FromSeconds(10));clock.Now+=TimeSpan.FromSeconds(11);
        coordinator.Tick(owner,true,true,true,true); Assert.AreEqual(AuraOwnershipState.RecoveryPending,coordinator.Lease!.OwnershipState);
        coordinator.CompleteLogicalRecovery(false);Assert.IsNotNull(coordinator.Lease);coordinator.CompleteLogicalRecovery(true);Assert.IsNull(coordinator.Lease);
    }
    [TestMethod] public void DisconnectSessionSwitchUiExitAndWorkerExitEnterRecovery() {
        var owner=FixtureOwner();
        foreach(var scenario in new[]{0,1,2,3,4}) {
            var c=new AuraOwnershipCoordinator();c.BeginLogicalLease(owner,123,Guid.NewGuid(),TimeSpan.FromSeconds(10));
            c.Tick(scenario==4?owner with{SessionId=99}:owner,scenario!=0,scenario!=1,scenario!=2,scenario!=3);
            Assert.AreEqual(AuraOwnershipState.RecoveryPending,c.Lease!.OwnershipState);c.CompleteLogicalRecovery(true);
        }
    }
    [TestMethod] public void JournalRejectsHardwareClaimsAndDifferentBoot() {
        var path=Path.Combine(Path.GetTempPath(),Guid.NewGuid()+".json");
        try {
            var c=new AuraOwnershipCoordinator();var lease=c.BeginLogicalLease(FixtureOwner(),123,Guid.NewGuid(),TimeSpan.FromSeconds(10));
            var record=new AuraRecoveryMetadata(1,DateTimeOffset.UnixEpoch,lease,true,false);AuraRecoveryJournal.Save(path,record);
            Assert.IsNotNull(AuraRecoveryJournal.ReadLogical(path,DateTimeOffset.UnixEpoch));Assert.IsNull(AuraRecoveryJournal.ReadLogical(path,DateTimeOffset.UnixEpoch.AddSeconds(1)));
            Assert.ThrowsExactly<InvalidOperationException>(()=>AuraRecoveryJournal.Save(path,record with{HardwareAcquired=true}));
        } finally { File.Delete(path); }
    }
    [TestMethod, TestCategory("DesktopSmoke")] public async Task ParentCrashKillsIsolatedWorkerWithoutUiRemainingAlive() {
        var evidence=Path.Combine(Path.GetTempPath(),Guid.NewGuid()+".json");var journal=evidence+".journal";
        using var parent=new Process{StartInfo=new(Path.Combine(AppContext.BaseDirectory,"host-fixture","AuraWorkerHostFixture.exe")){UseShellExecute=false,CreateNoWindow=true}};
        parent.StartInfo.ArgumentList.Add(Path.Combine(AppContext.BaseDirectory,"AuraWorkerFixture.exe"));parent.StartInfo.ArgumentList.Add(evidence);parent.StartInfo.ArgumentList.Add(journal);
        int? childPid=null;
        try {
            Assert.IsTrue(parent.Start()); using var deadline=new CancellationTokenSource(TimeSpan.FromSeconds(8));
            while(!File.Exists(evidence)) { if(parent.HasExited) Assert.Fail("HostFixtureExited:"+parent.ExitCode); await Task.Delay(30,deadline.Token); }
            using var data=JsonDocument.Parse(File.ReadAllText(evidence));childPid=data.RootElement.GetProperty("workerPid").GetInt32();
            using var child=Process.GetProcessById(childPid.Value); parent.Kill();await parent.WaitForExitAsync(deadline.Token);await child.WaitForExitAsync(deadline.Token);
            Assert.IsTrue(child.HasExited);Assert.IsNotNull(AuraRecoveryJournal.ReadLogical(journal,DateTimeOffset.UnixEpoch));
        } finally {
            if(!parent.HasExited) parent.Kill(); File.Delete(evidence);File.Delete(journal);
            if(childPid!=null) { try { using var orphan=Process.GetProcessById(childPid.Value); if(!orphan.HasExited) orphan.Kill(); } catch(ArgumentException) {} }
        }
    }
    [TestMethod] public void LightingXmlIsReadOnlyBoundedAndLocationAloneRemainsUncertain() {
        var a=Path.GetTempFileName();var b=Path.GetTempFileName();
        try {
            File.WriteAllText(a,"<root><device key='MB'><ledlist><led key='0'><location>8</location><locationname>Back Plate</locationname></led></ledlist></device></root>");
            File.WriteAllText(b,"<root><lastsynclist><device key='Mainboard_Master'>0</device></lastsynclist><lastunsynclist><device key='WDL_Keyboard'>47</device></lastunsynclist></root>");
            var result=AuraObservations.ReadLightingTopology(a,b);Assert.AreEqual(ExecutionState.Succeeded,result.Execution);Assert.AreEqual((uint)8,result.Zones[0].LocationId);
            Assert.AreEqual("47",result.LastUnsyncList["WDL_Keyboard"]);
            File.WriteAllText(a,"<!DOCTYPE root [<!ENTITY x SYSTEM 'file:///invalid'>]><root>&x;</root>");Assert.AreEqual(ExecutionState.Unavailable,AuraObservations.ReadLightingTopology(a,b).Execution);
        } finally {File.Delete(a);File.Delete(b);}
    }
    [TestMethod] public void NewStatesAppendExistingWireValues() {
        Assert.AreEqual(ExecutionState.Unavailable,JsonSerializer.Deserialize<ExecutionState>("4"));
        Assert.AreEqual(ExecutionState.TimedOut,JsonSerializer.Deserialize<ExecutionState>("5"));
        Assert.AreEqual(PlatformError.TruncatedFrame,JsonSerializer.Deserialize<PlatformError>("13"));
        Assert.AreEqual(PlatformError.VendorTimeout,JsonSerializer.Deserialize<PlatformError>("14"));
    }
    [TestMethod] public async Task SuccessfulNullGetterAndDuplicateJsonAreRejected() {
        using var snapshot=JsonDocument.Parse("{\"count\":{\"execution\":1,\"hresult\":0,\"value\":null}}");
        Assert.ThrowsExactly<InvalidDataException>(()=>AuraTopologyMapper.Value<int?>(snapshot.RootElement,"count"));
        using var frame=new MemoryStream();
        var payload=System.Text.Encoding.UTF8.GetBytes("{\"protocolVersion\":1,\"ProtocolVersion\":1}");
        var header=new byte[4];System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(header,payload.Length);
        frame.Write(header);frame.Write(payload);frame.Position=0;
        await Assert.ThrowsAsync<InvalidDataException>(()=>WorkerProtocol.ReadAsync(frame,default));
    }
    [TestMethod] public async Task CancellationClosesWorkerAndLogicalLease() {
        var backend=Backend("hang");using var stop=new CancellationTokenSource(TimeSpan.FromSeconds(5));
        var operation=backend.DiscoverAsync(stop.Token);
        while(backend.LogicalOwnership.Lease==null) {Assert.IsFalse(operation.IsCompleted);await Task.Delay(10,stop.Token);}
        using var child=Process.GetProcessById(backend.LogicalOwnership.Lease.WorkerPid);stop.Cancel();
        await Assert.ThrowsAsync<OperationCanceledException>(()=>operation);
        Assert.IsTrue(child.HasExited);
        Assert.IsNull(backend.LogicalOwnership.Lease);
    }
    [TestMethod,DataRow(-1),DataRow(0),DataRow(2097153)] public async Task WorkerFrameLengthIsBounded(int length) {
        var header=new byte[4];System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(header,length);
        using var frame=new MemoryStream(header);
        await Assert.ThrowsAsync<InvalidDataException>(()=>WorkerProtocol.ReadAsync(frame,default));
    }
    [TestMethod] public async Task EnumerationTimeoutPreservesAcknowledgedActivationEvidence() {
        var result=await Backend("enum-hang").DiscoverAsync();
        Assert.AreEqual(PlatformError.VendorTimeout,result.Error); Assert.AreEqual(ExecutionState.Succeeded,result.Activation);
        Assert.AreEqual(ExecutionState.TimedOut,result.Enumeration); Assert.IsNull(result.DeviceCount);
    }
}
