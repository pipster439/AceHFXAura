# Aura for ROG Falchion Ace HFX

Aura 是面向 **ROG Falchion Ace HFX** 的 Windows 灯光控制器。它通过 ASUS 键盘 HAL 向 68 个已标定按键推送 RGB 帧，并提供一个本地 Web Studio，用 Blockly 制作光效、编排前台程序与 CS2 Game State Integration（GSI）自动化。

当前 alpha 版本号以仓库根目录的 [`VERSION`](VERSION) 为单一事实源；核心能力与已知限制见 [`CHANGELOG.md`](CHANGELOG.md)。

当前版本的完整链路已经过真机验证：启动 daemon、打开 Studio、预览与发布 Blockly 光效、接收 CS2 GSI、按规则切换基础方案并叠加事件/状态光效。

## 你可以做什么

- **在 Studio 制作光效**：用 Blockly 组合全键填色、单键控制、波纹、颜色运算、时间、变量、循环、按键状态和 GSI 数据。
- **直接预览到键盘**：浏览器中的 JS 运行时与原生插件共享同一套顺序控制语义；停止预览后，键盘恢复显示当前已应用方案。
- **编排自动化**：按前台进程和 GSI 条件选择基础方案，也可以在击杀等事件发生时播放一次叠加，或在低血量等条件成立期间持续叠加。
- **自动部署 CS2 GSI**：检测 Steam 库中的 CS2 `cfg` 目录，经界面确认后写入 `gamestate_integration_aura.cfg`，并在诊断页显示连接状态、字段与事件。
- **动态发布插件**：Studio 将积木转译为 C++17、调用本机 MSVC 生成独立 DLL，再让 daemon 热加载，不需要重启灯光服务。
- **保留可回退的运行版本**：编辑源码、编译插件、加载 DLL 和切换配置引用是分开的；发布失败时继续运行上一版。

## 系统要求

- Windows 11 x64
- ROG Falchion Ace HFX
- ASUS 键盘 HAL：必须由用户本机的 Armoury Crate / `Aac_Keyboard` 官方驱动包安装；公开 Release 不携带该专有 DLL
- Visual Studio 2022 Build Tools 或 Visual Studio 2022，安装“使用 C++ 的桌面开发”、x64 MSVC 工具集和 Windows SDK
- CMake 3.20 或更新版本
- Node.js 与 npm（构建 Web Studio 时需要）

> Studio 的“发布”会在运行时查找 `vcvars64.bat` 和 `cl.exe`，并使用 Plugin SDK 头文件（单文件运行时自动释放，源码环境使用仓库 `include/`）编译插件。只查看、编辑和保存草稿不触发 C++ 编译。

> 这个 MSVC 要求不仅用于从源码构建：即使使用单文件 `Aura.exe`，只要要在 Studio 中“发布”原生光效，也必须安装 Visual Studio Build Tools 的 C++ Desktop workload、x64 MSVC 工具集和 Windows SDK。

## 从源码构建

在 **PowerShell（或 Windows Terminal）** 中执行：

```powershell
git clone https://github.com/pipster439/AceHFXAura.git
cd AceHFXAura

cd frontend
npm ci
npm test
npm run build
cd ..

cmake -S . -B build
cmake --build build --config Release
```

`npm run build` 会把 React/Blockly 应用打包为单文件 `web/index.html`。CMake 会构建：

- `aura_daemon.exe`：硬件推流、规则执行、插件管理、GSI 接收和 Web UI 监护；
- `aura_web_ui.exe`：仅绑定本机回环地址的配置与 Studio 服务；
- `test_gsi_rules.exe`：原生规则、GSI 和叠加行为测试。

构建完成后，CMake 还会把两个运行程序复制到仓库根目录。后续命令都应在仓库根目录执行。

## 首次运行

1. 确认键盘已连接，并关闭可能正在控制同一灯光通道的软件效果。
2. 在仓库根目录启动：

   ```cmd
   aura_daemon.exe
   ```

