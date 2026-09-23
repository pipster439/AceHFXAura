using System.Text.Json;

namespace Aura_WinUI.Services;

public static class EmbeddedStudioNavigation
{
    private const string Origin = "http://127.0.0.1:19898/";

    public static Uri InitialUrl(string tab, string theme)
    {
        if (tab is not ("studio" or "automation")) throw new ArgumentOutOfRangeException(nameof(tab));
        if (theme is not ("dark" or "light")) throw new ArgumentOutOfRangeException(nameof(theme));
        return new Uri($"{Origin}?host=winui&tab={tab}&theme={theme}");
    }

    public static string ThemeMessage(string theme)
    {
        if (theme is not ("dark" or "light")) throw new ArgumentOutOfRangeException(nameof(theme));
        return JsonSerializer.Serialize(new { type = "theme_changed", theme });
    }
}
