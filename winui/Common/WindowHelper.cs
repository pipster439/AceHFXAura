using System;
using System.Runtime.InteropServices;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Windows.Graphics;
using WinRT.Interop;

namespace Aura_WinUI.Common;

/// <summary>
/// 管理窗口 DPI 缩放、初始化尺寸以及操作系统级 MinWidth / MinHeight 约束。
/// </summary>
public static class WindowHelper
{
    private const int WM_GETMINMAXINFO = 0x0024;
    private const uint SUBCLASS_ID = 1001;

    [StructLayout(LayoutKind.Sequential)]
    private struct POINT
    {
        public int x;
        public int y;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MINMAXINFO
    {
        public POINT ptReserved;
        public POINT ptMaxSize;
        public POINT ptMaxPosition;
        public POINT ptMinTrackSize;
        public POINT ptMaxTrackSize;
    }

    private delegate IntPtr SubclassProc(IntPtr hWnd, uint uMsg, IntPtr wParam, IntPtr lParam, UIntPtr uIdSubclass, UIntPtr dwRefData);

    [DllImport("comctl32.dll", SetLastError = true)]
    private static extern bool SetWindowSubclass(IntPtr hWnd, SubclassProc pfnSubclass, UIntPtr uIdSubclass, UIntPtr dwRefData);

    [DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(IntPtr hWnd);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    public const int SW_HIDE = 0;
    public const int SW_SHOWNORMAL = 1;
    public const int SW_RESTORE = 9;

    private static SubclassProc? _subclassProc;
    private static int _minWidthDips = 600;
    private static int _minHeightDips = 500;

    /// <summary>
    /// 初始化窗口几何尺寸并挂钩 Win32 最小尺寸硬性约束。
    /// </summary>
    public static void InitializeWindowConstraints(Microsoft.UI.Xaml.Window window, int preferredWidthDip = 1060, int preferredHeightDip = 720, int minWidthDip = 600, int minHeightDip = 500)
    {
        _minWidthDips = minWidthDip;
        _minHeightDips = minHeightDip;

        IntPtr hWnd = WindowNative.GetWindowHandle(window);
        uint dpi = GetDpiForWindow(hWnd);
        double scale = dpi / 96.0;

        WindowId wndId = Win32Interop.GetWindowIdFromWindow(hWnd);
        AppWindow appWindow = AppWindow.GetFromWindowId(wndId);

        // 设置初始尺寸
        int initWidthPx = (int)(preferredWidthDip * scale);
        int initHeightPx = (int)(preferredHeightDip * scale);
        appWindow.Resize(new SizeInt32(initWidthPx, initHeightPx));

        // 最小尺寸通过 Win32 WM_GETMINMAXINFO 子类化拦截在 OS 层严格生效

        // 安装 Win32 窗口子类化钩子，拦截 WM_GETMINMAXINFO 实现内核级最小拉伸尺寸限制
        _subclassProc = WindowSubclassCallback;
        SetWindowSubclass(hWnd, _subclassProc, (UIntPtr)SUBCLASS_ID, UIntPtr.Zero);
    }

    private static IntPtr WindowSubclassCallback(IntPtr hWnd, uint uMsg, IntPtr wParam, IntPtr lParam, UIntPtr uIdSubclass, UIntPtr dwRefData)
    {
        if (uMsg == WM_GETMINMAXINFO)
        {
            IntPtr result = DefSubclassProc(hWnd, uMsg, wParam, lParam);
            uint dpi = GetDpiForWindow(hWnd);
            double scale = dpi / 96.0;

            MINMAXINFO mmi = Marshal.PtrToStructure<MINMAXINFO>(lParam);
            mmi.ptMinTrackSize.x = (int)(_minWidthDips * scale);
            mmi.ptMinTrackSize.y = (int)(_minHeightDips * scale);
            Marshal.StructureToPtr(mmi, lParam, false);
            return result;
        }

        return DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }

    [DllImport("comctl32.dll")]
    private static extern IntPtr DefSubclassProc(IntPtr hWnd, uint uMsg, IntPtr wParam, IntPtr lParam);
}
