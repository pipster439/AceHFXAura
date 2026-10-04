# 软件 CI 与本地 Device Profile smoke

## 单一软件入口

在仓库根目录使用 **PowerShell 7 / Windows x64**：

```powershell
pwsh ./tools/ci/run-ci.ps1
```

默认运行全部 required 软件验证，不需要现有 `build/`。脚本不 reset、checkout、clean
或丢弃工作区；dirty tree 可以运行，`working_tree_clean` 只是信息。

当前项目依赖：已安装的 Visual Studio C++ Desktop workload / Windows SDK、CMake
（最低版本见 CMakeLists.txt）、Python、Node 22 / npm、.NET 10。
脚本使用安装好的 CMake 默认 Visual Studio generator，不安装或升级 compiler/SDK。
GitHub 使用仓库现有的 Python 3.13、Node 22、.NET 10 setup 版本。
Windows App SDK、NuGet 和 npm dependencies 继续由现有项目/lockfile决定。

顺序：

1. Repository/source/daemon-only Profile ownership、migration guards；检测固定测试端口空闲。
2. `git diff --check`。
3. Gate A fake API regression、enumeration ABI compile、MTA triage compile 与 lighting-backend software tests/compile；不执行 vendor COM/hardware trial。
4. `frontend/npm ci`。必须在 configure 前执行，以注册 Studio 生成 DLL 的永久回归。
5. 全新 CMake configure → 默认 ALL_BUILD（包含测试和 fixture DLL）→ Release。
6. CTest JSON discovery → 全部注册测试；缺少关键永久 suite 时失败，不按固定数量验收。
7. 既有 WebUI / daemon **dry-run**、Lighting Automation v2、GSI contract/dictionary integration。
8. Aura.Tests（排除明确的 DesktopSmoke；配置 mock、客户端、迁移、P4B 测试正常运行）。
9. WinUI x64 Release build（运行前正常退出会锁住 build output 的开发版 Aura）。
10. AsusPlatform.Tests 与隔离的 FanTypeLibValidator.Tests（排除 OfflineMetadata；不读取已安装 vendor TypeLib 或激活 COM）。
11. npm test 与 frontend production build。Vite output 重定向到本次 build 下，避免改动 tracked `web/index.html`。

当前完整 summary 为18个 required stages（包括新增 Fan TypeLib validator）；以实际 stage registry 全部 passed、complete_required_run=true 和0 NOT RUN验收，不固定复用旧17阶段结果。

本轮未把桌面像素、包装包、USB capture、物理键盘行为放入 required software CI。
原 workflow 中的打包/包验收不再混入普通 CI；发行验收继续按
[Packaging](PACKAGING.md) / [Release checklist](RELEASE_CHECKLIST.md) 独立执行。

### 可选参数

```powershell
pwsh ./tools/ci/run-ci.ps1 -Configuration Release -OutputDirectory artifacts/ci
pwsh ./tools/ci/run-ci.ps1 -KeepBuild
pwsh ./tools/ci/run-ci.ps1 -SkipFrontend -SkipWinUI
```

默认每次使用新的 `build/ci/<run-id>/`，无递归删除。
`KeepBuild` 每次仍 configure，但复用专属 `build/ci/kept/`。
`SkipFrontend` 只跳过 frontend test/build，**不跳过 npm ci / native Studio fixture**。
任何 skip 都将 summary 标为 `partial`，不是完整 required CI PASS。
没有跳过核心安全 suites 的快捷参数。

端口 19897/19898 被占用时脚本失败并提示正常退出 Aura；不杀任何用户进程。
请不要在同一个工作区同时启动两次 CI（npm ci / WinUI obj 是共享构建资源）。
测试自建进程/配置仍由既有测试隔离并清理；CI 不修改用户 Aura 配置或 quarantine。

## GitHub Actions

Canonical：[ci.yml](../../.github/workflows/ci.yml)。沿用 push、pull_request、
workflow_dispatch（未引入新的 branch 名）；windows-latest，只调用上述入口。
同一 PR/branch 的新运行取消旧运行，job timeout 60 分钟。
contents: read、checkout 不保存 credentials，无 pull_request_target、write token 或 secrets。
遵循原仓库 major-version action pinning；若需要 SHA pinning，后续统一处理。

