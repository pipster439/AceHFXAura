<#
.SYNOPSIS
    Aura Clean-Machine One-Click Deployment Script for Windows 11 x64.
.DESCRIPTION
    面向全新 Windows 11 x64 环境的 ROG Falchion Ace HFX Aura 一键部署脚本。
    支持从解压后的发行目录、.zip 发布包（自动校验 .sha256 侧车与 checksums.json）或仓库 dist/ 产物部署：
    - 检测并按需引导安装依赖（64 位 Windows、WebView2 Evergreen Runtime、Python 3、VS C++ Build Tools）
    - 部署自包含 WinUI 客户端与 runtime-payload 至目标安装目录
    - 支持标准用户目录 (%LOCALAPPDATA%\Aura) 与便携模式 (-Portable)
    - 安全初始化数据目录与内置 plugins（effect_hall_reactive.dll / mcp / wooting 桥接），绝不覆盖已有 config.json
    - 支持创建开始菜单/桌面/开机自启快捷方式、导出 aura-mcp-config.json、提权安装 Wooting 桥接及干净卸载
#>

[CmdletBinding()]
param(
    [string]$PackagePath = "",
    [string]$InstallDir = "",
    [string]$DataDir = "",
    [switch]$Portable,
    [switch]$SkipPrereqCheck,
    [switch]$InstallWebView2,
    [switch]$NoStartMenuShortcut,
    [switch]$DesktopShortcut,
    [switch]$AutoStart,
    [switch]$InstallWootingBridge,
    [switch]$ExportMcpConfig,
    [string]$McpConfigOut = "",
    [switch]$ForceStop,
    [switch]$Launch,
    [switch]$Uninstall,
    [switch]$PurgeData,
    [switch]$VerifyChecksums,
    [switch]$Help,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ExtraArgs
)

$ErrorActionPreference = "Stop"
$env:PYTHONUTF8 = "1"
try {
    [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
    $OutputEncoding = [System.Text.Encoding]::UTF8
} catch {}

if ($ExtraArgs) {
    for ($i = 0; $i -lt $ExtraArgs.Count; $i++) {
        $arg = $ExtraArgs[$i]
        switch -Regex ($arg) {
            "^--?h(elp)?$"              { $Help = $true }
            "^-\?$"                     { $Help = $true }
            "^--portable$"              { $Portable = $true }
            "^--skip-prereq-check$"     { $SkipPrereqCheck = $true }
            "^--install-webview2$"      { $InstallWebView2 = $true }
            "^--no-start-menu$"         { $NoStartMenuShortcut = $true }
            "^--desktop-shortcut$"      { $DesktopShortcut = $true }
            "^--auto-start$"            { $AutoStart = $true }
            "^--install-wooting$"       { $InstallWootingBridge = $true }
            "^--export-mcp$"            { $ExportMcpConfig = $true }
            "^--force-stop$"            { $ForceStop = $true }
            "^--launch$"                { $Launch = $true }
            "^--uninstall$"             { $Uninstall = $true }
            "^--purge-data$"            { $PurgeData = $true }
            "^--verify-checksums$"      { $VerifyChecksums = $true }
            "^--package-path$" {
                if ($i + 1 -lt $ExtraArgs.Count) { $PackagePath = $ExtraArgs[++$i] }
            }
            "^--install-dir$" {
                if ($i + 1 -lt $ExtraArgs.Count) { $InstallDir = $ExtraArgs[++$i] }
            }
            "^--data-dir$" {
                if ($i + 1 -lt $ExtraArgs.Count) { $DataDir = $ExtraArgs[++$i] }
            }
            "^--mcp-config-out$" {
                if ($i + 1 -lt $ExtraArgs.Count) { $McpConfigOut = $ExtraArgs[++$i]; $ExportMcpConfig = $true }
            }
            default {
                throw "未知参数: $arg (运行 deploy_aura.ps1 -Help 查看用法)"
            }
        }
    }
}

if ($Help) {
    Write-Host @"
=========================================================
 ROG Falchion Ace HFX - Aura 干净环境一键部署脚本
=========================================================

用法:
    .\deploy_aura.ps1 [-PackagePath <解压目录或.zip>] [-InstallDir <安装目录>]
                      [-DataDir <数据目录>] [-Portable]
                      [-InstallWebView2] [-SkipPrereqCheck]
                      [-NoStartMenuShortcut] [-DesktopShortcut] [-AutoStart]
                      [-InstallWootingBridge] [-ExportMcpConfig] [-McpConfigOut <文件>]
                      [-ForceStop] [-Launch] [-VerifyChecksums]
                      [-Uninstall [-PurgeData]] [-Help]

常用示例:
    .\deploy_aura.bat
        从当前已解压发行包目录一键标准安装到 %LOCALAPPDATA%\Programs\Aura，
        初始化数据目录与内置插件，并创建开始菜单快捷方式。

    .\deploy_aura.bat -Portable -InstallDir "D:\Apps\Aura" -ExportMcpConfig -Launch
        部署为绿色便携版（数据/插件全部保存在 D:\Apps\Aura），导出 MCP 配置并启动。

    .\deploy_aura.bat -PackagePath ".\Aura-v0.1.0-alpha.9-rc.1-windows-x64.zip" -InstallWebView2 -InstallWootingBridge
        从发布 ZIP 包校对 SHA-256 并部署，缺少 WebView2 时自动静默安装，且提权安装 Wooting 桥接。

    .\deploy_aura.bat -Uninstall
        卸载 Aura 主程序与快捷方式（默认保留用户 config.json 与自定义插件）。
        如需同时删除用户数据，追加 -PurgeData。
"@
    exit 0
}

function Write-Header {
    param([string]$Message)
    Write-Host "`n=========================================================" -ForegroundColor Cyan
    Write-Host " $Message" -ForegroundColor Cyan
    Write-Host "=========================================================" -ForegroundColor Cyan
}

function Write-Step {
    param([string]$Tag, [string]$Message)
    Write-Host "`n>>> [$Tag] $Message" -ForegroundColor Yellow
}

function Write-Pass {
    param([string]$Message)
    Write-Host "  [PASS] $Message" -ForegroundColor Green
}

function Write-Info {
    param([string]$Message)
    Write-Host "  [*] $Message" -ForegroundColor Gray
}

function Write-Warn {
    param([string]$Message)
    Write-Host "  [WARN] $Message" -ForegroundColor DarkYellow
}

function Write-Fail {
    param([string]$Message)
    Write-Host "  [FAIL] $Message" -ForegroundColor Red
}

function Test-ValidPackageRoot {
    param([string]$DirPath)
    if ([string]::IsNullOrWhiteSpace($DirPath) -or -not (Test-Path -LiteralPath $DirPath -PathType Container)) {
        return $false
    }
    $exeOk = Test-Path -LiteralPath (Join-Path $DirPath "Aura.exe") -PathType Leaf
    $manifestOk = Test-Path -LiteralPath (Join-Path $DirPath "runtime-payload\runtime-manifest.json") -PathType Leaf
    return ($exeOk -and $manifestOk)
}

function Get-FileSha256Lower {
    param([string]$FilePath)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $stream = [System.IO.File]::OpenRead($FilePath)
        try {
            $hashBytes = $sha.ComputeHash($stream)
            return ([System.BitConverter]::ToString($hashBytes)).Replace("-", "").ToLowerInvariant()
        } finally {
            $stream.Dispose()
        }
    } finally {
        $sha.Dispose()
    }
}

