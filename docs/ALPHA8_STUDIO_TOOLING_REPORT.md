# ALPHA8_STUDIO_TOOLING_REPORT

最终状态：**ALPHA8_STUDIO_TOOLING_PASS**。Goal 6–9 软件门禁通过；最终 canonical CI 18/18 PASS、0 unexpected NOT RUN；扩展 desktop smoke 通过。只使用本地模拟、临时 data root、mock HTTP 和隔离 dry-run daemon。未打开/写入真实 HID、连接真实外部 LLM、push、tag 或 release。本轮 Phase 2 保留为未提交改动。

## Phase 0 — AI MVP checkpoint

原 baseline：`c8a0fdd1228a746dae185c93fb48e4510e70929f`。

**ALPHA8_AI_MVP_COMMIT：`616c05af8efcdfffab571a8039e4ad9b11aa877f`**；本地标题 `feat: add Studio AI assistant MVP`。

checkpoint 前核对 overnight 报告、24 个源文件与原证据 hashes 一致，`git diff --check`、frontend Studio 定向 29 PASS/1 既有 skip、C# Studio 27/27 通过。commit 未改变 build input；commit 后 working tree 干净，再开始 Phase 2。patch 以该 checkpoint 为基线，排除此前 24 个 MVP 变更。证据：`evidence/mvp-source-verification.json`、`evidence/mvp-checkpoint.json`、`logs/mvp-checkpoint.log`。

## 主要变更与 Goal gates

### Goal 6 — GOAL_6_PASS

测试台复用实际 Blockly 工程、现有 JS transpiler/sequence/simulation 和键盘可视化。独立帧只进入本地 visualization，不发 HID/daemon preview，不创建第二个 renderer。Play/Pause/Step/Reset/Loop 显示时刻、状态、场景、事件与验证。播放计时器只调用 step；renderer 的时间由场景推进，不读 wall clock。

scenario v1 必需 `schema_version/name/duration_ms/events`；可选 `frame_ms/initial_state/assertions`。事件为 `key_down/key_up/gsi_patch/foreground_process/timeline/reset`。同时间事件保留次序；帧时刻取固定步长、事件、断言与终点的有序并集。Reset/Loop 重建原 renderer state。断言验证完成、帧数、指定时刻/key RGB、按键/GSI/前台状态。

严格拒绝未知字段/事件、负数/未排序时间、非法键/字段、路径/代码与超限内容。限制：64 KiB、60 秒、512 事件、128 断言、frame_ms 20–1000。导入/导出 JSON；六个可导入内置实例在 `evidence/scenarios/scenario-1.json` 至 `scenario-6.json`。

内置 tap/hold/repeat、health 100→50→10、round_kills 状态、前台 A→B。parser、确定性 timing、tap/hold/repeat、实际 GSI preset 输出、foreground state、roundtrip、invalid rejection 和九模板 simulation parity 均通过。

### Goal 7 — GOAL_7_PASS

原生 journal 与 durable/published authority 独立，仅保存逻辑工程 ID、Blockly representation、draft publication metadata、UTC 时间/修订与 fingerprint。使用 `RuntimeLayoutResolver.DataRoot`，尊重 `AURA_DATA_ROOT`；文件名用大小写敏感 ID 的 SHA-256，避免 Windows CON/NUL/COM1 与大小写碰撞，journal/UI 保留原 ID。

debounce 800 ms、连续编辑 max-wait 5 秒；temp WriteThrough/Flush + atomic rename；退出 flush pending。每工程最多 10 snapshots，最多 200 journals；draft 256 KiB、journal 3.1 MB。AI Apply、Publish 必须先写 snapshot，Build 不写形式化 snapshot。RPC 限 origin、操作、字段、大小与逻辑 ID，没有任意路径或 config/publish 操作。

恢复 offer 由用户 Restore/Discard，恢复前实际 JS/C++/simulation validation，始终只为 draft。已知 durable 时间来自该 effect 的 source_updated_at/updated_at，而非全 config mtime；legacy 无时间戳时以 graph fingerprint 为基线。fingerprint 排除会重新生成的 Blockly block IDs，保留字段、位置、变量引用和 lifecycle，避免旧 preset 重载误报。密钥、credentials、诊断、compiler path 与 applied/published records 被拒绝，provider key 从不跨入草稿 bridge。

atomic failure、debounce/max-wait、crash restore、discard、clean save/shutdown、retention、AI/Publish snapshots、secret rejection、同哈希较新保存、无关 config 更新、Windows 保留名与 case-distinct IDs 回归通过。

