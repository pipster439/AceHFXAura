# Wooting Analog SDK Plugin for ASUS ROG Falchion Ace HFX (Aura M605)

此 Rust 插件 (`wooting_analog_plugin_aura_m605.dll`) 实现了 `wooting-analog-plugin-dev` (`0.7.1`) 的标准插件接口，从 Aura 后台守护进程 (`aura_daemon`) 发布的无锁命名共享内存 `Local\AuraM605HallAnalogV1` 读取 68 颗磁轴按键的实时霍尔归一化深度 (`0.0 ~ 1.0`)。

## 工作方式

1. 键码完全兼容：Falchion Ace HFX 固件霍尔流中的 `analog_id` 本身就是标准 USB HID Usage ID (`Usage Page 0x07`, 例如 `A = 4`, `W = 26`, `Space = 44`)，直接匹配 Wooting Analog SDK 的原生 HID 键码，无需二次映射。
2. 零硬件句柄竞争：`aura_daemon` 独占并安全维护 `0xFF00` 霍尔流，通过 seq-lock 共享内存广播深度；插件 DLL 仅执行只读映射，不会与 Aura 或键盘通信冲突。

## 构建

```powershell
cargo test --manifest-path G:\Aura\plugins\bridges\wooting-analog-m605\Cargo.toml
cargo build --release --manifest-path G:\Aura\plugins\bridges\wooting-analog-m605\Cargo.toml
```

输出产物：`G:\Aura\plugins\bridges\wooting-analog-m605\target\release\wooting_analog_plugin_aura_m605.dll`

## 安装到 Wooting Analog SDK 全局插件目录（需管理员 PowerShell）

```powershell
powershell -ExecutionPolicy Bypass -File G:\Aura\plugins\bridges\wooting-analog-m605\install_plugin.ps1
```

插件将被复制到 `C:\Program Files\WootingAnalogPlugins\aura-m605-plugin\wooting_analog_plugin_aura_m605.dll`。

## 使用方法

1. 启动 Aura 守护进程，并在「磁轴设置」页面打开 **实时行程**（或调用 `POST http://127.0.0.1:19897/api/magnetic/hall/start`）。
2. 打开任何支持 Wooting Analog SDK 的应用/工具（如 Universal Analog Controller / Wooting Analog Test App / 支持 Wooting Analog SDK 的游戏或 OBS 输入可视化工具）。
3. 停止采集时在 Aura 关闭 **实时行程**（后台发送安全的 `04 04 01`，不会锁死键盘输入）。
