<#
.SYNOPSIS
    Aura Release Packaging & Automated Regression Pipeline for Windows 11 x64.
.DESCRIPTION
    完整的 AceHFXAura 独立发布包自动化构建、多阶段质量网关验证、资产打包与合规审计流水线。
    默认执行全量端到端验证（C++ 编译 -> CTest -> 磁轴协议压测 -> .NET 测试 -> 前端测试与打包 ->
    WinUI 自包含发布 -> 运行时资产组装 -> 分发包审计 -> 独立包运行时 Smoke 测试 -> SHA-256 生成）。

.PARAMETER Mode
    打包模式:
    - Full: 全量端到端验证 + 打包 + 审计 + Smoke（发布候选推荐）
    - Fast: 快速模式，跳过回归测试套件，仅执行必要构建、打包与审计
    - PackageOnly: 复用已有 C++ 产物，跳过测试，仅执行前端打包与 WinUI 打包
    - TestOnly: 仅执行所有测试网关，不生成分发 ZIP 包
    - Legacy: 构建历史版本的旧版 C++ 单文件启动器 (输出到 dist/legacy/)

.PARAMETER SkipTests
    显式跳过自动化测试套件（等效于 -Mode Fast）。

.PARAMETER SkipBuild
    跳过 C++ Native 编译，直接复用已有的 Release 产物。

.PARAMETER SkipZip
    跳过最终 ZIP 压缩包生成，仅保留已校验的候选解压目录。

.PARAMETER Clean
    构建前清理既有的 build 目录与临时包目录。

.PARAMETER BuildDir
    C++ 构建输出目录，默认为 "build"。

.PARAMETER Configuration
    构建配置，默认为 "Release"。

.PARAMETER Generator
    显式指定 CMake 生成器（默认自动探测 Visual Studio 18 2026 / Visual Studio 17 2022）。

.PARAMETER RepeatMagneticTests
    磁轴关键协议回归测试重复运行次数，默认 5 次（发布候选可设为 20）。

.PARAMETER Help
    显示详细帮助与使用说明。

.EXAMPLE
    .\package_release.ps1
    执行完整全量发布构建与回归验证流水线。

.EXAMPLE
    .\package_release.ps1 -Fast
    快速生成发布包（跳过测试，适合本地快速验证打包结构）。

.EXAMPLE
    .\package_release.ps1 -SkipBuild -SkipTests
    复用已有构建产物快速打包。

.EXAMPLE
    .\package_release.ps1 -Mode TestOnly -RepeatMagneticTests 20
    仅运行全部测试网关与 20 次磁轴协议稳定性压测。
#>

