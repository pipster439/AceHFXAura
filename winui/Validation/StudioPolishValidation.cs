using System.Text.Json;
using Aura_WinUI.Pages;
using Aura_WinUI.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using static Aura_WinUI.Validation.StudioAiValidation;

namespace Aura_WinUI.Validation;

internal static class StudioPolishValidation
{
    internal static bool Requested => StudioToolingValidation.Requested && Environment.GetEnvironmentVariable("AURA_STUDIO_POLISH") == "1";
    internal static async Task RunAsync(MainWindow window, StudioPage page, WebView2 view, string directory, Action<string, object?> record) {
        if (!Requested) throw new InvalidOperationException("Owned offline fixture required");
        await view.EnsureCoreWebView2Async();
        T Find<T>(string name) where T : FrameworkElement => (T)page.FindName(name);
        var commands = Find<CommandBar>("StudioCommands");
        void Command(string id) => InvokeControl(commands.SecondaryCommands.OfType<AppBarButton>().First(b => b.Tag as string == id));
        async Task Click(string selector) => await Eval(view, $"(()=>{{const e=document.querySelector({JsonSerializer.Serialize(selector)});if(!e||e.disabled||e.closest('[inert]'))throw Error('Unavailable control');e.click();return true;}})()");
        async Task Capture(string name) {
            var file = await (await Windows.Storage.StorageFolder.GetFolderFromPathAsync(directory)).CreateFileAsync(name + ".png", Windows.Storage.CreationCollisionOption.ReplaceExisting);
            using var stream = await file.OpenAsync(Windows.Storage.FileAccessMode.ReadWrite);
            await view.CoreWebView2.CapturePreviewAsync(Microsoft.Web.WebView2.Core.CoreWebView2CapturePreviewImageFormat.Png, stream).AsTask().WaitAsync(TimeSpan.FromSeconds(10));
        }
        async Task CaptureNative(string name) {
            if (!Requested) throw new InvalidOperationException("Owned fixture capture required");
            var start = new System.Diagnostics.ProcessStartInfo("winapp") { UseShellExecute = false, CreateNoWindow = true, WindowStyle = System.Diagnostics.ProcessWindowStyle.Hidden, RedirectStandardOutput = true, RedirectStandardError = true };
            foreach (var arg in new[] { "ui", "screenshot", "--focus", "--output", Path.Combine(directory, name + ".png"), "--json", "--on", "local", "-a", Environment.ProcessId.ToString() }) start.ArgumentList.Add(arg);
            using var process = System.Diagnostics.Process.Start(start) ?? throw new InvalidOperationException("Owned capture not started");
            var stdout = process.StandardOutput.ReadToEndAsync(); var stderr = process.StandardError.ReadToEndAsync();
            await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(20));
            File.WriteAllText(Path.Combine(directory, name + "-capture.json"), await stdout + await stderr);
            if (process.ExitCode != 0) throw new InvalidOperationException("Owned native capture failed");
        }
        async Task SettleWindow(int width) {
            double last = -1; var stable = 0;
            await Until(() => {
                var ready = ((FrameworkElement)window.Content).ActualWidth >= width - 50 && ((FrameworkElement)window.Content).ActualWidth <= width + 5 && view.ActualWidth > 0;
                stable = ready && Math.Abs(view.ActualWidth - last) < 0.1 ? stable + 1 : 0; last = view.ActualWidth;
                return Task.FromResult(stable >= 3);
            }, "Native window layout did not settle before viewport emulation");
        }
        byte[]? exported = null;
        page.BundleExportFixture = data => { exported = data; File.WriteAllBytes(Path.Combine(directory, "fixture.auraeffect"), data); return Task.CompletedTask; };
        page.BundleImportFixture = () => Task.FromResult(exported);
        await Until(async () => (await Eval(view, "document.querySelector('[data-autosave-status]')?.textContent.includes('自动')||false")).GetBoolean(), "Draft storage not ready");
        Command("export_bundle"); await Until(async () => (await Eval(view, "!!document.querySelector('[data-bundle-dialog]')")).GetBoolean(), "Export dialog missing");
        await Click("[data-bundle-confirm]"); await Until(()=>Task.FromResult(exported != null), "Bundle not exported");
        await Until(async () => !(await Eval(view, "!!document.querySelector('[data-bundle-dialog]')")).GetBoolean(), "Export not complete");
        record("source-only .auraeffect export through native bounded ZIP codec", new { bytes = exported!.Length, picker = "isolated fixture delegate" });
        Command("import_bundle"); await Until(async () => (await Eval(view, "!!document.querySelector('[data-bundle-dialog]')")).GetBoolean(), "Import summary missing");
        using (var before = JsonDocument.Parse(File.ReadAllText(Path.Combine(RuntimeLayoutResolver.DataRoot,"config.json"))))
            if (before.RootElement.GetProperty("blockly_effects").EnumerateObject().Count() != 2) throw new InvalidOperationException("Import changed config before confirmation");
        await Capture("bundle-confirmation");
        const string importedName = "fixture_import_with_a_long_studio_name_123456789";
        await Eval(view, $"(()=>{{const e=document.querySelector('[data-bundle-name]');Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(e,{JsonSerializer.Serialize(importedName)});e.dispatchEvent(new Event('input',{{bubbles:true}}));return true;}})()");
        await Click("[data-bundle-confirm]"); await Until(()=>Task.FromResult(Find<TextBlock>("ProjectStatus").Text.StartsWith(importedName)), "Confirmed new draft not selected");
        using (var after = JsonDocument.Parse(File.ReadAllText(Path.Combine(RuntimeLayoutResolver.DataRoot,"config.json")))) {
            var draft=after.RootElement.GetProperty("blockly_effects").GetProperty(importedName);
            if(draft.EnumerateObject().Any(p=>p.Name.StartsWith("applied_")||p.Name.StartsWith("published_"))) throw new InvalidOperationException("Imported draft acquired publication authority");
        }
        record("validated bundle summary and explicit confirmation create isolated draft; no automatic Publish", new { name = importedName });
        Command("palette"); await Until(async () => (await Eval(view,"document.querySelectorAll('[data-palette-command]').length===13")).GetBoolean(),"Palette discovery");
        // Actual React search/keyboard event handlers, shared existing Validate action.
        await Eval(view,"(()=>{const e=document.querySelector('[aria-label=\"搜索工作室命令\"]');Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(e,'Validate');e.dispatchEvent(new Event('input',{bubbles:true}));return true;})()");
        await Until(async () => (await Eval(view,"document.querySelectorAll('[data-palette-command]').length===1")).GetBoolean(),"Palette filter");
        await Eval(view,"document.querySelector('[aria-label=\"搜索工作室命令\"]').dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true}))");
        await Until(async () => !(await Eval(view,"!!document.querySelector('[data-studio-palette]')")).GetBoolean(),"Palette Validate did not execute");
        Command("palette"); await Until(async () => (await Eval(view,"!!document.querySelector('[data-studio-palette]')")).GetBoolean(),"Palette reopen");
        await Eval(view,"document.querySelector('[aria-label=\"搜索工作室命令\"]').dispatchEvent(new KeyboardEvent('keydown',{key:'ArrowDown',bubbles:true}))");
        await Until(async () => (await Eval(view,"document.querySelector('[data-palette-selected=\"true\"]').dataset.paletteCommand==='open'")).GetBoolean(),"Palette keyboard navigation");
        await Capture("command-palette");
        await Eval(view,"document.querySelector('[aria-label=\"搜索工作室命令\"]').dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true}))");
        record("command discovery filtering ArrowDown Enter Validate and Esc use retained command handlers", null);
        Command("bench"); await Until(async () => (await Eval(view,"!!document.querySelector('[data-studio-bench]')")).GetBoolean(),"Imported project bench missing");
        await Click("[data-bench-play]"); await Until(async () => (await Eval(view,"document.querySelector('[data-studio-bench]').dataset.completed==='true'")).GetBoolean(),"Imported project scenario incomplete");
        record("imported project executes existing deterministic test bench", null);
        var root = (FrameworkElement)window.Content; var originalTheme = root.RequestedTheme; var scale = root.XamlRoot.RasterizationScale;
        try {
            foreach(var theme in new[]{ElementTheme.Light,ElementTheme.Dark}) foreach(var width in new[]{840,1520}) foreach(var zoom in new[]{1.25,1.5}) {
                root.RequestedTheme=theme; StudioPage.HostThemeChanged(); window.AppWindow.Resize(new((int)(width*scale),(int)(800*scale)));
                await SettleWindow(width);
                await view.CoreWebView2.CallDevToolsProtocolMethodAsync("Emulation.setDeviceMetricsOverride", JsonSerializer.Serialize(new { width=(int)(view.ActualWidth*scale/zoom), height=(int)(view.ActualHeight*scale/zoom), deviceScaleFactor=zoom, mobile=false }));
                await Until(async () => (await Eval(view,"(()=>{const b=document.querySelector('[data-studio-bench]');const r=b.getBoundingClientRect();const keys=[...b.querySelectorAll('button[aria-label$=\" 按键\"]')];return r.left>=0&&r.right<=innerWidth+1&&keys.length===68&&keys.every(k=>{const x=k.getBoundingClientRect();return x.width>0&&x.left>=r.left-1&&x.right<=r.right+1;});})()")).GetBoolean(),"Responsive bench keyboard clipped");
                var name=$"ux-{theme}-{width}-zoom{(int)(zoom*100)}"; await Capture(name);
                Command("palette"); await Until(async()=> (await Eval(view,"!!document.querySelector('[data-studio-palette]')")).GetBoolean(),"Palette matrix missing");
                if(!(await Eval(view,"(()=>{const r=document.querySelector('[data-studio-palette]').getBoundingClientRect();return r.left>=0&&r.right<=innerWidth&&r.width>0;})()")).GetBoolean()) throw new InvalidOperationException("Palette horizontal overflow");
                await Capture(name+"-palette"); await Eval(view,"document.querySelector('[aria-label=\"搜索工作室命令\"]').dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true}))");
            }
            record("light/dark narrow/wide bench and palette at 125/150 percent emulated WebView DPI", new { native_rasterization_scale=scale, scaling="DevTools viewport and deviceScaleFactor emulation; monitor DPI unchanged", cases=8 });
        } finally { root.RequestedTheme=originalTheme; StudioPage.HostThemeChanged(); await view.CoreWebView2.CallDevToolsProtocolMethodAsync("Emulation.clearDeviceMetricsOverride", "{}"); window.AppWindow.Resize(new((int)(1520*scale),(int)(800*scale))); await SettleWindow(1520); }
        // Exercise the manual checklist without issuing any provider request.
        InvokeControl(commands.SecondaryCommands.OfType<AppBarButton>().First(b=>b.Label=="AI 助手"));
        var manual=Find<CheckBox>("AiManualMode"); manual.IsChecked=true;
        if(Find<StackPanel>("AiManualChecklist").Visibility!=Visibility.Visible || Find<Button>("AiRepair").IsEnabled) throw new InvalidOperationException("Manual acceptance harness unavailable");
        await CaptureNative("manual-mode-native");
        manual.IsChecked=false;
        var status=Find<TextBlock>("AiStatus"); var proposal=Find<TextBlock>("AiProposal");
        status.Text=string.Concat(Enumerable.Repeat("本地验证错误：请检查支持的积木与播放方式。",60));
        proposal.Text=string.Concat(Enumerable.Repeat("typed proposal 未应用；长错误仍可滚动阅读。",60));
        status.UpdateLayout(); status.StartBringIntoView(new BringIntoViewOptions { AnimationDesired = false });
        await CaptureNative("long-ai-error-native");
        status.Text=""; proposal.Text="";
        var settingsTask=page.ShowLlmSettingsAsync(); ContentDialog? settingsDialog=null;
        await Until(()=> { settingsDialog=Microsoft.UI.Xaml.Media.VisualTreeHelper.GetOpenPopupsForXamlRoot(page.XamlRoot).SelectMany(p=>Descendants(p.Child).Prepend(p.Child)).OfType<ContentDialog>().FirstOrDefault(); return Task.FromResult(settingsDialog!=null); }, "Settings dialog missing");
        var model=Descendants(settingsDialog!).OfType<TextBox>().First(t=>t.Name=="LlmModel");
        model.Text="owner-manual-model-with-a-very-long-provider-family-and-version-name-123456789012345678901234567890";
        if(Descendants(settingsDialog!).OfType<PasswordBox>().First().Password.Length!=0) throw new InvalidOperationException("Stored credential echoed");
        await CaptureNative("long-model-settings-native");
        settingsDialog!.Hide(); await settingsTask;
        manual.IsChecked=false; InvokeControl(commands.SecondaryCommands.OfType<AppBarButton>().First(b=>b.Label=="AI 助手"));
        record("manual provider checklist, long model/error layout, hidden credential, retry disabled; zero provider requests", null);
        Find<ComboBox>("RecentProjects").SelectedItem="fixture";
        await Until(()=>Task.FromResult(Find<TextBlock>("ProjectStatus").Text.StartsWith("fixture ")),"Original fixture not reselected");
    }
}
