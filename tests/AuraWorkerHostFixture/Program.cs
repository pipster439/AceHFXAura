using System.Text.Json;
using AceHFX.AsusPlatform.Aura;
using AceHFX.AsusPlatform.Ipc;

// Software-only parent-crash fixture: no vendor activation, no hardware ownership.
if(args is not [var fixturePath,var evidencePath,var journalPath]) return 2;
await using var worker=new WorkerProcess(fixturePath,"hang");
var request=Guid.NewGuid();
await WorkerProtocol.WriteAsync(worker.Input,new(1,request,"discover"),CancellationToken.None);
await WorkerProtocol.ReadAsync(worker.Output,CancellationToken.None);
var ownership=new AuraOwnershipCoordinator();
var lease=ownership.BeginLogicalLease(NativeSecurity.Current(),worker.Pid,Guid.NewGuid(),TimeSpan.FromSeconds(30));
AuraRecoveryJournal.Save(journalPath,new(1,DateTimeOffset.UnixEpoch,lease,true,false));
File.WriteAllText(evidencePath,JsonSerializer.Serialize(new{hostPid=Environment.ProcessId,workerPid=worker.Pid}));
await Task.Delay(Timeout.Infinite);
return 0;
