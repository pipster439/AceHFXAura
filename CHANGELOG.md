# Changelog

所有值得关注的变更都记录在此。发行版本号以仓库根目录的 [`VERSION`](VERSION) 为单一事实源。

## [0.1.0-alpha.1]

### 核心能力

- 为 ROG Falchion Ace HFX 提供 68 键与灯条通道的 RGB 帧控制，包含 HAL 文件 SHA-256 和运行时签名双重兼容性 Gate。
- 本地 Web Studio：Blockly 光效编辑、真机预览、草稿保存、C++17 原生插件发布与热加载。
- 按前台进程与 CS2 GSI 状态执行自动化，支持基础方案、持续叠加和事件叠加。
- 自动检测 Steam/CS2 配置目录，由用户确认后安装 GSI 配置。
- 双进程 daemon/Web UI 架构、配置热重载、动态插件影子加载与单文件 `Aura.exe` 启动器。

### 已知限制

- 当前仅针对 ROG Falchion Ace HFX 和已验证的 ASUS HAL v1.3.46.0（SHA-256 `52d575bf942b7551b3f120c446bf0d853e36f9225c6b9a17407a80e0b1829f04`）。其他 HAL 版本会 fail closed。
- 公开 Release 不包含 `AacKbHal_x64.dll`；用户必须先安装 ASUS 官方 Armoury Crate / `Aac_Keyboard` 驱动包。
- Studio 原生发布需要本机 MSVC Build Tools、C++ Desktop workload、x64 工具集和 Windows SDK；未安装时仍可编辑、预览和保存草稿。
- CS2 实际提供的 GSI 字段受对局与观战模式限制；部分字段不会在第一人称对局中出现。
- 自动化基础方案使用 first-match 规则顺序；更高层的 Scene 容器尚未实现。
- 已发布插件的历史 DLL 尚不会自动清理。

[0.1.0-alpha.1]: https://github.com/pipster439/AceHFXAura/releases/tag/v0.1.0-alpha.1
