using System.Text.Json;
using Aura_WinUI.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Aura_WinUI.Pages;

public sealed partial class StudioPage
{
    private CancellationTokenSource? _aiCancel;
    private TaskCompletionSource<string>? _aiContext;
    private string? _aiRequest;
    private bool _aiProposalReady;
    private readonly StudioAssistantRepairBudget _repairBudget = new();
    private void AssistantToggle_Click(object sender, RoutedEventArgs args) {
        AiPanel.Visibility = AiPanel.Visibility == Visibility.Visible ? Visibility.Collapsed : Visibility.Visible; AdaptAssistant();
    }
    private void Shell_SizeChanged(object sender, SizeChangedEventArgs args) => AdaptAssistant();
    private void AdaptAssistant() {
        if (AiPanel == null || AssistantColumn == null) return;
        var wide = ShellRoot.ActualWidth >= 1050 && AiPanel.Visibility == Visibility.Visible;
        AssistantColumn.Width = new GridLength(wide ? 330 : 0);
        Grid.SetRow(AiPanel, 2); Grid.SetColumn(AiPanel, wide ? 1 : 0);
        AiPanel.HorizontalAlignment = wide ? HorizontalAlignment.Stretch : HorizontalAlignment.Right;
        AiPanel.Width = wide ? double.NaN : Math.Max(200, Math.Min(360, ShellRoot.ActualWidth - 24));
        AiPanel.MaxHeight = double.PositiveInfinity;
        // Narrow assistant is a focused native surface. Retain the editor without
        // compositing native controls over WebView2's surface.
        if (_initialized && LoadingPanel.Visibility == Visibility.Collapsed)
            StudioWebView.Visibility = AiPanel.Visibility == Visibility.Visible && !wide ? Visibility.Collapsed : Visibility.Visible;
    }
    private void AssistantExample_Click(object sender, RoutedEventArgs args) { if (sender is Button { Tag: string prompt }) { AiPrompt.Text = prompt; AiIntent.SelectedIndex = prompt.Contains("做一个按键") ? 0 : 1; } }
    private void AssistantError_Click(object sender, RoutedEventArgs args) => OpenErrorAssistant();
    private void OpenErrorAssistant() { AiPanel.Visibility = Visibility.Visible; AdaptAssistant(); AiIntent.SelectedIndex = 3; AiPrompt.Text = "解释此错误，并提出可验证的修复建议"; }
    private void AssistantCancel_Click(object sender, RoutedEventArgs args) => _aiCancel?.Cancel();
    private async void AssistantSend_Click(object sender, RoutedEventArgs args) => await RequestAssistantAsync(false);
    private async void AssistantRepair_Click(object sender, RoutedEventArgs args) => await RequestAssistantAsync(true);
    private async Task RequestAssistantAsync(bool repair)
    {
        if (_aiCancel != null || !_initialized || _shell.WorkType != "effect" || _shell.Busy) return;
        if (string.IsNullOrWhiteSpace(AiPrompt.Text)) { AiStatus.Text = "请描述目标。"; return; }
        try { if (repair) _repairBudget.TakeRepair(); else _repairBudget.StartRequest(); }
        catch (StudioLlmException ex) { AiStatus.Text = ex.Message; return; }
        AiRepair.IsEnabled = false;
        var intent = new[] { "generate", "modify", "explain", "error_analysis" }[Math.Clamp(AiIntent.SelectedIndex, 0, 3)];
        _aiRequest = Guid.NewGuid().ToString("N"); _aiProposalReady = false; AiApply.IsEnabled = AiReject.IsEnabled = false; AiUndo.IsEnabled = false; AiPreviewSwatches.Children.Clear(); AiProposal.Text = "";
        _aiCancel = new(); _aiContext = new(TaskCreationOptions.RunContinuationsAsynchronously);
        AiSend.IsEnabled = false; AiCancel.IsEnabled = true; AiStatus.Text = "正在读取最小工程上下文…";
        try {
            var settings = LlmSettings.Settings; settings.Endpoint(); var key = LlmSettings.Key();
            PostAssistant(new { type = "studio_ai_context", request_id = _aiRequest, intent, repair });
            var context = await _aiContext.Task.WaitAsync(TimeSpan.FromSeconds(5), _aiCancel.Token);
            context = StudioAssistantContracts.Context(context, AiPrompt.Text.Trim(), key);
            AiStatus.Text = "正在请求建议…";
            var reply = await new StudioLlmProvider(_llmHttp).CompleteAsync(settings, key, StudioAssistantContracts.SystemPrompt, context, _aiCancel.Token, StudioAssistantContracts.Schema);
            reply = StudioAssistantContracts.ValidateReply(reply);
            PostAssistant(new { type = "studio_ai_reply", request_id = _aiRequest, reply });
            AiStatus.Text = "正在验证建议与本地模拟…";
        }
        catch (OperationCanceledException) { AiStatus.Text = "请求已取消。"; _aiRequest = null; }
        catch (TimeoutException) { AiStatus.Text = "编辑器上下文读取超时。"; _aiRequest = null; }
        catch (StudioLlmException ex) { AiStatus.Text = ex.Message; _aiRequest = null; }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { AiStatus.Text = "AI 服务设置不可用。"; _aiRequest = null; }
        finally { _aiContext = null; _aiCancel.Dispose(); _aiCancel = null; AiSend.IsEnabled = true; AiCancel.IsEnabled = false; AiRepair.IsEnabled = _repairBudget.Count < 2 && _aiRequest != null; AiStatus.UpdateLayout(); AiStatus.StartBringIntoView(new BringIntoViewOptions { AnimationDesired = false }); }
    }
    private void AssistantApply_Click(object sender, RoutedEventArgs args) {
        if (_aiProposalReady && _aiCancel == null) PostAssistant(new { type = "studio_ai_apply", request_id = _aiRequest });
    }
    private void AssistantReject_Click(object sender, RoutedEventArgs args) => PostAssistant(new { type = "studio_ai_reject", request_id = _aiRequest });
    private void AssistantUndo_Click(object sender, RoutedEventArgs args) => PostAssistant(new { type = "studio_ai_undo", request_id = _aiRequest });
    private bool ReceiveAssistantMessage(string json)
    {
        if (json.Length > 65536) return false;
        try {
            using var doc = JsonDocument.Parse(json); var m = doc.RootElement; var type = m.GetProperty("type").GetString();
            if (type == "studio_ai_ask" && m.GetProperty("kind").GetString() is "validation" or "build" or "plugin_load") { OpenErrorAssistant(); return true; }
            if (type is not ("studio_ai_context" or "studio_ai_context_error" or "studio_ai_preview" or "studio_ai_applied")) return false;
            if (_aiRequest == null || m.GetProperty("request_id").GetString() != _aiRequest) return true;
            if (type == "studio_ai_context") _aiContext?.TrySetResult(m.GetProperty("context").GetRawText());
            else if (type == "studio_ai_context_error") _aiContext?.TrySetException(new StudioLlmException("context", "此工程超出 AI MVP 支持范围。"));
            else if (type == "studio_ai_applied") {
                AiStatus.Text = m.GetProperty("text").GetString(); AiApply.IsEnabled = AiReject.IsEnabled = false;
                AiUndo.IsEnabled = m.GetProperty("undo").GetBoolean(); _aiProposalReady = false;
            }
            else {
                var text = m.GetProperty("text").GetString() ?? "";
                if (text.Length > 16384) return true;
                _aiProposalReady = m.GetProperty("valid").GetBoolean(); AiProposal.Text = text;
                AiApply.IsEnabled = AiReject.IsEnabled = _aiProposalReady;
                AiPreviewSwatches.Children.Clear();
                if (_aiProposalReady && m.TryGetProperty("preview", out var frame) && frame.GetArrayLength() == 68) {
                    foreach (var rgb in frame.EnumerateArray().Take(6)) {
                        if (rgb.GetArrayLength() != 3) break;
                        var values = rgb.EnumerateArray().Select(v => v.GetDouble()).ToArray();
                        if (values.Any(v => !double.IsFinite(v) || v < 0 || v > 255)) break;
                        AiPreviewSwatches.Children.Add(new Border { Width = 24, Height = 24, CornerRadius = new CornerRadius(4),
                            Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(Windows.UI.Color.FromArgb(255, (byte)values[0], (byte)values[1], (byte)values[2])) });
                    }
                }
                AiStatus.Text = _aiProposalReady ? "建议已验证，尚未应用。" : "解释或建议验证结果";
                AiProposal.UpdateLayout(); (_aiProposalReady ? (FrameworkElement)AiApply : AiProposal).StartBringIntoView(new BringIntoViewOptions { AnimationDesired = false });
            }
            return true;
        } catch (Exception ex) when (ex is JsonException or KeyNotFoundException or InvalidOperationException) { return false; }
    }
}
