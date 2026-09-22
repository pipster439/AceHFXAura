## 0.1.0-alpha.3 candidate — breaking Automation cleanup (unreleased)

Supersedes the prior alpha.3 candidate. Automation v2 is the only supported model; retired nonempty sections require explicit development migration. Direct V2 Blockly authoring uses typed effect selections. Daemon-backed GSI simulation isolates sources and exercises real event detection and composition. Plugin ABI v1, LegacyEnvelope and legacy_hal remain supported. All candidate packaging and hashes must be regenerated; no release or tag is created by this work.

Earlier entries below are historical descriptions of their respective revisions.

# Changelog

所有值得关注的变更都记录在此。发行版本号以仓库根目录的 [`VERSION`](VERSION) 为单一事实源。

## [0.1.0-alpha.3]

### Automation v2

- 统一 state / rising / event 作者模型：state → Activate Profile 或持续 Trigger Effect；rising/event → 一次性 Trigger Effect。
- 明确 restart / ignore_while_active / stack / queue 重触发策略；堆叠和队列受容量、过期与每帧启动预算限制。
- capabilities / CRUD、legacy 来源与共存展示、遮蔽确认，以及显式 Application Rule → V2 Promote/Convert 事务。

### Effect runtime 与 Studio

- 插件实例绑定 generation，支持 finished/opacity 生命周期导出；Alpha/Additive 混合与 Replace/Overlay 合成，固定持续层/瞬态层顺序。
- Studio 显式 continuous / one-shot 发布，generation-safe 热重载及事务式发布/配置更新；旧运行 generation 安全保留，失败不覆盖原发布。
- 区分语义与外观配置重载，兼容时保留活动/排队效果；作用域失效或 GSI 过期取消关联工作，恢复不重播旧事件。v2 GSI 决策默认 3 秒新鲜度，与连接在线状态分开。

### 可靠性与发布验证

- 修复 Automation 接纳时的快照一致性：条件、事件与前台作用域来自同一不可变快照。
- 插件 generation 回收移出 Automation runtime mutex，避免销毁回调造成锁重入/死锁。
- 扩展生产实现回归覆盖；Windows CI 构建完整 CTest 目标及 Studio fixture，运行 dry-run daemon 集成。冻结 Stage 0 证据禁用 checkout 换行转换；共享 Studio fixture 生成步骤避免并发写入冲突。

### 已知限制

- 实验性 alpha，目标 Windows 11 x64 + ROG Falchion Ace HFX；本次自动化验证不代表 Owner 已完成真键盘或 live CS2 验收。Windows 10 未正式验证。
- 默认 auto 优先 native_hid；ASUS 专有 HAL DLL 不随包分发，仅 legacy/auto 回退使用用户本机已安装且通过 Gate 的组件。
- 独立 Light Bar 控制未实现。
- Studio 原生发布仍要求 MSVC Build Tools / Visual Studio C++ Desktop workload、x64 工具集与 Windows SDK。
- 历史插件 DLL 不自动清理；原生插件不提供崩溃/挂起隔离。
- CS2 实际字段取决于游戏模式与状态。
- 发行包是 legacy C++ launcher + daemon + Web Studio，不包含 WinUI；WinUI 仅完成源码构建验证，分发打包仍待单独完成。

## [0.1.0-alpha.2]

### 新增

- **Native Win32 HID MI_01 后端**：直接通过 Windows 原生 HID API (`SetupAPI` / `hid.lib`) 与键盘 `MI_01` 灯控端点通信。
- **默认 Auto 模式**：优先尝试 Native HID 连接；仅在 HID 端点不可用时安全回退至 legacy HAL。
- **显式 Backend 配置**：支持通过 CLI 参数 `--backend` 或配置文件 `hardware_backend` 指定 `auto`、`native_hid`、`legacy_hal`。
- **免专有驱动驻留**：Native HID 默认路径不加载、不调用 `AacKbHal_x64.dll`。

### 改进

- **USB 边界隔离保留**：内置固件特定的 64 字节 USB 边界隔离（Byte 63 / Slot 14 填充），规避当前硬件实测中观察到的 Byte63 / Slot14 色彩异常与闪烁。
- **HID 端点严格匹配**：基于 VID `0x0B05`、PID `0x1B7E`、UsagePage `0xFF00`、Usage `0x0001`、ReportLength 65 与接口路径进行严格白名单过滤。
- **Legacy HAL 架构降级**：原闭源驱动路径作为备用排查与回退方案（保留 SHA-256 与内存崩溃防御补丁）。

