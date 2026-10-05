# ALPHA8_STUDIO_AI_OVERNIGHT_REPORT

最终状态：**ALPHA8_STUDIO_AI_MVP_PASS**。Goal 0–5 已达到软件门禁；完整 canonical CI 的 18 个阶段全部 PASS，0 NOT RUN。桌面 mock smoke 的 11 个阶段通过。此结论只涵盖软件、fixture 与隔离 dry-run 验证。

## 基线、范围与结束状态

- 仓库：`G:\Aura`；starting commit / ending HEAD 均为 `c8a0fdd1228a746dae185c93fb48e4510e70929f`。
- 开始时 working tree 干净；结束时保留本任务的 24 个修改/新增文件，未 stage、commit、push、tag 或 release。
- 未修改 HID、M605、magnetic production protocol、HardwareSlot/51 00、Gear Link 或 Armoury Crate。无人值守测试未连接真实 LLM，没有使用用户现有凭据；硬件测试走已有 mock/software guard。
- 构建前用户正常退出 Aura，端口检测通过；测试进程为本任务启动的私有 dry-run/UI fixture。正常退出 UI、私有事件停止 daemon，未终止用户进程。
- `manifests/ending-git-status.txt`、`manifests/changed-files.json`、`hashes/source-sha256.json` 记录结束状态和来源。

## 架构与主要变更

保留缓存的 StudioPage/WebView2/Blockly。原生 CommandBar 负责 New/Open/Save draft、Preview、Build、Publish、工程选择、状态及诊断入口；嵌入页面隐藏重复入口，独立浏览器仍保留原工具栏。最近工程为当前会话的有界列表。宽内容区显示编辑器与 AI 并排，窄区显示可滚动的原生 AI 面板并暂时隐藏编辑器表面，实例和草稿仍保留。修复异步配置加载时初始工程选择错误，优先保留恢复的会话草稿。

ConfigSaveCoordinator、effect/project 格式、生命周期及 publish/reload ACK 语义保留。新 Build 只编译，不 reload/config publish。构建、保存、发布期间编辑器和相关命令使用同一 busy 状态。

OpenAI-compatible provider 使用轻量 HttpClient，禁止重定向，远程仅 HTTPS、HTTP 仅 loopback；验证模型、地址与 1–120 秒超时。请求上限 64 KiB，响应流上限 128 KiB，内容 64 KiB，含取消、HTTP 错误、truncation、工具调用/refusal 检查，无自动重试。API key 保存到 Windows Credential Manager，target 绑定归一化服务 endpoint，普通 settings JSON 仅保存非秘密字段。原生设置提供保存、变更/移除密钥和显式 Test Connection；不回显密钥。

AI 只读取当前工程的逻辑数值节点、受支持模板能力及经筛选的单条诊断。严格 C# 与 web parser 只接受 propose/explain、受支持 preset 和数值节点 edits，拒绝未知字段/动作、非法节点/范围和超限响应。structured output 可显式启用；本地严格验证始终运行。AI 不具备 shell/files/git/HID/process/native execution/publish 操作。

候选在独立 Blockly workspace 中完成 JS/C++ transpile 和三帧本地模拟；展示节点/字段 before→after、验证状态及六个预览色块。显式 Apply 只改变草稿，Save/Publish 继续由用户操作。字节级 snapshot 匹配拒绝过期 Apply/Undo；AI 独立一步 Undo 及分组 Blockly history 保留普通撤销。处理 malformed Blockly load 泄漏的全局 recordUndo/group 状态。每个原请求最多两次用户触发的修正建议，无自动修复/构建/发布循环。

模板共九个：保留已有六个，添加 Static/Reactive/Gradient，小型组合复用已有积木；标签、输入和能力从内容派生。validation/build/plugin load 错误提供 Ask AI 入口及中文示例。

## 各 Goal 门禁

| Goal | 结果 | 关键证据 |
|---|---|---|
| 0 audit | PASS | `evidence/ALPHA8_STUDIO_ARCHITECTURE.md` |
| 1 shell | GOAL_1_PASS | 原生模型/命令测试、WinUI build、frontend、原有 Studio 9/9 regression、最终桌面 smoke |
| 2 provider/settings | GOAL_2_PASS | success/timeout/401/403/429/5xx/malformed/truncated/cancel/size/redaction/URL 单元测试；随机 fixture target 的真实 Windows vault round-trip；设置 save/change/remove 与原生 mock Test Connection |
| 3 typed assistant | GOAL_3_PASS | generate/modify/explain/error analysis、严格拒绝、最小上下文测试 |
| 4 proposal | GOAL_4_PASS | diff/validation/apply/reject/undo/stale snapshot/普通 history/两次修正上限/无自动 publish 测试 |
| 5 polish | GOAL_5_PASS | 九模板实际 JS/C++ transpile 与 simulation、能力 metadata、diagnostics context；WinUI build |

