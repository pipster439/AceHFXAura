param(
    [string]$DestinationDir = "C:\Program Files\WootingAnalogPlugins\aura-m605-plugin",
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"

$destFile = Join-Path $DestinationDir "wooting_analog_plugin_aura_m605.dll"
if ($Uninstall) {
    if (Test-Path $destFile) {
        Remove-Item -Path $destFile -Force
        Write-Host "已卸载 Wooting Analog SDK 插件: $destFile"
    } else {
        Write-Host "未检测到已安装的 Wooting Analog SDK 插件: $destFile"
    }
    if ((Test-Path $DestinationDir) -and -not (Get-ChildItem -Path $DestinationDir -Force)) {
        Remove-Item -Path $DestinationDir -Force
    }
    exit 0
}

$packagedDll = Join-Path $PSScriptRoot "wooting_analog_plugin_aura_m605.dll"
$devDll = Join-Path $PSScriptRoot "target\release\wooting_analog_plugin_aura_m605.dll"
$dllPath = $null
if (Test-Path $packagedDll) {
    $dllPath = $packagedDll
} elseif (Test-Path $devDll) {
    $dllPath = $devDll
} else {
    throw "未找到 Wooting 桥接 DLL:`n  - $packagedDll`n  - $devDll`n如在源码仓库开发，请先运行: cargo build --release --manifest-path `"$PSScriptRoot\Cargo.toml`""
}

Write-Host "源插件文件: $dllPath"
Write-Host "目标插件目录: $DestinationDir"
New-Item -ItemType Directory -Path $DestinationDir -Force | Out-Null
Copy-Item -Path $dllPath -Destination $destFile -Force
Write-Host "已安装 Wooting Analog SDK 插件: $destFile"