### Goal 8 — GOAL_8_PASS

manifest v1 从实际 active graph + 已知 block semantics 推导 inputs/outputs/features/gsi_fields/diagnostics。非 authoritative cache、文本中偶然出现的 block 名与 inactive fallback shadow 不成为能力依据。旧 project derive on load，未改变 project/config schema。

用于 preset/gallery metadata、AI current context/preset filtering、测试台 recommendations 和明确 diagnostic。无 GSI block 的 modify/explain/error context 没有 CS2 fields 或无关 GSI preset；generate 可选所有已验证 preset。keyboard-only、GSI、mixed input、old project、过滤/推荐/unsupported 诊断通过。现有 orch_current_process 被识别为 Automation-only，明确拒绝其 Effect simulation；该 gate 证明现有支持边界，不声称增加前台 effect renderer。

### Goal 9 — GOAL_9_PASS

`Studio/recent.json` 原子持久化最多 10 个逻辑 ID，重启保留排序、对当前 catalog 过滤缺失/删除工程。未加载 catalog 或 Automation 页不清空记录，重复 status 不重写文件。“清除最近记录”仅清排序，不删工程。I/O/格式错误给中文提示并保留原文件，诊断不回显秘密或绝对路径。restart/bounds/prune/clear/atomic failure/no rewrite 测试通过。

## 验证与修正

最终定向：C# Studio **45/45**；frontend **81 PASS、1 既有 skip**，含 Test Bench 11、capabilities 10、web storage 4 个测试及原有 effect/AI/ordinary undo 回归。最终完整 CI 结果见下表和 TRX/JUnit。

修正了 fixture 编译与 busy control 时序、任何非 null error（含空文本）必须失败、WebView 初始化 analyzer、恢复时间边界、inactive shadow 假依赖、Windows device names/case ID journal 文件名。扩展 smoke 首次遇到宽 Blockly 图形被邻接预览遮挡，document hit-test 选错控件；改为向观察到的 SVG field 派发标准 pointer/input 事件，随后通过。截图等待实际 ResizeObserver 视口稳定，确认 68 键横向可见。未删除断言、skip 新测试、增加固定 sleep 放过失败或降低 guard。三轮 earlier canonical 都为 18/18，但因复核修正而 superseded；只用 accepted run 做最终结论。原生既有 C4996/C4100、MSTest warnings 保留，无 blanket suppression。最终 WinUI build 无新增 analyzer warning。

前三轮 canonical 都为 18/18，因复核修正 superseded。第四轮 canonical-delivery 虽然 18 阶段通过，但用户 alpha.7 Aura 在开始后重新启动，生命周期测试因 Existing user daemon 跳过，因此排除该轮验收。用户正常退出后重跑完整 canonical；accepted .NET 所有测试均执行、0 skip。该事件和最终结果均保留证据。

随后 canonical-isolated-final 原生构建因 G: 空间耗尽停止（No space left on device / MSB3191）；未用该失败运行验收。仅清理本任务四个构建目录内 1063 个生成的 .obj/.pdb/.ilk 文件，释放 3,177,025,841 bytes，保留源码、日志和必要证据；439 个冻结输入 hash 均未变。canonical-space-recovered 续跑后发现执行句柄失效，且没有存活 CI/CMake/MSBuild 进程，原生构建未完成，排除该运行；没有将 running 状态文件当作通过证据。canonical-durable-final 以隐藏独立进程从新 build root 执行全部阶段，为唯一最终 accepted run。

## Accepted canonical CI

命令：`pwsh -NoProfile -File tools/ci/run-ci.ps1 -OutputDirectory audit_artifacts/alpha8_tooling/canonical-durable-final`，无 SkipFrontend/SkipWinUI/KeepBuild。

UTC `2026-10-05T20:52:45.5722831Z` → `2026-10-05T21:02:01.4939936Z`。439 个已记录 build/test inputs 在运行前后 hash 一致，accepted run 后未改源码。