原有 Studio release regression 的 9 个测试使用已有历史 isolated launcher fixture 和源目录 web runtime，以验证 SDK/编译错误/路径解析/实际 fixture 编译；这不是本轮 alpha.8 release 包的验证。本轮未制作 alpha.8 release 包。

## 完整 canonical CI

命令：`pwsh -NoProfile -File tools\ci\run-ci.ps1 -OutputDirectory audit_artifacts\alpha8\canonical-accepted`。无 SkipFrontend/SkipWinUI/KeepBuild。开始 `2026-10-05T18:35:27.4921637Z`，结束 `2026-10-05T18:43:16.1041886Z`。摘要 `manifests/ci-summary.json`。

| 阶段 | 结果 | 耗时 | 证据包位置 |
|---|---|---|---|
| guards | PASS | 0.6s | logs/canonical/guards.log |
| diff-check | PASS | 0.1s | logs/canonical/diff-check.log |
| frontend-dependencies | PASS | 9.2s | logs/canonical/frontend-install.log |
| configure | PASS | 7.0s | logs/canonical/configure.log |
| native-build | PASS | 258.2s | logs/canonical/native-build.log |
| ctest-discovery | PASS | 0.1s | logs/canonical/ctest-discovery.log |
| ctest | PASS | 34.8s | logs/canonical/ctest.log |
| daemon-integration | PASS | 26.3s | logs/canonical/daemon-integration.log |
| dotnet-test | PASS | 5.4s | logs/canonical/dotnet-test.log |
| asus-platform-test | PASS | 17.5s | logs/canonical/asus-platform-test.log |
| fan-typelib-validator-test | PASS | 1.8s | logs/canonical/fan-typelib-validator-test.log |
| aura-gate-a-software | PASS | 36.1s | logs/canonical/aura-gate-a-software.log |
| aura-enumeration-software | PASS | 11.9s | logs/canonical/aura-enumeration-software.log |
| aura-mta-triage-software | PASS | 26.1s | logs/canonical/aura-mta-triage-software.log |
| winui-build | PASS | 11.9s | logs/canonical/winui-build.log |
| frontend-test | PASS | 1.5s | logs/canonical/frontend-test.log |
| frontend-build | PASS | 3.7s | logs/canonical/frontend-build.log |
| lighting-backend-software | PASS | 16.1s | logs/canonical/lighting-backend-software.log |

TRX counters：
- `asus-platform.trx`: 84 passed / 84 total; 0 notExecuted, 0 failed.
- `aura.trx`: 202 passed / 202 total; 0 notExecuted, 0 failed.
- `fan-typelib-validator.trx`: 23 passed / 23 total; 0 notExecuted, 0 failed.

frontend 最终测试 56 passed / 57 total、0 failed、1 个原有环境相关 skip；无新 skip。已有可选 process/DesktopSmoke/OfflineMetadata 排除遵循 canonical 原定义，不削弱门禁。CTest 25/25、daemon integration 45/45；plugin runtime/reload、packaging、native hardware guards 均通过。既有编译器/分析器 warning 原样保留；WinUI 最终 build 0 errors。

## 桌面 smoke 与截图

`tools/ci/run-studio-ai-smoke.py` 使用当前 source-built WinUI、当前 frontend 和 freshly built daemon，临时 config/data/WebView2 profile；loopback HTTP mock、fake key、private shutdown event。实际请求 4 次，全为 local fixture。原生 UIA Invoke 与 WebView ExecuteScript 驱动明确步骤；window-scoped winapp UIA inspect/screenshot 只针对该测试 PID。

通过：原生连接测试不回显密钥；shell/editor load；修改周期 3→2 的 diff、本地 preview 与未自动 Apply；Apply→Save 2；Undo→Save 3；mock C2039 build 错误 Ask AI 最小上下文；非法 publish 动作拒绝；Dark/Light 与 1280/600 DIP；无 `/api/reload_plugin`。mock compile 错误由 fixture fetch 拦截，未发布 DLL。

- `screenshots/window-composite.png` 为实际宽窗口合成截图（1520 DIP 请求，当前主机缩放）；AI diff 与 Apply 及保留的 Blockly 同时可见。
- `screenshots/native-Light-600.png`、`native-Dark-600.png` 为窄窗口原生表面截图。
- `screenshots/native-proposal-dark.png` 是 RenderTargetBitmap，**不包含 WebView2 图像**；`editor-proposal-dark.png` 单独记录 editor。不要把两者误称完整窗口。
- `evidence/smoke/smoke-results.json`、`mock-requests.json`、`uia-tree.json` 是结果与来源。未做人工 100%/200% DPI、所有分辨率/系统主题或真实 endpoint 验收。