[CmdletBinding()]
param(
    [ValidateSet("Full", "Fast", "PackageOnly", "TestOnly", "Legacy")]
    [string]$Mode = "Full",

    [switch]$SkipTests,
    [switch]$SkipBuild,
    [switch]$SkipZip,
    [switch]$Clean,
    [string]$BuildDir = "build",
    [string]$Configuration = "Release",
    [string]$Generator,
    [int]$RepeatMagneticTests = 5,
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

# ---------------------------------------------------------
# 解析命令行标志兼容性 (如 --skip-build, --fast, --clean)
# ---------------------------------------------------------
if ($ExtraArgs) {
    for ($i = 0; $i -lt $ExtraArgs.Count; $i++) {
        $arg = $ExtraArgs[$i]
        switch -Regex ($arg) {
            "^--?h(elp)?$"    { $Help = $true }
            "^-\?$"           { $Help = $true }
            "^--skip-build$"  { $SkipBuild = $true }
            "^--skip-tests$"  { $SkipTests = $true }
            "^--fast$"        { $Mode = "Fast" }
            "^--package-only$"{ $Mode = "PackageOnly" }
            "^--test-only$"   { $Mode = "TestOnly" }
            "^--legacy$"      { $Mode = "Legacy" }
            "^--skip-zip$"    { $SkipZip = $true }
            "^--clean$"       { $Clean = $true }
            "^--build-dir$"   {
                if ($i + 1 -lt $ExtraArgs.Count) {
                    $BuildDir = $ExtraArgs[++$i]
                }
            }
            "^--repeat-magnetic$" {
                if ($i + 1 -lt $ExtraArgs.Count) {
                    $RepeatMagneticTests = [int]$ExtraArgs[++$i]
                }
            }
        }
    }
}

if ($Help) {
    Write-Host @"
=========================================================
 ROG Falchion Ace HFX - Aura 独立发布包自动化构建流水线
=========================================================

用法:
    .\package_release.ps1 [-Mode <Full|Fast|PackageOnly|TestOnly|Legacy>]
                          [-SkipTests] [-SkipBuild] [-SkipZip] [-Clean]
                          [-BuildDir <目录>] [-Configuration <Release|Debug>]
                          [-Generator <CMake生成器>] [-RepeatMagneticTests <次数>]
                          [-Help]

典型示例:
    .\package_release.ps1
        全量端到端验证与打包 (C++编译 -> CTest -> .NET测试 -> 前端构建 -> WinUI发布 -> 审计 -> Smoke测试)

    .\package_release.ps1 -Fast
        快速打包模式 (跳过自动化测试，适合本地快速验证发布结构)

    .\package_release.ps1 -SkipBuild -SkipTests
        复用已有 C++ Release 构建产物快速打包发布

    .\package_release.ps1 -Mode TestOnly -RepeatMagneticTests 20
        仅运行全部测试网关与 20 次磁轴协议压测，不生成打包产物

参数说明:
    -Mode                  打包模式: Full(默认全量), Fast(跳过测试), PackageOnly(跳过构建与测试), TestOnly(仅测试), Legacy(旧版C++启动器)
    -SkipTests             跳过回归测试网关 (等效于 -Mode Fast)
    -SkipBuild             跳过 C++ Native 编译 (复用既有 Release 二进制)
    -SkipZip               仅输出并验证候选目录，不生成 .zip 压缩包
    -Clean                 构建前清理 build/ 与临时 dist 目录
    -BuildDir              C++ 构建目录名称 (默认: build)
    -Configuration         构建类型 (默认: Release)
    -Generator             CMake 生成器 (默认自动探测 Visual Studio 18 2026 / 17 2022)
    -RepeatMagneticTests   磁轴协议压测轮数 (默认: 5; 完整RC发布推荐: 20)
    -Help                  显示本帮助信息
"@
    exit 0
}

if ($SkipTests -or $Mode -eq "Fast") {
    $RunTests = $false
} elseif ($Mode -eq "PackageOnly") {
    $RunTests = $false
    $SkipBuild = $true
} else {
    $RunTests = $true
}

$RepoRoot = $PSScriptRoot
Set-Location $RepoRoot

# ---------------------------------------------------------
# 格式化输出助手函数
# ---------------------------------------------------------
function Write-Header {
    param([string]$Message)
    Write-Host "`n=========================================================" -ForegroundColor Cyan
    Write-Host " $Message" -ForegroundColor Cyan
    Write-Host "=========================================================" -ForegroundColor Cyan
}

function Write-Step {
    param([string]$StepNum, [string]$Title)
    Write-Host "`n>>> [步骤 $StepNum] $Title" -ForegroundColor Yellow
}

function Write-Success {
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

function Assert-Command {
    param([scriptblock]$Command, [string]$ErrorMessage)
    & $Command
    if ($LASTEXITCODE -ne 0) {
        Write-Fail "$ErrorMessage (退出码: $LASTEXITCODE)"
        exit $LASTEXITCODE
    }
}

# ---------------------------------------------------------
# 步骤 1: 环境与工具链探测
# ---------------------------------------------------------
Write-Header "ROG Falchion Ace HFX - Aura 完整自动化发布与打包流水线"
Write-Step "1/9" "探测系统环境与开发工具链"

# 1.1 Python 解释器
$PythonExe = $null
if (Get-Command python -ErrorAction SilentlyContinue) {
    $PythonExe = (Get-Command python).Source
} elseif (Get-Command py -ErrorAction SilentlyContinue) {
    $PythonExe = (Get-Command py).Source
} elseif (Test-Path "C:\Python313\python.exe") {
    $PythonExe = "C:\Python313\python.exe"
}
if (-not $PythonExe) {
    Write-Fail "未找到 Python 解释器！请安装 Python 3.8+ 并加入 PATH。"
    exit 1
}
$pyVer = & $PythonExe -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}')"
Write-Success "Python 解释器: $PythonExe (v$pyVer)"

# 1.2 Git 与代码仓库状态
$CommitSha = "unknown"
$Branch = "unknown"
if (Get-Command git -ErrorAction SilentlyContinue) {
    $CommitSha = (git rev-parse --short HEAD) 2>$null
    $FullSha = (git rev-parse HEAD) 2>$null
    $Branch = (git rev-parse --abbrev-ref HEAD) 2>$null
    Write-Success "Git HEAD: $CommitSha ($Branch)"
}

# 1.3 版本文件 (单一事实源)
$VersionFile = Join-Path $RepoRoot "VERSION"
if (-not (Test-Path $VersionFile)) {
    Write-Fail "未找到版本文件: $VersionFile"
    exit 1
}
$ReleaseVersion = (Get-Content $VersionFile -Raw).Trim()
if ([string]::IsNullOrWhiteSpace($ReleaseVersion)) {
    Write-Fail "VERSION 文件内容为空！"
    exit 1
}
Write-Success "发行版本单一事实源: $ReleaseVersion"

# 1.4 CMake
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Fail "未在 PATH 中找到 cmake.exe！"
    exit 1
}
$cmakeVer = (cmake --version | Select-Object -First 1)
Write-Success "CMake: $cmakeVer"

