using System.Text.Json;
using Aura_WinUI.Pages;
using Aura_WinUI.Services;
using Microsoft.UI.Xaml.Controls;
using static Aura_WinUI.Validation.StudioAiValidation;

namespace Aura_WinUI.Validation;

// Explicit owned desktop fixture. No test commands are accepted by production Studio.
internal static class StudioToolingValidation
{
    internal static bool Requested => Environment.GetCommandLineArgs().Contains("--validate-studio-tooling") &&
        Environment.GetEnvironmentVariable("AURA_MAGNETIC_VALIDATION_OFFLINE") == "1" &&
        !string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("AURA_STUDIO_TOOLING_DIR")) &&
        !string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("AURA_DATA_ROOT")) &&
        File.Exists(Path.Combine(RuntimeLayoutResolver.DataRoot, ".aura-studio-tooling-fixture"));
    internal static async Task RunAsync(MainWindow window)
    {
        var directory = Path.GetFullPath(Environment.GetEnvironmentVariable("AURA_STUDIO_TOOLING_DIR")!);
        var stage = Environment.GetEnvironmentVariable("AURA_STUDIO_TOOLING_STAGE");
        var results = new List<object>(); string? error = null; string? target = null;
        void Record(string phase, object? evidence = null) => results.Add(new { phase, evidence });
        void Write() => File.WriteAllText(Path.Combine(directory, stage + "-results.json"), JsonSerializer.Serialize(new { error, results }, new JsonSerializerOptions { WriteIndented = true }));
        try {
            Directory.CreateDirectory(directory);
            if (stage is not ("crash" or "recover" or "clean")) throw new InvalidOperationException("Unknown fixture stage");
            var runtime = await AuraControlClient.Instance.GetRuntimeStatusAsync();
            if (!runtime.IsOnline || !runtime.IsDryRun) throw new InvalidOperationException("Fixture requires an existing isolated dry-run daemon");
            await DaemonSupervisor.Instance.RefreshAsync();
            var address = Environment.GetEnvironmentVariable("AURA_STUDIO_AI_MOCK_URL")!;
            if (!Uri.TryCreate(address, UriKind.Absolute, out var uri) || !uri.IsLoopback || uri.Scheme != "http") throw new InvalidOperationException("Local mock required");
            var scale = window.Content.XamlRoot.RasterizationScale;
            window.AppWindow.Resize(new((int)(1520 * scale), (int)(800 * scale)));
            window.NavigateTo(typeof(StudioPage)); var page = (StudioPage)MainWindow.CurrentNavFrame!.Content;
            T Find<T>(string name) where T : Microsoft.UI.Xaml.FrameworkElement => (T)page.FindName(name);
            var view = Find<WebView2>("StudioWebView");
            await Until(async () => view.CoreWebView2 != null && (await Eval(view, "!!document.querySelector('[data-studio-storage]') && !!document.querySelector('.blocklySvg')")).GetBoolean(), "Studio editor/storage surface not loaded");
            await view.EnsureCoreWebView2Async();
            await Until(() => Task.FromResult(Find<TextBlock>("ProjectStatus").Text.Contains("fixture")), "Fixture project not loaded");
            string Config() => File.ReadAllText(Path.Combine(RuntimeLayoutResolver.DataRoot, "config.json"));
            string Journal() => new StudioDraftStore(RuntimeLayoutResolver.DataRoot).JournalPath("fixture");
            double? Period(JsonElement node) {
                if (node.ValueKind == JsonValueKind.Object) {
                    if (node.TryGetProperty("PERIOD_SEC", out var p)) return (p.TryGetProperty("shadow", out var shadow) ? shadow : p.GetProperty("block")).GetProperty("fields").GetProperty("NUM").GetDouble();
                    foreach (var child in node.EnumerateObject()) { var value = Period(child.Value); if (value != null) return value; }
                } else if (node.ValueKind == JsonValueKind.Array) foreach (var p in node.EnumerateArray()) { var value = Period(p); if (value != null) return value; }
                return null;
            }
            double SavedPeriod() { using var doc = JsonDocument.Parse(Config()); return Period(doc.RootElement.GetProperty("blockly_effects").GetProperty("fixture").GetProperty("blockly_json")) ?? -1; }
            JsonElement JournalData() { using var doc = JsonDocument.Parse(File.ReadAllText(Journal())); return doc.RootElement.Clone(); }
            void NoPublishedChange() {
                using var doc = JsonDocument.Parse(Config());
                if (doc.RootElement.GetProperty("blockly_effects").GetProperty("fixture").TryGetProperty("applied_plugin_name", out _)) throw new InvalidOperationException("Draft operation published an effect");
            }
            var commands = Find<CommandBar>("StudioCommands");
            void ShowAi() => InvokeControl(commands.SecondaryCommands.OfType<AppBarButton>().First(b => b.Label == "AI 助手"));
            void Bench() => InvokeControl(commands.SecondaryCommands.OfType<AppBarButton>().First(b => b.Tag as string == "bench"));
            async Task Click(string selector) => await Eval(view, $"(()=>{{const e=document.querySelector({JsonSerializer.Serialize(selector)});if(!e||e.disabled)throw Error('Unavailable control');e.click();return true;}})()");
            async Task ProposeApply() {
                ShowAi(); Find<TextBox>("AiPrompt").Text = "把当前效果速度提高一点";
                Invoke(Find<Button>("AiSend")); await Until(() => Task.FromResult(Find<Button>("AiApply").IsEnabled), "AI proposal not validated: " + Find<TextBlock>("AiStatus").Text);
                NoPublishedChange(); Invoke(Find<Button>("AiApply"));
                await Until(() => Task.FromResult(Find<Button>("AiUndo").IsEnabled), "AI apply failed: " + Find<TextBlock>("AiStatus").Text);
            }
            Record("open isolated Studio and configured preset", new { runtime.IsDryRun, stage });
            if (stage == "crash") {
                if (StudioPolishValidation.Requested) await StudioPolishValidation.RunAsync(window, page, view, directory, (phase, evidence) => Record(phase, evidence));
                await Until(async () => (await Eval(view, "document.querySelector('[data-autosave-status]')?.textContent.includes('自动')||false")).GetBoolean(), "Storage not ready");
                Bench(); await Until(async () => (await Eval(view, "!!document.querySelector('[data-studio-bench]')")).GetBoolean(), "Bench not opened");
                await Click("[data-bench-play]"); await Until(async () => (await Eval(view, "document.querySelector('[data-studio-bench]').dataset.completed==='true'")).GetBoolean(), "Key tap not completed");
                Record("key tap scenario completes on existing effect visualization");
                await Click("[data-bench-reset]"); await Click("[data-bench-play]"); await Click("[data-bench-pause]");
                var paused = await Eval(view, "document.querySelector('[data-studio-bench]').dataset.time");
                await Click("[data-bench-step]"); var stepped = await Eval(view, "document.querySelector('[data-studio-bench]').dataset.time");
                await Click("[data-bench-step]"); var advanced = await Eval(view, "document.querySelector('[data-studio-bench]').dataset.time");
                if (advanced.GetString() == stepped.GetString()) throw new InvalidOperationException("Step did not advance deterministic clock");
                await Click("[data-bench-reset]"); Record("pause step reset", new { paused, stepped, advanced });
                // Select the actual React control through the browser's native value setter.
                Find<ComboBox>("RecentProjects").SelectedItem = "fixture_health";
                await Until(() => Task.FromResult(Find<TextBlock>("ProjectStatus").Text.StartsWith("fixture_health ")), "Health effect not selected");
                Bench(); await Until(async () => (await Eval(view, "!!document.querySelector('[data-studio-bench]')")).GetBoolean(), "Health bench not opened");
                await Eval(view, "(()=>{const e=document.querySelector('[aria-label=\"测试场景\"]');Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype,'value').set.call(e,'CS2 生命值变化');e.dispatchEvent(new Event('change',{bubbles:true}));return true;})()");
                await Click("[data-bench-play]"); await Until(async () => (await Eval(view, "document.querySelector('[data-studio-bench]').dataset.completed==='true' && document.querySelector('[data-bench-state]').textContent.includes('10')")).GetBoolean(), "Health scenario did not complete");
                Record("CS2 health scenario 100 to 50 to 10");
                Find<ComboBox>("RecentProjects").SelectedItem = "fixture";
                await Until(() => Task.FromResult(Find<TextBlock>("ProjectStatus").Text.StartsWith("fixture ")), "Original effect not selected");
                // Edit the real Blockly numeric field using browser input, without
                // exposing an extra production command or touching host files.
                var point = await Eval(view, "(()=>{const e=[...document.querySelectorAll('.blocklySvg .blocklyBlockCanvas .blocklyText')].find(e=>e.textContent==='3'&&e.getBoundingClientRect().width>0);const r=e.getBoundingClientRect();const hit=document.elementFromPoint(r.x+r.width/2,r.y+r.height/2);return {x:r.x+r.width/2,y:r.y+r.height/2,width:innerWidth,height:innerHeight,hit:hit?.tagName,hit_text:hit?.textContent.slice(0,80)};})()");
                Record("numeric field location", point);
                var beforeEdit = await (await Windows.Storage.StorageFolder.GetFolderFromPathAsync(directory)).CreateFileAsync("editor-before-edit.png", Windows.Storage.CreationCollisionOption.ReplaceExisting);
                using (var stream = await beforeEdit.OpenAsync(Windows.Storage.FileAccessMode.ReadWrite)) await view.CoreWebView2.CapturePreviewAsync(Microsoft.Web.WebView2.Core.CoreWebView2CapturePreviewImageFormat.Png, stream).AsTask().WaitAsync(TimeSpan.FromSeconds(10));
                // Blockly 13 binds pointer gestures on the SVG field group.
                // Dispatch the UI gesture to the observed field. A wide Blockly
                // graph can extend behind the clipped adjacent preview pane;
                // document hit-testing would select that pane instead.
                await Eval(view, "(()=>{const e=[...document.querySelectorAll('.blocklySvg .blocklyBlockCanvas .blocklyText')].find(e=>e.textContent==='3'&&e.getBoundingClientRect().width>0).closest('.blocklyField');const r=e.getBoundingClientRect();for(const type of ['pointerdown','pointerup'])e.dispatchEvent(new PointerEvent(type,{bubbles:true,cancelable:true,composed:true,pointerId:1,pointerType:'mouse',isPrimary:true,button:0,buttons:type==='pointerdown'?1:0,clientX:r.x+r.width/2,clientY:r.y+r.height/2}));return true;})()");
                await Until(async () => (await Eval(view, "!!document.querySelector('.blocklyHtmlInput')")).GetBoolean(), "Numeric field editor did not open");
                await Eval(view, "(()=>{const e=document.querySelector('.blocklyHtmlInput');e.value='4';e.dispatchEvent(new Event('input',{bubbles:true}));e.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true}));return true;})()");
                await Until(() => Task.FromResult(File.Exists(Journal()) && JournalData().GetProperty("recovery").ValueKind != JsonValueKind.Null), "Autosave did not persist dirty draft");
                var journal = JournalData();
                if (Period(journal.GetProperty("recovery").GetProperty("draft").GetProperty("json")) != 4 || SavedPeriod() != 3 || journal.GetProperty("snapshots").GetArrayLength() != 0) throw new InvalidOperationException("Manual edit/autosave boundary");
                NoPublishedChange(); Record("real Blockly edit, atomic autosave; durable project unchanged", new { revision = journal.GetProperty("revision").GetInt64() }); Write();
                // Intentionally skip normal App shutdown ONLY for this explicit owned crash fixture.
                Environment.Exit(23);
            } else if (stage == "recover") {
                await Until(async () => (await Eval(view, "!!document.querySelector('[data-recovery-offer]')")).GetBoolean(), "Crash recovery was not offered");
                if (SavedPeriod() != 3) throw new InvalidOperationException("Recovery automatically replaced durable draft");
                await Click("[data-recovery-restore]"); await Until(async () => !(await Eval(view, "!!document.querySelector('[data-recovery-offer]')")).GetBoolean(), "Restore did not dismiss offer");
                Record("restart offers recovery; explicit restore remains draft"); NoPublishedChange();
                var settings = new StudioLlmSettings(address, "fixture", 5, true); target = StudioLlmSettingsStore.Target(settings);
                new StudioLlmSettingsStore(Path.Combine(RuntimeLayoutResolver.DataRoot, "studio-llm-settings.json"), new StudioCredentialStore()).Save(settings, "fixture-only-key");
                await ProposeApply();
                await Until(() => Task.FromResult(JournalData().GetProperty("snapshots").GetArrayLength() == 1), "Pre-AI snapshot not persisted");
                if (Period(JournalData().GetProperty("snapshots")[0].GetProperty("draft").GetProperty("json")) != 4 || SavedPeriod() != 3) throw new InvalidOperationException("AI snapshot did not capture restored unsaved draft");
                Record("mock AI proposal and explicit Apply after crash recovery; pre-AI snapshot contains restored draft");
                ShowAi(); // Collapse the native AI panel to expose the retained restore controls.
                // Restore the actual pre-AI snapshot through the retained editor's UI.
                await Eval(view, "(()=>{const e=document.querySelector('[data-studio-snapshots]');const v=e.options[1].value;Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype,'value').set.call(e,v);e.dispatchEvent(new Event('change',{bubbles:true}));return true;})()");
                await Click("[data-snapshot-restore]");
                await Until(async () => (await Eval(view, "[...document.querySelectorAll('.blocklyText')].some(e=>e.textContent==='4')")).GetBoolean(), "Pre-AI Blockly graph was not restored");
                await Until(() => Task.FromResult(JournalData().GetProperty("recovery").ValueKind != JsonValueKind.Null && Period(JournalData().GetProperty("recovery").GetProperty("draft").GetProperty("json")) == 4), "Snapshot restore did not autosave pre-AI draft");
                await Until(() => Task.FromResult(Find<AppBarButton>("SaveCommand").IsEnabled), "Save control remained busy after restore");
                InvokeControl(Find<AppBarButton>("SaveCommand"));
                await Until(() => Task.FromResult(SavedPeriod() == 4 && JournalData().GetProperty("recovery").ValueKind == JsonValueKind.Null), "Clean save did not clear recovery");
                NoPublishedChange(); Record("pre-AI snapshot restored to draft, explicit clean save");
                await Eval(view, "(()=>{window.__toolingCalls=[];const original=window.fetch;window.fetch=async(...a)=>{const u=String(a[0]);window.__toolingCalls.push(u);if(u==='/api/compile_effect')return new Response(JSON.stringify({success:false,stage:'compile_failed',message:'isolated publish fixture'}),{status:400,headers:{'Content-Type':'application/json'}});return original(...a);};return true;})()");
                await Until(() => Task.FromResult(Find<AppBarButton>("PublishCommand").IsEnabled), "Publish control remained busy after Save");
                InvokeControl(Find<AppBarButton>("PublishCommand"));
                await Until(() => Task.FromResult(JournalData().GetProperty("snapshots").EnumerateArray().Any(s => s.GetProperty("reason").GetString() == "before_publish")), "Publish checkpoint missing");
                await Until(() => Task.FromResult(Find<TextBlock>("ValidationSummary").Text.Contains("失败")), "Mock publish build did not fail");
                NoPublishedChange(); var calls = await Eval(view, "window.__toolingCalls");
                if (calls.EnumerateArray().Any(c => c.GetString() == "/api/reload_plugin")) throw new InvalidOperationException("Unexpected publication");
                Record("Publish checkpoint before failing local compiler fixture; no reload", calls);
                await Eval(view, "[...document.querySelectorAll('button')].find(e=>e.textContent.trim()==='关闭')?.click()");
                Bench(); await Until(async () => (await Eval(view, "!!document.querySelector('[data-studio-bench]')")).GetBoolean(), "Bench capture surface");
                await Until(async () => (await Eval(view, "(()=>{const b=document.querySelector('[data-studio-bench]');const r=b.getBoundingClientRect();const keys=[...b.querySelectorAll('button[aria-label$=\" 按键\"]')];return keys.length===68&&keys.every(k=>{const x=k.getBoundingClientRect();return x.width>0&&x.left>=r.left&&x.right<=r.right;});})()")).GetBoolean(), "Bench keyboard did not settle within its horizontal viewport");
                var file = await (await Windows.Storage.StorageFolder.GetFolderFromPathAsync(directory)).CreateFileAsync("test-bench.png", Windows.Storage.CreationCollisionOption.ReplaceExisting);
                using (var stream = await file.OpenAsync(Windows.Storage.FileAccessMode.ReadWrite))
                    await view.CoreWebView2.CapturePreviewAsync(Microsoft.Web.WebView2.Core.CoreWebView2CapturePreviewImageFormat.Png, stream).AsTask().WaitAsync(TimeSpan.FromSeconds(10));
                File.WriteAllText(Path.Combine(directory, "capture-ready.json"), JsonSerializer.Serialize(new { process_id = Environment.ProcessId }));
                if (Environment.GetEnvironmentVariable("AURA_STUDIO_AI_CAPTURE") == "1") await Task.Delay(15000);
            } else {
                await Until(async () => (await Eval(view, "document.querySelector('[data-autosave-status]')?.textContent.includes('自动')||false")).GetBoolean(), "Storage not ready after clean restart");
                if ((await Eval(view, "!!document.querySelector('[data-recovery-offer]')")).GetBoolean()) throw new InvalidOperationException("False recovery after clean save/shutdown");
                NoPublishedChange(); Record("clean shutdown/restart has no false recovery; snapshots retained", new { snapshots = JournalData().GetProperty("snapshots").GetArrayLength() });
                var recent = Path.Combine(RuntimeLayoutResolver.DataRoot, "Studio", "recent.json");
                using (var doc = JsonDocument.Parse(File.ReadAllText(recent))) {
                    if (!doc.RootElement.GetProperty("projects").EnumerateArray().Any(p => p.GetString() == "fixture_health")) throw new InvalidOperationException("Recent project did not survive restart");
                }
                InvokeControl(Find<AppBarButton>("ClearRecentCommand"));
                using (var doc = JsonDocument.Parse(File.ReadAllText(recent))) if (doc.RootElement.GetProperty("projects").GetArrayLength() != 0) throw new InvalidOperationException("Clear recent failed");
                if (Find<ComboBox>("RecentProjects").Items.Count != (StudioPolishValidation.Requested ? 3 : 2)) throw new InvalidOperationException("Clear recent removed project catalog");
                Record("recent logical IDs persist through restart; Clear recent preserves projects");
            }
        } catch (Exception ex) { error = ex.GetType().Name + ": " + StudioLlmRedaction.Filter(ex.Message); }
        finally {
            if (target != null) new StudioCredentialStore().Remove(target);
            Directory.CreateDirectory(directory); Write(); await App.RequestExit();
        }
    }
}
