# AceHFXAura alpha.4 UI polish 验证报告

日期：2026-09-24。工作区：`G:\Aura`。候选包：`dist\Aura-v0.1.0-alpha.4-windows-x64-082395afc163d506`。本轮没有 commit、push、tag 或发布。

## 实施范围

- 设置页采用 Windows Community Toolkit 的 `SettingsCard` / `SettingsExpander`；主题、托盘和服务按钮保留原事件处理。核心详情按需展开，宽屏右侧显示真实运行与配置状态。
- 原生导航和内嵌 Web/Blockly 的固定界面文案统一为中文；专有名词、用户创建的名称、API 字段、JSON 枚举和 Blockly 内部 ID 保持原值。
- 首页、灯效、游戏集成和设置在宽屏使用主内容与有用的上下文栏；普通宽度维持紧凑布局。原生导航展开宽度设为 220 DIP。
- 游戏集成移除重复的模拟编辑表单，只显示 GSI/CS2、遥测、来源、新鲜度和诊断，并提供“打开自动化工作室”。模拟编辑保留在工作室的自动化侧栏，`/api/gsi/simulation` 未改。
- 工作室继续由单个 WebView2 承载 Blockly。作品栏与检查栏收窄，窄窗口改为覆盖侧栏；画布扩展。依据用户提供的 1920×1080 截图，修复 224px 作品栏标题、导入、新建和状态被挤成竖排的问题。
- 更新原生布局、前端宿主消息和状态文字测试；包校验新增 Toolkit DLL、XBF、deps 和 PRI 资源检查。

## 自动验证

| 项目 | 结果 | 证据 |
| --- | --- | --- |
| 前端单元测试 | 38 通过，1 跳过，0 失败 | `npm test` |
| .NET 测试 | 47 通过，2 个需显式启用的测试跳过 | `dotnet test tests/Aura.Tests/Aura.Tests.csproj -c Release --no-restore` |
| Release x64、自包含发布、包清单/资源 | 通过 | `python tools/package_release.py --build-dir build --skip-build --skip-zip`；候选包包含 `Aura.pri`、`Pages/SettingsPage.xbf`、SettingsControls DLL；包校验通过 |
| 提取包干净启动、二次实例转发、外部核心所有权 | 1 通过 | `PackagedGuiLoadsXamlRedirectsSecondaryAndPreservesExternalDaemon`，`AURA_PACKAGE_DIR` 指向上述候选包 |
| 原生布局和 SettingsExpander | 40 个页面/主题/尺寸组合通过 | `build/ui-polish-validation-release/layout-results.json`，错误为 `null`，横向滚动宽度为 0；截图在同目录 |
| 隔离 WebView2 Studio 回归 | 42 个阶段通过 | `build/studio-polish-validation-4/studio-results.json`，错误为 `null`；含 50 次原生导航重入、未保存草稿、Effect/Automation 切换、两种侧栏浮层、深浅主题、600×500 至最大化和实际 API 保存；截图在同目录 |
| 工作区补丁格式 | 通过 | `git diff --check` |

原生布局验证的实际 `RasterizationScale` 为 **1.5**，覆盖 600×500、800×600、1060×720、1600×1000 DIP 和当前显示器最大化；深色/浅色；主页、灯效、游戏集成、设置。展开后的设置页截图确认工作室状态、配置健康、运行时与浏览器入口均完整显示。用户提供的两张 1920×1080 Studio 截图用于定位作品栏文字换行问题；修复后由隔离 WebView2 测试生成 `dark-maximized.png` 和 `automation-dark-maximized.png`，确认操作按钮和状态不再竖排，Blockly 占据主区域。

## 待人工验收

- 100% 和 200% DPI、跟随系统主题、2560/3840 等价宽度，以及 1920×1080 的最终代码版，需要在相应环境复查中文截断、焦点顺序和 SettingsCard 对齐。
- Blockly 工具箱/浮层层级和未保存工作区保留已由隔离 WebView2 测试覆盖；最终候选包仍需人工检查积木拖拽、键盘焦点和真实鼠标操作。
- 真机键盘、真实 CS2/GSI、Native HID、发布插件的本机编译链仍按 release checklist 单独验收。
- 包构建使用 `--skip-build` 复用已有 native sidecar；它验证了当前 UI 发布物及打包启动，未声明重新构建所有 native 产物。

Computer Use 桌面检查由用户按 Esc 停止，停止后未再调用该工具。修复后的 Studio 截图来自隔离 WebView2 测试，不等同于用户桌面的手工验收。
