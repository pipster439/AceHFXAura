namespace Aura_WinUI.Services;

public static class ColorBrightnessHelper
{
    public static int GetBrightnessPercent(int r, int g, int b)
    {
        int cr = Math.Clamp(r, 0, 255);
        int cg = Math.Clamp(g, 0, 255);
        int cb = Math.Clamp(b, 0, 255);
        int max = Math.Max(cr, Math.Max(cg, cb));
        return Math.Clamp((int)Math.Round(max / 255.0 * 100.0), 0, 100);
    }

    public static (int r, int g, int b) NormalizeToFullBrightness(int r, int g, int b, (int r, int g, int b)? fallback = null)
    {
        int cr = Math.Clamp(r, 0, 255);
        int cg = Math.Clamp(g, 0, 255);
        int cb = Math.Clamp(b, 0, 255);
        int max = Math.Max(cr, Math.Max(cg, cb));
        if (max > 0)
        {
            return (
                Math.Clamp((int)Math.Round(cr * 255.0 / max), 0, 255),
                Math.Clamp((int)Math.Round(cg * 255.0 / max), 0, 255),
                Math.Clamp((int)Math.Round(cb * 255.0 / max), 0, 255)
            );
        }

        if (fallback is { } fb)
        {
            int fr = Math.Clamp(fb.r, 0, 255);
            int fg = Math.Clamp(fb.g, 0, 255);
            int fbVal = Math.Clamp(fb.b, 0, 255);
            if (Math.Max(fr, Math.Max(fg, fbVal)) > 0)
            {
                return NormalizeToFullBrightness(fr, fg, fbVal);
            }
        }

        return (255, 255, 255);
    }

    public static (int r, int g, int b) ScaleByBrightness((int r, int g, int b) baseRgb, int brightnessPercent)
    {
        double factor = Math.Clamp(brightnessPercent, 0, 100) / 100.0;
        return (
            Math.Clamp((int)Math.Round(Math.Clamp(baseRgb.r, 0, 255) * factor), 0, 255),
            Math.Clamp((int)Math.Round(Math.Clamp(baseRgb.g, 0, 255) * factor), 0, 255),
            Math.Clamp((int)Math.Round(Math.Clamp(baseRgb.b, 0, 255) * factor), 0, 255)
        );
    }
}
