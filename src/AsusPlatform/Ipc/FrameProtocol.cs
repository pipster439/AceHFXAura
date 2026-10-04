using System.Buffers.Binary;
using System.Text.Json;

namespace AceHFX.AsusPlatform.Ipc;

public sealed class ProtocolException(PlatformError error, string diagnostic) : IOException(diagnostic)
{
    public PlatformError Error { get; } = error;
}
public static class FrameProtocol
{
    public static async Task<T> ReadAsync<T>(Stream stream, CancellationToken token)
    {
        var header = new byte[4];
        await ReadExactAsync(stream, header, token);
        var length = BinaryPrimitives.ReadInt32LittleEndian(header);
        if (length <= 0) throw new ProtocolException(PlatformError.MalformedRequest, "InvalidFrameLength");
        if (length > Protocol.MaximumMessageBytes) throw new ProtocolException(PlatformError.RequestTooLarge, "MaximumMessageBytesExceeded");
        var payload = new byte[length];
        await ReadExactAsync(stream, payload, token);
        try
        {
            // Duplicate properties can produce inconsistent interpretations between peers.
            using var document = JsonDocument.Parse(payload, new JsonDocumentOptions { MaxDepth = 16 });
            if (document.RootElement.ValueKind != JsonValueKind.Object)
                throw new JsonException();
            var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var property in document.RootElement.EnumerateObject())
                if (!names.Add(property.Name)) throw new JsonException();
            return JsonSerializer.Deserialize<T>(payload, Protocol.Json) ?? throw new JsonException();
        }
        catch (JsonException) { throw new ProtocolException(PlatformError.MalformedRequest, "InvalidJsonPayload"); }
    }
    public static async Task WriteAsync<T>(Stream stream, T message, CancellationToken token)
    {
        var payload = JsonSerializer.SerializeToUtf8Bytes(message, Protocol.Json);
        if (payload.Length > Protocol.MaximumMessageBytes)
            throw new ProtocolException(PlatformError.RequestTooLarge, "MaximumMessageBytesExceeded");
        var header = new byte[4];
        BinaryPrimitives.WriteInt32LittleEndian(header, payload.Length);
        await stream.WriteAsync(header, token);
        await stream.WriteAsync(payload, token);
        await stream.FlushAsync(token);
    }
    private static async Task ReadExactAsync(Stream stream, Memory<byte> buffer, CancellationToken token)
    {
        while (!buffer.IsEmpty)
        {
            var read = await stream.ReadAsync(buffer, token);
            if (read == 0) throw new ProtocolException(PlatformError.TruncatedFrame, "DisconnectedDuringFrame");
            buffer = buffer[read..];
        }
    }
}
