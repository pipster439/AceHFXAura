# alpha.7 current closure: ASUS_COMMAND9_RELEASE_CLOSURE

**PACKAGE_CANDIDATE_PASS — NOT FINAL RELEASE.** Candidate `49fc2019c37099ca` was accepted on 2026-10-04.
Final source reconciliation, commit, push, exact-commit hosted CI and clean artifact rebuild are now Owner-authorized; tag/release/public publication remain pending Owner approval.

The candidate was built from an uncommitted cumulative worktree. Its SHA256 is `290bc0d08b36998b3119de955483923411dfdeb881b318d2e7631e4c7c155b71`.
Eight later FanWorker validation/diagnostic and regression-wiring inputs were explicitly authorized for alpha.7; they require fresh complete software validation and exact-commit rebuild. Candidate acceptance does not validate those later inputs.

Current related closure: [HardwareSlot](HARDWARE_SLOT_RELEASE_CLOSURE.md), [plugin reload](PLUGIN_RELOAD_ROOT_CAUSE.md), [ASUS command9](ASUS_COMMAND9_RELEASE_CLOSURE.md), [package candidate](PACKAGE_CANDIDATE_AUDIT.md).

HardwareSlot physical gates, candidate 1→5→1, packaged reconnect and CS2 smoke remain retained historical PASS evidence. Plugin fixture root cause is closed; production reload lifecycle was unchanged. Command IDs 1–8 remain unchanged, ID9 is additive and protocol version stays1.
Known limits: external unauthorized selector sender UNKNOWN; reconnect mismatch branch has software coverage only; NVM mechanism/wear UNKNOWN; no hidden hardware-bank authoring.

## HISTORICAL CHECKPOINT / SUPERSEDED — preserved investigation and acceptance record

All dated stop points and unexecuted-stage statements below describe their original checkpoint. They do not supersede the current status above or claim final hosted CI/runtime acceptance. Original evidence references remain retained locally.

# alpha.7 Command 9 IPC Compatibility Audit

日期：2026-10-04（Asia/Shanghai）。证据时间为 UTC。
**SOFTWARE_CI_PASS — READY_FOR_PACKAGE_CANDIDATE。STOP，等待Owner review。**
本轮范围：唯一 StableWireEnums blocker、专项软件回归、完整 canonical CI 和版本检查。
本 agent 未修改 production。plugin reload、HardwareSlot、M605、lighting ownership 或
reconnect 源码保持本轮开始时的内容；Owner授权保留并验证并行工作追加的cooling变更。

## 决策：Case A / stale fixture

`GetCoolingReadOnlySnapshot=9` 已有正式 read-only handler、provider、service wiring、
client 调用与 WinUI Settings 消费方。它不是只有 enum 的空壳命令，移除它会破坏现有调用。
最小修复保留全部 production 源码和 `Protocol.Version=1`，更新并强化测试。

**existing IDs 1–8 unchanged; new ID 9 additive.**

## 引入位置与 working tree provenance

| 项目 | 当前证据 |
|---|---|
| 定义位置 | `src/AsusPlatform/Ipc/Contracts.cs:7`，明确值9 |
| 当前 Git 基线 | `d567de6326baa69a00500958f4b6109ec5cb1087` |
| Git 来源 | `src/AsusPlatform/` 与 `tests/AsusPlatform.Tests/` 为本轮开始前已有 untracked work；Git没有该文件的引入commit |
| 最早保留的 command9 源码快照 | 上轮 `artifacts/reload-rootcause-20261004/remaining-blocker/Contracts.cs` 已含9；上一canonical failure实际记录actual1–9 |
| 文件时间戳 | 仅元数据；不能证明command9首次作者、引入时间或某个commit，不据此归因 |
| 当前使用意图 | request enum→async dispatcher→service provider→client→WinUI完整链条支持alpha.7 read-only IPC extension |
| 旧文档/测试 | M1列表只到1–8；先前58例中没有command9专用round-trip/dispatcher coverage |

可确认它在本轮之前已存在；没有可核实的首次引入commit或作者。
`command9-provenance.json`、Git状态、历史源快照和本轮source witnesses保留精确hash。
不把“本轮确认并支持该additive命令”伪造成历史owner决定的证据。

