using System;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.Windows.AppLifecycle;

namespace Aura_WinUI;

public static class Program
{
    [STAThread]
    public static void Main(string[] args)
    {
        // 1. 获取启动参数与注册应用单实例 Key
        AppActivationArguments activatedArgs = AppInstance.GetCurrent().GetActivatedEventArgs();
        AppInstance keyInstance = AppInstance.FindOrRegisterForKey("AceHFXAura_WinUI_App");

        if (!keyInstance.IsCurrent)
        {
            // 次实例 (Secondary instance): 等待重定向完成并退出，严禁使用 Process.Kill 强杀
            keyInstance.RedirectActivationToAsync(activatedArgs).AsTask().GetAwaiter().GetResult();
            return;
        }

        // 主实例 (Primary instance): 注册 Activated 事件监听来自次实例的唤醒
        keyInstance.Activated += (sender, e) =>
        {
            App.HandleSecondaryActivation(e);
        };

        // 2. 初始化 WinRT COM 支持并启动 XAML UI 消息循环
        WinRT.ComWrappersSupport.InitializeComWrappers();

        Application.Start((p) =>
        {
            var context = new DispatcherQueueSynchronizationContext(
                DispatcherQueue.GetForCurrentThread());
            System.Threading.SynchronizationContext.SetSynchronizationContext(context);
            new App();
        });
    }
}