| 阶段 | 结果 | 耗时 | 证据包位置 |
|---|---|---|---|
| guards | PASS | 1.2s | logs/canonical/guards.log |
| diff-check | PASS | 0.1s | logs/canonical/diff-check.log |
| frontend-dependencies | PASS | 12.1s | logs/canonical/frontend-install.log |
| configure | PASS | 8.0s | logs/canonical/configure.log |
| native-build | PASS | 311.6s | logs/canonical/native-build.log |
| ctest-discovery | PASS | 0.1s | logs/canonical/ctest-discovery.log |
| ctest | PASS | 38.1s | logs/canonical/ctest.log |
| daemon-integration | PASS | 25.9s | logs/canonical/daemon-integration.log |
| dotnet-test | PASS | 6.2s | logs/canonical/dotnet-test.log |
| asus-platform-test | PASS | 18.2s | logs/canonical/asus-platform-test.log |
| fan-typelib-validator-test | PASS | 2.0s | logs/canonical/fan-typelib-validator-test.log |
| aura-gate-a-software | PASS | 41.9s | logs/canonical/aura-gate-a-software.log |
| aura-enumeration-software | PASS | 14.2s | logs/canonical/aura-enumeration-software.log |
| aura-mta-triage-software | PASS | 31.0s | logs/canonical/aura-mta-triage-software.log |
| winui-build | PASS | 17.8s | logs/canonical/winui-build.log |
| frontend-test | PASS | 2.4s | logs/canonical/frontend-test.log |
| frontend-build | PASS | 5.0s | logs/canonical/frontend-build.log |
| lighting-backend-software | PASS | 19.9s | logs/canonical/lighting-backend-software.log |

CTest 全注册表 25/25，daemon integration 通过。.NET counters：`{"asus-platform.trx": {"total": "84", "executed": "84", "passed": "84", "failed": "0", "error": "0", "timeout": "0", "aborted": "0", "inconclusive": "0", "passedButRunAborted": "0", "notRunnable": "0", "notExecuted": "0", "disconnected": "0", "warning": "0", "completed": "0", "inProgress": "0", "pending": "0"}, "aura.trx": {"total": "220", "executed": "220", "passed": "220", "failed": "0", "error": "0", "timeout": "0", "aborted": "0", "inconclusive": "0", "passedButRunAborted": "0", "notRunnable": "0", "notExecuted": "0", "disconnected": "0", "warning": "0", "completed": "0", "inProgress": "0", "pending": "0"}, "fan-typelib-validator.trx": {"total": "23", "executed": "23", "passed": "23", "failed": "0", "error": "0", "timeout": "0", "aborted": "0", "inconclusive": "0", "passedButRunAborted": "0", "notRunnable": "0", "notExecuted": "0", "disconnected": "0", "warning": "0", "completed": "0", "inProgress": "0", "pending": "0"}}`。摘要 `manifests/ci-summary.json`，TRX/JUnit 在 `evidence/test-results/`。本地 CI 不等同于 hosted CI/真实硬件/真实 provider/AOT/release 验收。

## 扩展 desktop smoke 与截图

使用 accepted build 的 daemon/frontend 与新构建 WinUI。Sandbox target stale、未运行；本次 unpackaged recovery diagnostic 在 local 进行。UIA/capture 限本任务创建的 PID；不控制用户 Aura。临时 root 有 marker，启动前检查 private core PID、dry_run、loopback mock URL。

真实流程：Studio/preset → key tap → pause/step/reset → 选实际 health preset 执行 GSI 场景 → 原工程 browser input 编辑真实 Blockly 数值 3→4 → 等待 atomic autosave → owned fixture UI 自行异常退出码 23（跳过 normal shutdown）→ 同 root restart/offer/Restore → mock AI proposal/Apply 4→2 → restore pre-AI snapshot 至 draft 4 → 显式 Save → Publish 前 snapshot + mock compile error → clean exit/restart 无误报 → persistent recent/Clear。

每阶段比对 profiles/default/orchestration/publication records authority。自动保存、恢复和 AI Apply 不改 durable/published state；只有显式 Save 修改 durable draft。Publish compile-error fixture 未 reload plugin。mock request 只带相关 numeric nodes 和 time manifest，无 GSI dependency/真实 key。fake loopback credential 退出时移除；private daemon 用 named event 正常停止，未终止用户进程。

| Fixture | 结果 | 阶段数 | 证据 |
|---|---|---|---|
| crash | PASS | 6 | evidence/desktop/crash-results.json |
| recover | PASS | 5 | evidence/desktop/recover-results.json |
| clean | PASS | 3 | evidence/desktop/clean-results.json |

截图：`screenshots/test-bench.png`（实际 WebView）、`screenshots/window-composite.png`（PID scoped 整窗）；UIA/capture JSON 在 `evidence/desktop/`。工作区原件：`G:/Aura/audit_artifacts/alpha8_tooling/smoke-final-verified`。