3. daemon 会自动启动同目录的 `aura_web_ui.exe`。浏览器访问 [http://127.0.0.1:19898](http://127.0.0.1:19898)。
4. 打开“工作室”。可以先选择已有方案，也可以新建光效草稿；点击播放按钮即可预览。
5. 需要 CS2 自动化时，打开“CS2 遥测诊断”，确认检测到的 CS2 `cfg` 目录并安装 GSI 配置。启动 CS2 后，页面应显示 GSI 在线。
6. 回到“工作室 → 自动化”，可以载入“CS2 完整示例”，检查规则后点击“保存并应用”。

如果根目录没有 `config.json`，daemon 会自动从 `config.example.json` 创建它。这个文件只是首次启动模板；进入 Studio 并保存自动化后，前端会把旧的 `rules` / `gsi_bindings` 迁移到当前的 `orchestration.version = 2` 统一规则列表，所以不建议照旧 README 手工拼接配置片段。

第二次启动同一程序时，单实例保护会保留正在运行的 daemon，并打开现有 Web UI。

## Studio 工作流

### 制作 Blockly 光效

“制作光效”负责定义一个方案每一帧怎样着色。积木按画布顺序执行；多个顶层积木堆按位置排序，目前不作为彼此独立的并发脚本运行。

脚本可以使用：

- 全键、单键、遍历按键和波纹效果；
- RGB/HSV、亮度、插值、周期颜色和几何坐标；
- 数学、逻辑、变量、循环与流程控制；
- 已用时间、阶段、按键按下状态；
- 玩家血量、C4 状态及其他 GSI 数值或枚举字段。

“等待”是可跨帧继续的控制流。它会保留当前颜色、变量、循环位置和下一条指令，不会阻塞 daemon 的硬件推流线程。脚本到末尾后会在下一帧重新从头执行；变量会一直保留到该光效重新启动。单帧最多执行 4096 条指令，重复次数上限为 10000，单次等待上限为 60000 ms。

### 草稿与发布

每个 Blockly 光效有三个可见状态：

- **草稿**：只有 `blockly_json` 源码，没有已发布插件；
- **未发布修改**：草稿已变化，键盘仍运行上次发布的版本；
- **已发布**：当前源码已经转译、编译并被 daemon 确认加载。

“保存草稿”只更新 `config.json` 中的编辑源码。“发布”执行以下事务式流程：

1. 检查 Web API v2 和 Studio Runtime v2；
2. 从同一 Blockly 工作区生成用于版本摘要和编译的 C++；
3. 用唯一名称生成 `plugins/src/effect_studio_<name>_<id>.cpp` 和对应 DLL；
4. 请求 daemon 加载该 DLL 并等待成功确认；
5. 最后更新 Profile 的 `plugin_name` 和光效的已发布元数据。

编译、加载或配置保存失败时，旧 DLL 和旧 Profile 引用仍可继续使用。发布产生的历史版本暂时保留在 `plugins/`，当前版本不会自动清理它们。

### 设置自动化

“自动化”负责决定何时使用一个光效：

- **基础方案**按规则从上到下评估，第一个匹配项生效；没有规则命中时使用兜底方案。
- **事件叠加**在事件发生时播放一次，例如击杀扩散。相同事件再次发生会重新开始自己的叠加；一个渲染周期内的多次同类事件合并为最新一次。
- **状态叠加**在条件成立期间持续运行，例如生命值低于 20 时的红色呼吸；条件失效后立即移除。
- 多个叠加可以共存，优先级数值越大越晚合成。事件和状态外层的组合条件会完整保留。
- 离开 CS2 前台或 GSI 离线后，游戏叠加会被清理并恢复匹配的桌面/程序方案；离开期间的事件不会在返回游戏时补播。

点击“保存并应用”时，Studio 会先发布自动化所引用的 Blockly 草稿，再提交统一规则。画布在切换页面时还会暂存到当前浏览器标签页的 `sessionStorage`；它不是持久发布，关闭标签页前仍应保存。

更详细的积木执行与联动语义见 [docs/STUDIO_WORKFLOW.md](docs/STUDIO_WORKFLOW.md)。

## GSI 自动化

daemon 在 `127.0.0.1:19897` 接收 Valve GSI POST，并把嵌套数据转换为可供规则与光效读取的字段。Web UI 通过 `127.0.0.1:19898` 代理状态、配置安装、预览、编译和插件重载请求；两个服务都只绑定本机回环地址。

GSI 自动化只在 CS2/CS:GO 进程位于前台且最近 10 秒内收到数据时启用。适配器还会根据连续状态推导击杀、爆头、受伤、死亡、复活、炸弹、回合和比赛阶段等 `event.*` 字段。新 payload 会清理对应节点的旧瞬态值，避免把上一回合状态错误带入下一回合。

Studio 的条件可以组合进程、GSI 字段、`and`、`or` 和 `not`。常用比较运算包括 `==`、`!=`、`<`、`<=`、`>`、`>=` 与 `contains`。第一人称对局中可用字段仍受 CS2 实际发送的数据限制；观战/GOTV 字段不会凭空补全。

## 动态插件系统

内置方案和动态插件使用相同的 `IEffect` 渲染接口。daemon 启动时扫描 `plugins/` 中的 DLL（惯例文件名为 `effect_*.dll`），也接受 Studio 发出的单插件热重载请求。

插件管理器会先把 DLL 复制到 `plugins/.cache` 再加载，因此 Windows 不会锁住原文件，Studio 可以继续生成新版本。每次发布使用不可变的唯一插件名；正在运行的实例通过共享所有权保持有效，加载失败不会破坏旧实例。

Profile 只保存逻辑方案名与当前 `plugin_name` 引用。自动化引用 Profile，而不是直接引用某个临时 DLL 文件名，因此发布新版本后无需重写所有规则。

## 当前运行结构

```text
浏览器 Studio (127.0.0.1:19898)
        │ 配置 / 编译 / 预览 / GSI 诊断
        ▼
aura_web_ui.exe
        │ 本机 HTTP 转发
        ▼
aura_daemon.exe (127.0.0.1:19897)
        ├─ ForegroundMonitor：前台进程事件
        ├─ GsiAdapter：CS2 状态与派生事件
        ├─ RuleEngine：统一规则、兜底方案、配置热重载
        ├─ EffectEngine + OverlayManager：基础方案与多层叠加
        ├─ PluginManager：插件发现、影子加载与热重载
        └─ AuraAdapter：将 128 通道帧推送到 ASUS 键盘 HAL
```

硬件调用只发生在 daemon 主推流路径；GSI 和 Web 请求线程只更新内存状态或转发请求。配置写入后由 daemon 按文件时间戳热重载。全局与 Profile 帧率会被限制在 10–100 FPS；配置没有指定帧率时回退到 25 FPS，仓库的首次启动模板当前设为 100 FPS。

`suppress_web_ui`（Studio 中的“免打扰”）仍是可选的运行规则：命中时 daemon 会暂时停止 Web UI 子进程，灯光与 GSI 服务继续运行；离开该规则后 Web UI 会自动恢复。因此在启用免打扰的游戏前台，浏览器页面暂时不可访问属于预期行为。

## 常用命令

```cmd
:: 正常运行
aura_daemon.exe

:: 不加载硬件驱动，用于检查规则、Web UI 和 GSI
aura_daemon.exe --dry-run

:: 指定配置和键位表
aura_daemon.exe --config config.json --keymap calibrated_keymap.json

:: 硬件初始化/释放循环
aura_daemon.exe --test-init 100

:: 持续推流稳定性测试，参数单位为分钟
aura_daemon.exe --test-stability 30

:: 查看完整参数
aura_daemon.exe --help
```

只调试 Web 服务时可以运行：

```cmd
aura_web_ui.exe --port 19898 --config config.json
```

此模式没有 daemon，因此硬件预览、插件加载确认和 GSI 实时状态不可用。

## 构建发布包

在仓库根目录执行：

```cmd
package_release.bat
```

脚本会从 [`VERSION`](VERSION) 读取版本，通过 `vswhere` 检测本机安装的 Visual Studio 版本（支持 Visual Studio 2022 与 Visual Studio 2026）并动态匹配对应 CMake 生成器，构建两个 Release 进程、校验可再分发资产，并生成 `dist/Aura.exe` 和便携 ZIP。

出于保守的第三方资产策略，打包脚本不会从本机复制、内嵌或附带 `AacKbHal_x64.dll`。程序运行时从用户已安装的 ASUS 官方目录/注册路径定位 DLL，并且只加载通过已验证 SHA-256 与内存签名 Gate 的版本。如果缺失或版本不受支持，请通过 Armoury Crate / ASUS 官方驱动包安装或修复，不要从非官方来源下载 DLL。

发布前请逐项完成 [`RELEASE_CHECKLIST.md`](RELEASE_CHECKLIST.md)。

单文件 `Aura.exe` 会自动释放运行时所需的 Aura Plugin SDK，因此无需源码 checkout 即可使用 Studio 的原生发布功能。原生发布仍要求本机安装 Visual Studio / Build Tools 的 C++ Desktop workload、x64 MSVC 工具集和 Windows SDK。

## 验证

前端测试与生产构建：

```cmd
cd frontend
npm test
npm run build
```

原生测试：

```cmd
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

仓库还包含 GSI、编译接口、插件压力与端到端脚本，位于 `tests/`。依照工程规范，自动化 CI 测试通过仅代表模拟护栏通过；涉及真实键盘、ASUS HAL 物理推流与 CS2 实机对局的最终验收须由人工依据 [docs/MANUAL_TESTS.md](docs/MANUAL_TESTS.md) 手动执行核验。

## 免责声明与第三方资产声明 (Disclaimer & Third-Party Notice)

1. **商标与版权**：ASUS、ROG (Republic of Gamers)、Armoury Crate 及相关标志均为 ASUSTeK Computer Inc. 的注册商标或商标。Counter-Strike、CS2 与 Game State Integration (GSI) 均为 Valve Corporation 的注册商标或商标。本项目为独立第三方开源软件，与华硕或 Valve 均无官方关联、赞助或背书关系。
2. **底层驱动组件**：本项目对键盘底层的灯效控制通过 ASUS 官方硬件抽象库（如 `AacKbHal_x64.dll`）实现。该 DLL 属于华硕专有资产，不属于 AceHFXAura 的 GPL 授权内容；本仓库未获得其重新分发授权，因此公开 Release 默认不内嵌或附带该 DLL，只使用用户本机已安装且通过兼容性 Gate 的华硕官方组件。
3. **软件许可 (LICENSE)**：AceHFXAura 项目代码采用 GNU General Public License v3.0 only（SPDX 标识：`GPL-3.0-only`），详情见根目录 [LICENSE](LICENSE)。发布与使用本项目须遵守当地法律法规及第三方相关最终用户许可协议 (EULA)。

## License

AceHFXAura is licensed under the GNU General Public License v3.0 only (`GPL-3.0-only`).
See [LICENSE](LICENSE) for details.

- ASUS, ROG, Armoury Crate, and related marks/assets belong to their respective owners.
- `AacKbHal_x64.dll` is an ASUS proprietary component and is **not** covered by the AceHFXAura GPL license.
- Public releases do not bundle or redistribute this DLL; Aura only interacts with official ASUS drivers installed locally that pass compatibility gate verification.