## 实现与调用链

| 层 | 当前实现 |
|---|---|
| `RequestDispatcher.Dispatch` | 验证version、非空ID、known command、重复ID、session limit、ClientHello-first；无provider返回UnsupportedCapability/CoolingReadNotSupported |
| `RequestDispatcher.DispatchAsync` | 仅通过上述验证后调用`ICoolingReadOnlyProvider.ReadAsync`，响应放在typed `Cooling` field |
| `ServiceHost.CreateServer` | 给SecurePipeServer安装CoolingReadOnlyProvider |
| `SecurePipeServer` | 首帧解析后先OS authentication；验证完成后才创建dispatcher并DispatchAsync；后续继续检查client/session lifetime |
| `CoolingReadOnlyProvider` | System/session0 authorization barrier，串行gate、worker timeout与backoff，返回明确不可用/失败状态 |
| `AceHFXFanWorker` | 固定ReadFanSnapshot事务、受限read interface与bounded snapshot；没有本轮新增功能或物理调用 |
| `IAceHfxServiceClient` / `AceHfxServiceClient` | 固定numeric command接口，当前先检查service PID，再ClientHello并验证响应version/ID/envelope，然后发送目标命令 |
| `AsusPlatform.GetCoolingAsync` | SendAsync(command9)并取Cooling字段；缺失/失败返回Unobserved，不把缺失遥测写成零 |
| AceHFXService CLI | 既有`--client-cooling`调用9；本轮未运行 |
| WinUI Settings | PollCoolingAsync→App.AsusPlatform.GetCoolingAsync；状态文字使用typed snapshot |
| C++ daemon | 没有command9/cooling调用；GSI/plugin调度不依赖此IPC命令 |
| capability contract | IAsusPlatform.GetCoolingAsync、ICoolingReadOnlyProvider、CoolingRuntimeSnapshot/Capabilities均已存在；缺省实现UnsupportedCapability |

本次新增测试仅使用fake provider、framed DTO和真实provider的拒绝路径，不连接已安装service
或启动真实fan worker；另外加入的cooling suite使用FanWorkerFixture假进程。
上述链条的存在不等于真实设备cooling验收或write readiness。

## Protocol v1 compatibility

- Command名和值逐个固定：ClientHello1、Ping2、GetProtocolVersion3、GetServiceVersion4、
  GetServerIdentity5、GetAsusRuntimeCapabilities6、GetAsusServiceStatus7、GetAsusComponentVersions8。
  新9为GetCoolingReadOnlySnapshot；没有reorder/renumber或Protocol.Version变更。
- request必需shape仍是protocolVersion/requestId/command；command为numeric JSON。
  四字节LE长度、128KiB bound、depth16、GUID、duplicate property拒绝均不变。
- response必需envelope不变，Cooling是既有additive optional typed field。
  验证不认识Cooling的legacy-shaped DTO仍能读取version、ID、success、error。
- 旧ClientHello的数值、hello-first顺序、Protocol.Version=1与ServerHello version校验不变。
- 当前unknown command路径返回UnsupportedCapability/UnknownCommand，不调用provider。
  M1原文同样定义unknown command安全失败；old server收到9应走该分支。
  这是文档/源码兼容性推断，未运行独立旧server binary；本轮实测unknown999和无provider的v1 server。
- old client无需发送9；忽略optional Cooling字段。new client收到旧server的UnsupportedCapability，
  按当前源码保持明确Unobserved fallback。client路径为静态核验，未运行real service round trip。
- 未找到“每次新增command必须bump protocol”的项目规则。M2已在v1追加error14–16且保留旧值；
  M1定义unknown字段惰性容忍及unknown命令安全失败，支持本次兼容扩展。

M1文档新增醒目historical checkpoint与dated additive note，保留原1–8历史说明。

## 最小测试修复及完整coverage

只改 `tests/AsusPlatform.Tests/PlatformTests.cs`：

1. StableWireEnums完整集合expected改为1–9；新增9的explicit assertion，逐个固定旧1–8名称与数值。
   PlatformError、CapabilityState、ExecutionState原断言保留，整个方法成功后证明它们均执行。