## 已知限制、风险和未解决事项

- 最终 build 首次 cold-start smoke 在 UI 操作前被 runtime owner guard 拒绝；当次未记录具体错误 PID，根因未定位，未计为通过。未改断言/源码；记录探针随后匹配 expected/actual PID 并完成 14 阶段，再用原 runner 无探针完整复验。相关拒绝日志和去除临时绝对路径的 identity 记录在 evidence/logs。该启动拒绝属于保留的运行限制，不被描述为已修复。
- foreground 场景仅验证输入 state；Effect renderer 不读 Automation 前台条件。kill 场景用 round_kills state，未恢复旧 event.* Boolean pulse。
- 按键能量沿用预览 held/released 1/0，没有新衰减/硬件模型。custom GSI fields 仍可 round-trip，测试台对字典外能力明确拒绝。
- 宽 Blockly 图形和旧侧栏键盘仍可能需要滚动/缩放；测试台使用自己的现有可视化视口。桌面 smoke 采用真实 DOM/UIA 控件，不代表人工鼠标、多 DPI 完整验收。
- 普通 standalone browser 没有 native data-root bridge，会明确提示 recovery/snapshots 需要桌面宿主；legacy 无保存时间戳依赖 fingerprint。
- I/O 失败保留旧日志并提示；不承诺断电/磁盘损坏后的物理介质 persistence。恢复不能冒充 published state。
- 没有必需项未解决；真实 HID/LLM、hosted CI、Native AOT、多 DPI/屏幕阅读器人工 acceptance 不属于本轮证明。

## Deferred / 范围

未添加 arbitrary AI topology/C++、自动 Publish、shell/filesystem tools、hardware preview、HardwareSlot/onboard/firmware/HID research、cloud sync、marketplace、multi-agent Studio、bundle format 或专业 timeline editor。未改生产协议/风扇写入/Armoury/Gear Link。没有 push/tag/release，交付后停止。

## Phase 2 修改文件

- `frontend/src/blockly/presets.js`
- `frontend/src/components/EffectStudio.jsx`
- `frontend/src/components/KeyboardVisualizer.jsx`
- `frontend/src/components/StudioTestBench.jsx`
- `frontend/src/utils/effectCapabilities.js`
- `frontend/src/utils/studioAssistant.js`
- `frontend/src/utils/studioHost.js`
- `frontend/src/utils/studioStorage.js`
- `frontend/src/utils/studioTestBench.js`
- `frontend/tests/effect-capabilities.test.mjs`
- `frontend/tests/studio-assistant.test.mjs`
- `frontend/tests/studio-storage.test.mjs`
- `frontend/tests/studio-test-bench.test.mjs`
- `tests/Aura.Tests/StudioLlmTests.cs`
- `tests/Aura.Tests/StudioPersistenceTests.cs`
- `tests/Aura.Tests/StudioRecentTests.cs`
- `tools/ci/run-studio-ai-smoke.py`
- `tools/ci/run-studio-tooling-smoke.py`
- `winui/App.xaml.cs`
- `winui/Pages/StudioPage.Persistence.cs`
- `winui/Pages/StudioPage.xaml`
- `winui/Pages/StudioPage.xaml.cs`
- `winui/Services/StudioAssistantContracts.cs`
- `winui/Services/StudioDraftStore.cs`
- `winui/Services/StudioRecentStore.cs`
- `winui/Services/StudioShellModel.cs`
- `winui/Validation/StudioAiValidation.cs`
- `winui/Validation/StudioToolingValidation.cs`
- `docs/ALPHA8_STUDIO_TOOLING_REPORT.md`

## 交付与复核

报告：`G:/Aura/docs/ALPHA8_STUDIO_TOOLING_REPORT.md`。delivery：`G:/Aura/audit_artifacts/alpha8_tooling/delivery`。

独立 task-only patch 以 MVP checkpoint 为基线，由两个临时 Git index 分别生成与 cached apply-check/actual apply，逐项比对 Git blob，不改用户 index。证据 ZIP 从独立新 staging 构建，仅包含 REPORT/logs/manifests/hashes/screenshots/必要小型 evidence；不含完整源码、build/bin/obj/node_modules/cache、旧包、nested archive、重复文件或真实 secrets。ZIP/patch SHA-256 放包外；逐条内容/唯一性/hash/秘密模式检查记录 delivery-validation.json。
