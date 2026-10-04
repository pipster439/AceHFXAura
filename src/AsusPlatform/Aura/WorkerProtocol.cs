using System.Buffers.Binary;
using System.Text.Json;

namespace AceHFX.AsusPlatform.Aura;

internal sealed record WorkerRequest(int ProtocolVersion, Guid RequestId, string Command);
internal sealed record WorkerMessage(int ProtocolVersion, Guid RequestId, int Sequence, string Kind,
    string Stage, int WorkerPid, JsonElement? Snapshot);
internal static class WorkerProtocol
{
    internal const int MaximumFrame = 2 * 1024 * 1024;
    internal static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web) { MaxDepth = 24 };
    internal static async Task WriteAsync(Stream stream, WorkerRequest request, CancellationToken token)
    {
        var data = JsonSerializer.SerializeToUtf8Bytes(request, Json);
        var header = new byte[4]; BinaryPrimitives.WriteInt32LittleEndian(header,data.Length);
        await stream.WriteAsync(header,token); await stream.WriteAsync(data,token); await stream.FlushAsync(token);
    }
    internal static async Task<WorkerMessage> ReadAsync(Stream stream, CancellationToken token)
    {
        var header=new byte[4]; await stream.ReadExactlyAsync(header,token);
        var length=BinaryPrimitives.ReadInt32LittleEndian(header);
        if(length<=0 || length>MaximumFrame) throw new InvalidDataException("WorkerFrameSizeInvalid");
        var data=new byte[length]; await stream.ReadExactlyAsync(data,token);
        using var doc=JsonDocument.Parse(data,new JsonDocumentOptions{MaxDepth=24});
        ValidateProperties(doc.RootElement);
        return JsonSerializer.Deserialize<WorkerMessage>(data,Json) ?? throw new InvalidDataException("WorkerNullResponse");
    }
    private static void ValidateProperties(JsonElement element)
    {
        if(element.ValueKind==JsonValueKind.Object) {
            var names=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach(var property in element.EnumerateObject()) {
                if(!names.Add(property.Name)) throw new InvalidDataException("WorkerDuplicateProperty");
                ValidateProperties(property.Value);
            }
        } else if(element.ValueKind==JsonValueKind.Array)
            foreach(var child in element.EnumerateArray()) ValidateProperties(child);
    }
}