2. 扩展既有RoundTrip：numeric9、request字段集合、framed request/response、Cooling typed field、
   unavailable snapshot状态、legacy-shaped envelope字段容忍。
3. 扩展既有DispatcherDefendsContract（async）：hello1成功、9accepted、response.Cooling、
   unknown/empty ID/version mismatch/pre-hello/duplicate ID拒绝且provider调用数不增加，
   unsupported provider与unavailable snapshot状态，不依赖已安装service。
4. 真实CoolingReadOnlyProvider授权拒绝路径：AccessDenied/PermissionDenied，workerStarts=0。
   已有identity policy tests继续覆盖SID/session/PID/liveness不匹配；OS pipe认证顺序另有静态审计。

这些是两个既有case内部的command9专项场景，保留原58例，没有skip/retry/quarantine，
没有删除稳定枚举或安全断言，没有放宽production协议。

## 软件验证

Targeted命令：`dotnet test tests/AsusPlatform.Tests/AsusPlatform.Tests.csproj -c Release --nologo
--filter 'TestCategory!=DesktopSmoke' --logger 'trx;LogFileName=asus-targeted.trx'
--results-directory artifacts/command9-closure-20261004/targeted-results`。

第一次targeted **58/58 PASS，0 failed、0 skipped**。在并行cooling变更加入后，
随后targeted81/81 PASS；在后续validation变更后，最终targeted **84/84 PASS，0 failed、0 skipped**，包含原58例与另外新增26例。
各次本轮实际执行的log与TRX保存在evidence目录。
DesktopSmoke是canonical原有范围边界，未新skip任何case；不替代真实service/设备验收。
StableWireEnums完整PASS；RoundTrip、DispatcherDefendsContract及既有authentication tests均PASS。

最终完整canonical命令：`pwsh ./tools/ci/run-ci.ps1`，exit=0。
开始2026-10-03T21:13:05.2182871Z，结束21:21:26.8423307Z；
build：`build/ci/20261003-211305-d5aec59b/Release`。
overall=passed，complete_required_run=true，**17/17 stages PASS，0 NOT RUN**。

| 必须阶段 | 结果 |
|---|---|
| CTest | 25/25 PASS |
| daemon integration | 45/45 PASS |
| Aura.Tests | 176/176 PASS |
| AsusPlatform.Tests | 84/84 PASS，含原58例与保留的26例cooling tests，0 skip |
| Gate A / enumeration / MTA triage / lighting-backend | 全部PASS |
| WinUI Release | PASS，0 error，canonical内1条已有WUI4001 warning |
| frontend test | RUN + PASS，42 PASS、1已有intentional skip、0 failure |
| frontend build | RUN + PASS |
| guards / diff-check / deps / configure / native build / discovery | 全部PASS |

CI后按既定closure要求native与WinUI再build PASS；WinUI再build0errors/0warnings。
已review `artifacts/reload-rootcause-20261004/version_smoke.py`及imported packaging模块：
只读取版本资源、VERSION与metadata resolver，main guards不会运行package函数；
daemon使用既有隔离temporary directory、PID/dry_run检查与owned-process teardown。
实际运行脚本于21:22:10.604704Z完成PASS，未连接真实HID/ASUS vendor worker。

| 版本项 | 验证值 |
|---|---|
| VERSION | 0.1.0-alpha.7 |
| daemon Windows resource / runtime identity | 0.1.0-alpha.7 |
| aura_web_ui resource | 0.1.0-alpha.7 |
| WinUI Aura.exe / Aura.dll resource | 0.1.0-alpha.7（按现有version resolver去除可选build metadata） |
| device-profile diagnostics daemon.product_version | 0.1.0-alpha.7 |
| packaging metadata source / resolve_release_version | VERSION / 0.1.0-alpha.7（仅resolver，未打包） |

`version-consistency.json`保留runtime、diagnostics、binary paths与四个SHA256；
`version-smoke.log`与post-ci build logs保留执行结果。脚本本轮才运行，不冒称上一轮已PASS。

### 并行源码变更与第一轮canonical failure

