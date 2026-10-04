using System.Text.Json;
using AceHFX.AsusPlatform;
using AceHFX.AsusPlatform.Aura;

if(args.Length!=0) { Console.Error.WriteLine("ReadOnlyDiagnosticsNoArguments"); return 2; }
using var stop=new CancellationTokenSource(TimeSpan.FromSeconds(100));
var backend=new AuraRuntimeBackend();
var result=await backend.DiscoverAsync(stop.Token);
Console.WriteLine(JsonSerializer.Serialize(result,new JsonSerializerOptions(JsonSerializerDefaults.Web){WriteIndented=true,MaxDepth=24}));
return result.Error==PlatformError.None ? 0 : 1;
