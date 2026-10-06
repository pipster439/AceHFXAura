using Aura_WinUI.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Aura_WinUI.Pages;

public sealed partial class StudioPage
{
    private bool _settingsOpen;
    private StudioLlmSettingsStore? _llmSettings;
    private readonly HttpClient _llmHttp = StudioLlmProvider.CreateHttpClient();
    private StudioLlmSettingsStore LlmSettings
    {
        get {
            if (_llmSettings != null) return _llmSettings;
            var store = new StudioLlmSettingsStore(Path.Combine(RuntimeLayoutResolver.DataRoot, "studio-llm-settings.json"), new StudioCredentialStore());
            store.Load(); return _llmSettings = store;
        }
    }
    private async void LlmSettings_Click(object sender, RoutedEventArgs args)
    {
        if (_settingsOpen) return;
        _settingsOpen = true;
        try { await ShowLlmSettingsAsync(); }
        catch (StudioLlmException ex) { ValidationSummary.Text = ex.Message; }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { ValidationSummary.Text = "无法访问 AI 服务设置。"; }
        finally { _settingsOpen = false; }
    }
    internal async Task ShowLlmSettingsAsync()
    {
        var store = LlmSettings; var saved = store.Settings;
        var address = new TextBox { Name = "LlmBaseUrl", Header = "Base URL", Text = saved.BaseUrl };
        var model = new TextBox { Name = "LlmModel", Header = "模型", Text = saved.Model };
        var key = new PasswordBox { Name = "LlmKey", Header = "设置或更换 API 密钥（留空保留）", PasswordRevealMode = PasswordRevealMode.Hidden };
        var timeout = new NumberBox { Header = "超时（秒）", Minimum = 1, Maximum = 120, Value = saved.TimeoutSeconds };
        var responseMode = new ComboBox { Name = "LlmResponseMode", Header = "响应模式", HorizontalAlignment = HorizontalAlignment.Stretch,
            ItemsSource = new[] { "Auto · 普通文字与受限建议", "JSON Object · 服务支持 JSON mode", "JSON Schema · 服务明确支持严格 schema" }, SelectedIndex = (int)saved.EffectiveResponseMode };
        var message = new TextBlock { Name = "LlmSettingsStatus", Text = "密钥保存在 Windows 凭据管理器；更改服务地址需要单独设置密钥。", TextWrapping = TextWrapping.Wrap };
        var save = new Button { Name = "LlmSave", Content = "保存设置" }; var remove = new Button { Name = "LlmRemove", Content = "删除此服务密钥" };
        var test = new Button { Name = "LlmTest", Content = "测试连接" };
        var stack = new StackPanel { Spacing = 12, MaxWidth = 420 };
        stack.Children.Add(new TextBlock { Text = "提供商：OpenAI 兼容 HTTP" });
        foreach (var control in new UIElement[] { address, model, key, timeout, responseMode, message, save, remove, test })  { if (control is FrameworkElement named && named.Name.Length > 0) Microsoft.UI.Xaml.Automation.AutomationProperties.SetAutomationId(named, named.Name); stack.Children.Add(control); }
        using var cancellation = new CancellationTokenSource();
        var dialog = new ContentDialog { Title = "AI 服务设置", Content = new ScrollViewer { Content = stack }, CloseButtonText = "关闭", XamlRoot = XamlRoot };
        dialog.Closing += (_, _) => cancellation.Cancel();
        StudioLlmSettings Read() => new(address.Text.Trim(), model.Text.Trim(), double.IsFinite(timeout.Value) ? (int)timeout.Value : 0, ResponseMode: (StudioResponseMode)responseMode.SelectedIndex);
        save.Click += (_, _) => {
            try { _aiCancel?.Cancel(); store.Save(Read(), string.IsNullOrEmpty(key.Password) ? null : key.Password); key.Password = ""; message.Text = "设置已保存。"; }
            catch (StudioLlmException ex) { message.Text = ex.Message; }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { message.Text = "无法保存服务设置。"; }
        };
        remove.Click += (_, _) => {
            try { _aiCancel?.Cancel(); new StudioCredentialStore().Remove(StudioLlmSettingsStore.Target(Read())); key.Password = ""; message.Text = "密钥已删除。"; }
            catch (StudioLlmException ex) { message.Text = ex.Message; }
        };
        test.Click += async (_, _) => {
            test.IsEnabled = save.IsEnabled = remove.IsEnabled = false;
            try {
                var settings = Read(); settings.Endpoint();
                var credential = string.IsNullOrEmpty(key.Password) ? new StudioCredentialStore().Read(StudioLlmSettingsStore.Target(settings)) : key.Password;
                if (credential == null) throw new StudioLlmException("credential", "请先设置密钥。");
                await new StudioLlmProvider(_llmHttp).CompleteAsync(settings, credential, "Return JSON {\"message\":\"ok\",\"tool_calls\":[]}.", "Connection test only.", cancellation.Token, StudioConversationContracts.Schema);
                message.Text = "连接成功。";
            }
            catch (OperationCanceledException) { message.Text = "已取消。"; }
            catch (StudioLlmException ex) { message.Text = ex.Message; }
            finally { test.IsEnabled = save.IsEnabled = remove.IsEnabled = true; }
        };
        await dialog.ShowAsync(); key.Password = "";
    }
}