## 失败、修复与风险

保留必要失败证据，不把失败的 run 当成最终 PASS：

1. Goal 1 默认 release launcher 路径不存在，改为已存在的历史隔离 fixture 后 9/9；没有为此新做 release。
2. Goal 4 history 测试发现 malformed workspace load 把全局 recordUndo 改为 false；finally 恢复状态，真实普通 undo history 回归通过。
3. 首轮 canonical 在新截图代码使用 FileStream 时编译失败；改为 Windows Storage IRandomAccessStream。之后 UI 发现异步初始工程选择不正确并修复。
4. 窄窗口 native/web 合成背景透出；改为不透明主题背景与保留但隐藏 web 表面。截图验证发现隐藏 WebView2 的 CapturePreview 等待：补超时、可见性断言和明确宽内容区（1520 DIP，导航栏占用空间）。两个失败的截图验证和最终 11 阶段 PASS 均有记录。
5. 第二轮 canonical 与测试窗口并行，exe 被本任务窗口锁定，WinUI copy 失败；改为顺序执行，正常结束 fixture 后完整重跑并全部通过。未修改 safety guards/断言来过门禁。
6. 失败截图运行的 Python cleanup 因 GUI 等待提前异常；确保 daemon event/server cleanup 在 finally 执行，显式 validation 模式关闭托盘最小化以便正常 Close。旧 fixture UI 恢复宽度后经既有退出流程结束，daemon 通过它的私有事件停止，无用户进程操作。自动审批拒绝删除 `C:\Users\ROG\AppData\Local\Temp\aura-studio-ai-rm9kcj8n`（策略阻止），该临时 profile 保留且未打包。

已知限制：只支持既有数值节点和预置模板组合，不支持任意 C++/积木拓扑生成；原生预览为六个样本色块，完整键盘仍由原编辑器负责；recent 列表仅会话；AI 一步 Undo 在后续编辑后拒绝以保护这些编辑；无需真实 LLM/硬件的软件 MVP 不等于这些端到端验证。没有 unresolved required gate。

建议后续 alpha.8 工作：用户明确 opt-in 后验证真实兼容 endpoint；扩展经验证的 typed block 操作；完善持久化 recent/键盘快捷键和多 DPI 人工 UI 检查。不会自动开始这些范围外工作。

## 修改文件

- `docs/ALPHA8_STUDIO_AI_OVERNIGHT_REPORT.md`
- `docs/ALPHA8_STUDIO_ARCHITECTURE.md`
- `frontend/src/blockly/presets.js`
- `frontend/src/components/EffectStudio.jsx`
- `frontend/src/components/Studio.jsx`
- `frontend/src/utils/applyEffect.js`
- `frontend/src/utils/studioAssistant.js`
- `frontend/src/utils/studioHost.js`
- `frontend/tests/studio-assistant.test.mjs`
- `frontend/tests/studio-host.test.mjs`
- `tests/Aura.Tests/StudioLlmTests.cs`
- `tests/Aura.Tests/StudioShellTests.cs`
- `tools/ci/run-studio-ai-smoke.py`
- `winui/App.xaml.cs`
- `winui/Pages/StudioPage.Assistant.cs`
- `winui/Pages/StudioPage.LlmSettings.cs`
- `winui/Pages/StudioPage.xaml`
- `winui/Pages/StudioPage.xaml.cs`
- `winui/Services/StudioAssistantContracts.cs`
- `winui/Services/StudioLlmProvider.cs`
- `winui/Services/StudioLlmSettingsStore.cs`
- `winui/Services/StudioShellModel.cs`
- `winui/Validation/StudioAiValidation.cs`
- `winui/Validation/StudioValidation.cs`

## 交付与复核

独立 `ALPHA8_STUDIO_AI.patch` 仅包含本任务 24 个文件的修改；temporary Git index 从 starting HEAD 做 cached apply check/实际 apply 并逐个比对工作区 Git blob，不修改用户 index。`evidence/patch-validation.json` 记录结果。

`ALPHA8_STUDIO_AI_EVIDENCE.zip` 从新的独立 staging 目录构建，只包含本报告、logs/manifests/hashes/screenshots/必要小型 evidence，无完整源码/仓库、build、bin/obj/node_modules、cache、敏感凭据、旧包或嵌套包。ZIP 文件名唯一性、内容集合、逐项 SHA-256 和秘密模式扫描在生成后复核。ZIP 的 SHA-256 在包外 `ALPHA8_STUDIO_AI_EVIDENCE.zip.sha256`；patch 与 hash 也独立提供。