cache 仅 npm download / NuGet packages，key 依赖 package-lock/project/config hashes。
不缓存 build tree、用户配置、device-profiles、诊断、HID 状态。
`if: always()` 上传指定日志/summary/CTest XML/TRX；upload failure 不掩盖主测试结果。
Step Summary 只有 stage/result/duration 和 overall，不粘贴完整日志。

**GitHub-hosted CI: NO REAL HARDWARE WRITES。**

software entrypoint 强制 `CI=true`，清除 package/desktop opt-in 变量。
CTest 使用 mock transport；真实 daemon integration 显式 dry-run。
Native HID 测试的 `--live-hardware` 在 CI/GITHUB_ACTIONS 下先拒绝，退出码2；
本地 hardware script 的写入模式同样在任何请求/配置/foreground之前拒绝。
CI 不启动正常 daemon，不执行任何 M605/51 54/reset/Apply gate 真机事务，
不清 quarantine，不验证物理 reconnect。
这不是 OS 级沙盒：新测试必须遵守同样的 mock/dry-run约定，不能把实体接口测试注册进普通 CTest。

## Artifacts

默认 `artifacts/ci/`（Git ignored）：

- `ci-summary.json`：schema_version1、configuration、informational working_tree_clean、
  complete_required_run、UTC timestamps、stages(name/outcome/duration_ms/exit_code/log)、overall。
- `*.log`：每阶段日志；失败阶段和 summary 仍保存。首次失败停止后续执行。
- `ctest-discovery.json` / `ctest-results.xml`。
- `dotnet-test/<run-id>/aura.trx`。
- `step-summary.md`。

不要上传整个 artifacts 或 build/user home；workflow 明确只上传上述软件证据。
每次复用相同 OutputDirectory 会更新同名日志；未执行的stage预先标记not-run，避免旧日志冒充本轮。TRX使用独立run目录；以summary的test_results_directory定位本轮结果。需要长期比较时使用独立目录。

## 本地硬件 smoke（不属于 required CI）

本轮仅实现 harness，**不执行真实 hardware smoke**。

默认纯读取 daemon cached diagnostics，无 app launch、配置 mutation、Profile Activate：

```powershell
pwsh ./tools/hardware/run-profile-automation-smoke.ps1
```

显式授权本地真实自动 Profile switching：

```powershell
pwsh ./tools/hardware/run-profile-automation-smoke.ps1 `
  -ProfileA "Desktop" `
  -ProfileB "Gaming" `
  -AllowHardwareWrites
```

也仍支持直接传入稳定 GUID：

```powershell
pwsh ./tools/hardware/run-profile-automation-smoke.ps1 `
  -ProfileA '<GUID>' -ProfileB '<GUID>' `
  -AllowHardwareWrites