第一轮canonical于20:47:06.6450811Z开始、20:53:55.7252688Z结束：
CTest25/25、daemon45/45、Aura.Tests176/176、WinUI build和lighting/software gates PASS；
AsusPlatform80 PASS/1 FAIL（DispatcherDefendsContract），frontend NOT RUN。

运行中其他工作更新了AsusPlatform.GetCoolingAsync（增加service-running preflight）、
FanWorker程序、CoolingRuntime字段并新增CoolingTests/FanWorkerFixture。
我的新增测试通过fake client直接调用GetCoolingAsync，意外依赖了本机真实SCM service状态，
与这个新preflight不兼容；原StableWireEnums已经PASS。
先前commentary提到“snapshot校验”不准确；源码核验后明确为broker运行状态preflight。

本轮修正仅删除新增的实时service-dependent client调用，保留全部原case语义与用户要求的
wire/dispatcher/response/unsupported/malformed/auth/numeric9 coverage。
client存在及fallback仍由静态源码证明；不修改生产preflight、不安装/启动service，不skip/retry-until-pass。
第一轮失败与scope drift记录封存在`ci-attempt1/`。
并行工作随后将broker preflight移至真正的AceHfxServiceClient；GetCoolingAsync wrapper
恢复为直接委托client。本轮仍保留不依赖已安装service的wire/dispatcher测试。
Owner明确答复“保留当前改动并验证（推荐）”，因此最终完整CI纳入当前cooling suite；
并行生产及测试改动不属于本轮patch。

### 上一PASS之后的source drift与最终重新验证

前一完整canonical于2026-10-03T20:56:41.7090297Z至21:03:44.2247291Z为17/17 PASS，
AsusPlatform81/81；其全部日志/TRX、版本检查与源码hash保留于`ci-pass-before-drift/`。
交付前核验发现`CoolingSnapshotValidation.cs`在21:07:28Z又被并行工作更新，
并新增3个cooling validation tests；旧PASS无法覆盖这些最新源码。
没有通过重跑碰运气：明确针对新增源码运行targeted84/84及新的完整canonical，
上面最终表格仅使用这次新的完整运行与其CI后版本检查。
最终完整CI从开始到交付前native/protected和managed输入hash一致；
`stable_during_final_ci=true`、`validated_build_inputs_stable=true`。

## 交付与停止边界

证据根：`artifacts/command9-closure-20261004/`。历史reload证据与HardwareSlot物理证据均保留。
production-before/production-unchanged manifest记录并行cooling差异，并核验其余protected
production、plugin fixtures和VERSION未改变；final-ci-source-stability核验最终完整CI的native输入
及managed/cooling输入从CI开始到最终核对均保持稳定；此前source drift另存，不冒称历史CI均稳定。
本轮patch基于本轮开始时工作区snapshot，依赖既有累计alpha.7 worktree；不是全部发行源码。
不commit/push/tag/package/release，不Gear Link/USBPcap/physical retest。

## 问题、风险与未解决事项

- 本次允许的并行cooling改动曾在前一CI期间及其后追加，均保留并验证；
  最终完整CI开始前采集native与managed hash，直到交付前核对一致。历史过程并非静止工作区。
  并行生产/测试改动不归本agent所有，未混入task-only patch。
- Command9实现来源为untracked工作，没有可核实的首次作者/commit；不能伪造历史来源。
  实际旧server binary和已安装service round trip未执行，兼容性边界由固定contract、
  unknown-command rejection、legacy-shaped DTO与safe fallback源码支持。
- 数字9的显式wire pin会产生MSTEST0032常量条件warning；按用户要求保留该pin，不抑制。
  既有WUI4001与并行CoolingTests的常量warning不作为runtime/physical proof，没有通过隐瞒warning凑PASS。
- HardwareSlot物理gates原证据有效、未重做；sender仍UNKNOWN，no auto-reclaim；
  reconnect mismatch只SOFTWARE PASS；NVM机制UNKNOWN，不能把power-retention变成Flash机制结论。
- 未执行package candidate、extracted runtime、clean startup、isolated alpha.6→alpha.7 upgrade、
  artifact inventory/hash或exact final commit hosted Windows CI。须Owner review后进入这些步骤。

最终STOP：**READY_FOR_PACKAGE_CANDIDATE**，不是final release。
