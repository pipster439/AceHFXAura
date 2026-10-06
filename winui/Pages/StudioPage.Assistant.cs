using System.Text.Json;
using Aura_WinUI.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.System;
using Windows.UI.Core;

namespace Aura_WinUI.Pages;

public sealed partial class StudioPage
{
    private CancellationTokenSource? _aiCancel;
    private string? _aiRequest;
    private bool _aiProposalReady;
    private readonly Dictionary<string, StudioConversationSession> _conversations = new();
    private string _conversationProject = "";
    private StudioConversationSession? _conversation;
    private readonly Dictionary<string, TaskCompletionSource<JsonElement>> _chatCalls = new();
    private JsonElement? _chatCandidate;
    private string _candidateFingerprint = "";
    private bool _followChat = true;
    private bool _updatingChatScroll;
    private int _conversationEpoch;
    private void AssistantToggle_Click(object sender, RoutedEventArgs args) {
        AiPanel.Visibility = AiPanel.Visibility == Visibility.Visible ? Visibility.Collapsed : Visibility.Visible; AdaptAssistant();
    }
    private void Shell_SizeChanged(object sender, SizeChangedEventArgs args) => AdaptAssistant();
    private void AdaptAssistant() {
        if (AiPanel == null || AssistantColumn == null) return;
        var wide = ShellRoot.ActualWidth >= 1050 && AiPanel.Visibility == Visibility.Visible;
        AssistantColumn.Width = new GridLength(wide ? 370 : 0);
        Grid.SetRow(AiPanel, 2); Grid.SetColumn(AiPanel, wide ? 1 : 0);
        AiPanel.HorizontalAlignment = wide ? HorizontalAlignment.Stretch : HorizontalAlignment.Right;
        AiPanel.Width = wide ? double.NaN : Math.Max(200, Math.Min(420, ShellRoot.ActualWidth - 24));
        AiPanel.MaxHeight = double.PositiveInfinity;
        if (_initialized && LoadingPanel.Visibility == Visibility.Collapsed)
            StudioWebView.Visibility = AiPanel.Visibility == Visibility.Visible && !wide ? Visibility.Collapsed : Visibility.Visible;
    }
    private void AssistantExample_Click(object sender, RoutedEventArgs args) { if (sender is Button { Tag: string prompt }) { AiPrompt.Text = prompt; AiPrompt.Focus(FocusState.Programmatic); } }
    private void AssistantError_Click(object sender, RoutedEventArgs args) => OpenErrorAssistant();
    private void OpenErrorAssistant() { AiPanel.Visibility = Visibility.Visible; AdaptAssistant(); AiPrompt.Text = "为什么当前工程失败了？能帮我修一下吗？"; }
    private void AssistantCancel_Click(object sender, RoutedEventArgs args) => _aiCancel?.Cancel();
    private async void AssistantSend_Click(object sender, RoutedEventArgs args) => await RequestAssistantAsync(false);
    private async void AssistantRetry_Click(object sender, RoutedEventArgs args) => await RequestAssistantAsync(true);
    private async void AssistantPrompt_KeyDown(object sender, KeyRoutedEventArgs args) {
        if (args.Key != VirtualKey.Enter || Microsoft.UI.Input.InputKeyboardSource.GetKeyStateForCurrentThread(VirtualKey.Shift).HasFlag(CoreVirtualKeyStates.Down)) return;
        args.Handled = true; await RequestAssistantAsync(false);
    }
    private void AssistantScroll_Changed(object sender, ScrollViewerViewChangedEventArgs args) {
        if (!_updatingChatScroll && !args.IsIntermediate) _followChat = AiHistoryScroll.ScrollableHeight - AiHistoryScroll.VerticalOffset < 32;
    }
    private void ScrollChat() {
        if (!_followChat) return;
        DispatcherQueue.TryEnqueue(() => { if (!_closed && _followChat) { AiHistoryScroll.UpdateLayout(); AiHistoryScroll.ChangeView(null, AiHistoryScroll.ScrollableHeight, null, true); } });
    }
    private void RenderConversation() {
        _updatingChatScroll = true; AiMessages.Children.Clear();
        if (_conversation != null) foreach (var message in _conversation.Messages) {
            var text = new TextBlock { Text = message.Text, TextWrapping = TextWrapping.Wrap, IsTextSelectionEnabled = true };
            var bubble = new Border { Child = new StackPanel { Spacing = 4, Children = { new TextBlock { Text = message.Role == "user" ? "你" : message.Role == "assistant" ? "助手" : "草稿状态", FontWeight = Microsoft.UI.Text.FontWeights.SemiBold }, text } },
                Padding = new Thickness(10), CornerRadius = new CornerRadius(8), HorizontalAlignment = message.Role == "user" ? HorizontalAlignment.Right : HorizontalAlignment.Stretch,
                Style = (Style)Resources[message.Role == "user" ? "ChatUserBubble" : "ChatAssistantBubble"] };
            if (message.Role == "user") { text.Style = (Style)Resources["ChatUserText"]; ((TextBlock)((StackPanel)bubble.Child).Children[0]).Style = (Style)Resources["ChatUserText"]; }
            AiMessages.Children.Add(bubble);
        }
        AiEmptyPrompts.Visibility = _conversation?.Messages.Count > 0 ? Visibility.Collapsed : Visibility.Visible;
        ScrollChat(); DispatcherQueue.TryEnqueue(() => _updatingChatScroll = false);
    }
    private void AddChat(string role, string text) { _conversation?.Add(role, text); RenderConversation(); }
    private void ObserveConversation(string name, string fingerprint) {
        if (_conversationProject != name) {
            _conversationEpoch++; _aiCancel?.Cancel(); _conversationProject = name;
            if (!_conversations.TryGetValue(name, out _conversation)) {
                if (_conversations.Count >= 20) _conversations.Remove(_conversations.Keys.First());
                _conversations[name] = _conversation = new();
            }
            _aiRequest = null; _chatCandidate = null; _aiProposalReady = false;
            AiProposalCard.Visibility = Visibility.Collapsed; AiApply.IsEnabled = AiReject.IsEnabled = AiUndo.IsEnabled = false;
            AiRetry.IsEnabled = _conversation.RetryPrompt != null; _followChat = true; RenderConversation();
        }
        if (_conversation!.Observe(fingerprint)) {
            if (_aiProposalReady) { _aiProposalReady = false; AiApply.IsEnabled = false; AiProposalTitle.Text = "建议已过期"; AiProposal.Text += "\n已过期：当前草稿指纹已变化。"; }
            AddChat("notice", "项目已发生变化，后续建议基于最新草稿。");
        }
    }
    private async Task<JsonElement> EditorChatAsync(object message, string call, CancellationToken token) {
        var completion = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously); _chatCalls[call] = completion;
        try { PostAssistant(message); return await completion.Task.WaitAsync(TimeSpan.FromSeconds(8), token); }
        finally { _chatCalls.Remove(call); }
    }
    private async Task RequestAssistantAsync(bool retry) {
        if (_aiCancel != null || !_initialized || _shell.WorkType != "effect" || _shell.Busy) return;
        var prompt = retry ? _conversation?.RetryPrompt : AiPrompt.Text.Trim();
        if (string.IsNullOrWhiteSpace(prompt)) { AiStatus.Text = "请输入消息。"; return; }
        var request = Guid.NewGuid().ToString("N"); _aiRequest = request; _aiCancel = new(); var token = _aiCancel.Token;
        _aiProposalReady = false; AiApply.IsEnabled = AiReject.IsEnabled = false; AiProposalCard.Visibility = Visibility.Collapsed;
        AiSend.IsEnabled = AiRetry.IsEnabled = false; AiCancel.IsEnabled = true; AiStatus.Text = "正在读取当前草稿…"; _chatCandidate = null;
        StudioConversationSession? session = null; var epoch = _conversationEpoch;
        try {
            var settings = LlmSettings.Settings; settings.Endpoint(); var key = LlmSettings.Key();
            var context = await EditorChatAsync(new { type = "studio_chat_begin", request_id = request }, "begin", token);
            StudioConversationContracts.Exact(context, "intent", "name", "publication", "nodes", "presets", "diagnostic", "diagnostic_kind", "capabilities", "fingerprint", "active_proposal");
            var fingerprint = context.GetProperty("fingerprint").GetString()!;
            if (!System.Text.RegularExpressions.Regex.IsMatch(fingerprint, "^[a-f0-9]{64}$")) throw new StudioLlmException("context", "工程指纹无效。");
            var legacy = JsonSerializer.Serialize(new { intent = "modify", name = context.GetProperty("name"), publication = context.GetProperty("publication"), nodes = context.GetProperty("nodes"), presets = Array.Empty<object>(), diagnostic = "", diagnostic_kind = "none", capabilities = context.GetProperty("capabilities") });
            using var safe = JsonDocument.Parse(StudioAssistantContracts.Context(legacy, prompt, key));
            var currentJson = StudioLlmRedaction.Filter(JsonSerializer.Serialize(new { studio = safe.RootElement.GetProperty("studio"), fingerprint, active_proposal = context.GetProperty("active_proposal") }), key);
            using var current = JsonDocument.Parse(currentJson);
            ObserveConversation(_shell.Name, fingerprint); epoch = _conversationEpoch; _aiRequest = request; session = _conversation!; session.RetryPrompt = StudioLlmRedaction.Filter(prompt, key);
            session.Add("user", StudioLlmRedaction.Filter(prompt, key)); AiPrompt.Text = ""; _followChat = true; RenderConversation();
            AiStatus.Text = "助手正在思考…";
            var result = await new StudioConversationOrchestrator(new(_llmHttp)).TurnAsync(settings, key, session, current.RootElement,
                async (tool, cancellation) => {
                    var call = Guid.NewGuid().ToString("N");
                    var value = await EditorChatAsync(new { type = "studio_chat_tool", request_id = request, call_id = call, name = tool.Name, arguments = tool.Arguments }, call, cancellation);
                    var filtered = StudioLlmRedaction.Filter(value.GetRawText(), key);
                    using var parsed = JsonDocument.Parse(filtered); var clean = parsed.RootElement.Clone();
                    if (tool.Name == "propose_effect_change") _chatCandidate = clean;
                    return clean;
                }, status => AiStatus.Text = status, token);
            token.ThrowIfCancellationRequested();
            if (request != _aiRequest || session != _conversation || session.Fingerprint != fingerprint) throw new StudioLlmException("stale", "项目在请求期间发生变化；建议已作废，请基于最新草稿继续。");
            AddChat("assistant", result.Message); if (!result.ActionsRejected) session.RetryPrompt = null;
            if (result.ActionsRejected) {
                _chatCandidate = null; PostAssistant(new { type = "studio_chat_cancel", request_id = request });
                AiStatus.Text = "未生成可执行修改建议；回复中的动作已拒绝，草稿未改变。";
            } else {
                if (_chatCandidate.HasValue) ShowConversationProposal(_chatCandidate.Value);
                AiStatus.Text = _chatCandidate.HasValue ? "本轮完成；所有修改仍需你确认。" : "本轮完成 · 仅文字回复，未生成修改建议。";
            }
        } catch (OperationCanceledException) { AiStatus.Text = "本轮已取消；未应用任何修改。"; }
        catch (TimeoutException) { AiStatus.Text = "编辑器响应超时；未自动重试。"; }
        catch (StudioLlmException ex) { AiStatus.Text = ex.Message; }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException or InvalidOperationException or KeyNotFoundException) { AiStatus.Text = "助手或工程上下文不可用；未执行修改。"; }
        finally {
            if (session?.RetryPrompt != null || token.IsCancellationRequested) { PostAssistant(new { type = "studio_chat_cancel", request_id = request }); _aiProposalReady = false; AiApply.IsEnabled = false; if (session == _conversation && epoch == _conversationEpoch) AddChat("notice", AiStatus.Text); }
            _chatCalls.Clear(); _aiCancel.Dispose(); _aiCancel = null; AiSend.IsEnabled = true; AiCancel.IsEnabled = false; AiRetry.IsEnabled = _conversation?.RetryPrompt != null;
        }
    }
    private void ShowConversationProposal(JsonElement candidate) {
        _candidateFingerprint = candidate.GetProperty("fingerprint").GetString()!;
        _aiProposalReady = candidate.GetProperty("valid").GetBoolean() && _conversation?.Fingerprint == _candidateFingerprint;
        var diff = string.Join("\n", candidate.GetProperty("changes").EnumerateArray().Select(c => c.GetProperty("field").GetString() + ": " + c.GetProperty("before").GetString() + " → " + c.GetProperty("after").GetString()));
        AiProposal.Text = candidate.GetProperty("summary").GetString() + "\n" + diff + "\n" + (_aiProposalReady ? "验证通过 · 本地模拟通过 · 尚未应用" : "未通过验证：" + candidate.GetProperty("diagnostic").GetString()) + "\n基准草稿：" + _candidateFingerprint[..12] + "…";
        ToolTipService.SetToolTip(AiProposalCard, "完整基准指纹：" + _candidateFingerprint);
        AiProposalTitle.Text = "建议修改 · 尚未应用";
        AiProposalCard.Visibility = Visibility.Visible; AiApply.IsEnabled = _aiProposalReady; AiReject.IsEnabled = true;
        AiPreviewSwatches.Children.Clear(); AiPreviewSwatches.Visibility = Visibility.Collapsed;
        if (candidate.TryGetProperty("preview", out var preview) && preview.GetArrayLength() == 68) foreach (var rgb in preview.EnumerateArray().Take(6)) {
            var values = rgb.EnumerateArray().Select(v => v.GetDouble()).ToArray(); if (values.Length != 3 || values.Any(v => !double.IsFinite(v) || v < 0 || v > 255)) break;
            AiPreviewSwatches.Children.Add(new Border { Width = 24, Height = 24, CornerRadius = new CornerRadius(4), Background = new SolidColorBrush(Windows.UI.Color.FromArgb(255, (byte)values[0], (byte)values[1], (byte)values[2])) });
        }
        ScrollChat();
    }
    private void AssistantPreview_Click(object sender, RoutedEventArgs args) { AiPreviewSwatches.Visibility = Visibility.Visible; ScrollChat(); }
    private void AssistantBench_Click(object sender, RoutedEventArgs args) { AiPanel.Visibility = Visibility.Collapsed; AdaptAssistant(); PostAssistant(new { type = "studio_command", command = "bench" }); }
    private void AssistantClear_Click(object sender, RoutedEventArgs args) {
        _conversationEpoch++; _aiCancel?.Cancel(); PostAssistant(new { type = "studio_chat_clear" }); _conversation?.Clear(); _aiRequest = null;
        _aiProposalReady = false; AiApply.IsEnabled = AiReject.IsEnabled = AiRetry.IsEnabled = false; AiProposalCard.Visibility = Visibility.Collapsed; AiStatus.Text = "会话已清空；草稿未改变。"; RenderConversation();
    }
    private void AssistantApply_Click(object sender, RoutedEventArgs args) {
        if (_aiProposalReady && _aiCancel == null && _conversation?.Fingerprint == _candidateFingerprint) PostAssistant(new { type = "studio_ai_apply", request_id = _aiRequest });
    }
    private void AssistantReject_Click(object sender, RoutedEventArgs args) => PostAssistant(new { type = "studio_ai_reject", request_id = _aiRequest });
    private void AssistantUndo_Click(object sender, RoutedEventArgs args) => PostAssistant(new { type = "studio_ai_undo", request_id = _aiRequest });
    private bool ReceiveAssistantMessage(string json) {
        if (json.Length > 65536) return false;
        try {
            using var doc = JsonDocument.Parse(json); var m = doc.RootElement; var type = m.GetProperty("type").GetString();
            if (type == "studio_ai_catalog") return true;
            if (type == "studio_chat_revision") {
                StudioConversationContracts.Exact(m, "type", "name", "fingerprint"); var name = m.GetProperty("name").GetString()!; var fingerprint = m.GetProperty("fingerprint").GetString()!;
                if (name.Length > 128 || !System.Text.RegularExpressions.Regex.IsMatch(fingerprint, "^[a-f0-9]{64}$")) return true;
                ObserveConversation(name, fingerprint); return true;
            }
            if (type == "studio_ai_ask") { OpenErrorAssistant(); return true; }
            if (type is not ("studio_chat_result" or "studio_ai_applied")) return false;
            if (m.GetProperty("request_id").GetString() != _aiRequest) return true;
            if (type == "studio_chat_result") {
                if (_chatCalls.TryGetValue(m.GetProperty("call_id").GetString()!, out var completion)) {
                    if (m.TryGetProperty("error", out _)) completion.TrySetException(new StudioLlmException("tool", "工程已变化、请求已取消或工具被拒绝；请基于最新草稿继续。"));
                    else completion.TrySetResult(m.GetProperty("result").Clone());
                }
            } else {
                AiStatus.Text = StudioLlmRedaction.Filter(m.GetProperty("text").GetString()!); AddChat("notice", AiStatus.Text);
                AiProposalTitle.Text = AiStatus.Text.Contains("已应用") ? "已应用至草稿" : AiStatus.Text.Contains("撤销") ? "修改已撤销" : AiStatus.Text.Contains("放弃") ? "建议已放弃" : "建议状态";
                if (AiStatus.Text.Contains("已应用")) AiProposal.Text = AiProposal.Text.Replace("尚未应用", "已应用至草稿");
                AiApply.IsEnabled = AiReject.IsEnabled = false; _aiProposalReady = false; AiUndo.IsEnabled = m.GetProperty("undo").GetBoolean();
            }
            return true;
        } catch (Exception ex) when (ex is JsonException or KeyNotFoundException or InvalidOperationException or ArgumentException) { return false; }
    }
}
