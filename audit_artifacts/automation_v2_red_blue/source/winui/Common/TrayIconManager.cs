using System;
using System.IO;
using System.Runtime.InteropServices;
using WinRT.Interop;

namespace Aura_WinUI.Common;

/// <summary>
/// 管理系统托盘图标 (Win32 Shell_NotifyIconW)、上下文菜单与窗口显示/隐藏状态。
/// </summary>
public sealed class TrayIconManager : IDisposable
{
    private const int WM_USER = 0x0400;
    private const int WM_TRAYICON = WM_USER + 100;

    private const int WM_LBUTTONUP = 0x0202;
    private const int WM_LBUTTONDBLCLK = 0x0203;
    private const int WM_RBUTTONUP = 0x0205;

    private const uint NIM_ADD = 0x00000000;
    private const uint NIM_MODIFY = 0x00000001;
    private const uint NIM_DELETE = 0x00000002;

    private const uint NIF_MESSAGE = 0x00000001;
    private const uint NIF_ICON = 0x00000002;
    private const uint NIF_TIP = 0x00000004;

    private const uint TPM_RIGHTBUTTON = 0x0002;
    private const uint TPM_RETURNCMD = 0x0100;
    private const uint TPM_NONOTIFY = 0x0080;

    private const uint MF_STRING = 0x00000000;
    private const uint MF_SEPARATOR = 0x00000800;
    private const uint MF_GRAYED = 0x00000001;
    private const uint MF_DEFAULT = 0x00001000;

    private const uint SUBCLASS_TRAY_ID = 1002;

