using AceHFX.AsusPlatform;

namespace AceHFX.Service;

// Data only. Future hardware ownership must attach to this authenticated connection.
// M1 never creates a hardware lease or calls any vendor release/acquire method.
public sealed class SessionLifecycle(SecurityContext owner) : IDisposable
{
    public SecurityContext Owner { get; } = owner;
    public Guid ConnectionId { get; } = Guid.NewGuid();
    public DeviceControlLease? Lease { get; private set; }
    public bool Closed { get; private set; }
    internal void AttachFutureLease(DeviceControlLease lease)
    {
        if (Closed || lease.OwnerPid != Owner.ProcessId || lease.OwnerSid != Owner.Sid) throw new InvalidOperationException("LeaseOwnerMismatch");
        Lease = lease;
    }
    public void Dispose() { Closed = true; Lease = null; }
}