function Assert-ZipSidecarSha256 {
    param([string]$ZipFilePath)
    $sidecar = "$ZipFilePath.sha256"
    if (-not (Test-Path -LiteralPath $sidecar -PathType Leaf)) {
        Write-Info "未发现 .sha256 侧车文件，跳过 ZIP 外层哈希比对: $sidecar"
        return
    }
    $raw = (Get-Content -LiteralPath $sidecar -Raw -Encoding UTF8).Trim()
    if ([string]::IsNullOrWhiteSpace($raw)) {
        throw "SHA-256 侧车文件为空: $sidecar"
    }
    $expected = ($raw -split "\s+")[0].Trim().ToLowerInvariant()
    if ($expected -notmatch "^[a-f0-9]{64}$") {
        throw "SHA-256 侧车格式无效: $sidecar"
    }
    $actual = Get-FileSha256Lower -FilePath $ZipFilePath
    if ($actual -ne $expected) {
        throw "ZIP 文件 SHA-256 校验失败!`n  期望: $expected`n  实际: $actual`n  文件: $ZipFilePath"
    }
    Write-Pass "ZIP SHA-256 校验一致 ($($expected.Substring(0, 12))...)"
}

function Assert-DirectoryChecksums {
    param([string]$PackageRoot, [switch]$Required)
    $checksumFile = Join-Path $PackageRoot "checksums.json"
    if (-not (Test-Path -LiteralPath $checksumFile -PathType Leaf)) {
        if ($Required) {
            throw "未找到 checksums.json: $checksumFile"
        }
        return
    }
    $raw = Get-Content -LiteralPath $checksumFile -Raw -Encoding UTF8
    $entries = ConvertFrom-Json -InputObject $raw
    $checked = 0
    foreach ($prop in $entries.PSObject.Properties) {
        $rel = $prop.Name -replace '/', '\'
        $expected = [string]$prop.Value
        $target = Join-Path $PackageRoot $rel
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
            throw "安装包文件缺失: $($prop.Name)"
        }
        $actual = Get-FileSha256Lower -FilePath $target
        if ($actual -ne $expected.ToLowerInvariant()) {
            throw "安装包文件完整性校验失败: $($prop.Name)"
        }
        $checked++
    }
    Write-Pass "checksums.json 校验通过 ($checked 个文件)"
}