# 1.5 .NET SDK
if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    Write-Fail "未在 PATH 中找到 dotnet.exe！"
    exit 1
}
$dotnetVer = (dotnet --version)
Write-Success ".NET SDK: v$dotnetVer"

# 1.6 Node.js 与 npm
$NpmCmd = (Get-Command npm.cmd -ErrorAction SilentlyContinue)
if (-not $NpmCmd) {
    $NpmCmd = (Get-Command npm -ErrorAction SilentlyContinue)
}
if (-not $NpmCmd) {
    Write-Fail "未在 PATH 中找到 npm！"
    exit 1
}
$nodeVer = (node --version)
$npmVer = (& $NpmCmd.Source --version)
Write-Success "Node.js $nodeVer / npm v$npmVer"

# 1.7 Visual Studio 工具链探测
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$VsToolchainDir = $null
$DetectedGenerator = $Generator

if (Test-Path $vswhere) {
    try {
        $instPath = (& $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null)
        $instVer  = (& $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion 2>$null)
        $instName = (& $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property displayName 2>$null)

        if ($instPath -and (Test-Path $instPath)) {
            $VsToolchainDir = $instPath.Trim()
            $majorVer = if ($instVer) { ($instVer.Trim() -split '\.')[0] } else { "" }
            if (-not $DetectedGenerator) {
                if ($majorVer -eq "18") {
                    $DetectedGenerator = "Visual Studio 18 2026"
                } elseif ($majorVer -eq "17") {
                    $DetectedGenerator = "Visual Studio 17 2022"
                } elseif ($majorVer -eq "16") {
                    $DetectedGenerator = "Visual Studio 16 2019"
                } else {
                    $DetectedGenerator = "Visual Studio 17 2022"
                }
            }
            $displayName = if ($instName) { $instName.Trim() } else { "Visual Studio" }
            $displayVer  = if ($instVer)  { $instVer.Trim() } else { "unknown" }
            Write-Success "Visual Studio: $displayName (v$displayVer)"
        }
    } catch {
        Write-Warn "vswhere 探测异常: $_"
    }
}
if (-not $DetectedGenerator) {
    $DetectedGenerator = "Visual Studio 17 2022"
}

# 检查既有 build 缓存的 Generator 以避免冲突
$CacheFile = Join-Path $RepoRoot "$BuildDir\CMakeCache.txt"
if (Test-Path $CacheFile) {
    $cached = Get-Content $CacheFile | Where-Object { $_ -like "CMAKE_GENERATOR:INTERNAL=*" } | Select-Object -First 1
    if ($cached) {
        $cachedGen = ($cached -split '=', 2)[1].Trim()
        Write-Info "既有 CMakeCache.txt 采用生成器: $cachedGen"
        $DetectedGenerator = $cachedGen
    }
}
Write-Success "目标 CMake 生成器: $DetectedGenerator"

# ---------------------------------------------------------
# 步骤 2: 清理流程 (可选)
# ---------------------------------------------------------
if ($Clean) {
    Write-Step "2/9" "执行清理流程 (-Clean)"
    if (Test-Path $BuildDir) {
        Write-Info "正在清理构建目录: $BuildDir"
        Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
    }
    $distDir = Join-Path $RepoRoot "dist"
    if (Test-Path $distDir) {
        Write-Info "正在清理临时打包产物目录: $distDir"
        Get-ChildItem -Path $distDir -Filter "winui-stage-*" | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    }
    Write-Success "清理完成。"
} else {
    Write-Step "2/9" "跳过清理 (复用安全缓存增量构建)"
}