### 修复

- **Overlapped I/O 超时与取消生命周期**：改用 `CancelIoEx` 精准取消，并在返回前通过 `GetOverlappedResult(..., TRUE)` 确保 completion drain，妥善处理 completion race，杜绝栈上 `OVERLAPPED` 生命周期逸出导致 UAF；补齐同步与异步 65 字节短写校验。
- **Legacy HAL 模块彻底卸载**：`ReleaseLegacyHalInternal()` 在释放 COM 接口后执行 `FreeLibrary(hHalMod_)` 与状态指针置零，确保 legacy → native 切换后 DLL 不再驻留进程。
- **异常退出隔离探测后端继承**：主进程在加载配置后统一计算 `resolved_backend`，`--probe-hardware` 隔离子进程无条件继承配置的 backend，避免配置 `native_hid` 时子进程回退到 auto / legacy。
- **严格 Backend 参数与配置校验**：引入 `TryParseHardwareBackend`，CLI 非法后端参数立即以错误码 1 退出，`config.json` 中拼写错误（如 `native_hd`）明确使 `LoadConfig` 失败，杜绝静默降级为 Auto。

### 已知限制

- 当前仅在 Windows 11 x64 + ROG Falchion Ace HFX 实机上完成严格验证；Windows 10 未经验证。
- 独立 Light Bar 控制未实现；在当前已验证的 MI_01 Native Direct RGB 路径下，顶部 Light Bar 灯光表现跟随 Row 1。
- Studio 原生 C++ 插件发布仍要求本机安装 MSVC Build Tools / Visual Studio、C++ Desktop workload 与 Windows SDK。
- CS2 实际提供的 GSI 字段受对局与观战模式限制。
- 已发布插件的历史 DLL 尚不会自动清理。

## [0.1.0-alpha.1]

### 核心能力

- 为 ROG Falchion Ace HFX 提供 68 键与灯条通道的 RGB 帧控制，包含 HAL 文件 SHA-256 和运行时签名双重兼容性 Gate。
- 本地 Web Studio：Blockly 光效编辑、真机预览、草稿保存、C++17 原生插件发布与热加载。
- 按前台进程与 CS2 GSI 状态执行自动化，支持基础方案、持续叠加和事件叠加。
- 自动检测 Steam/CS2 配置目录，由用户确认后安装 GSI 配置。
- 双进程 daemon/Web UI 架构、配置热重载、动态插件影子加载与单文件 `Aura.exe` 启动器。

### 许可与分发

- 项目代码正式采用 GNU General Public License v3.0 only (`GPL-3.0-only`)，根目录附带标准 [`LICENSE`](LICENSE) 文件。
- ASUS 专有 HAL DLL（`AacKbHal_x64.dll`）不属于 GPL 授权内容，亦不随公开发行包分发；运行时仅动态加载用户本机已安装且通过兼容性 Gate 校验的官方驱动。
- 单文件 `Aura.exe` 包含运行时所需的 Aura Plugin SDK，无需源码 checkout 即可使用 Studio 的原生发布功能，但原生发布仍要求本机安装 MSVC Build Tools、C++ Desktop workload、x64 工具集和 Windows SDK。

### 已知限制

- 当前仅针对 ROG Falchion Ace HFX 和已验证的 ASUS HAL v1.3.46.0（SHA-256 `52d575bf942b7551b3f120c446bf0d853e36f9225c6b9a17407a80e0b1829f04`）。其他 HAL 版本会 fail closed。
- 公开 Release 不包含 `AacKbHal_x64.dll`；用户必须先安装 ASUS 官方 Armoury Crate / `Aac_Keyboard` 驱动包。
- Studio 原生发布需要本机 MSVC Build Tools、C++ Desktop workload、x64 工具集和 Windows SDK；未安装时仍可编辑、预览和保存草稿。
- CS2 实际提供的 GSI 字段受对局与观战模式限制；部分字段不会在第一人称对局中出现。
- 自动化基础方案使用 first-match 规则顺序；更高层的 Scene 容器尚未实现。
- 已发布插件的历史 DLL 尚不会自动清理。

[0.1.0-alpha.1]: https://github.com/pipster439/AceHFXAura/releases/tag/v0.1.0-alpha.1