function Find-TargetProcesses {
    param([string[]]$RootDirs)
    $normalized = @()
    foreach ($d in $RootDirs) {
        if (-not [string]::IsNullOrWhiteSpace($d)) {
            $full = [System.IO.Path]::GetFullPath($d).TrimEnd('\') + '\'
            $normalized += $full
        }
    }
    $matches = New-Object System.Collections.Generic.List[object]
    $names = @("Aura", "AuraWorker", "aura_daemon", "aura_web_ui")
    foreach ($proc in (Get-Process -ErrorAction SilentlyContinue | Where-Object { $names -contains $_.ProcessName })) {
        $exePath = $null
        try {
            $exePath = $proc.Path
        } catch {
            $exePath = $null
        }
        if ([string]::IsNullOrWhiteSpace($exePath)) { continue }
        foreach ($prefix in $normalized) {
            if ($exePath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                $matches.Add($proc)
                break
            }
        }
    }
    return $matches
}

function Assert-TargetPathsUnlocked {
    param([string[]]$RootDirs, [switch]$AllowForceStop)
    $procs = Find-TargetProcesses -RootDirs $RootDirs
    if ($procs.Count -eq 0) {
        return
    }
    $desc = ($procs | ForEach-Object { "$($_.ProcessName) (PID=$($_.Id))" }) -join ", "
    if (-not $AllowForceStop) {
        throw "目标安装/数据目录仍有运行中进程: $desc`n请先从系统托盘退出 Aura 后重试，或追加 -ForceStop 自动关闭目标目录进程。"
    }
    Write-Warn "正在关闭目标目录内的运行中进程: $desc"
    foreach ($p in $procs) {
        try {
            if ($p.MainWindowHandle -ne [IntPtr]::Zero) {
                [void]$p.CloseMainWindow()
            }
        } catch {}
    }
    Start-Sleep -Milliseconds 1200
    foreach ($p in $procs) {
        try {
            if (-not $p.HasExited) {
                Stop-Process -Id $p.Id -Force -ErrorAction Stop
            }
        } catch {}
    }
    Start-Sleep -Milliseconds 300
    Write-Pass "已停止目标目录内的旧进程"
}

function Get-WebView2Version {
    $regPaths = @(
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
        "HKLM:\SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
        "HKCU:\Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"
    )
    foreach ($rp in $regPaths) {
        if (Test-Path -LiteralPath $rp) {
            try {
                $pv = (Get-ItemProperty -LiteralPath $rp -Name "pv" -ErrorAction SilentlyContinue).pv
                if (-not [string]::IsNullOrWhiteSpace($pv) -and $pv -ne "0.0.0.0") {
                    return $pv
                }
            } catch {}
        }
    }
    return $null
}

function Install-WebView2Runtime {
    Write-Info "尝试安装 Microsoft Edge WebView2 Evergreen Runtime..."
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if ($winget) {
        Write-Info "使用 winget 安装 Microsoft.EdgeWebView2Runtime..."
        & $winget.Source install --id Microsoft.EdgeWebView2Runtime --silent --accept-package-agreements --accept-source-agreements
        $verAfterWinget = Get-WebView2Version
        if ($verAfterWinget) {
            return $verAfterWinget
        }
        Write-Warn "winget 安装未立即生效，尝试官方 Evergreen Bootstrapper..."
    }
    $bootstrapper = Join-Path $env:TEMP ("MicrosoftEdgeWebView2Setup-" + [Guid]::NewGuid().ToString("N") + ".exe")
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri "https://go.microsoft.com/fwlink/p/?LinkId=2124703" -OutFile $bootstrapper -UseBasicParsing
        $proc = Start-Process -FilePath $bootstrapper -ArgumentList "/silent /install" -Wait -PassThru
        if ($proc.ExitCode -ne 0) {
            throw "WebView2 Bootstrapper 退出码: $($proc.ExitCode)"
        }
    } finally {
        if (Test-Path -LiteralPath $bootstrapper) {
            Remove-Item -LiteralPath $bootstrapper -Force -ErrorAction SilentlyContinue
        }
    }
    return (Get-WebView2Version)
}

