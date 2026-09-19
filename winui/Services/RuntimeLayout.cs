using System;
using System.IO;

namespace Aura_WinUI.Services;

public sealed record RuntimeLayout(
    string DaemonExecutablePath,
    string WorkingDirectory,
    string ConfigPath,
    string KeymapPath
);

public static class RuntimeLayoutResolver
{
    public static RuntimeLayout? Resolve()
    {
        // 1. 源码开发环境优先 (Dev Priority)：
        // 沿 BaseDirectory 向上递归查找仓库根目录标志 (include/engine/effect.h 与 calibrated_keymap.json)
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir != null && dir.Exists)
        {
            string devKeymap = Path.Combine(dir.FullName, "calibrated_keymap.json");
            string devEffectHeader = Path.Combine(dir.FullName, "include", "engine", "effect.h");

            if (File.Exists(devKeymap) && File.Exists(devEffectHeader))
            {
                string repoRoot = dir.FullName;
                string daemonExe = Path.Combine(repoRoot, "aura_daemon.exe");
                if (!File.Exists(daemonExe))
                {
                    daemonExe = Path.Combine(repoRoot, "build", "Release", "aura_daemon.exe");
                }

                if (File.Exists(daemonExe))
                {
                    string devConfig = Path.Combine(repoRoot, "config.json");
                    if (!File.Exists(devConfig))
                    {
                        string exampleCfg = Path.Combine(repoRoot, "config.example.json");
                        if (File.Exists(exampleCfg))
                        {
                            try { File.Copy(exampleCfg, devConfig, false); } catch { }
                        }
                    }

                    return new RuntimeLayout(
                        DaemonExecutablePath: daemonExe,
                        WorkingDirectory: repoRoot,
                        ConfigPath: devConfig,
                        KeymapPath: devKeymap
                    );
                }
            }
            dir = dir.Parent;
        }

        // 2. 便携绿色版优先 (Portable Priority)：
        // 若执行文件所在目录下同时存在 calibrated_keymap.json 与 aura_daemon.exe (且非源码树)
        string appBase = AppContext.BaseDirectory;
        string portableKeymap = Path.Combine(appBase, "calibrated_keymap.json");
        string portableDaemon = Path.Combine(appBase, "aura_daemon.exe");

        if (File.Exists(portableDaemon) && File.Exists(portableKeymap))
        {
            string portableConfig = Path.Combine(appBase, "config.json");
            if (!File.Exists(portableConfig))
            {
                string exampleCfg = Path.Combine(appBase, "config.example.json");
                if (File.Exists(exampleCfg))
                {
                    try { File.Copy(exampleCfg, portableConfig, false); } catch { }
                }
            }

            return new RuntimeLayout(
                DaemonExecutablePath: portableDaemon,
                WorkingDirectory: appBase,
                ConfigPath: portableConfig,
                KeymapPath: portableKeymap
            );
        }

        // 3. 正式单文件 / 桌面客户端 Canonical 路径：
        // 运行时缓存层: %LOCALAPPDATA%\Aura\runtime\ (只读/易刷新资产)
        // 用户配置层:   %LOCALAPPDATA%\Aura\config.json (规范唯一持久化配置)
        string localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        string auraAppData = Path.Combine(localAppData, "Aura");
        string runtimeDir = Path.Combine(auraAppData, "runtime");

        string canonicalDaemon = Path.Combine(runtimeDir, "aura_daemon.exe");
        string canonicalKeymap = Path.Combine(runtimeDir, "calibrated_keymap.json");
        string canonicalConfig = Path.Combine(auraAppData, "config.json");

        if (File.Exists(canonicalDaemon))
        {
            // 旧配置兼容迁移：若规范路径尚无配置，且当前工作目录存在合法 config.json，则一次性复制迁移
            if (!File.Exists(canonicalConfig))
            {
                try
                {
                    string cwdConfig = Path.Combine(Environment.CurrentDirectory, "config.json");
                    if (File.Exists(cwdConfig) && !string.Equals(Path.GetFullPath(cwdConfig), Path.GetFullPath(canonicalConfig), StringComparison.OrdinalIgnoreCase))
                    {
                        Directory.CreateDirectory(auraAppData);
                        File.Copy(cwdConfig, canonicalConfig, false);
                    }
                }
                catch
                {
                    // 忽略迁移异常，回退至模板初始化
                }
            }

            // 若仍无配置，从 runtime\config.example.json 初始化
            if (!File.Exists(canonicalConfig))
            {
                string exampleCfg = Path.Combine(runtimeDir, "config.example.json");
                if (File.Exists(exampleCfg))
                {
                    try
                    {
                        Directory.CreateDirectory(auraAppData);
                        File.Copy(exampleCfg, canonicalConfig, false);
                    }
                    catch { }
                }
            }

            string keymapToUse = File.Exists(canonicalKeymap) ? canonicalKeymap : Path.Combine(auraAppData, "calibrated_keymap.json");

            return new RuntimeLayout(
                DaemonExecutablePath: canonicalDaemon,
                WorkingDirectory: auraAppData,
                ConfigPath: canonicalConfig,
                KeymapPath: keymapToUse
            );
        }

        return null;
    }
}
