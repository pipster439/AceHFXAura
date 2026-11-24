$ErrorActionPreference = "Stop"

$dllPath = Join-Path $PSScriptRoot "target\release\wooting_analog_plugin_aura_m605.dll"
if (-not (Test-Path $dllPath)) {
    throw "未找到编译产物: $dllPath`n请先运行: cargo build --release --manifest-path `"$PSScriptRoot\Cargo.toml`""
}

$destDir = "C:\Program Files\WootingAnalogPlugins\aura-m605-plugin"
Write-Host "目标插件目录: $destDir"
New-Item -ItemType Directory -Path $destDir -Force | Out-Null
Copy-Item -Path $dllPath -Destination (Join-Path $destDir "wooting_analog_plugin_aura_m605.dll") -Force
Write-Host "已安装 Wooting Analog SDK 插件: $destDir\wooting_analog_plugin_aura_m605.dll"