function Test-Prerequisites {
    param([switch]$AutoInstallWebView2)
    $summary = [ordered]@{
        OsArch = if ([Environment]::Is64BitOperatingSystem) { "x64" } else { "x86" }
        WebView2 = $null
        Python = $null
        MsvcBuildTools = $null
    }
    if (-not [Environment]::Is64BitOperatingSystem) {
        throw "Aura 仅支持 Windows 11 x64 (64 位操作系统)。"
    }
    Write-Pass "操作系统架构: Windows 64-bit ($([System.Environment]::OSVersion.VersionString))"

    $wv2 = Get-WebView2Version
    if (-not $wv2 -and $AutoInstallWebView2) {
        $wv2 = Install-WebView2Runtime
    }
    $summary.WebView2 = $wv2
    if ($wv2) {
        Write-Pass "WebView2 Evergreen Runtime: v$wv2"
    } else {
        Write-Warn "未检测到 Microsoft Edge WebView2 Evergreen Runtime（打开 Studio 页面需要它；核心托盘控灯不依赖）。`n        可加参数 -InstallWebView2 自动安装，或运行: winget install Microsoft.EdgeWebView2Runtime"
    }

    $pyCmd = Get-Command python -ErrorAction SilentlyContinue
    if (-not $pyCmd) { $pyCmd = Get-Command py -ErrorAction SilentlyContinue }
    if ($pyCmd) {
        try {
            $pyVer = (& $pyCmd.Source -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}')") 2>$null
            if ($LASTEXITCODE -eq 0 -and $pyVer) {
                $summary.Python = $pyVer.Trim()
                Write-Pass "Python (MCP 服务可选依赖): v$($summary.Python)"
            }
        } catch {}
    }
    if (-not $summary.Python) {
        Write-Info "未检测到 Python 3：外部 AI MCP 服务 (aura_mcp_server.py) 需要 Python 3.8+；日常桌面端运行不受影响。"
    }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        try {
            $instVer = (& $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion 2>$null)
            if ($instVer) {
                $summary.MsvcBuildTools = $instVer.Trim()
                Write-Pass "MSVC C++ Build Tools (Studio 原生 DLL 编译可选依赖): v$($summary.MsvcBuildTools)"
            }
        } catch {}
    }
    if (-not $summary.MsvcBuildTools) {
        Write-Info "未检测到 MSVC C++ Build Tools：日常控灯、Blockly 草稿与 EffectProgram IR 均可正常运行，仅 Studio 编译原生 C++ DLL 需要安装 Visual Studio C++ 桌面开发工作负载。"
    }
    return $summary
}

