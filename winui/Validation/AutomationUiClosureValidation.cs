using System.Net;
using System.Reflection;
using System.Text;
using System.Text.Json;
using Aura_WinUI.Pages;
using Aura_WinUI.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace Aura_WinUI.Validation;

// Opt-in native control interaction with an in-process HTTP fake only. No HID,
// daemon launch, game, keyboard input injection, screenshot or visual acceptance.
internal static class AutomationUiClosureValidation
{
    internal static bool Requested => Environment.GetCommandLineArgs().Contains("--validate-automation-ui-closure") &&
        Environment.GetEnvironmentVariable("AURA_MAGNETIC_VALIDATION_OFFLINE") == "1" &&
        !string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("AURA_UI_VALIDATION_DIR"));
    private sealed class FakeApi : HttpMessageHandler
    {
        internal readonly List<DeviceProfile> Profiles = [new() { Name = "Offline desktop" }];
        internal DeviceProfileAutomationConfig Saved = new() { Enabled = true };
        internal long Revision = 1;
        internal int Creates, Saves;
        internal bool Reject;
        protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token)
        {
            DeviceProfile? created = null;
            if (request.Method == HttpMethod.Post) {
                var raw = await request.Content!.ReadAsStringAsync(token);
                using var json = JsonDocument.Parse(raw); var body = json.RootElement;
                if (body.GetProperty("expected_revision").GetInt64() != Revision) throw new InvalidOperationException("Fixture revision mismatch");
                if (request.RequestUri!.AbsolutePath.EndsWith("/create")) {
                    Creates++;
                    if (Reject) return new(HttpStatusCode.UnprocessableEntity) { Content = new StringContent("{\"status\":\"error\",\"error\":\"duplicate\"}",Encoding.UTF8,"application/json") };
                    created = new() { Name = body.GetProperty("name").GetString()!, ActivationBackend = "hardware_slot",
                        HardwareSlot = body.GetProperty("hardware_slot").GetInt32(), Lighting = new() { ControlOwner = body.GetProperty("lighting_control_owner").GetString() } };
                    Profiles.Add(created);
                } else if (request.RequestUri.AbsolutePath.EndsWith("/automation")) {
                    Saves++; Saved = body.GetProperty("device_profile_automation").Deserialize<DeviceProfileAutomationConfig>(ProfileJson.Options)!;
                } else throw new InvalidOperationException("Unexpected mutation in offline UI fixture");
                Revision++;
            }
            return new(HttpStatusCode.OK) { Content = new StringContent(JsonSerializer.Serialize(new ProfileApiResponse {
                Status = "ok", ApiVersion = 1, LightingControlOwnerSupported = true, HardwareProfileCreateSupported = true,
                DocumentRevision = Revision, Profile = created, Profiles = Profiles.Select(p => p.Clone()).ToList(),
                DeviceProfileAutomation = JsonSerializer.SerializeToElement(Saved, ProfileJson.Options), AutomationConfigurationAvailable = true,
                AutomationDecision = new() { Enabled = true, HardwareActivationAllowed = true, ConfigurationDocumentRevision = Revision }
            },ProfileJson.Options), Encoding.UTF8,"application/json") };
        }
    }
    private static IEnumerable<DependencyObject> Walk(DependencyObject root)
    {
        yield return root;
        for(int i=0;i<VisualTreeHelper.GetChildrenCount(root);i++)
            foreach(var item in Walk(VisualTreeHelper.GetChild(root,i))) yield return item;
    }
    private static T Find<T>(DependencyObject root,string id) where T:FrameworkElement => Walk(root).OfType<T>()
        .Single(e=>AutomationProperties.GetAutomationId(e)==id);
    private static void Invoke(Button button) => ((IInvokeProvider)new ButtonAutomationPeer(button).GetPattern(PatternInterface.Invoke)).Invoke();
    private static void Press(ContentDialog dialog,string name) => Invoke(Walk(dialog).OfType<Button>().Last(b=>b.Name==name && b.Visibility==Visibility.Visible && b.ActualWidth>0 && b.IsEnabled));
    private static async Task Until(Func<bool> ready)
    {
        for(int i=0;i<100;i++) { if(ready())return; await Task.Delay(50); }
        throw new TimeoutException("Offline UI condition not reached");
    }
    private static void Check(bool condition,string message) { if(!condition)throw new InvalidOperationException(message); }
    internal static async Task RunAsync(MainWindow window)
    {
        var directory=Path.GetFullPath(Environment.GetEnvironmentVariable("AURA_UI_VALIDATION_DIR")!);
        var observations=new List<string>(); string? error=null;
        try {
            var api=new FakeApi(); var model=new DeviceProfileAutomationModel(new ProfileControlClient(new HttpClient(api)));
            await model.RefreshAsync(); var page=new DeviceProfileAutomationPage(model); MainWindow.CurrentNavFrame!.Content=page;
            await Until(()=>page.IsLoaded && page.XamlRoot is not null && !model.IsBusy);
            ContentDialog? CurrentDialog() => VisualTreeHelper.GetOpenPopupsForXamlRoot(page.XamlRoot)
                .SelectMany(p=>Walk(p.Child)).OfType<ContentDialog>().FirstOrDefault();
            var editing=(Task)typeof(DeviceProfileAutomationPage).GetMethod("EditRuleAsync",BindingFlags.Instance|BindingFlags.NonPublic)!.Invoke(page,new object?[]{null})!;
            await Until(()=>CurrentDialog() is not null); var dialog=CurrentDialog()!;
            var slot=Find<ComboBox>(dialog,"DeviceAutomationSlot");
            var target=Find<ComboBox>(dialog,"DeviceAutomationRuleTarget");
            for(int i=1;i<=5;i++){slot.SelectedIndex=i;Check(target.Items.Count==0,"Empty slot unexpectedly has a Profile");}
            observations.Add("Native slot picker exposes 1-5; empty slots do not create automatically");
            var expander=Walk(dialog).OfType<Expander>().Single();expander.IsExpanded=true; await Task.Delay(80);
            Find<TextBox>(dialog,"DeviceAutomationProcessInput").Text="fixture.exe";
            Find<TextBox>(dialog,"DeviceAutomationCreateName").Text="UI slot3";
            Find<ComboBox>(dialog,"DeviceAutomationCreateSlot").SelectedIndex=2;
            Find<ComboBox>(dialog,"DeviceAutomationCreateLighting").SelectedIndex=1;
            Invoke(Find<Button>(dialog,"DeviceAutomationCreateConfirm"));
            await Until(()=>api.Creates==1 && !model.IsBusy && target.SelectedItem is DeviceProfileTargetChoice {Id:not null});
            var created=((DeviceProfileTargetChoice)target.SelectedItem).Id;
            Check(api.Saves==0 && model.Draft!.Bindings.Count==0,"Create silently saved a rule");
            Press(dialog,"PrimaryButton");await editing;
            Check(model.Draft!.Bindings.Single().ProfileId==created,"Native rule did not bind created UUID");
            Check(await model.SaveAsync(),"Rule save failed"); observations.Add("Explicit create + native rule confirmation binds created Profile ID; separate save succeeds");
            typeof(DeviceProfileAutomationPage).GetMethod("FallbackTarget_Click",BindingFlags.Instance|BindingFlags.NonPublic)!.Invoke(page,new object[]{page,new RoutedEventArgs()});
            await Until(()=>CurrentDialog() is not null);dialog=CurrentDialog()!;
            Find<ComboBox>(dialog,"DeviceAutomationSlot").SelectedIndex=3;
            target=Find<ComboBox>(dialog,"DeviceAutomationRuleTarget");Check(target.Items.Count==1,"Existing slot Profile missing");
            target.SelectedIndex=0; Press(dialog,"PrimaryButton");await Until(()=>CurrentDialog() is null &&
                !(bool)typeof(DeviceProfileAutomationPage).GetField("_dialogOpen",BindingFlags.Instance|BindingFlags.NonPublic)!.GetValue(page)!);
            Check(model.Draft.FallbackProfileId==created,"Fallback did not use same Profile UUID");
            Check(await model.SaveAsync(),"Fallback save failed"); observations.Add("Fallback uses same slot/Profile picker and ID binding");
            // Reopen an existing rule; changing local controls then Cancel must keep saved/draft values.
            var rule=model.Draft.Bindings.Single();editing=(Task)typeof(DeviceProfileAutomationPage).GetMethod("EditRuleAsync",BindingFlags.Instance|BindingFlags.NonPublic)!.Invoke(page,new object?[]{rule.RuleId})!;
            await Until(()=>CurrentDialog() is not null);dialog=CurrentDialog()!;
            Find<TextBox>(dialog,"DeviceAutomationProcessInput").Text="cancelled.exe";
            Press(dialog,"CloseButton");await editing;
            Check(model.Draft.Bindings.Single().ProcessName=="fixture.exe","Cancel modified rule");observations.Add("Native edit/cancel/reopen retains prior rule");
            editing=(Task)typeof(DeviceProfileAutomationPage).GetMethod("EditRuleAsync",BindingFlags.Instance|BindingFlags.NonPublic)!.Invoke(page,new object?[]{null})!;
            await Until(()=>CurrentDialog() is not null);dialog=CurrentDialog()!;
            expander=Walk(dialog).OfType<Expander>().Single();expander.IsExpanded=true;
            await Until(()=>Walk(dialog).OfType<TextBox>().Any(e=>AutomationProperties.GetAutomationId(e)=="DeviceAutomationCreateName"));
            var name=Find<TextBox>(dialog,"DeviceAutomationCreateName");name.Text="failed draft";api.Reject=true;
            Invoke(Find<Button>(dialog,"DeviceAutomationCreateConfirm"));await Until(()=>api.Creates==2 && !model.IsBusy);
            Check(name.Text=="failed draft" && model.Draft.Bindings.Count==1,"Rejected create lost editor or changed rule");
            Check(Find<InfoBar>(dialog,"DeviceAutomationCreateResult").Message.Contains("被拒绝"),"Rejected create displayed success");
            Press(dialog,"CloseButton");await editing;observations.Add("Native create rejection preserves input and existing rule without fake success");
            var fallback=(ComboBox)page.FindName("FallbackPicker");fallback.IsDropDownOpen=true;
            var source=fallback.ItemsSource;var selected=fallback.SelectedItem;
            await Task.Delay(1200);Check(fallback.IsDropDownOpen && ReferenceEquals(source,fallback.ItemsSource) && Equals(selected,fallback.SelectedItem),"Poll rebuilt open picker");
            fallback.IsDropDownOpen=false; observations.Add("Polling retains open fallback picker, item source and selection");
        }catch(Exception ex){error=ex.ToString();}
        finally {
            Directory.CreateDirectory(directory);
            File.WriteAllText(Path.Combine(directory,"automation-ui-results.json"),JsonSerializer.Serialize(new {
                source="Native WinUI controls + in-process Fake HTTP; NOT physical/visual/DPI acceptance",error,observations},new JsonSerializerOptions{WriteIndented=true}));
            await App.RequestExit();
        }
    }
}
