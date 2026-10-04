using System.Text.Json;

namespace AceHFX.AsusPlatform
{
    // Shared M1 connection hook, extended additively for Aura logical leases. This is never a hardware grant.
    public sealed record DeviceControlLease(Guid LeaseId, int OwnerPid, string OwnerSid, DateTimeOffset CreatedAt,
        DateTimeOffset LastHeartbeat, DateTimeOffset ExpiresAt, int SessionId = -1, int WorkerPid = 0,
        Guid BackendInstanceId = default, Aura.AuraOwnershipState OwnershipState = Aura.AuraOwnershipState.Unknown);
}
namespace AceHFX.AsusPlatform.Aura
{
    public enum AuraWatchdogEvent { AcquireStarted = 0, Acquired = 1, Heartbeat = 2, ReleaseStarted = 3,
        Released = 4, RecoveryRequired = 5, RecoverySucceeded = 6, RecoveryFailed = 7 }
    public enum AuraRecoveryCause { UiExited = 0, WorkerExited = 1, VendorTimeout = 2, UserDisconnected = 3,
        BrokerDisconnected = 4, LeaseExpired = 5, SessionChanged = 6, GracefulExit = 7 }
    public sealed record AuraWatchdogRecord(DateTimeOffset Timestamp, AuraWatchdogEvent Event, Guid LeaseId,
        string Reason, bool HardwareAcquired = false);
    public sealed record AuraRecoveryMetadata(int SchemaVersion, DateTimeOffset BootEpoch, DeviceControlLease Lease,
        bool LogicalOnly, bool HardwareAcquired);
    public sealed class AuraOwnershipCoordinator
    {
        private readonly object _gate=new();
        private readonly List<AuraWatchdogRecord> _events=[];
        private readonly TimeProvider _clock;
        public AuraOwnershipCoordinator(TimeProvider? clock=null) { _clock=clock??TimeProvider.System; }
        public DeviceControlLease? Lease {get;private set;}
        public AuraOwnershipState ObservedHardwareOwner => AuraOwnershipState.Unknown;
        public IReadOnlyList<AuraWatchdogRecord> Events {get {lock(_gate) return _events.ToArray();}}
        public PlatformError RequestHardwareOwnership() => PlatformError.AuraWriteApprovalRequired;
        public DeviceControlLease BeginLogicalLease(SecurityContext owner,int workerPid,Guid backend,TimeSpan lifetime) {
            lock(_gate) {
                if(Lease!=null) throw new InvalidOperationException("DuplicateLogicalLease");
                if(owner.IsSystem || owner.ProcessId<=0 || owner.SessionId<=0 || string.IsNullOrWhiteSpace(owner.Sid) || workerPid<=0 || backend==Guid.Empty || lifetime<=TimeSpan.Zero || lifetime>TimeSpan.FromMinutes(1)) throw new ArgumentException("LogicalLeaseIdentityInvalid");
                var now=_clock.GetUtcNow(); Lease=new(Guid.NewGuid(),owner.ProcessId,owner.Sid,now,now,now+lifetime,owner.SessionId,workerPid,backend,AuraOwnershipState.ClaimPending);
                Record(AuraWatchdogEvent.AcquireStarted,"LogicalOnly");
                Lease=Lease with {OwnershipState=AuraOwnershipState.OwnedByAceHFXAura}; Record(AuraWatchdogEvent.Acquired,"SimulatedOwnershipNoVendorCall"); return Lease;
            }
        }
        public void Heartbeat(Guid leaseId,SecurityContext owner,TimeSpan lifetime) {
            lock(_gate) {
                if(Lease==null || Lease.LeaseId!=leaseId || Lease.OwnerPid!=owner.ProcessId || Lease.OwnerSid!=owner.Sid || Lease.SessionId!=owner.SessionId) throw new InvalidOperationException("LeaseOwnerMismatch");
                if(lifetime<=TimeSpan.Zero || lifetime>TimeSpan.FromMinutes(1)) throw new ArgumentException("LeaseLifetimeInvalid");
                if(Lease.ExpiresAt<=_clock.GetUtcNow() || Lease.OwnershipState!=AuraOwnershipState.OwnedByAceHFXAura) { Recover(AuraRecoveryCause.LeaseExpired); throw new InvalidOperationException("LeaseInactive"); }
                var now=_clock.GetUtcNow(); Lease=Lease with {LastHeartbeat=now,ExpiresAt=now+lifetime}; Record(AuraWatchdogEvent.Heartbeat,"LogicalOnly");
            }
        }
        public void Tick(SecurityContext current,bool ownerAlive,bool workerAlive,bool brokerConnected,bool userConnected) {
            lock(_gate) {
                if(Lease==null) return;
                if(!ownerAlive) Recover(AuraRecoveryCause.UiExited);
                else if(!workerAlive) Recover(AuraRecoveryCause.WorkerExited);
                else if(!brokerConnected) Recover(AuraRecoveryCause.BrokerDisconnected);
                else if(!userConnected) Recover(AuraRecoveryCause.UserDisconnected);
                else if(current.SessionId!=Lease.SessionId || current.Sid!=Lease.OwnerSid || current.ProcessId!=Lease.OwnerPid) Recover(AuraRecoveryCause.SessionChanged);
                else if(Lease.ExpiresAt<=_clock.GetUtcNow()) Recover(AuraRecoveryCause.LeaseExpired);
            }
        }
        public void Recover(AuraRecoveryCause cause) {
            lock(_gate) {
                if(Lease==null || Lease.OwnershipState==AuraOwnershipState.RecoveryPending) return;
                Lease=Lease with {OwnershipState=AuraOwnershipState.RecoveryPending}; Record(AuraWatchdogEvent.RecoveryRequired,cause.ToString());
            }
        }
        public void CompleteLogicalRecovery(bool success) {
            lock(_gate) {
                if(Lease?.OwnershipState!=AuraOwnershipState.RecoveryPending) throw new InvalidOperationException("NoRecoveryPending");
                Record(success?AuraWatchdogEvent.RecoverySucceeded:AuraWatchdogEvent.RecoveryFailed,"LogicalOnlyVendorRestorationNotAttempted");
                if(success) Lease=null;
            }
        }
        public void ReleaseLogicalLease() {
            lock(_gate) {
                if(Lease==null) return;
                Lease=Lease with {OwnershipState=AuraOwnershipState.ReleasePending}; Record(AuraWatchdogEvent.ReleaseStarted,"LogicalOnly");
                Record(AuraWatchdogEvent.Released,"NoVendorReleaseControl"); Lease=null;
            }
        }
        private void Record(AuraWatchdogEvent action,string reason) {
            if(_events.Count==128) _events.RemoveAt(0);
            _events.Add(new(_clock.GetUtcNow(),action,Lease!.LeaseId,reason));
        }
    }
    // Software-only crash journal. It cannot authorize vendor acquisition/release; imported data remains untrusted.
    public static class AuraRecoveryJournal
    {
        public static void Save(string path,AuraRecoveryMetadata record) {
            if(!record.LogicalOnly || record.HardwareAcquired) throw new InvalidOperationException("AURA_WRITE_APPROVAL_REQUIRED");
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
            var temporary=path+"."+Guid.NewGuid().ToString("N")+".tmp";
            try { File.WriteAllText(temporary,JsonSerializer.Serialize(record,WorkerProtocol.Json)); File.Move(temporary,path,true); }
            finally { if(File.Exists(temporary)) File.Delete(temporary); }
        }
        public static AuraRecoveryMetadata? ReadLogical(string path,DateTimeOffset bootEpoch) {
            try {
                using var file=File.OpenRead(path); if(file.Length>4096) return null;
                var record=JsonSerializer.Deserialize<AuraRecoveryMetadata>(file,WorkerProtocol.Json);
                return record is {SchemaVersion:1,LogicalOnly:true,HardwareAcquired:false} && record.BootEpoch==bootEpoch ? record : null;
            } catch(Exception e) when(e is IOException or JsonException or UnauthorizedAccessException) { return null; }
        }
    }
    public sealed class AuraRecoveryCoordinator(AuraOwnershipCoordinator ownership)
    {
        public void WorkerFailed(PlatformError error) => ownership.Recover(error==PlatformError.VendorTimeout ? AuraRecoveryCause.VendorTimeout : AuraRecoveryCause.WorkerExited);
        public void GracefulExit() => ownership.ReleaseLogicalLease();
        public bool RestoreVendorHardwareEnabled => false;
    }
}
