using System.Text.Json;
using Aura_WinUI.Pages;
using Aura_WinUI.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace Aura_WinUI.Validation;

internal static class StudioAiValidation
{
    internal static bool FullSmoke => Environment.GetCommandLineArgs().Contains("--validate-studio-ai") &&
        !string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("AURA_STUDIO_AI_VALIDATION_DIR")) &&
        !string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("AURA_DATA_ROOT")) &&
        File.Exists(Path.Combine(RuntimeLayoutResolver.DataRoot, ".aura-studio-ai-fixture")) &&
        Environment.GetEnvironmentVariable("AURA_MAGNETIC_VALIDATION_OFFLINE") == "1";
    internal static bool Requested => SettingsOnly || FullSmoke;
    internal static bool SettingsOnly => Environment.GetCommandLineArgs().Contains("--validate-studio-ai-settings") &&
        !string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("AURA_STUDIO_AI_VALIDATION_DIR")) &&
        !string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("AURA_DATA_ROOT")) &&
        Environment.GetEnvironmentVariable("AURA_MAGNETIC_VALIDATION_OFFLINE") == "1";
    internal static IEnumerable<DependencyObject> Descendants(DependencyObject parent) {
        for (var i = 0; i < VisualTreeHelper.GetChildrenCount(parent); i++) {
            var child = VisualTreeHelper.GetChild(parent, i); yield return child;
            foreach (var d in Descendants(child)) yield return d;
        }
    }
    internal static void Invoke(Button button) {
        var peer = new Microsoft.UI.Xaml.Automation.Peers.ButtonAutomationPeer(button);
        ((Microsoft.UI.Xaml.Automation.Provider.IInvokeProvider)peer.GetPattern(Microsoft.UI.Xaml.Automation.Peers.PatternInterface.Invoke)).Invoke();
    }
    internal static void InvokeControl(FrameworkElement control) {
        var peer = Microsoft.UI.Xaml.Automation.Peers.FrameworkElementAutomationPeer.CreatePeerForElement(control);
        ((Microsoft.UI.Xaml.Automation.Provider.IInvokeProvider)peer.GetPattern(Microsoft.UI.Xaml.Automation.Peers.PatternInterface.Invoke)).Invoke();
    }
    internal static async Task Until(Func<Task<bool>> condition, string message) {
        var deadline = DateTime.UtcNow.AddSeconds(20);
        while (!await condition()) { if (DateTime.UtcNow > deadline) throw new TimeoutException(message); await Task.Delay(50); }
    }
    internal static async Task<JsonElement> Eval(Microsoft.UI.Xaml.Controls.WebView2 view, string script) {
        await view.EnsureCoreWebView2Async();
        using var doc = JsonDocument.Parse(await view.ExecuteScriptAsync(script)); return doc.RootElement.Clone();
    }
    private static async Task Capture(MainWindow window, string directory, string name) {
        var bitmap = new Microsoft.UI.Xaml.Media.Imaging.RenderTargetBitmap(); await bitmap.RenderAsync(window.Content);
        var buffer = await bitmap.GetPixelsAsync(); var bytes = new byte[buffer.Length];
        using (var reader = Windows.Storage.Streams.DataReader.FromBuffer(buffer)) reader.ReadBytes(bytes);
        var folder = await Windows.Storage.StorageFolder.GetFolderFromPathAsync(directory);
        var file = await folder.CreateFileAsync(name + ".png", Windows.Storage.CreationCollisionOption.ReplaceExisting);
        using var stream = await file.OpenAsync(Windows.Storage.FileAccessMode.ReadWrite);
        var encoder = await Windows.Graphics.Imaging.BitmapEncoder.CreateAsync(Windows.Graphics.Imaging.BitmapEncoder.PngEncoderId, stream);
        encoder.SetPixelData(Windows.Graphics.Imaging.BitmapPixelFormat.Bgra8, Windows.Graphics.Imaging.BitmapAlphaMode.Premultiplied,
            (uint)bitmap.PixelWidth, (uint)bitmap.PixelHeight, 96, 96, bytes); await encoder.FlushAsync();
    }
    internal static async Task RunSmokeAsync(MainWindow window)
    {
        var directory = Path.GetFullPath(Environment.GetEnvironmentVariable("AURA_STUDIO_AI_VALIDATION_DIR")!);
        var results = new List<object>(); string? error = null; string? target = null;
        try {
            Directory.CreateDirectory(directory);
            var status = await AuraControlClient.Instance.GetRuntimeStatusAsync();
            if (!status.IsOnline || !status.IsDryRun) throw new InvalidOperationException("AI smoke requires pre-existing isolated dry-run core");
            await DaemonSupervisor.Instance.RefreshAsync();
            var mockUrl = Environment.GetEnvironmentVariable("AURA_STUDIO_AI_MOCK_URL") ?? "";
            if (!Uri.TryCreate(mockUrl, UriKind.Absolute, out var url) || !url.IsLoopback || url.Scheme != "http") throw new InvalidOperationException("Mock provider must be loopback HTTP");
            var initialScale = window.Content.XamlRoot.RasterizationScale;
            window.AppWindow.Resize(new((int)(1520 * initialScale), (int)(800 * initialScale)));
            window.NavigateTo(typeof(StudioPage)); var page = (StudioPage)MainWindow.CurrentNavFrame!.Content;
            T Find<T>(string name) where T : FrameworkElement => (T)page.FindName(name);
            var view = Find<WebView2>("StudioWebView");
            await Until(async () => view.CoreWebView2 != null && (await Eval(view, "!!document.querySelector('.blocklySvg')")).GetBoolean(), "WebView editor did not load");
            await Until(() => Task.FromResult(Find<TextBlock>("ProjectStatus").Text.Contains("fixture")), "Initial configured project did not load");
            var settings = new StudioLlmSettings(mockUrl, "fixture", 5, true);
            var store = new StudioLlmSettingsStore(Path.Combine(RuntimeLayoutResolver.DataRoot, "studio-llm-settings.json"), new StudioCredentialStore());
            target = StudioLlmSettingsStore.Target(settings); store.Save(settings, "fixture-only-key");
            var settingsShowing = page.ShowLlmSettingsAsync(); ContentDialog? settingsDialog = null;
            await Until(() => {
                settingsDialog = VisualTreeHelper.GetOpenPopupsForXamlRoot(page.XamlRoot).SelectMany(p => Descendants(p.Child).Prepend(p.Child)).OfType<ContentDialog>().FirstOrDefault();
                return Task.FromResult(settingsDialog != null);
            }, "Provider settings did not open");
            var controls = Descendants(settingsDialog!).OfType<FrameworkElement>().ToArray();
            var responseModes = controls.OfType<ComboBox>().Single(c => c.Name == "LlmResponseMode").Items.Cast<string>().ToArray();
            if (responseModes.Length != 3 || !responseModes[0].StartsWith("Auto") || !responseModes[1].StartsWith("JSON Object") || !responseModes[2].StartsWith("JSON Schema")) throw new InvalidOperationException("Provider response modes missing");
            results.Add(new { phase = "native Settings exposes Auto, JSON Object, JSON Schema" });
            var connectionTest = controls.OfType<Button>().First(b => b.Name == "LlmTest");
            var connectionStatus = controls.OfType<TextBlock>().First(b => b.Name == "LlmSettingsStatus");
            Invoke(connectionTest);
            await Until(() => Task.FromResult(connectionTest.IsEnabled && connectionStatus.Text == "连接成功。"), "Mock connection test failed: " + connectionStatus.Text);
            if (controls.OfType<PasswordBox>().Single().Password != "" || connectionStatus.Text.Contains("fixture-only-key")) throw new InvalidOperationException("Connection test echoed key");
            settingsDialog!.Hide(); await settingsShowing; results.Add(new { phase = "native mock test connection, no key echo" });
            // Instrument requests only in this explicitly isolated test WebView.
            await Eval(view, """
                (()=>{window.__aiSmokeCalls=[];const original=window.fetch;
                  window.fetch=async(...args)=>{const url=String(args[0]);const method=args[1]?.method||'GET';
                    window.__aiSmokeCalls.push({url,method});
                    if(url==='/api/compile_effect')return new Response(JSON.stringify({success:false,stage:'compile_failed',message:'mock error C2039: fixture member'}),{status:400,headers:{'Content-Type':'application/json'}});
                    return original(...args);};return true;})()
                """);
            results.Add(new { phase = "shell and retained editor load", dry_run = status.IsDryRun });
            var commands = Find<CommandBar>("StudioCommands");
            InvokeControl(commands.SecondaryCommands.OfType<AppBarButton>().First(b => b.Label == "AI 助手"));
            Find<TextBox>("AiPrompt").Text = "把当前效果速度提高一点";
            var prompt = Find<TextBox>("AiPrompt"); var proposal = Find<TextBlock>("AiProposal"); var aiStatus = Find<TextBlock>("AiStatus");
            var apply = Find<Button>("AiApply"); var undo = Find<Button>("AiUndo"); var send = Find<Button>("AiSend");
            string ConfigText() => File.ReadAllText(Path.Combine(RuntimeLayoutResolver.DataRoot, "config.json"));
            double? Period(JsonElement value) {
                if (value.ValueKind == JsonValueKind.Object) {
                    if (value.TryGetProperty("PERIOD_SEC", out var p)) { var b = p.TryGetProperty("shadow", out var shadow) ? shadow : p.GetProperty("block"); return b.GetProperty("fields").GetProperty("NUM").GetDouble(); }
                    foreach (var item in value.EnumerateObject()) { var result = Period(item.Value); if (result.HasValue) return result; }
                } else if (value.ValueKind == JsonValueKind.Array) foreach (var item in value.EnumerateArray()) { var result = Period(item); if (result.HasValue) return result; }
                return null;
            }
            double SavedPeriod() { using var doc = JsonDocument.Parse(ConfigText()); return Period(doc.RootElement.GetProperty("blockly_effects").GetProperty("fixture").GetProperty("blockly_json")) ?? -1; }
            var beforeConfig = ConfigText(); Invoke(send);
            await Until(() => Task.FromResult(apply.IsEnabled), "Validated proposal did not render: " + aiStatus.Text);
            if (!proposal.Text.Contains("→") || !proposal.Text.Contains("尚未应用") || ConfigText() != beforeConfig || undo.IsEnabled) throw new InvalidOperationException("Diff/explicit apply boundary failed");
            results.Add(new { phase = "mock modify diff, validation and local preview", automatic_apply = false });
            proposal.UpdateLayout(); apply.StartBringIntoView(new BringIntoViewOptions { AnimationDesired = false });
            await Task.Delay(100); // Screenshot waits for the next rendered frame, after functional assertions.
            await Capture(window, directory, "native-proposal-dark");
            if (view.Visibility != Visibility.Visible || page.ActualWidth < 1050) throw new InvalidOperationException("Editor capture requires the wide, visible retained surface");
            var screenshotFolder = await Windows.Storage.StorageFolder.GetFolderFromPathAsync(directory);
            var screenshotFile = await screenshotFolder.CreateFileAsync("editor-proposal-dark.png", Windows.Storage.CreationCollisionOption.ReplaceExisting);
            using (var webCapture = await screenshotFile.OpenAsync(Windows.Storage.FileAccessMode.ReadWrite))
                await view.CoreWebView2.CapturePreviewAsync(Microsoft.Web.WebView2.Core.CoreWebView2CapturePreviewImageFormat.Png, webCapture).AsTask().WaitAsync(TimeSpan.FromSeconds(10));
            Invoke(apply); await Until(() => Task.FromResult(undo.IsEnabled && aiStatus.Text.Contains("已应用")), "Apply did not complete: " + aiStatus.Text);
            InvokeControl(Find<AppBarButton>("SaveCommand"));
            await Until(() => Task.FromResult(SavedPeriod() == 2), "Native save did not persist proposed period");
            Invoke(undo); await Until(() => Task.FromResult(!undo.IsEnabled && aiStatus.Text.Contains("已撤销")), "Undo failed: " + aiStatus.Text);
            InvokeControl(Find<AppBarButton>("SaveCommand"));
            await Until(() => Task.FromResult(SavedPeriod() == 3), "Undo did not restore original period");
            if (ConfigText().Contains("applied_plugin_name")) throw new InvalidOperationException("AI unexpectedly published a plugin");
            results.Add(new { phase = "native apply/save/undo/save", period_after_apply = 2, period_after_undo = 3 });
            InvokeControl(Find<AppBarButton>("BuildCommand"));
            await Until(() => Task.FromResult(Find<TextBlock>("ValidationSummary").Text.Contains("失败")), "Mock build error did not appear");
            Invoke(Find<Button>("AskAiError"));
            if (!Find<TextBox>("AiPrompt").Text.Contains("失败")) throw new InvalidOperationException("Ask AI did not fill ordinary chat prompt");
            Invoke(send); await Until(() => Task.FromResult((proposal.Text.Contains("C2039") || Descendants(Find<StackPanel>("AiMessages")).OfType<TextBlock>().Any(t => t.Text.Contains("C2039"))) && send.IsEnabled), "Mock error analysis failed: " + aiStatus.Text);
            results.Add(new { phase = "build error Ask AI minimal context" });
            // Rejected model output never produces an applyable proposal.
            prompt.Text = "invalid-action"; Invoke(send);
            await Until(() => Task.FromResult(send.IsEnabled && aiStatus.Text.Contains("不符合")), "Invalid action not rejected");
            if (apply.IsEnabled) throw new InvalidOperationException("Invalid action enabled Apply");
            results.Add(new { phase = "invalid action rejection" });
            foreach (var theme in new[] { ElementTheme.Dark, ElementTheme.Light }) {
                ((FrameworkElement)window.Content).RequestedTheme = theme;
                foreach (var size in new[] { (1280, 800), (600, 500) }) {
                    var scale = window.Content.XamlRoot.RasterizationScale;
                    ((Microsoft.UI.Windowing.OverlappedPresenter)window.AppWindow.Presenter).Restore();
                    window.AppWindow.Resize(new((int)(size.Item1 * scale), (int)(size.Item2 * scale)));
                    await Until(() => Task.FromResult(page.ActualWidth > 0 && Find<Border>("AiPanel").ActualWidth > 0), "Layout did not settle");
                    await Task.Delay(100); // Allow a rendered frame after native layout settlement for screenshot only.
                    var state = await Eval(view, "({overflow:document.documentElement.scrollWidth-window.innerWidth,canvas:document.querySelector('.blocklySvg')?.getBoundingClientRect().width||0})");
                    if (state.GetProperty("overflow").GetDouble() > 1 || state.GetProperty("canvas").GetDouble() < 200 || commands.ActualWidth > page.ActualWidth + 1) throw new InvalidOperationException("Native shell/editor clipping");
                    await Capture(window, directory, $"native-{theme}-{size.Item1}");
                    results.Add(new { phase = $"theme/width {theme}/{size.Item1}", state });
                }
            }
            var calls = await Eval(view, "window.__aiSmokeCalls");
            if (calls.EnumerateArray().Any(c => c.GetProperty("url").GetString() == "/api/reload_plugin")) throw new InvalidOperationException("Unexpected plugin load");
            results.Add(new { phase = "no automatic publish/plugin load", calls });
            if (Environment.GetEnvironmentVariable("AURA_STUDIO_AI_CAPTURE") == "1") {
                var scale = window.Content.XamlRoot.RasterizationScale;
                window.AppWindow.Resize(new((int)(1520 * scale), (int)(800 * scale)));
                prompt.Text = "把当前效果速度提高一点"; Invoke(send);
                await Until(() => Task.FromResult(apply.IsEnabled), "Capture proposal did not validate");
                apply.StartBringIntoView(new BringIntoViewOptions { AnimationDesired = false }); await Task.Delay(100);
            }
            File.WriteAllText(Path.Combine(directory, "capture-ready.json"), JsonSerializer.Serialize(new { process_id = Environment.ProcessId }));
            // External window-scoped UIA inspection/capture can run while this test owns the app.
            if (Environment.GetEnvironmentVariable("AURA_STUDIO_AI_CAPTURE") == "1") await Task.Delay(15000);
        } catch (Exception ex) { error = StudioLlmRedaction.Filter(ex.Message); }
        finally {
            if (target != null) new StudioCredentialStore().Remove(target);
            Directory.CreateDirectory(directory);
            File.WriteAllText(Path.Combine(directory, "smoke-results.json"), JsonSerializer.Serialize(new { error, results }, new JsonSerializerOptions { WriteIndented = true }));
            await App.RequestExit();
        }
    }
    internal static async Task RunSettingsAsync(MainWindow window)
    {
        string? error = null; var results = new List<string>(); var target = "";
        var directory = Path.GetFullPath(Environment.GetEnvironmentVariable("AURA_STUDIO_AI_VALIDATION_DIR")!);
        try {
            Directory.CreateDirectory(directory);
            var status = await AuraControlClient.Instance.GetRuntimeStatusAsync();
            if (status.IsOnline) throw new InvalidOperationException("Settings-only test requires no daemon");
            window.NavigateTo(typeof(StudioPage)); var page = (StudioPage)MainWindow.CurrentNavFrame!.Content;
            var deadline = DateTime.UtcNow.AddSeconds(10);
            while (page.XamlRoot == null) { if (DateTime.UtcNow > deadline) throw new TimeoutException("XAML root"); await Task.Delay(50); }
            var showing = page.ShowLlmSettingsAsync(); ContentDialog? dialog = null;
            while (dialog == null) {
                dialog = VisualTreeHelper.GetOpenPopupsForXamlRoot(page.XamlRoot).SelectMany(p => Descendants(p.Child).Prepend(p.Child)).OfType<ContentDialog>().FirstOrDefault();
                if (DateTime.UtcNow > deadline) throw new TimeoutException("Settings dialog"); await Task.Delay(50);
            }
            T Find<T>(string name) where T : FrameworkElement => Descendants(dialog).OfType<T>().First(c => c.Name == name);
            var address = Find<TextBox>("LlmBaseUrl"); var model = Find<TextBox>("LlmModel"); var key = Find<PasswordBox>("LlmKey");
            if (key.Password.Length != 0 || key.PasswordRevealMode != PasswordRevealMode.Hidden) throw new InvalidOperationException("Credential UI exposure");
            address.Text = "http://127.0.0.1:32199/ui-" + Guid.NewGuid(); model.Text = "fixture"; key.Password = "fixture-only-key";
            target = StudioLlmSettingsStore.Target(new(address.Text, model.Text));
            Invoke(Find<Button>("LlmSave")); await Task.Delay(100);
            var savedPath = Path.Combine(RuntimeLayoutResolver.DataRoot, "studio-llm-settings.json");
            if (!File.Exists(savedPath) || File.ReadAllText(savedPath).Contains("fixture-only-key") || key.Password != "") throw new InvalidOperationException("Settings save failed/redaction");
            var vault = new StudioCredentialStore(); if (vault.Read(target) != "fixture-only-key") throw new InvalidOperationException("Credential save failed");
            key.Password = "fixture-changed-key"; Invoke(Find<Button>("LlmSave"));
            if (vault.Read(target) != "fixture-changed-key" || key.Password != "") throw new InvalidOperationException("Credential change failed");
            Invoke(Find<Button>("LlmRemove")); if (vault.Read(target) != null) throw new InvalidOperationException("Credential removal failed");
            if (!Find<Button>("LlmTest").IsEnabled) throw new InvalidOperationException("Connection action unavailable");
            results.AddRange(["settings dialog", "save", "change credential", "remove credential", "no plaintext/key echo", "explicit connection action"]);
            dialog.Hide(); await showing;
        } catch (Exception ex) { error = StudioLlmRedaction.Filter(ex.Message); }
        finally {
            if (target != "") new StudioCredentialStore().Remove(target);
            Directory.CreateDirectory(directory);
            File.WriteAllText(Path.Combine(directory, "settings-results.json"), JsonSerializer.Serialize(new { error, results }));
            await App.RequestExit();
        }
    }
}
