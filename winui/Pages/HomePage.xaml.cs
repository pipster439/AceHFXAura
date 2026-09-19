using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Aura_WinUI.Services;

namespace Aura_WinUI.Pages;

public sealed partial class HomePage : Page
{
    public HomePage()
    {
        InitializeComponent();
        Loaded += HomePage_Loaded;
    }

    private async void HomePage_Loaded(object sender, RoutedEventArgs e)
    {
        await RefreshStatusAsync();
    }

    private async void RefreshStatusBtn_Click(object sender, RoutedEventArgs e)
    {
        await RefreshStatusAsync();
    }

    private void OpenStudioBtn_Click(object sender, RoutedEventArgs e)
    {
        // 通过 MainWindow 统筹导航通道进入 Studio，自动处理防重与选中同步
        MainWindow.CurrentInstance?.NavigateTo(typeof(StudioPage));
    }

    private async System.Threading.Tasks.Task RefreshStatusAsync()
    {
        DaemonStatusText.Text = "正在检测...";
        await DaemonSupervisor.Instance.EnsureStartedAsync();
        DaemonStatusText.Text = DaemonSupervisor.Instance.StatusDescription;
    }
}
