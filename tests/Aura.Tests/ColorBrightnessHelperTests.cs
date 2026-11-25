using Aura_WinUI.Services;

namespace Aura.Tests;

[TestClass]
public sealed class ColorBrightnessHelperTests
{
    [TestMethod]
    public void GetBrightnessPercent_ComputesExpectedPercentages()
    {
        Assert.AreEqual(0, ColorBrightnessHelper.GetBrightnessPercent(0, 0, 0));
        Assert.AreEqual(6, ColorBrightnessHelper.GetBrightnessPercent(0, 5, 15));
        Assert.AreEqual(100, ColorBrightnessHelper.GetBrightnessPercent(255, 25, 41));
    }

    [TestMethod]
    public void NormalizeToFullBrightness_HandlesDimAndBlackColorsWithFallback()
    {
        Assert.AreEqual((0, 85, 255), ColorBrightnessHelper.NormalizeToFullBrightness(0, 5, 15));
        Assert.AreEqual((255, 25, 41), ColorBrightnessHelper.NormalizeToFullBrightness(0, 0, 0, (255, 25, 41)));
        Assert.AreEqual((255, 255, 255), ColorBrightnessHelper.NormalizeToFullBrightness(0, 0, 0, (0, 0, 0)));
    }

    [TestMethod]
    public void ScaleByBrightness_ScalesProportionallyAcrossBounds()
    {
        Assert.AreEqual((38, 4, 6), ColorBrightnessHelper.ScaleByBrightness((255, 25, 41), 15));
        Assert.AreEqual((0, 0, 0), ColorBrightnessHelper.ScaleByBrightness((255, 25, 41), 0));
        Assert.AreEqual((255, 25, 41), ColorBrightnessHelper.ScaleByBrightness((255, 25, 41), 100));
    }
}
