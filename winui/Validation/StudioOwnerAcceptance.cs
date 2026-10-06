using Aura_WinUI.Pages;
using Aura_WinUI.Services;

namespace Aura_WinUI.Validation;

// Owner-only manual UI session: no mock delegates, automatic prompts, requests or Apply.
// Inherits the CLI/offline/data-root fixture guard; the already-owned core must be dry-run.
internal static class StudioOwnerAcceptance
{
    internal static bool Requested => StudioAiValidation.FullSmoke && Environment.GetEnvironmentVariable("AURA_STUDIO_OWNER_ACCEPTANCE") == "1";
    internal static async Task OpenAsync(MainWindow window) {
        var runtime = await AuraControlClient.Instance.GetRuntimeStatusAsync();
        if (!runtime.IsOnline || !runtime.IsDryRun) { await App.RequestExit(); return; }
        window.NavigateTo(typeof(StudioPage));
    }
}