```

daemon 必须已运行、M605 Clean、无 quarantine，两个 Profile 必须已由用户审查；
脚本不创建/修改 Profile、不清 latch、不控制物理 RT开关。
HardwareWrites 无论 CI还是GITHUB_ACTIONS 为true都拒绝（非false/0的非空marker也保守拒绝）。
DaemonUrl只接受本地HTTP origin。脚本绝不启动/停止 Aura或daemon。

测试程序默认：
- Program A：`notepad.exe`
- Program B：`charmap.exe`（Character Map，替代了可能被用户卸载的 `mspaint.exe`）
支持通过 `-ProgramA` / `-ProgramB` 覆盖。若两个程序解析为相同 basename 或任一程序找不到，在任何配置变更前拒绝执行。

Profile 解析规则（通过 daemon Profile API canonical state，不直接读用户文件）：
- 若为合法 GUID：查找并验证该 GUID 当前存在，不存在则报错。
- 若为名称：按名称大小写不敏感精确全词匹配（不进行子串、前缀或模糊匹配）。
- 重名 profile（如存在大小写不同重名）必须 fail closed，提示 ambiguous 并列出 name + GUID。
- 找不到 profile 时报错并列出可用 profile 名称，不 dump 完整原始 JSON。
- Profile A 与 Profile B 解析为同一 GUID 时拒绝执行。
- 在任何 hardware write 前输出 Hardware smoke plan（程序、Profile 名称与 GUID、切换序列）。

脚本先 GET automation，保留整个原配置（含 extensions）在内存，用最新expected_revision
原子添加最高优先级 Notepad/Charmap bindings 并启用 automation；fallback、已有规则不变。
已有相同程序的最高优先级规则会拒绝测试。配置保存可能立刻产生自动硬件Apply，
所以**仅显式AllowHardwareWrites**进入此路径。

提示用户依次聚焦真实 `notepad.exe → charmap.exe → notepad.exe`。
Start-Process仅打开应用；Win32 GetForegroundWindow核实真正的basename，不存PID/path。
观察本机foreground稳定750ms（>500msdebounce）且 daemon decision/selected/active/dirty
一致，再持续1.2秒确认decision_sequence与activation_attempt不重复。
每阶段最多一次activation；无法取得foreground/超时、quarantine、generation变更或daemon
重启都会失败，不伪造PASS。默认每阶段120秒，可按实际RT长计划调整。
本工具不关闭Notepad/Charmap（可能有用户未保存内容）。

finally使用最新revision恢复原automation，先比较当前配置确实仍是本工具安装的版本；
不覆盖并发用户修改。恢复后重新GET确认。失败明确输出RESTORE FAILED并保留summary中的
temporary_rule_ids和original_automation_enabled，供用户通过UI/API检查移除临时规则；
不保存真实完整配置到evidence。
恢复配置后现有daemon可能按原规则/fallback重新选择；harness不冒充恢复原物理键盘状态。
网络timeout可能发生在commit之后，finally仍会检查/恢复，不假定请求失败就未写入。

输出：`before.json`、`notepad-a.json`、`charmap-b.json`、`notepad-return-a.json`、
`final.json`、hardware-smoke-summary及同名zip。Summary 中记录 profile_a/b（requested、resolved_name、resolved_id）以及 program_a/b（requested、observed_foreground），不记录可执行文件全路径。仅allowlisted字段，包含process_instance_id、
foreground basename、decision/attempt、selected/active/dirty、revision、quarantine、generation、UTC。
不导出原配置、PID、路径、raw HID或USBcapture。

`-ManualHold`是可选人工stage，要求观察真正的Manual动作、同一Notepad identity hold、
foreground稳定时不被抢回；再Charmap→Notepad验证恢复。不调用假Manual通知。
注意：切到Aura窗口Apply通常会改变foreground identity，可能无法得到Notepad hold；
脚本此时明确失败，而不会模拟或绕过已有语义。软件ManualHold合同由mock/integration长期覆盖。

组合入口：

```powershell
pwsh ./tools/ci/run-local-validation.ps1 # SoftwareOnly
pwsh ./tools/ci/run-local-validation.ps1 -HardwareSmoke -AllowHardwareWrites `
  -ProfileA '<GUID>' -ProfileB '<GUID>'
```

软件和hardware用独立summary；后者失败不改写软件历史。
先关闭用户daemon跑软件CI，组合入口会在hardware stage前提示等待；此时自行正常启动daemon并确认后继续。组合入口不自动管理lifecycle。

### 证据边界

smoke只能证明host-observed Profile switching，不能给physical RT switch、actuation feel、DKS、
retrigger、拔插、firmware persistence、quarantine恢复打Physical PASS。
这些仍需用户实际验收。软件CI绿色不授权硬件写入或发布。

## 增加永久回归

优先将测试放进现有生产源支持的CTest/MSTest，不创建版本专属runner。
Legacy migration/P4B cases继续由device_profile_runtime（include fixtures）、coordinator、binding
suites运行；includes/function registration/source guard防止fixture意外脱离入口。
新增独立C++测试在CMake add_test并使用mock，不枚举exe、固定数量或commit。
新source/schema guards加入check-source；基础设施测试在ci_infrastructure中覆盖。
不要用reference模型冒充生产实现，也不要为CI绿色绕过安全preflight。