    private const int CMD_OPEN = 1001;
    private const int CMD_EXIT = 1002;

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct NOTIFYICONDATA
    {
        public uint cbSize;
        public IntPtr hWnd;
        public uint uID;
        public uint uFlags;
        public uint uCallbackMessage;
        public IntPtr hIcon;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string szTip;
        public uint dwState;
        public uint dwStateMask;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string szInfo;
        public uint uTimeoutOrVersion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string szInfoTitle;
        public uint dwInfoFlags;
        public Guid guidItem;
        public IntPtr hBalloonIcon;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct POINT
    {
        public int x;
        public int y;
    }

    private delegate IntPtr SubclassProc(IntPtr hWnd, uint uMsg, IntPtr wParam, IntPtr lParam, UIntPtr uIdSubclass, UIntPtr dwRefData);

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern bool Shell_NotifyIcon(uint dwMessage, ref NOTIFYICONDATA lpData);

    [DllImport("comctl32.dll", SetLastError = true)]
    private static extern bool SetWindowSubclass(IntPtr hWnd, SubclassProc pfnSubclass, UIntPtr uIdSubclass, UIntPtr dwRefData);

    [DllImport("comctl32.dll", SetLastError = true)]
    private static extern bool RemoveWindowSubclass(IntPtr hWnd, SubclassProc pfnSubclass, UIntPtr uIdSubclass);

    [DllImport("comctl32.dll")]
    private static extern IntPtr DefSubclassProc(IntPtr hWnd, uint uMsg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr LoadImage(IntPtr hInst, string name, uint type, int cx, int cy, uint fuLoad);

    [DllImport("user32.dll")]
    private static extern bool DestroyIcon(IntPtr hIcon);

    [DllImport("user32.dll")]
    private static extern IntPtr CreatePopupMenu();

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern bool AppendMenu(IntPtr hMenu, uint uFlags, uint uIDNewItem, string? lpNewItem);

    [DllImport("user32.dll")]
    private static extern bool DestroyMenu(IntPtr hMenu);

    [DllImport("user32.dll")]
    private static extern int TrackPopupMenuEx(IntPtr hMenu, uint uFlags, int x, int y, IntPtr hWnd, IntPtr lpTPMParams);

    [DllImport("user32.dll")]
    private static extern bool GetCursorPos(out POINT lpPoint);

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern uint RegisterWindowMessage(string lpString);

    private readonly IntPtr _hWnd;
    private readonly Action _onOpen;
    private readonly Action _onExit;
    private readonly uint _wmTaskbarCreated;
    private IntPtr _hIcon = IntPtr.Zero;
    private bool _isAdded = false;
    private string _daemonStatusText = "守护进程: 运行中";
    private SubclassProc? _subclassProc;

    public static bool MinimizeToTrayEnabled { get; set; } = true;

    public TrayIconManager(Microsoft.UI.Xaml.Window window, Action onOpen, Action onExit)
    {
        _hWnd = WindowNative.GetWindowHandle(window);
        _onOpen = onOpen;
        _onExit = onExit;
        _wmTaskbarCreated = RegisterWindowMessage("TaskbarCreated");

        LoadAppIcon();

        // 挂钩 MainWindow 消息循环以接收 WM_TRAYICON 回调
        _subclassProc = TraySubclassCallback;
        SetWindowSubclass(_hWnd, _subclassProc, (UIntPtr)SUBCLASS_TRAY_ID, UIntPtr.Zero);

        AddTrayIcon();
    }

    public void UpdateStatus(string statusText)
    {
        _daemonStatusText = statusText;
        if (_isAdded)
        {
            var nid = CreateNotifyIconData();
            nid.uFlags = NIF_TIP;
            nid.szTip = $"Aura ({statusText})";
            Shell_NotifyIcon(NIM_MODIFY, ref nid);
        }
    }

    private void LoadAppIcon()
    {
        try
        {
            string icoPath = Path.Combine(AppContext.BaseDirectory, "Assets", "AppIcon.ico");
            if (File.Exists(icoPath))
            {
                const uint IMAGE_ICON = 1;
                const uint LR_LOADFROMFILE = 0x0010;
                _hIcon = LoadImage(IntPtr.Zero, icoPath, IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
            }
        }
        catch
        {
            _hIcon = IntPtr.Zero;
        }
    }

    private NOTIFYICONDATA CreateNotifyIconData()
    {
        var nid = new NOTIFYICONDATA();
        nid.cbSize = (uint)Marshal.SizeOf(typeof(NOTIFYICONDATA));
        nid.hWnd = _hWnd;
        nid.uID = 1;
        nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = _hIcon;
        nid.szTip = $"Aura - ROG Falchion Ace HFX ({_daemonStatusText})";
        return nid;
    }

    private void AddTrayIcon()
    {
        var nid = CreateNotifyIconData();
        _isAdded = Shell_NotifyIcon(NIM_ADD, ref nid);
    }

    public void RemoveTrayIcon()
    {
        if (_isAdded)
        {
            var nid = CreateNotifyIconData();
            Shell_NotifyIcon(NIM_DELETE, ref nid);
            _isAdded = false;
        }
    }

    private IntPtr TraySubclassCallback(IntPtr hWnd, uint uMsg, IntPtr wParam, IntPtr lParam, UIntPtr uIdSubclass, UIntPtr dwRefData)
    {
        if (_wmTaskbarCreated != 0 && uMsg == _wmTaskbarCreated)
        {
            // Windows Explorer 重启后自动重新向系统任务栏注册托盘图标
            AddTrayIcon();
            return IntPtr.Zero;
        }

        if (uMsg == WM_TRAYICON)
        {
            int mouseMsg = (int)lParam;
            if (mouseMsg == WM_LBUTTONUP || mouseMsg == WM_LBUTTONDBLCLK)
            {
                _onOpen?.Invoke();
                return IntPtr.Zero;
            }
            else if (mouseMsg == WM_RBUTTONUP)
            {
                ShowContextMenu();
                return IntPtr.Zero;
            }
        }

        return DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }

    private void ShowContextMenu()
    {
        GetCursorPos(out POINT pt);
        IntPtr hMenu = CreatePopupMenu();

        AppendMenu(hMenu, MF_STRING | MF_DEFAULT, CMD_OPEN, "打开 Aura");
        AppendMenu(hMenu, MF_SEPARATOR, 0, null);
        AppendMenu(hMenu, MF_STRING | MF_GRAYED, 0, _daemonStatusText);
        AppendMenu(hMenu, MF_SEPARATOR, 0, null);
        AppendMenu(hMenu, MF_STRING, CMD_EXIT, "退出 Aura");

        SetForegroundWindow(_hWnd);
        int cmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, _hWnd, IntPtr.Zero);
        DestroyMenu(hMenu);

        if (cmd == CMD_OPEN)
        {
            _onOpen?.Invoke();
        }
        else if (cmd == CMD_EXIT)
        {
            _onExit?.Invoke();
        }
    }

    public void Dispose()
    {
        RemoveTrayIcon();
        if (_subclassProc != null)
        {
            RemoveWindowSubclass(_hWnd, _subclassProc, (UIntPtr)SUBCLASS_TRAY_ID);
            _subclassProc = null;
        }
        if (_hIcon != IntPtr.Zero)
        {
            DestroyIcon(_hIcon);
            _hIcon = IntPtr.Zero;
        }
    }
}
