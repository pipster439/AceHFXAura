using System.Text.Json;
using Aura_WinUI.Pages;
using Aura_WinUI.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using static Aura_WinUI.Validation.StudioAiValidation;

namespace Aura_WinUI.Validation;

internal static class StudioConversationValidation
{
    internal static bool Requested => StudioAiValidation.FullSmoke && Environment.GetEnvironmentVariable("AURA_STUDIO_CONVERSATION") == "1";
    internal static async Task RunAsync(MainWindow window) {
        var directory = Path.GetFullPath(Environment.GetEnvironmentVariable("AURA_STUDIO_AI_VALIDATION_DIR")!);
        var results = new List<object>(); string? error = null; string? target = null;
        void Record(string phase, object? evidence = null) => results.Add(new { phase, evidence });
        try {
            var runtime = await AuraControlClient.Instance.GetRuntimeStatusAsync();
            if (!Requested || !runtime.IsOnline || !runtime.IsDryRun) throw new InvalidOperationException("Owned offline dry-run fixture required");
            var address = Environment.GetEnvironmentVariable("AURA_STUDIO_AI_MOCK_URL")!;
            if (!Uri.TryCreate(address, UriKind.Absolute, out var uri) || !uri.IsLoopback || uri.Scheme != "http") throw new InvalidOperationException("Loopback mock required");
            var scale = window.Content.XamlRoot.RasterizationScale;
            window.AppWindow.Resize(new((int)(1520 * scale), (int)(850 * scale)));
            window.NavigateTo(typeof(StudioPage)); var page = (StudioPage)MainWindow.CurrentNavFrame!.Content;
            T Find<T>(string name) where T : FrameworkElement => (T)page.FindName(name);
            var view = Find<WebView2>("StudioWebView");
            await Until(async () => view.CoreWebView2 != null && (await Eval(view, "!!document.querySelector('.blocklySvg')")).GetBoolean(), "Editor not ready");
            await view.EnsureCoreWebView2Async();
            await Until(() => Task.FromResult(Find<TextBlock>("ProjectStatus").Text.StartsWith("fixture ")), "Fixture project not ready");
            var settings = new StudioLlmSettings(address, "fixture", 10); target = StudioLlmSettingsStore.Target(settings);
            new StudioLlmSettingsStore(Path.Combine(RuntimeLayoutResolver.DataRoot, "studio-llm-settings.json"), new StudioCredentialStore()).Save(settings, "fixture-only-key");
            var commands = Find<CommandBar>("StudioCommands");
            void ShowAi() { if (Find<Border>("AiPanel").Visibility != Visibility.Visible) InvokeControl(commands.SecondaryCommands.OfType<AppBarButton>().First(b => b.Label == "AI 助手")); }
            string Messages() => string.Join("\n", Descendants(Find<StackPanel>("AiMessages")).OfType<TextBlock>().Select(t => t.Text));
            string Config() => File.ReadAllText(Path.Combine(RuntimeLayoutResolver.DataRoot, "config.json"));
            var originalConfig = Config();
            async Task Chat(string text) {
                ShowAi(); Find<TextBox>("AiPrompt").Text = text; Invoke(Find<Button>("AiSend"));
                await Until(() => Task.FromResult(Find<Button>("AiSend").IsEnabled), "Chat did not finish: " + Find<TextBlock>("AiStatus").Text);
                if (!Find<TextBlock>("AiStatus").Text.Contains("本轮完成")) throw new InvalidOperationException("Chat failed: " + Find<TextBlock>("AiStatus").Text);
            }
            async Task Capture(string name) {
                var start = new System.Diagnostics.ProcessStartInfo("winapp") { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true, WindowStyle = System.Diagnostics.ProcessWindowStyle.Hidden };
                foreach (var arg in new[] { "ui", "screenshot", "--focus", "--output", Path.Combine(directory, name + ".png"), "--json", "--on", "local", "-a", Environment.ProcessId.ToString() }) start.ArgumentList.Add(arg);
                using var process = System.Diagnostics.Process.Start(start)!;
                var output = process.StandardOutput.ReadToEndAsync(); var stderr = process.StandardError.ReadToEndAsync();
                await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(20));
                if (process.ExitCode != 0) throw new InvalidOperationException("Owned screenshot failed");
                File.WriteAllText(Path.Combine(directory, name + ".json"), await output + await stderr);
            }
            ShowAi();
            if (page.FindName("AiIntent") != null || page.FindName("AiPreset") != null || !Find<TextBox>("AiPrompt").AcceptsReturn || Find<StackPanel>("AiEmptyPrompts").Visibility != Visibility.Visible) throw new InvalidOperationException("Mode-dependent UI or missing composer/empty state");
            Record("one conversation UI, no mode selector, multiline composer and quick prompts");
            await Chat("这个效果为什么消失得这么快？");
            if (!Messages().Contains("周期")) throw new InvalidOperationException("Explanation missing");
            Record("ordinary natural explanation as assistant message");
            await Chat("帮我柔和一点，但颜色别变。");
            if (!Find<Button>("AiApply").IsEnabled || !Find<TextBlock>("AiProposal").Text.Contains("3 → 2")) throw new InvalidOperationException("Initial typed card/diff missing");
            if (Config() != originalConfig) throw new InvalidOperationException("Proposal changed durable project");
            Invoke(Find<Button>("AiPreview")); await Capture("conversation-proposal");
            Record("typed proposal with actual numeric diff, local preview, full fingerprint; no Apply");
            await Chat("再慢一点，但别拖太长。");
            if (!Find<TextBlock>("AiProposal").Text.Contains("3 → 2.5") || !Find<Button>("AiApply").IsEnabled) throw new InvalidOperationException("Followup did not retain pending context");
            if (Config() != originalConfig) throw new InvalidOperationException("Followup applied automatically");
            Record("followup without prior Apply, second proposal based on same latest draft");
            Invoke(Find<Button>("AiApply")); await Until(() => Task.FromResult(Find<Button>("AiUndo").IsEnabled), "Explicit Apply failed");
            if (Config() != originalConfig) throw new InvalidOperationException("Apply wrote durable/published state");
            var journal = new StudioDraftStore(RuntimeLayoutResolver.DataRoot).JournalPath("fixture");
            await Until(() => Task.FromResult(File.Exists(journal) && File.ReadAllText(journal).Contains("before_ai_apply")), "Pre-AI snapshot missing");
            Record("explicit Apply creates pre-AI snapshot; durable/runtime authority unchanged");
            Invoke(Find<Button>("AiBench")); await Until(async () => (await Eval(view, "!!document.querySelector('[data-studio-bench]')")).GetBoolean(), "Bench not open");
            await Eval(view, "document.querySelector('[data-bench-play]').click();true");
            await Until(async () => (await Eval(view, "document.querySelector('[data-studio-bench]').dataset.completed==='true'")).GetBoolean(), "Bench did not complete");
            Record("Test Bench runs current applied draft through existing simulation");
            await Chat("现在这个版本比刚才改了什么？");
            if (!Messages().Contains("当前草稿周期为 2.5")) throw new InvalidOperationException("Applied draft answer does not match current state");
            Record("followup answer matches actual applied draft");
            Invoke(Find<Button>("AiUndo")); await Until(() => Task.FromResult(!Find<Button>("AiUndo").IsEnabled), "Undo failed");
            Record("one-step AI Undo, revision notice and draft-only boundary");
            await Eval(view, """
                (()=>{window.__chatSmokeCalls=[];const original=window.fetch;window.fetch=async(...args)=>{const url=String(args[0]);window.__chatSmokeCalls.push(url);
                if(url==='/api/compile_effect')return new Response(JSON.stringify({success:false,stage:'compile_failed',message:'mock error C2039: fixture member'}),{status:400,headers:{'Content-Type':'application/json'}});return original(...args);};return true;})()
                """);
            InvokeControl(Find<AppBarButton>("BuildCommand"));
            await Until(() => Task.FromResult(Find<TextBlock>("ValidationSummary").Text.Contains("失败")), "Actual mocked build error missing");
            await Chat("为什么失败了？能修吗？");
            if (!Messages().Contains("C2039") || !Find<TextBlock>("AiProposal").Text.Contains("C2039") || !Find<Button>("AiApply").IsEnabled) throw new InvalidOperationException("Actual error not used in conversation");
            if (Config() != originalConfig) throw new InvalidOperationException("Error chat automatically saved/published");
            Record("error conversation reads actual C2039 diagnostic, bounded proposal remains user-only");
            await Capture("conversation-error");
            Find<TextBox>("AiPrompt").Text = "invalid-action"; Invoke(Find<Button>("AiSend"));
            await Until(() => Task.FromResult(Find<Button>("AiSend").IsEnabled), "Invalid tool turn did not stop");
            if (Find<Button>("AiApply").IsEnabled || !Find<TextBlock>("AiStatus").Text.Contains("安全结构")) throw new InvalidOperationException("Unknown/Publish tool accepted");
            Record("model Publish tool rejected; explicit retry available, no automatic retry");
            if (!Find<Button>("AiRetry").IsEnabled) throw new InvalidOperationException("Explicit retry unavailable");
            Invoke(Find<Button>("AiRetry")); await Until(() => Task.FromResult(Find<Button>("AiSend").IsEnabled), "Retry did not complete");
            if (Find<Button>("AiApply").IsEnabled || !Find<TextBlock>("AiStatus").Text.Contains("安全结构")) throw new InvalidOperationException("Retry bypassed tool validation");
            Record("one explicit Retry sends a new turn through same tool validator");
            Find<TextBox>("AiPrompt").Text = "cancel-wait"; Invoke(Find<Button>("AiSend"));
            await Until(() => Task.FromResult(Find<Button>("AiCancel").IsEnabled && File.Exists(Path.Combine(directory, "mock-inflight-ready.json"))), "Provider did not receive cancellable in-flight request");
            Invoke(Find<Button>("AiCancel")); await Until(() => Task.FromResult(Find<Button>("AiSend").IsEnabled), "Cancel did not finish");
            if (Find<Button>("AiApply").IsEnabled || !Find<TextBlock>("AiStatus").Text.Contains("取消")) throw new InvalidOperationException("Cancelled response became proposal");
            Record("cancel non-streaming in-flight turn; no partial candidate Apply");
            Invoke(Find<Button>("AiClear"));
            if (Find<StackPanel>("AiMessages").Children.Count != 0 || Find<StackPanel>("AiEmptyPrompts").Visibility != Visibility.Visible || Find<Button>("AiApply").IsEnabled) throw new InvalidOperationException("Clear did not remove session/proposal");
            Record("Clear removes conversation memory and pending proposal, no draft change");
            var calls = await Eval(view, "window.__chatSmokeCalls"); if (calls.EnumerateArray().Any(c => c.GetString() == "/api/reload_plugin")) throw new InvalidOperationException("Unexpected Publish");
            if (Config() != originalConfig) throw new InvalidOperationException("Published/config state changed");
            foreach (var file in Directory.EnumerateFiles(RuntimeLayoutResolver.DataRoot, "*.json", SearchOption.AllDirectories)) if (File.ReadAllText(file).Contains("fixture-only-key")) throw new InvalidOperationException("Secret persisted");
            Record("no automatic Publish/reload, no secret or conversation persistence");
            await Capture("conversation-cleared");
            File.WriteAllText(Path.Combine(directory, "capture-ready.json"), JsonSerializer.Serialize(new { process_id = Environment.ProcessId }));
            if (Environment.GetEnvironmentVariable("AURA_STUDIO_AI_CAPTURE") == "1") await Task.Delay(15000);
        } catch (Exception ex) { error = ex.GetType().Name + ": " + StudioLlmRedaction.Filter(ex.Message); }
        finally {
            if (target != null) new StudioCredentialStore().Remove(target);
            Directory.CreateDirectory(directory); File.WriteAllText(Path.Combine(directory, "smoke-results.json"), JsonSerializer.Serialize(new { error, results }, new JsonSerializerOptions { WriteIndented = true }));
            await App.RequestExit();
        }
    }
}