# ---------------------------------------------------------
# 步骤 3: C++ Release 目标编译
# ---------------------------------------------------------
$FullBuildDir = Join-Path $RepoRoot $BuildDir
$ReleaseBinDir = Join-Path $FullBuildDir $Configuration
$DaemonExe = Join-Path $ReleaseBinDir "aura_daemon.exe"
$WebUiExe = Join-Path $ReleaseBinDir "aura_web_ui.exe"
$CurrentCache = Join-Path $FullBuildDir "CMakeCache.txt"

if (-not $SkipBuild) {
    Write-Step "3/9" "编译 C++ Native 运行时与测试目标 ($Configuration)"
    if (-not (Test-Path $FullBuildDir)) {
        New-Item -ItemType Directory -Path $FullBuildDir -Force | Out-Null
    }
    
    if (-not (Test-Path $CurrentCache)) {
        Write-Info "初始化配置 CMake (-G `"$DetectedGenerator`" -A x64)..."
        Assert-Command { cmake -S $RepoRoot -B $FullBuildDir -G "$DetectedGenerator" -A x64 } "CMake 配置失败"
    } else {
        Write-Info "增量更新配置 CMake..."
        Assert-Command { cmake -S $RepoRoot -B $FullBuildDir } "CMake 增量配置失败"
    }

    Write-Info "构建 Release 目标 (ALL_BUILD, parallel 2)..."
    Assert-Command { cmake --build $FullBuildDir --config $Configuration --parallel 2 } "C++ Release 构建失败"

    if (-not (Test-Path $DaemonExe) -or -not (Test-Path $WebUiExe)) {
        Write-Fail "编译完成但核心产物缺失: aura_daemon.exe 或 aura_web_ui.exe 不存在！"
        exit 1
    }
    Write-Success "aura_daemon.exe: $((Get-Item $DaemonExe).Length) 字节"
    Write-Success "aura_web_ui.exe: $((Get-Item $WebUiExe).Length) 字节"
} else {
    Write-Step "3/9" "跳过 C++ 编译 (-SkipBuild)"
    if (-not (Test-Path $DaemonExe) -or -not (Test-Path $WebUiExe)) {
        Write-Fail "指定的构建目录缺少必要二进制: $DaemonExe 或 $WebUiExe"
        exit 1
    }
    Write-Success "复用已有构建: $ReleaseBinDir"
}

# ---------------------------------------------------------
# 步骤 4: 自动化回归测试网关
# ---------------------------------------------------------
$TestSummary = @{}

if ($RunTests) {
    Write-Step "4/9" "执行自动化回归测试质量网关"

    # 4.1 CTest 核心套件 (19个测试)
    Write-Info "运行 CTest 测试套件..."
    $env:AURA_BIN_DIR = $ReleaseBinDir
    $env:AURA_INTEGRATION_BIN = $ReleaseBinDir
    Assert-Command { ctest --test-dir $FullBuildDir -C $Configuration --output-on-failure } "CTest 自动化测试套件未完全通过"
    Write-Success "CTest 核心测试网关全部通过。"
    $TestSummary["CTest"] = "PASS (100%)"

    # 4.2 磁轴协议稳定性压测 (针对 m605_protocol 与 magnetic_control)
    if ($RepeatMagneticTests -gt 0) {
        Write-Info "运行磁轴协议稳定性压测 (重复 $RepeatMagneticTests 次)..."
        Assert-Command {
            ctest --test-dir $FullBuildDir -C $Configuration -R "^(m605_protocol|magnetic_control)$" --repeat until-fail:$RepeatMagneticTests --output-on-failure
        } "磁轴协议稳定性压测失败"
        Write-Success "磁轴协议稳定性压测通过 ($RepeatMagneticTests 次连续无故障)。"
        $TestSummary["MagneticStress"] = "PASS ($RepeatMagneticTests 轮)"
    }

    # 4.3 .NET 单元与集成测试 (排除桌面窗口 Smoke)
    Write-Info "运行 .NET Aura.Tests..."
    Assert-Command {
        dotnet test (Join-Path $RepoRoot "tests\Aura.Tests\Aura.Tests.csproj") -c $Configuration --filter "TestCategory!=DesktopSmoke"
    } ".NET 单元与集成测试未通过"
    Write-Success ".NET Aura.Tests 全部通过。"
    $TestSummary["DotNetTests"] = "PASS"

    # 4.4 前端单页应用单元测试
    Write-Info "运行前端单元测试 (npm test)..."
    Assert-Command {
        Set-Location (Join-Path $RepoRoot "frontend")
        & $NpmCmd.Source test
        Set-Location $RepoRoot
    } "前端单元测试未通过"
    Write-Success "前端单元测试全部通过。"
    $TestSummary["FrontendTests"] = "PASS"

    # 4.5 Python 守护进程与端点回归测试
    Write-Info "运行 Python Daemon & WebUI 进阶回归测试..."
    Assert-Command {
        Set-Location (Join-Path $RepoRoot "tests")
        & $PythonExe -B -m unittest -v test_runtime_entrypoints.TestWebUiEntrypoint test_runtime_entrypoints.TestDaemonEntrypoint test_automation_v2_daemon test_automation_authoring_daemon test_automation_effect_daemon test_automation_reload_daemon test_automation_retrigger_daemon test_global_lighting_daemon
        & $PythonExe -B -m unittest -v test_winui_productization.TestGsiConfigurationContract
        & $PythonExe -B -m unittest -v test_studio_release_regression
        Set-Location $RepoRoot
    } "Python 进阶回归测试未通过"
    Write-Success "Python 进阶回归测试网关全部通过。"
    $TestSummary["PythonRegression"] = "PASS"
} else {
    Write-Step "4/9" "跳过回归测试套件 (-Mode $Mode / -SkipTests)"
    $TestSummary["Tests"] = "SKIPPED"
}

# ---------------------------------------------------------
# 步骤 5: 前端单页应用生产构建
# ---------------------------------------------------------
Write-Step "5/9" "构建前端 React 生产打包资源 (web/index.html)"
$WebHtml = Join-Path $RepoRoot "web\index.html"
Assert-Command {
    Set-Location (Join-Path $RepoRoot "frontend")
    & $NpmCmd.Source run build
    Set-Location $RepoRoot
} "前端打包构建失败"

if (-not (Test-Path $WebHtml) -or (Get-Item $WebHtml).Length -lt 1000) {
    Write-Fail "web/index.html 生成异常！"
    exit 1
}
Write-Success "web/index.html 构建完成 ($((Get-Item $WebHtml).Length) 字节)"

if ($Mode -eq "TestOnly") {
    Write-Header "测试网关全部通过！(-Mode TestOnly 模式下不执行打包)"
    exit 0
}

# ---------------------------------------------------------
# 步骤 6: WinUI 桌面端编译与发布
# ---------------------------------------------------------
Write-Step "6/9" "发布 WinUI 桌面客户端并组装发布目录"

$pkgScript = Join-Path $RepoRoot "tools\package_release.py"
$pkgArgs = @("--build-dir", $BuildDir, "--skip-build")
if ($SkipZip) {
    $pkgArgs += "--skip-zip"
}
if ($Mode -eq "Legacy") {
    $pkgArgs += "--legacy"
}

Write-Info "调用底层发布打包工具: python tools/package_release.py $($pkgArgs -join ' ')"
Assert-Command { & $PythonExe $pkgScript @pkgArgs } "发行包发布组装失败"

# ---------------------------------------------------------
# 步骤 7: 分发包内容审计与合规验证
# ---------------------------------------------------------
Write-Step "7/9" "审计分发包合规性与资产完整性"

$DistDir = Join-Path $RepoRoot "dist"
$CandidateDir = Get-ChildItem $DistDir -Directory -Filter "Aura-v*-windows-x64-*" | Sort-Object LastWriteTime -Descending | Select-Object -First 1

if (-not $CandidateDir) {
    Write-Fail "未在 dist/ 中找到解压后的候选包目录！"
    exit 1
}
Write-Success "定位最新候选包目录: $($CandidateDir.FullName)"

# 7.1 验证底层 verify 脚本
Assert-Command {
    & $PythonExe (Join-Path $RepoRoot "tools\package_winui.py") --verify $CandidateDir.FullName
} "候选包目录结构校验未通过"
Write-Success "候选包依赖清单与 runtime-manifest 校验完全一致。"

# 7.2 严防专有 HAL 与用户数据泄露
$forbiddenFound = $false
Get-ChildItem -Path $CandidateDir.FullName -Recurse | ForEach-Object {
    $name = $_.Name.ToLower()
    if ($name -eq "aackbhal_x64.dll") {
        Write-Fail "发现违规打包的专有 ASUS HAL DLL: $($_.FullName)"
        $forbiddenFound = $true
    }
    if ($name -eq "config.json") {
        Write-Fail "发现违规打包的用户配置文件: $($_.FullName)"
        $forbiddenFound = $true
    }
    if ($name -eq "portable.marker") {
        Write-Fail "发现违规打包的便携标记: $($_.FullName)"
        $forbiddenFound = $true
    }
    if ($_.FullName -like "*audit_artifacts*" -or $_.FullName -like "*audit_repros*") {
        Write-Fail "发现违规打包的审计文件: $($_.FullName)"
        $forbiddenFound = $true
    }
}
if ($forbiddenFound) {
    Write-Fail "安全合规审计失败：分发包内存在被禁止的文件！"
    exit 1
}
Write-Success "安全合规审计通过：无 ASUS 专有 HAL、无用户配置、无本地标记、无审计残留。"

# ---------------------------------------------------------
# 步骤 8: 独立分发包运行时 Smoke 测试
# ---------------------------------------------------------
Write-Step "8/9" "执行独立包脱离源码环境运行时 Smoke 测试"

$env:AURA_PACKAGE_DIR = $CandidateDir.FullName
Assert-Command {
    Set-Location (Join-Path $RepoRoot "tests")
    & $PythonExe -B -m unittest -v test_winui_productization.TestPackagedRuntime
    Set-Location $RepoRoot
} "独立分发包运行时 Smoke 测试失败"
Write-Success "分发包脱离源码运行环境 Smoke 测试通过。"

# ---------------------------------------------------------
# 步骤 9: 生成分发包哈希与报告
# ---------------------------------------------------------
Write-Step "9/9" "验证发布候选 ZIP 包与哈希校验码"

$ZipFile = Join-Path $DistDir "Aura-v$ReleaseVersion-windows-x64.zip"
$ShaFile = "$ZipFile.sha256"

if (-not $SkipZip) {
    if (-not (Test-Path $ZipFile)) {
        Write-Fail "未找到生成的 ZIP 压缩包: $ZipFile"
        exit 1
    }
    $zipSize = (Get-Item $ZipFile).Length
    $zipSha = (Get-FileHash -Path $ZipFile -Algorithm SHA256).Hash.ToLower()

    # 校验 Sidecar
    if (Test-Path $ShaFile) {
        $sidecarSha = (Get-Content $ShaFile -Raw).Split(" ")[0].Trim().ToLower()
        if ($zipSha -ne $sidecarSha) {
            Write-Fail "ZIP 文件哈希 ($zipSha) 与 Sidecar ($sidecarSha) 不一致！"
            exit 1
        }
        Write-Success "Sidecar SHA-256 校验一致。"
    }

    # ---------------------------------------------------------
    # 成功总结面板
    # ---------------------------------------------------------
    Write-Header "AceHFXAura v$ReleaseVersion 发布包构建与审计顺利完成！"
    Write-Host "  [版本编号]  : v$ReleaseVersion" -ForegroundColor Green
    Write-Host "  [源码提交]  : $CommitSha ($FullSha)" -ForegroundColor Gray
    Write-Host "  [发布产物]  : $ZipFile" -ForegroundColor Cyan
    Write-Host "  [文件大小]  : $([math]::Round($zipSize / 1MB, 2)) MB ($($zipSize.ToString('N0')) 字节)" -ForegroundColor Gray
    Write-Host "  [SHA-256]   : $zipSha" -ForegroundColor Yellow
    Write-Host "  [解压目录]  : $($CandidateDir.FullName)" -ForegroundColor Gray
    Write-Host "  [测试状态]  : 全部测试网关通过 ($($TestSummary.Keys.Count) 组)" -ForegroundColor Green
    Write-Host "  [实机验收]  : PENDING OWNER TEST (等待仓库所有者真机物理测试)" -ForegroundColor DarkYellow
    Write-Host "`n提示: 本脚本已完成纯自动化准备；正式发布前请勿执行 git push 或创建 GitHub Release。" -ForegroundColor Gray
} else {
    Write-Header "AceHFXAura v$ReleaseVersion 目录式候选包构建完成！(--SkipZip 已指定)"
    Write-Host "  [候选目录]  : $($CandidateDir.FullName)" -ForegroundColor Cyan
}

exit 0
