using System.Net;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Aura_WinUI.Services;

public sealed record StudioLlmSettings(string BaseUrl = "https://api.openai.com/v1/", string Model = "", int TimeoutSeconds = 30, bool StructuredOutput = false)
{
    public Uri Endpoint()
    {
        if (!Uri.TryCreate(BaseUrl, UriKind.Absolute, out var uri) ||
            !(uri.Scheme == "https" || (uri.Scheme == "http" && uri.IsLoopback)) ||
            !string.IsNullOrEmpty(uri.UserInfo) || !string.IsNullOrEmpty(uri.Query) || !string.IsNullOrEmpty(uri.Fragment) ||
            BaseUrl.Length > 2048 || Model.Length is < 1 or > 128 || Model.Any(char.IsControl) || TimeoutSeconds is < 1 or > 120)
            throw new StudioLlmException("invalid_settings", "请检查服务地址、模型和超时（1–120 秒）；远程服务必须使用 HTTPS。");
        return new Uri(BaseUrl.TrimEnd('/') + "/chat/completions");
    }
}
// Exceptions intentionally never retain provider bodies, prompts, headers, URLs or inner errors.
public sealed class StudioLlmException(string code, string message) : Exception(message)
{
    public string Code { get; } = code;
}
public static partial class StudioLlmRedaction
{
    [GeneratedRegex(@"(?i)(?:Bearer\s+\S+|sk-[a-z0-9_-]+|(?:api[_ -]?key|token|secret|password)\s*[:=]\s*[^\s,;]+)")]
    private static partial Regex Secrets();
    [GeneratedRegex(@"(?:[A-Za-z]:[\\/][^\r\n\s]+|\\\\[^\r\n\s]+|/(?:home|Users|tmp|var)/[^\r\n\s]+)")]
    private static partial Regex Paths();
    public static string Filter(string text, string? secret = null)
    {
        if (!string.IsNullOrEmpty(secret)) text = text.Replace(secret, "[已隐藏]", StringComparison.Ordinal);
        return Paths().Replace(Secrets().Replace(text, "[已隐藏]"), "[路径已隐藏]");
    }
}
public sealed class StudioLlmProvider(HttpClient http)
{
    public const int MaxRequestBytes = 65536, MaxResponseBytes = 131072, MaxContentBytes = 65536;
    public async Task<string> CompleteAsync(StudioLlmSettings settings, string key, string system, string context,
        CancellationToken cancellation = default, JsonElement? schema = null)
    {
        var endpoint = settings.Endpoint();
        if (key.Length is < 1 or > 4096 || key.Any(char.IsControl)) throw new StudioLlmException("credential", "请设置有效 API 密钥。");
        var body = new Dictionary<string, object> { ["model"] = settings.Model,
            ["messages"] = new[] { new { role = "system", content = system }, new { role = "user", content = StudioLlmRedaction.Filter(context, key) } },
            ["stream"] = false };
        if (settings.StructuredOutput && schema.HasValue)
            body["response_format"] = new { type = "json_schema", json_schema = new { name = "aura_studio_proposal", strict = true, schema = schema.Value } };
        var bytes = JsonSerializer.SerializeToUtf8Bytes(body);
        if (bytes.Length > MaxRequestBytes) throw new StudioLlmException("request_size", "请求超过大小限制，请缩小工程或问题范围。");
        using var request = new HttpRequestMessage(HttpMethod.Post, endpoint);
        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", key);
        request.Content = new ByteArrayContent(bytes); request.Content.Headers.ContentType = new("application/json");
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellation);
        timeout.CancelAfter(TimeSpan.FromSeconds(settings.TimeoutSeconds));
        try
        {
            using var response = await http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, timeout.Token);
            if (!response.IsSuccessStatusCode)
                throw response.StatusCode switch {
                    HttpStatusCode.Unauthorized or HttpStatusCode.Forbidden => new StudioLlmException("auth", "认证失败，请检查密钥和权限。"),
                    HttpStatusCode.TooManyRequests => new StudioLlmException("rate_limit", "请求过于频繁（HTTP 429）；未自动重试，请稍后手动重试。"),
                    _ => new StudioLlmException("http", "服务请求失败（HTTP " + (int)response.StatusCode + "）。") };
            if (response.Content.Headers.ContentLength > MaxResponseBytes) throw TooLarge();
            await using var stream = await response.Content.ReadAsStreamAsync(timeout.Token);
            using var buffer = new MemoryStream(); var chunk = new byte[4096]; int read;
            while ((read = await stream.ReadAsync(chunk, timeout.Token)) > 0) {
                if (buffer.Length + read > MaxResponseBytes) throw TooLarge(); buffer.Write(chunk, 0, read);
            }
            using var json = JsonDocument.Parse(buffer.ToArray(), new JsonDocumentOptions { MaxDepth = 32 });
            var choices = json.RootElement.GetProperty("choices");
            if (choices.GetArrayLength() != 1 || choices[0].GetProperty("finish_reason").GetString() != "stop")
                throw new StudioLlmException("truncated", "响应未完整结束，请重试或缩小请求。");
            var message = choices[0].GetProperty("message");
            if (message.TryGetProperty("tool_calls", out _) || (message.TryGetProperty("refusal", out var refusal) && refusal.ValueKind != JsonValueKind.Null))
                throw new StudioLlmException("response", "服务未返回可用的工作室建议。");
            var content = message.GetProperty("content").GetString();
            if (string.IsNullOrWhiteSpace(content)) throw new StudioLlmException("response", "服务返回空响应。");
            if (Encoding.UTF8.GetByteCount(content) > MaxContentBytes) throw TooLarge();
            return StudioLlmRedaction.Filter(content, key);
        }
        catch (OperationCanceledException) when (cancellation.IsCancellationRequested) { throw; }
        catch (OperationCanceledException) { throw new StudioLlmException("timeout", "服务请求超时；未自动重试。"); }
        catch (HttpRequestException) { throw new StudioLlmException("network", "无法连接服务。"); }
        catch (Exception ex) when (ex is JsonException or KeyNotFoundException or InvalidOperationException or IOException) {
            throw new StudioLlmException("response", "响应格式无效或连接中断。");
        }
    }
    private static StudioLlmException TooLarge() => new("response_size", "响应超过大小限制。");
    public static HttpClient CreateHttpClient() => new(new HttpClientHandler { AllowAutoRedirect = false }) { Timeout = Timeout.InfiniteTimeSpan };
}
