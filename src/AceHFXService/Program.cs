using System.Text.Json;
using System.ServiceProcess;
using AceHFX.AsusPlatform.Ipc;
using AceHFX.AsusPlatform.Runtime;
using AceHFX.Service;

if (args.SequenceEqual(new[] { "--discover" }))
{
    Console.WriteLine(JsonSerializer.Serialize(new AsusCapabilityDetector(new AsusRuntimeDetector()).Discover(), Protocol.Json));
    return;
}
if (args.SequenceEqual(new[] { "--client-status" }))
{
    var response = await new AceHfxServiceClient().SendAsync(Command.GetAsusRuntimeCapabilities);
    Console.WriteLine(JsonSerializer.Serialize(response, Protocol.Json));
    Environment.ExitCode = response.Success ? 0 : 1;
    return;
}
if (args.SequenceEqual(new[] { "--client-cooling" }))
{
    var response = await new AceHfxServiceClient().SendAsync(Command.GetCoolingReadOnlySnapshot);
    Console.WriteLine(JsonSerializer.Serialize(response, Protocol.Json));
    Environment.ExitCode = response.Success && response.Cooling?.Error == AceHFX.AsusPlatform.PlatformError.None ? 0 : 1;
    return;
}
if (args.SequenceEqual(new[] { "--console" }))
{
    // Console testing cannot impersonate the production broker or accept production clients.
    var name = $"{Protocol.PipeName}.Dev.{Environment.ProcessId}";
    var log = new StructuredLog(true);
    using var stop = new CancellationTokenSource();
    Console.CancelKeyPress += (_, e) => { e.Cancel = true; stop.Cancel(); };
    await ServiceHost.CreateServer(name, log).RunAsync(stop.Token);
    return;
}
if (args.Length != 0) throw new ArgumentException("UnknownServiceArgument");
ServiceBase.Run(new ServiceHost());
