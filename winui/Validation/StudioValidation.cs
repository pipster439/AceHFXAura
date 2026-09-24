using System.Text.Json;
using Aura_WinUI.Pages;
using Aura_WinUI.Services;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.Web.WebView2.Core;
using Windows.Storage;

namespace Aura_WinUI.Validation;

// Explicit, isolated desktop test entry. Normal launches never run this code.
internal static class StudioValidation
{
    internal static bool Requested => Environment.GetCommandLineArgs().Contains("--validate-studio") &&
        !string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("AURA_STUDIO_VALIDATION_DIR"));

    private static async Task Until(Func<Task<bool>> ready, string reason, int seconds = 15)
    {
        var deadline = DateTime.UtcNow.AddSeconds(seconds);
        Exception? lastError = null;
        while (true)
        {
            try { if (await ready()) return; }
            catch (Exception ex) { lastError = ex; }
            if (DateTime.UtcNow > deadline) throw new TimeoutException(reason, lastError);
            await Task.Delay(80);
        }
    }

    private static async Task<JsonElement> Eval(WebView2 view, string code)
    {
        var result = await view.ExecuteScriptAsync(code);
        using var json = JsonDocument.Parse(result);
        return json.RootElement.Clone();
    }

    private static async Task<JsonElement> Snapshot(WebView2 view) => await Eval(view, """
        (() => {
          const host = document.querySelector('[data-aura-host]');
          const studio = document.querySelector('[data-studio-workspace]');
          const canvas = document.querySelector('.blocklySvg');
          return { host: host?.getAttribute('data-aura-host') ?? 'standalone',
            workspace: studio?.getAttribute('data-studio-workspace') ?? null,
            theme: document.documentElement.getAttribute('data-theme'),
            sidebar: !!document.querySelector('aside[aria-label="主要导航"]'),
            editor: !!canvas, canvasWidth: canvas?.getBoundingClientRect().width ?? 0,
            viewportWidth: window.innerWidth, overflow: Math.max(0, document.documentElement.scrollWidth-window.innerWidth),
            controls: !!document.querySelector('button[aria-label="作品列表"]') };
        })()
        """);

    private static async Task Click(WebView2 view, string script)
    {
        var result = await Eval(view, script);
        if (result.ValueKind != JsonValueKind.True) throw new InvalidOperationException("Studio control was unavailable: " + script);
    }

    private static async Task Capture(WebView2 view, string directory, string name)
    {
        var folder = await StorageFolder.GetFolderFromPathAsync(directory);
        var file = await folder.CreateFileAsync(name + ".png", CreationCollisionOption.ReplaceExisting);
        using var stream = await file.OpenAsync(FileAccessMode.ReadWrite);
        await view.CoreWebView2.CapturePreviewAsync(CoreWebView2CapturePreviewImageFormat.Png, stream);
    }

    private static async Task<JsonElement> OverlayProbe(WebView2 view, string kind)
    {
        if (kind is not ("works" or "inspector")) throw new ArgumentOutOfRangeException(nameof(kind));
        return await Eval(view, """
            (() => { const kind='_PANEL_KIND_', panel=document.querySelector(`[data-studio-overlay="${kind}"]`),
                scrim=document.querySelector('[data-studio-overlay="scrim"]'), editor=document.querySelector('[data-studio-editor]');
              if (!panel || !scrim || !editor) return {missing:true};
              const r=panel.getBoundingClientRect();
              const x=kind==='works' ? r.left+Math.min(100,r.width/2) : r.right-Math.min(100,r.width/2);
              const y=r.top+Math.min(180,r.height/2);
              const hit=document.elementFromPoint(x,y);
              const outsideX=kind==='works' ? innerWidth-24 : 24;
              const outsideHit=document.elementFromPoint(outsideX,Math.min(180,innerHeight-24));
              const toolbox=document.querySelector('.blocklyToolbox');
              const flyout=document.querySelector('.blocklyToolboxFlyout,.blocklyFlyout');
              return {kind,panelHit:panel.contains(hit),scrimHit:outsideHit===scrim,
                topAtPanel:typeof hit?.className==='string'?hit.className:hit?.className?.baseVal,
                editorInert:editor.hasAttribute('inert'), editorPointer:getComputedStyle(editor).pointerEvents,
                editorZ:getComputedStyle(editor).zIndex,panelZ:getComputedStyle(panel).zIndex,
                scrimZ:getComputedStyle(scrim).zIndex,
                toolboxZ:toolbox?getComputedStyle(toolbox).zIndex:null,
                selected:!!document.querySelector('.blocklyToolbox [aria-selected="true"]'),
                flyoutVisible:flyout && getComputedStyle(flyout).display!=='none'};
            })()
            """.Replace("_PANEL_KIND_", kind));
    }

    private static async Task<JsonElement> CheckOverlay(WebView2 view, string kind, string directory, string label)
    {
        await Eval(view, "window.__auraCanvasBeforeOverlay=document.querySelector('.blocklySvg'); true");
        var categoryToolbox = (await Eval(view, "!!document.querySelector('.blocklyToolbox')")).GetBoolean();
        if (categoryToolbox)
        {
            var point = await Eval(view, """
                (()=>{const category=document.querySelector('.blocklyToolboxCategoryContainer');
                  if (!category) return null;
                  const rect=category.getBoundingClientRect();
                  return {x:rect.left+rect.width/2,y:rect.top+rect.height/2};})()
                """);
            if (point.ValueKind != JsonValueKind.Object) throw new InvalidOperationException("Blockly category is missing");
            var x = point.GetProperty("x").GetDouble();
            var y = point.GetProperty("y").GetDouble();
            await view.CoreWebView2.CallDevToolsProtocolMethodAsync("Input.dispatchMouseEvent",
                JsonSerializer.Serialize(new { type="mousePressed", x, y, button="left", clickCount=1 }));
            await view.CoreWebView2.CallDevToolsProtocolMethodAsync("Input.dispatchMouseEvent",
                JsonSerializer.Serialize(new { type="mouseReleased", x, y, button="left", clickCount=1 }));
            await Until(async () => (await Eval(view, "(() => {const f=document.querySelector('.blocklyToolboxFlyout,.blocklyFlyout');return !!f && getComputedStyle(f).display!=='none'})()")).GetBoolean(),
                "Blockly category did not open its flyout before opening overlay");
        }
        else await Until(async () => (await Eval(view, "(() => {const f=document.querySelector('.blocklyFlyout');return !!f && getComputedStyle(f).display!=='none'})()")).GetBoolean(),
            "Automation's permanent flyout was not visible before opening overlay");
        await Click(view, $"(() => {{ const button=document.querySelector('button[aria-label=\"{(kind == "works" ? "作品列表" : "预览与检查")}\"]'); button?.click(); return !!button; }})()");
        JsonElement state = default;
        await Until(async () =>
        {
            state = await OverlayProbe(view, kind);
            return state.ValueKind == JsonValueKind.Object && !state.TryGetProperty("missing", out _) &&
                state.GetProperty("panelHit").GetBoolean() && state.GetProperty("scrimHit").GetBoolean() &&
                state.GetProperty("editorInert").GetBoolean() && state.GetProperty("editorPointer").GetString() == "none" &&
                state.GetProperty("editorZ").GetString() == "0" && state.GetProperty("panelZ").GetString() == "20" &&
                state.GetProperty("scrimZ").GetString() == "10" && !state.GetProperty("selected").GetBoolean() &&
                !state.GetProperty("flyoutVisible").GetBoolean();
        }, $"{label}: Blockly remained above or interactive through the {kind} overlay");
        await Capture(view,directory,label);
        await Click(view, "(() => { document.querySelector('[data-studio-overlay=\"scrim\"]')?.click(); return true; })()");
        await Until(async () => (await Eval(view, "(() => {const e=document.querySelector('[data-studio-editor]'),f=document.querySelector('.blocklyFlyout');return !e.hasAttribute('inert') && getComputedStyle(e).pointerEvents!=='none' && window.__auraCanvasBeforeOverlay===document.querySelector('.blocklySvg') && (!!document.querySelector('.blocklyToolbox') || (!!f && getComputedStyle(f).display!=='none'))})()")).GetBoolean(),
            $"{label}: closing overlay did not restore the same Blockly canvas");
        return state;
    }

    internal static async Task RunAsync(MainWindow window)
    {
        var directory = Path.GetFullPath(Environment.GetEnvironmentVariable("AURA_STUDIO_VALIDATION_DIR")!);
        var results = new List<object>(); string? error = null;
        try
        {
            Directory.CreateDirectory(directory);
            await DaemonSupervisor.Instance.EnsureStartedAsync();
            var status = await AuraControlClient.Instance.GetRuntimeStatusAsync();
            if (!status.IsOnline || !status.IsDryRun)
                throw new InvalidOperationException("Studio validation requires an isolated dry-run core");
            await Until(async () => { await Task.Yield(); return window.Content.XamlRoot != null; }, "XAML root did not load");
            window.NavigateTo(typeof(StudioPage));
            var page = (StudioPage)MainWindow.CurrentNavFrame!.Content;
            var view = (WebView2)page.FindName("StudioWebView");
            await Until(async () => {
                if (view.CoreWebView2 == null) return false;
                try { return (await Snapshot(view)).GetProperty("editor").GetBoolean(); }
                catch { return false; }
            }, "Embedded Effect Studio did not render", 25);
            var url = view.Source;
            if (url != EmbeddedStudioNavigation.InitialUrl("studio", page.ActualTheme == ElementTheme.Light ? "light" : "dark"))
                throw new InvalidOperationException("Studio URL differs from the embedded contract: " + url);
            var initial = await Snapshot(view);
            if (initial.GetProperty("host").GetString() != "winui" || initial.GetProperty("workspace").GetString() != "effect" || initial.GetProperty("sidebar").GetBoolean())
                throw new InvalidOperationException("Embedded Studio contains the standalone product shell");
            results.Add(new { phase = "initial", state = initial });
            await Capture(view, directory, "embedded-effect");

            // Exercise the real React creation dialog against this isolated config.
            await Eval(view, """
                (() => { window.__createSaves=[]; const original=window.fetch;
                  window.fetch=async (...args) => { const response=await original(...args);
                    if (String(args[0])==='/api/config' && args[1]?.method==='POST') window.__createSaves.push(response.status);
                    return response; }; return true; })()
                """);
            async Task OpenCreationDialog()
            {
                await Click(view, "(() => {const button=document.querySelector('button[title=\"新建光效草稿\"]');button?.click();return !!button})()");
                await Until(async () => (await Eval(view, "!!document.querySelector('[role=dialog][aria-label=\"新建光效草稿\"]')")).GetBoolean(), "Creation dialog did not open");
            }
            await OpenCreationDialog();
            if (!(await Eval(view, "(() => {const d=document.querySelector('[role=dialog]');return [...d.querySelectorAll('button')].filter(b=>b.textContent.trim()==='取消').length===1 && !!d.querySelector('input[type=radio][value=continuous]:checked') && !d.querySelector('input[type=number]')})()")).GetBoolean())
                throw new InvalidOperationException("Creation dialog has duplicate Cancel or incorrect default lifecycle");
            await Click(view, "(() => {const r=document.querySelector('[role=dialog] input[type=radio][value=one_shot]');r?.click();return !!r})()");
            await Until(async () => (await Eval(view, "document.querySelector('[role=dialog] input[type=number]')?.value==='300'")).GetBoolean(), "One-shot fade control did not appear");
            await Eval(view, "window.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true})); true");
            await Until(async () => !(await Eval(view, "!!document.querySelector('[role=dialog]')")).GetBoolean(), "Escape did not cancel creation");
            await OpenCreationDialog();
            await Click(view, "(() => {const x=document.querySelector('[role=dialog] button[aria-label=\"关闭新建光效弹窗\"]');x?.click();return !!x})()");
            await Until(async () => !(await Eval(view, "!!document.querySelector('[role=dialog]')")).GetBoolean(), "Close button did not cancel creation");
            await OpenCreationDialog();
            await Click(view, "(() => {const mask=document.querySelector('[role=dialog]')?.parentElement;mask?.dispatchEvent(new MouseEvent('mousedown',{bubbles:true}));return !!mask})()");
            await Until(async () => !(await Eval(view, "!!document.querySelector('[role=dialog]')")).GetBoolean(), "Backdrop did not cancel creation");
            if (!(await Eval(view, "window.__createSaves.length===0")).GetBoolean()) throw new InvalidOperationException("Cancelled creation saved a draft");
            results.Add(new { phase = "creation cancel methods" });
            async Task CreateDraft(string name, string mode)
            {
                await OpenCreationDialog();
                await Eval(view, """
                    (() => {const d=document.querySelector('[role=dialog]'), input=d.querySelector('input[type=text]');
                      const setter=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set;
                      setter.call(input,'_DRAFT_NAME_');input.dispatchEvent(new Event('input',{bubbles:true}));
                      d.querySelector('input[type=radio][value=_DRAFT_MODE_]').click();return true;})()
                    """.Replace("_DRAFT_NAME_", name).Replace("_DRAFT_MODE_", mode));
                await Until(async () => (await Eval(view, "!!document.querySelector('[role=dialog] button[type=submit]:not(:disabled)')")).GetBoolean(), "Creation form did not accept the draft name");
                await Click(view, "(() => {const b=document.querySelector('[role=dialog] button[type=submit]');b?.click();return !!b})()");
                await Until(async () => !(await Eval(view, "!!document.querySelector('[role=dialog]')")).GetBoolean(), "Draft was not created");
            }
            bool PublicationSaved(string name, string mode, int fade)
            {
                var dataRoot = Environment.GetEnvironmentVariable("AURA_DATA_ROOT");
                if (string.IsNullOrWhiteSpace(dataRoot)) return false;
                using var document = JsonDocument.Parse(File.ReadAllText(Path.Combine(dataRoot, "config.json")));
                if (!document.RootElement.GetProperty("blockly_effects").TryGetProperty(name, out var effect)) return false;
                var publication = effect.GetProperty("publication");
                return publication.GetProperty("mode").GetString() == mode && publication.GetProperty("fade_out_ms").GetInt32() == fade;
            }
            await CreateDraft("validation_continuous", "continuous");
            await Until(() => Task.FromResult(PublicationSaved("validation_continuous", "continuous", 0)), "Continuous creation did not persist publication metadata");
            await CreateDraft("validation_once", "one_shot");
            await Until(() => Task.FromResult(PublicationSaved("validation_once", "one_shot", 300)), "One-shot creation did not persist publication metadata");
            if (!(await Eval(view, "document.querySelector('[aria-label=\"当前光效生命周期\"]')?.innerText.includes('单次光效 · 淡出 300 ms') ?? false")).GetBoolean())
                throw new InvalidOperationException("One-shot lifecycle badge is not visible after creation");
            results.Add(new { phase = "creation publication roundtrip" });
            await Click(view, "(() => {document.querySelector('[data-studio-overlay=\"scrim\"]')?.click();return true})()");

            // A selected Blockly toolbox previously escaped above the compact works panel.
            var scaleAtStart = window.Content.XamlRoot.RasterizationScale;
            window.AppWindow.Resize(new((int)(600 * scaleAtStart), (int)(500 * scaleAtStart)));
            await Task.Delay(250);
            results.Add(new { phase = "effect works overlay", state = await CheckOverlay(view,"works",directory,"effect-works-600") });
            results.Add(new { phase = "effect inspector overlay", state = await CheckOverlay(view,"inspector",directory,"effect-inspector-600") });

            // Pick a visible preset through the real UI; its workspace remains unsaved during native navigation.
            await Click(view, """
                (() => { document.querySelector('button[aria-expanded="false"]')?.click(); const button = [...document.querySelectorAll('button')].find(x => x.textContent?.includes('低血量警戒')); button?.click(); return !!button; })()
                """);
            await Until(async () => (await Eval(view, "document.body.innerText.includes('template_low_health_warning')")).GetBoolean(), "Effect draft did not change");
            await Eval(view, "window.__auraStudioIdentity = 'retained-editor'; true");
            var originalCore = view.CoreWebView2;
            var browserProcess = originalCore.BrowserProcessId;
            int navigations = 0;
            void CountNavigation(CoreWebView2 sender, CoreWebView2NavigationStartingEventArgs args) => navigations++;
            originalCore.NavigationStarting += CountNavigation;
            try
            {
                for (int i = 0; i < 50; i++)
                {
                    window.NavigateTo(typeof(HomePage));
                    await Task.Delay(25);
                    window.NavigateTo(typeof(StudioPage));
                    await Task.Delay(25);
                    if (!ReferenceEquals(page, MainWindow.CurrentNavFrame.Content) ||
                        !ReferenceEquals(originalCore, view.CoreWebView2) || view.CoreWebView2.BrowserProcessId != browserProcess)
                        throw new InvalidOperationException("Studio WebView was recreated during native navigation");
                }
                var kept = await Eval(view, "window.__auraStudioIdentity === 'retained-editor' && document.body.innerText.includes('template_low_health_warning')");
                if (!kept.GetBoolean() || navigations != 0) throw new InvalidOperationException("Navigation reloaded or lost the unsaved Blockly editor");
                results.Add(new { phase = "50 reentries", navigations, browserProcess });
            }
            finally { originalCore.NavigationStarting -= CountNavigation; }

            // Exercise the existing HTTP save paths in the isolated dry-run package.
            await Eval(view, """
                (() => { window.__studioSaves=[]; const original=window.fetch;
                  window.fetch=async (...args) => { const response=await original(...args);
                    const url=String(args[0]); const method=args[1]?.method ?? 'GET';
                    if ((url==='/api/config' && method==='POST') || (url==='/api/automation/v2/rules' && method==='PUT'))
                      window.__studioSaves.push({url,method,status:response.status});
                    return response; }; return true; })()
                """);
            await Click(view, """
                (() => { const toggle=[...document.querySelectorAll('button')].find(x=>x.textContent?.includes('作品操作 · 保存与发布')); toggle?.click();
                  const save=[...document.querySelectorAll('button')].find(x=>x.textContent?.trim()==='保存草稿'); save?.click(); return !!save; })()
                """);
            await Until(async () => (await Eval(view, "window.__studioSaves?.some(x=>x.url==='/api/config' && x.status===200) ?? false")).GetBoolean(), "Effect draft did not save through the existing config API");
            results.Add(new { phase = "effect draft HTTP save" });

            // Studio switches workspaces in React without a top-level WebView navigation.
            await Click(view, """
                (() => { document.querySelector('button[aria-label="作品列表"]')?.click(); const item=[...document.querySelectorAll('[class*="cursor-pointer"]')].find(x=>x.textContent?.includes('主联动规则编排')); item?.click(); return !!item; })()
                """);
            await Until(async () => { var state = await Snapshot(view); return state.GetProperty("workspace").GetString() == "orchestration" && state.GetProperty("editor").GetBoolean(); }, "Automation workspace did not open");
            var automation = await Snapshot(view);
            if (!automation.GetProperty("editor").GetBoolean() || (await Eval(view, "window.__auraStudioIdentity")).GetString() != "retained-editor")
                throw new InvalidOperationException("Effect/Automation switch reloaded WebView");
            results.Add(new { phase = "automation switch", state = automation });
            await Capture(view, directory, "embedded-automation");
            await Click(view, """
                (() => { const save=[...document.querySelectorAll('button')].find(x=>x.textContent?.trim()==='保存并应用');
                  if (!save || save.disabled) return false; save.click(); return true; })()
                """);
            await Until(async () => (await Eval(view, "window.__studioSaves?.some(x=>x.url==='/api/automation/v2/rules' && x.status===200) ?? false")).GetBoolean(), "Automation did not save through its daemon API");
            await Until(async () => (await Eval(view, "(() => {const save=[...document.querySelectorAll('button')].find(x=>x.textContent?.trim()==='保存并应用'); return !!save && !save.disabled && !!document.querySelector('.blocklySvg')})()")).GetBoolean(), "Automation editor did not recover after save");
            results.Add(new { phase = "automation daemon HTTP save" });
            foreach (var theme in new[] { ElementTheme.Dark, ElementTheme.Light })
            {
                ((FrameworkElement)window.Content).RequestedTheme = theme;
                var expected = theme == ElementTheme.Dark ? "dark" : "light";
                await Until(async () => (await Snapshot(view)).GetProperty("theme").GetString() == expected, "Automation theme did not follow WinUI");
                var scale = window.Content.XamlRoot.RasterizationScale;
                foreach (var size in new[] { (600, 500), (800, 600), (1060, 720), (0, 0) })
                {
                    var presenter = (OverlappedPresenter)window.AppWindow.Presenter;
                    if (size.Item1 == 0) presenter.Maximize();
                    else { presenter.Restore(); window.AppWindow.Resize(new((int)(size.Item1 * scale), (int)(size.Item2 * scale))); }
                    await Task.Delay(250);
                    var label = $"automation-{expected}-{(size.Item1 == 0 ? "maximized" : $"{size.Item1}x{size.Item2}")}";
                    var state = await Snapshot(view);
                    if (state.GetProperty("overflow").GetDouble() > 1 || state.GetProperty("canvasWidth").GetDouble() < 240)
                        throw new InvalidOperationException("Automation editor is clipped or overflows at " + label);
                    await Capture(view,directory,label);
                    results.Add(new { phase = label, state, scale });
                    if (size.Item1 is 600 or 800)
                    {
                        results.Add(new { phase = label + " works", state = await CheckOverlay(view,"works",directory,label + "-works") });
                        results.Add(new { phase = label + " inspector", state = await CheckOverlay(view,"inspector",directory,label + "-inspector") });
                    }
                }
            }
            await Click(view, """
                (() => { document.querySelector('button[aria-label="作品列表"]')?.click(); const item=[...document.querySelectorAll('[class*="cursor-pointer"]')].find(x=>x.textContent?.includes('validation_effect')); item?.click(); return !!item; })()
                """);
            await Until(async () => (await Snapshot(view)).GetProperty("workspace").GetString() == "effect", "Effect workspace did not reopen");

            // Theme changes are a single display message and must not reload the editor.
            foreach (var theme in new[] { ElementTheme.Dark, ElementTheme.Light })
            {
                ((FrameworkElement)window.Content).RequestedTheme = theme;
                var expected = theme == ElementTheme.Dark ? "dark" : "light";
                await Until(async () => (await Snapshot(view)).GetProperty("theme").GetString() == expected, "WinUI theme was not reflected in Studio");
                var scale = window.Content.XamlRoot.RasterizationScale;
                foreach (var size in new[] { (600, 500), (800, 600), (1060, 720), (0, 0) })
                {
                    var presenter = (OverlappedPresenter)window.AppWindow.Presenter;
                    if (size.Item1 == 0) presenter.Maximize();
                    else { presenter.Restore(); window.AppWindow.Resize(new((int)(size.Item1 * scale), (int)(size.Item2 * scale))); }
                    await Task.Delay(250);
                    var state = await Snapshot(view);
                    if (state.GetProperty("overflow").GetDouble() > 1 || state.GetProperty("canvasWidth").GetDouble() < 240)
                        throw new InvalidOperationException("Embedded editor is clipped or overflows at " + size);
                    var label = $"{expected}-{(size.Item1 == 0 ? "maximized" : $"{size.Item1}x{size.Item2}")}";
                    await Capture(view, directory, label);
                    results.Add(new { phase = label, state, scale });
                    if (size.Item1 is 600 or 800)
                    {
                        results.Add(new { phase = label + " works", state = await CheckOverlay(view,"works",directory,label + "-works") });
                        results.Add(new { phase = label + " inspector", state = await CheckOverlay(view,"inspector",directory,label + "-inspector") });
                    }
                }
            }

            // Browser entry remains the full product. Use the same test WebView/profile after draft checks.
            await Eval(view, "localStorage.setItem('aura-theme','light'); true");
            view.Source = new Uri("http://127.0.0.1:19898/");
            await Until(async () => (await Snapshot(view)).GetProperty("host").GetString() == "standalone", "Standalone Web shell did not render");
            var standalone = await Snapshot(view);
            if (!standalone.GetProperty("sidebar").GetBoolean() || standalone.GetProperty("theme").GetString() != "light")
                throw new InvalidOperationException("Standalone navigation or stored theme regressed");
            if (!(await Eval(view, "!document.querySelector('[data-studio-overlay=\"scrim\"]') && !document.querySelector('[data-studio-editor]')")).GetBoolean())
                throw new InvalidOperationException("Standalone shell inherited embedded overlay state");
            results.Add(new { phase = "standalone", state = standalone });
            await Capture(view, directory, "standalone-web");

            view.Source = new Uri("http://127.0.0.1:19898/?tab=studio");
            await Until(async () => { var state = await Snapshot(view); return state.GetProperty("sidebar").GetBoolean() && state.GetProperty("editor").GetBoolean(); },
                "Standalone Studio did not render inside the full Web shell");
            if (!(await Eval(view, "(() => {const e=document.querySelector('[data-studio-editor]');return !!e && !e.hasAttribute('inert') && getComputedStyle(e).zIndex==='auto' && !document.querySelector('[data-studio-overlay=\"scrim\"]')})()")).GetBoolean())
                throw new InvalidOperationException("Standalone Studio inherited the embedded stacking boundary");
            results.Add(new { phase = "standalone studio", state = await Snapshot(view) });
            await Capture(view,directory,"standalone-studio");

            view.Source = EmbeddedStudioNavigation.InitialUrl("automation", "dark");
            await Until(async () => { var state = await Snapshot(view); return state.GetProperty("host").GetString() == "winui" && state.GetProperty("workspace").GetString() == "orchestration" && state.GetProperty("editor").GetBoolean(); }, "Embedded Automation initial URL did not render");
            results.Add(new { phase = "automation initial", state = await Snapshot(view) });
        }
        catch (Exception ex)
        {
            error = ex.ToString();
            if (MainWindow.CurrentNavFrame?.Content is StudioPage page)
            {
                var view = page.FindName("StudioWebView") as WebView2;
                error += $"\nStudio: core={DaemonSupervisor.Instance.CoreReady}, web={DaemonSupervisor.Instance.StudioWebReady}, " +
                    $"source={view?.Source}, webview_initialized={view?.CoreWebView2 != null}, " +
                    $"loading={((TextBlock)page.FindName("LoadingStatusText")).Text}, " +
                    $"banner={((InfoBar)page.FindName("StudioInfoBar")).Title}";
                if (view?.CoreWebView2 != null)
                {
                    try { error += "\nDOM: " + (await Eval(view, "({url:location.href,body:document.body?.innerText?.slice(0,800),root:document.querySelector('[data-aura-host]')?.outerHTML?.slice(0,200)})")).ToString(); }
                    catch (Exception diagnostic) { error += "\nDOM probe: " + diagnostic.Message; }
                    try { await Capture(view,directory,"failed-studio"); } catch { }
                }
            }
        }
        finally
        {
            File.WriteAllText(Path.Combine(directory, "studio-results.json"), JsonSerializer.Serialize(new { error, results }, new JsonSerializerOptions { WriteIndented = true }));
            await App.RequestExit();
        }
    }
}
