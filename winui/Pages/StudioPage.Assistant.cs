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
    private bool _manualConfirming;
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
        if (_manualConfirming || _aiCancel != null || !_initialized || _shell.WorkType != "effect" || _shell.Busy) return;
        if (string.IsNullOrWhiteSpace(AiPrompt.Text)) { AiStatus.Text = "请描述目标。"; return; }
        if (AiManualMode.IsChecked == true) {
            if (repair) { AiStatus.Text = "人工验收模式禁止重试；如需再次请求，请明确发起新请求。"; return; }
            var confirm = new ContentDialog { XamlRoot = XamlRoot, Title = "确认请求真实 provider", Content = "将发送所选工程的最小上下文与提示词。可能产生费用；本次仅请求一次，不会自动重试或应用。", PrimaryButtonText = "发送一次", CloseButtonText = "取消", DefaultButton = ContentDialogButton.Close };
            _manualConfirming = true;
            try { if (await confirm.ShowAsync() != ContentDialogResult.Primary || _aiCancel != null || _shell.Busy || _closed) return; }
            finally { _manualConfirming = false; }
        }
        try { if (repair) _repairBudget.TakeRepair(); else _repairBudget.StartRequest(); }
        catch (StudioLlmException ex) { AiStatus.Text = ex.Message; return; }
        AiRepair.IsEnabled = false;
        var intent = new[] { "generate", "modify", "explain", "error_analysis" }[Math.Clamp(AiIntent.SelectedIndex, 0, 3)];
        _aiRequest = Guid.NewGuid().ToString("N"); _aiProposalReady = false; AiApply.IsEnabled = AiReject.IsEnabled = false; AiUndo.IsEnabled = false; AiPreviewSwatches.Children.Clear(); AiProposal.Text = "";
        _aiCancel = new(); _aiContext = new(TaskCreationOptions.RunContinuationsAsynchronously);
        AiSend.IsEnabled = false; AiCancel.IsEnabled = true; AiStatus.Text = "正在读取最小工程上下文…";
        try {
            var settings = LlmSettings.Settings; settings.Endpoint(); var key = LlmSettings.Key();
            PostAssistant(new { type = "studio_ai_context", request_id = _aiRequest, intent, repair, preset_id = (AiPreset.SelectedItem as ComboBoxItem)?.Tag as string });
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
        finally { _aiContext = null; _aiCancel.Dispose(); _aiCancel = null; AiSend.IsEnabled = true; AiCancel.IsEnabled = false; AiRepair.IsEnabled = AiManualMode.IsChecked != true && _repairBudget.Count < 2 && _aiRequest != null; AiStatus.UpdateLayout(); AiStatus.StartBringIntoView(new BringIntoViewOptions { AnimationDesired = false }); }
    }
    private void ManualMode_Click(object sender, RoutedEventArgs args) {
        if (AiManualChecklist == null || AiRepair == null || AiPrompt == null) return;
        AiManualChecklist.Visibility = AiManualMode.IsChecked == true ? Visibility.Visible : Visibility.Collapsed;
        if (AiManualMode.IsChecked == true) {
            AiRepair.IsEnabled = false; AiPrompt.Text = "把当前效果的速度提高一点，不改变颜色。";
            if (_initialized && !_closed) PostAssistant(new { type = "studio_command", command = "bench" });
        }
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
            if (type == "studio_ai_catalog") {
                StudioDraft.Exact(m, "type", "presets");
                var presets = m.GetProperty("presets"); if (presets.GetArrayLength() is < 1 or > 20) return true;
                var catalog = presets.EnumerateArray().Select(p => {
                    StudioDraft.Exact(p, "id", "name"); var id = p.GetProperty("id").GetString()!; var name = p.GetProperty("name").GetString()!;
                    if (!System.Text.RegularExpressions.Regex.IsMatch(id, "^[a-z_][a-z0-9_]{0,63}$") || name.Length > 128) throw new StudioPersistenceException("模板元数据无效");
                    return new ComboBoxItem { Content = name, Tag = id };
                }).ToArray();
                if (AiPreset.Items.Count == 0) { foreach (var item in catalog) AiPreset.Items.Add(item); AiPreset.SelectedIndex = 0; }
                return true;
            }
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
        } catch (Exception ex) when (ex is JsonException or KeyNotFoundException or InvalidOperationException or StudioPersistenceException or ArgumentException) { return false; }
    }
}