function Copy-TreeFiltered {
    param(
        [string]$SourceDir,
        [string]$DestDir,
        [string[]]$ExcludeTopNames = @()
    )
    $srcFull = [System.IO.Path]::GetFullPath($SourceDir).TrimEnd('\')
    $dstFull = [System.IO.Path]::GetFullPath($DestDir).TrimEnd('\')
    if (-not (Test-Path -LiteralPath $dstFull)) {
        New-Item -ItemType Directory -Path $dstFull -Force | Out-Null
    }
    $topExcluded = @{}
    foreach ($name in $ExcludeTopNames) {
        $topExcluded[$name.ToLowerInvariant()] = $true
    }
    foreach ($item in (Get-ChildItem -LiteralPath $srcFull -Force)) {
        $lowerName = $item.Name.ToLowerInvariant()
        if ($topExcluded.ContainsKey($lowerName)) {
            continue
        }
        $target = Join-Path $dstFull $item.Name
        if ($item.PSIsContainer) {
            Copy-TreeFiltered -SourceDir $item.FullName -DestDir $target
        } else {
            Copy-Item -LiteralPath $item.FullName -Destination $target -Force
        }
    }
}

function New-AuraShortcut {
    param(
        [string]$ShortcutPath,
        [string]$TargetPath,
        [string]$WorkingDirectory,
        [string]$IconPath,
        [string]$Description
    )
    $parent = Split-Path -Path $ShortcutPath -Parent
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $shell = New-Object -ComObject WScript.Shell
    $sc = $shell.CreateShortcut($ShortcutPath)
    $sc.TargetPath = $TargetPath
    $sc.WorkingDirectory = $WorkingDirectory
    if (Test-Path -LiteralPath $IconPath -PathType Leaf) {
        $sc.IconLocation = "$IconPath,0"
    }
    $sc.Description = $Description
    $sc.Save()
}

function Remove-AuraShortcuts {
    $candidates = @(
        (Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Aura\Aura.lnk"),
        (Join-Path ([Environment]::GetFolderPath("Desktop")) "Aura.lnk"),
        (Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Startup\Aura.lnk")
    )
    foreach ($sc in $candidates) {
        if (Test-Path -LiteralPath $sc -PathType Leaf) {
            Remove-Item -LiteralPath $sc -Force
            Write-Pass "已移除快捷方式: $sc"
        }
    }
    $startMenuFolder = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Aura"
    if ((Test-Path -LiteralPath $startMenuFolder -PathType Container) -and -not (Get-ChildItem -LiteralPath $startMenuFolder -Force)) {
        Remove-Item -LiteralPath $startMenuFolder -Force
    }
}

function Write-McpClientConfig {
    param([string]$OutPath, [string]$McpScriptPath)
    $pyExe = "python"
    if (-not (Get-Command python -ErrorAction SilentlyContinue) -and (Get-Command py -ErrorAction SilentlyContinue)) {
        $pyExe = "py"
    }
    $cfgObj = [ordered]@{
        mcpServers = [ordered]@{
            aura = [ordered]@{
                command = $pyExe
                args = @($McpScriptPath)
                env = [ordered]@{
                    AURA_DAEMON_URL = "http://127.0.0.1:19897"
                    AURA_STUDIO_URL = "http://127.0.0.1:19898"
                }
            }
        }
    }
    $json = ($cfgObj | ConvertTo-Json -Depth 6) + "`n"
    $parent = Split-Path -Path $OutPath -Parent
    if (-not [string]::IsNullOrWhiteSpace($parent) -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($OutPath, $json, $utf8NoBom)
    return $json
}

# ---------------------------------------------------------
# 主流程：路径解析
# ---------------------------------------------------------
if ($Portable -and -not [string]::IsNullOrWhiteSpace($DataDir)) {
    throw "-Portable 与 -DataDir 不能同时指定：便携模式的数据目录固定为安装目录本身。"
}

if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    if ($Portable) {
        $InstallDir = Join-Path $env:LOCALAPPDATA "AuraPortable"
    } else {
        $InstallDir = Join-Path $env:LOCALAPPDATA "Programs\Aura"
    }
}
$ResolvedInstallDir = [System.IO.Path]::GetFullPath($InstallDir)

if ($Portable) {
    $ResolvedDataDir = $ResolvedInstallDir
} elseif (-not [string]::IsNullOrWhiteSpace($DataDir)) {
    $ResolvedDataDir = [System.IO.Path]::GetFullPath($DataDir)
} else {
    $ResolvedDataDir = Join-Path $env:LOCALAPPDATA "Aura"
}

if ($Uninstall) {
    Write-Header "Aura 卸载流程"
    Assert-TargetPathsUnlocked -RootDirs @($ResolvedInstallDir, $ResolvedDataDir) -AllowForceStop:$ForceStop
    Remove-AuraShortcuts
    if (Test-Path -LiteralPath $ResolvedInstallDir) {
        Remove-Item -LiteralPath $ResolvedInstallDir -Recurse -Force
        Write-Pass "已移除安装目录: $ResolvedInstallDir"
    } else {
        Write-Info "安装目录不存在: $ResolvedInstallDir"
    }
    if ($PurgeData) {
        if ((Test-Path -LiteralPath $ResolvedDataDir) -and ($ResolvedDataDir -ne $ResolvedInstallDir)) {
            Remove-Item -LiteralPath $ResolvedDataDir -Recurse -Force
            Write-Pass "已清除用户数据与插件目录: $ResolvedDataDir"
        }
    } else {
        if (-not $Portable -and (Test-Path -LiteralPath $ResolvedDataDir)) {
            Write-Info "已保留用户配置与插件数据: $ResolvedDataDir (如需同时删除请加 -PurgeData)"
        }
    }
    Write-Pass "卸载完成。"
    exit 0
}

Write-Header "ROG Falchion Ace HFX - Aura 干净环境一键部署"

$TempExtractRoot = $null
try {
    # 1. 解析发布源（已解压目录 / ZIP 包 / dist 最新产物）
    Write-Step "1/6" "定位并校验 Aura 发布包"
    $SourceRoot = $null

    if (-not [string]::IsNullOrWhiteSpace($PackagePath)) {
        $resolvedPkg = [System.IO.Path]::GetFullPath($PackagePath)
        if (-not (Test-Path -LiteralPath $resolvedPkg)) {
            throw "指定的发布包路径不存在: $resolvedPkg"
        }
        if (Test-Path -LiteralPath $resolvedPkg -PathType Leaf) {
            if ([System.IO.Path]::GetExtension($resolvedPkg).ToLowerInvariant() -ne ".zip") {
                throw "不支持的发布包文件类型（仅支持 .zip 或已解压目录）: $resolvedPkg"
            }
            Assert-ZipSidecarSha256 -ZipFilePath $resolvedPkg
            Add-Type -AssemblyName System.IO.Compression.FileSystem
            $TempExtractRoot = Join-Path $env:TEMP ("aura-deploy-" + [Guid]::NewGuid().ToString("N"))
            New-Item -ItemType Directory -Path $TempExtractRoot -Force | Out-Null
            Write-Info "正在解压发布包至临时目录..."
            [System.IO.Compression.ZipFile]::ExtractToDirectory($resolvedPkg, $TempExtractRoot)
            if (Test-ValidPackageRoot -DirPath $TempExtractRoot) {
                $SourceRoot = $TempExtractRoot
            } else {
                $subDirs = @(Get-ChildItem -LiteralPath $TempExtractRoot -Directory)
                if ($subDirs.Count -eq 1 -and (Test-ValidPackageRoot -DirPath $subDirs[0].FullName)) {
                    $SourceRoot = $subDirs[0].FullName
                }
            }
        } else {
            if (Test-ValidPackageRoot -DirPath $resolvedPkg) {
                $SourceRoot = $resolvedPkg
            }
        }
    } else {
        if (Test-ValidPackageRoot -DirPath $PSScriptRoot) {
            $SourceRoot = $PSScriptRoot
            Write-Info "从当前解压发布目录部署: $SourceRoot"
        } else {
            $distDir = Join-Path $PSScriptRoot "dist"
            if (Test-Path -LiteralPath $distDir -PathType Container) {
                $latestDir = Get-ChildItem -LiteralPath $distDir -Directory -Filter "Aura-v*-windows-x64-*" |
                    Where-Object { Test-ValidPackageRoot -DirPath $_.FullName } |
                    Sort-Object LastWriteTime -Descending |
                    Select-Object -First 1
                if ($latestDir) {
                    $SourceRoot = $latestDir.FullName
                    Write-Info "自动发现 dist/ 最新候选目录: $SourceRoot"
                } else {
                    $latestZip = Get-ChildItem -LiteralPath $distDir -File -Filter "Aura-v*-windows-x64.zip" |
                        Sort-Object LastWriteTime -Descending |
                        Select-Object -First 1
                    if ($latestZip) {
                        Assert-ZipSidecarSha256 -ZipFilePath $latestZip.FullName
                        Add-Type -AssemblyName System.IO.Compression.FileSystem
                        $TempExtractRoot = Join-Path $env:TEMP ("aura-deploy-" + [Guid]::NewGuid().ToString("N"))
                        New-Item -ItemType Directory -Path $TempExtractRoot -Force | Out-Null
                        Write-Info "自动发现 dist/ 发布包并解压: $($latestZip.FullName)"
                        [System.IO.Compression.ZipFile]::ExtractToDirectory($latestZip.FullName, $TempExtractRoot)
                        if (Test-ValidPackageRoot -DirPath $TempExtractRoot) {
                            $SourceRoot = $TempExtractRoot
                        }
                    }
                }
            }
        }
    }

    if (-not $SourceRoot -or -not (Test-ValidPackageRoot -DirPath $SourceRoot)) {
        throw "未能定位完整的 Aura WinUI 发布包（缺少 Aura.exe 或 runtime-payload\runtime-manifest.json）。请将脚本放在解压后的发布目录内，或使用 -PackagePath 指定 ZIP/目录。"
    }
    $manifestObj = ConvertFrom-Json -InputObject (Get-Content -LiteralPath (Join-Path $SourceRoot "runtime-payload\runtime-manifest.json") -Raw -Encoding UTF8)
    $PkgVersion = [string]$manifestObj.version
    $PkgBuildId = [string]$manifestObj.build_id
    Write-Pass "已验证发布源: v$PkgVersion (build $PkgBuildId) -> $SourceRoot"

    if ($VerifyChecksums -or $TempExtractRoot) {
        Assert-DirectoryChecksums -PackageRoot $SourceRoot -Required:$VerifyChecksums
    }

    # 2. 环境前提检测
    if (-not $SkipPrereqCheck) {
        Write-Step "2/6" "检测目标系统运行环境"
        $prereq = Test-Prerequisites -AutoInstallWebView2:$InstallWebView2
    } else {
        Write-Step "2/6" "跳过运行环境前提检测 (-SkipPrereqCheck)"
        $prereq = $null
    }

    # 3. 进程安全检查 & 安装应用程序文件
    Write-Step "3/6" "部署自包含 WinUI 应用与 runtime-payload"
    Assert-TargetPathsUnlocked -RootDirs @($ResolvedInstallDir, $ResolvedDataDir) -AllowForceStop:$ForceStop

    $SourceNormalized = [System.IO.Path]::GetFullPath($SourceRoot).TrimEnd('\')
    $InstallNormalized = $ResolvedInstallDir.TrimEnd('\')
    if ($SourceNormalized -ieq $InstallNormalized) {
        Write-Info "当前已在目标安装目录运行，跳过自拷贝: $ResolvedInstallDir"
    } else {
        if (-not (Test-Path -LiteralPath $ResolvedInstallDir)) {
            New-Item -ItemType Directory -Path $ResolvedInstallDir -Force | Out-Null
        }
        $oldPayload = Join-Path $ResolvedInstallDir "runtime-payload"
        if (Test-Path -LiteralPath $oldPayload) {
            Remove-Item -LiteralPath $oldPayload -Recurse -Force
        }
        # 复制安装程序文件，跳过任何可能残留的运行态/用户配置文件
        Copy-TreeFiltered -SourceDir $SourceRoot -DestDir $ResolvedInstallDir -ExcludeTopNames @(
            "config.json", "portable.marker", "client-settings.json", "logs", "WebView2", "runtime", "aura-mcp-config.json"
        )
        Write-Pass "已部署程序与运行时资源至: $ResolvedInstallDir"
    }

    $markerPath = Join-Path $ResolvedInstallDir "portable.marker"
    if ($Portable) {
        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText($markerPath, "portable`n", $utf8NoBom)
        Write-Pass "已写入便携模式标记: $markerPath"
    } elseif (Test-Path -LiteralPath $markerPath -PathType Leaf) {
        Remove-Item -LiteralPath $markerPath -Force
        Write-Info "已清除旧 portable.marker，保持标准数据目录模式"
    }

    # 4. 初始化数据目录与内置插件
    Write-Step "4/6" "初始化数据目录与内置插件/扩展 ($ResolvedDataDir)"
    $PluginsDestDir = Join-Path $ResolvedDataDir "plugins"
    New-Item -ItemType Directory -Path $PluginsDestDir -Force | Out-Null

    $UserConfigPath = Join-Path $ResolvedDataDir "config.json"
    if (Test-Path -LiteralPath $UserConfigPath -PathType Leaf) {
        Write-Pass "保留已有用户配置（未覆盖）: $UserConfigPath"
    } else {
        Write-Info "首次启动将从已校验的 runtime-payload/config.example.json 自动生成 config.json"
    }

    $SourcePluginsDir = Join-Path $SourceRoot "plugins"
    if (Test-Path -LiteralPath $SourcePluginsDir -PathType Container) {
        $SrcPluginsNorm = [System.IO.Path]::GetFullPath($SourcePluginsDir).TrimEnd('\')
        $DstPluginsNorm = [System.IO.Path]::GetFullPath($PluginsDestDir).TrimEnd('\')
        if ($SrcPluginsNorm -ine $DstPluginsNorm) {
            Copy-TreeFiltered -SourceDir $SourcePluginsDir -DestDir $PluginsDestDir -ExcludeTopNames @(".cache", "target")
        }
        Write-Pass "已同步内置插件、MCP 服务与 Wooting 桥接至: $PluginsDestDir"
    } else {
        Write-Warn "发布源中未发现 plugins/ 目录，仅初始化空插件目录。"
    }

    # 5. 快捷方式、MCP 配置导出与可选 Wooting 桥接安装
    Write-Step "5/6" "配置快捷方式与扩展集成"
    $InstalledExe = Join-Path $ResolvedInstallDir "Aura.exe"
    $IconPath = Join-Path $ResolvedInstallDir "Assets\AppIcon.ico"

    if (-not $NoStartMenuShortcut) {
        $smShortcut = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Aura\Aura.lnk"
        New-AuraShortcut -ShortcutPath $smShortcut -TargetPath $InstalledExe -WorkingDirectory $ResolvedInstallDir -IconPath $IconPath -Description "Aura for ROG Falchion Ace HFX"
        Write-Pass "已创建开始菜单快捷方式: $smShortcut"
    }
    if ($DesktopShortcut) {
        $deskShortcut = Join-Path ([Environment]::GetFolderPath("Desktop")) "Aura.lnk"
        New-AuraShortcut -ShortcutPath $deskShortcut -TargetPath $InstalledExe -WorkingDirectory $ResolvedInstallDir -IconPath $IconPath -Description "Aura for ROG Falchion Ace HFX"
        Write-Pass "已创建桌面快捷方式: $deskShortcut"
    }
    if ($AutoStart) {
        $startupShortcut = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Startup\Aura.lnk"
        New-AuraShortcut -ShortcutPath $startupShortcut -TargetPath $InstalledExe -WorkingDirectory $ResolvedInstallDir -IconPath $IconPath -Description "Aura for ROG Falchion Ace HFX"
        Write-Pass "已创建开机自启快捷方式: $startupShortcut"
    }

    $McpServerScript = Join-Path $PluginsDestDir "mcp\aura_mcp_server.py"
    $ExportedMcpFile = $null
    if ($ExportMcpConfig -or -not [string]::IsNullOrWhiteSpace($McpConfigOut)) {
        if (-not (Test-Path -LiteralPath $McpServerScript -PathType Leaf)) {
            throw "未找到 MCP 服务脚本: $McpServerScript"
        }
        $ExportedMcpFile = if ([string]::IsNullOrWhiteSpace($McpConfigOut)) {
            Join-Path $ResolvedDataDir "aura-mcp-config.json"
        } else {
            [System.IO.Path]::GetFullPath($McpConfigOut)
        }
        $mcpJson = Write-McpClientConfig -OutPath $ExportedMcpFile -McpScriptPath $McpServerScript
        Write-Pass "已生成 MCP 客户端配置: $ExportedMcpFile"
        try {
            Set-Clipboard -Value $mcpJson
            Write-Info "已将 MCP JSON 配置复制到剪贴板，可直接粘贴到 Antigravity / Claude Desktop / Cursor。"
        } catch {}
    }

    if ($InstallWootingBridge) {
        $wootingInstaller = Join-Path $PluginsDestDir "bridges\wooting-analog-m605\install_plugin.ps1"
        $wootingDll = Join-Path $PluginsDestDir "bridges\wooting-analog-m605\wooting_analog_plugin_aura_m605.dll"
        if (-not (Test-Path -LiteralPath $wootingInstaller -PathType Leaf) -or -not (Test-Path -LiteralPath $wootingDll -PathType Leaf)) {
            Write-Warn "未找到完整 Wooting 桥接文件，跳过安装: $wootingDll"
        } else {
            $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
            $isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
            if ($isAdmin) {
                & $wootingInstaller
                if ($LASTEXITCODE -ne 0) { throw "Wooting 桥接安装失败 (退出码: $LASTEXITCODE)" }
            } else {
                Write-Info "安装 Wooting Analog SDK 全局插件需要管理员权限，正在请求 UAC 提权..."
                $elev = Start-Process -FilePath "powershell.exe" -Verb RunAs -Wait -PassThru -ArgumentList @(
                    "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$wootingInstaller`""
                )
                if ($elev.ExitCode -ne 0) {
                    throw "Wooting 桥接提权安装失败 (退出码: $($elev.ExitCode))"
                }
            }
            Write-Pass "已安装 Wooting Analog SDK 桥接至 C:\Program Files\WootingAnalogPlugins\aura-m605-plugin\"
        }
    }

    # 6. 部署完成摘要
    Write-Step "6/6" "部署完成"
    Write-Header "Aura v$PkgVersion 部署成功！"
    Write-Host "  [程序版本]  : v$PkgVersion (build $PkgBuildId)" -ForegroundColor Green
    Write-Host "  [部署模式]  : $(if ($Portable) { '便携模式 (portable.marker)' } else { '标准模式' })" -ForegroundColor Gray
    Write-Host "  [安装目录]  : $ResolvedInstallDir" -ForegroundColor Cyan
    Write-Host "  [数据目录]  : $ResolvedDataDir" -ForegroundColor Cyan
    Write-Host "  [插件目录]  : $PluginsDestDir" -ForegroundColor Gray
    if (Test-Path -LiteralPath $McpServerScript -PathType Leaf) {
        Write-Host "  [MCP 脚本]  : $McpServerScript" -ForegroundColor Gray
    }
    if ($ExportedMcpFile) {
        Write-Host "  [MCP 配置]  : $ExportedMcpFile" -ForegroundColor Yellow
    }
    Write-Host "`n后续提示:" -ForegroundColor Gray
    Write-Host "  1. 运行 `"$InstalledExe`" 启动 Aura。" -ForegroundColor Gray
    Write-Host "  2. 如需导出外部 AI MCP 配置: .\deploy_aura.bat -ExportMcpConfig" -ForegroundColor Gray
    Write-Host "  3. 如需接入 Wooting Analog SDK: .\deploy_aura.bat -InstallWootingBridge" -ForegroundColor Gray

    if ($Launch) {
        Write-Info "正在启动 Aura.exe..."
        Start-Process -FilePath $InstalledExe -WorkingDirectory $ResolvedInstallDir
    }
} finally {
    if ($TempExtractRoot -and (Test-Path -LiteralPath $TempExtractRoot)) {
        Remove-Item -LiteralPath $TempExtractRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

exit 0
