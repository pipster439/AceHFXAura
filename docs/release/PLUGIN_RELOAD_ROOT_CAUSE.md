# alpha.7 current closure: PLUGIN_RELOAD_ROOT_CAUSE

**PACKAGE_CANDIDATE_PASS — NOT FINAL RELEASE.** Candidate `49fc2019c37099ca` was accepted on 2026-10-04.
Final source reconciliation, commit, push, exact-commit hosted CI and clean artifact rebuild are now Owner-authorized; tag/release/public publication remain pending Owner approval.

The candidate was built from an uncommitted cumulative worktree. Its SHA256 is `290bc0d08b36998b3119de955483923411dfdeb881b318d2e7631e4c7c155b71`.
Eight later FanWorker validation/diagnostic and regression-wiring inputs were explicitly authorized for alpha.7; they require fresh complete software validation and exact-commit rebuild. Candidate acceptance does not validate those later inputs.

Current related closure: [HardwareSlot](HARDWARE_SLOT_RELEASE_CLOSURE.md), [plugin reload](PLUGIN_RELOAD_ROOT_CAUSE.md), [ASUS command9](ASUS_COMMAND9_RELEASE_CLOSURE.md), [package candidate](PACKAGE_CANDIDATE_AUDIT.md).

HardwareSlot physical gates, candidate 1→5→1, packaged reconnect and CS2 smoke remain retained historical PASS evidence. Plugin fixture root cause is closed; production reload lifecycle was unchanged. Command IDs 1–8 remain unchanged, ID9 is additive and protocol version stays1.
Known limits: external unauthorized selector sender UNKNOWN; reconnect mismatch branch has software coverage only; NVM mechanism/wear UNKNOWN; no hidden hardware-bank authoring.

## HISTORICAL CHECKPOINT / SUPERSEDED — preserved investigation and acceptance record

All dated stop points and unexecuted-stage statements below describe their original checkpoint. They do not supersede the current status above or claim final hosted CI/runtime acceptance. Original evidence references remain retained locally.

# Plugin Reload CI Blocker Root Cause — alpha.7

日期：2026-10-04（Asia/Shanghai）。证据时间为 UTC。
**PLUGIN RELOAD BLOCKER CLOSED — SOFTWARE_CI_PASS / READY_FOR_PACKAGE_CANDIDATE。**
停止等待Owner review；尚未打包、安装/升级验证或发布。

## 最终软件 closure — 2026-10-04

Plugin reload的fixture root cause与原语义回归保持闭环；本轮未再改plugin production或fixture。
Command9经实现、service/client/UI调用链与兼容性审计确认为v1 additive read-only命令。
既有IDs1–8不变，new ID9 additive；StableWireEnums stale fixture已修复并强化。
细节见 [ASUS_COMMAND9_RELEASE_CLOSURE.md](ASUS_COMMAND9_RELEASE_CLOSURE.md)。

最新完整 `pwsh ./tools/ci/run-ci.ps1` exit=0：
2026-10-03T21:13:05.2182871Z → 21:21:26.8423307Z，
build `build/ci/20261003-211305-d5aec59b/Release`。
`overall=passed`、`complete_required_run=true`、**17/17 stages PASS，0 NOT RUN**。
CTest25/25、daemon45/45、Aura.Tests176/176、AsusPlatform84/84、Gate A、enumeration、
MTA triage、lighting-backend、WinUI、frontend test/build均PASS。
AsusPlatform数量由原58例加并行cooling工作26例组成；原StableWireEnums整个方法已PASS。
frontend42 PASS+1已有intentional skip；没有新增skip/quarantine/retry-until-pass。

完整CI之后native/WinUI再build PASS，已审阅并实际运行现有version_smoke.py：
VERSION、daemon、WinUI、diagnostics、packaging metadata source均为0.1.0-alpha.7。
检查时间21:22:10.604704Z，全部daemon操作为dry-run；未执行package函数或真实硬件调用。
原件：`artifacts/command9-closure-20261004/version-consistency.json`与canonical logs。

Owner授权保留并验证并行cooling改动，未混入本轮patch。最终完整CI的native/protected与managed输入自开始到最终核对均hash一致；
之前CI期间和之后发生的并行cooling变更及其重验记录另存，不混淆历史与最终源码。
硬件物理证据仍是原alpha.6-labelled binary历史验收，没有重新测试或篡改历史。

下文保留根因事件链、原失败与旧软件停止点；最终状态以上述dated closure为准。

## 根因与证据边界

已确定的可复现根因是 **测试夹具没有隔离实时 GSI 输入**，分类为 **D：fixture 输入／观察前提错误**。
旧测试将 health=10 写到真实 `/gsi`，随后假定 `health < 15` 一直成立。
`--dry-run` 只隔离硬件，不隔离仍监听 19897 的真实 GSI HTTP 服务。
另一条合法 health=100 输入会让持久规则变为 False；生产正确退休旧持久实例，
即使 generation 2 已提交，也不会创建新的持久实例。原 one-shot 仍渲染。
这正好产生 `persistent did not swap to new DLL`，延长等待不会改变 False 条件。

对原失败 binary `build/ci/20261003-085942-7681a1e0/Release`：

- 完全相同原 case 首次 PASS，随后连续 **10/10 PASS**，没有据此宣称修复。
- audit-only 在 reload 前插入一条合法外部 `/gsi` health=100 输入，原断言不变，**3/3 FAIL**。
- 临时事件 instrumentation 的正常对照 PASS；同一受控污染对照 FAIL。
- 失败对照证明 generation 2 已 committed、owner 正在 reconciliation、持久条件 False、
  旧持久实例销毁、旧 one-shot 继续渲染；不是 generation 未发布或 scheduler 没有 tick。

较早失败日志记录前台 cs2.exe，而本轮原 binary 连续 PASS 时前台为 ChatGPT.exe、未运行 CS2。
这与污染机制相符，但前台名称不是 HTTP sender 证据。历史失败没有完整输入事件记录，
**无法独立回溯确认历史每一次失败的发送者或全部失败均由同一输入造成**。
修复针对已证明的夹具缺陷，不将历史猜测写成进程归因结论。

| 分类 | 受控失败中的明确事件 | 判定 |
|---|---|---|
| A：generation never active | `generation_committed=2`；decision 读到 `target_generation=2` | 排除 |
| B：active but graph not reconciled | owner 执行 reconciliation，并以 `condition_not_true` 退休旧 persistent | 排除 |
| C：instance exists but no tick/render | 没有 new persistent creation；commit 后 old one-shot 持续 render | 排除 |
| D：fixture 输入／观察前提错误 | reload 前 `live_payload health=100`，persistent `truth=0` | **已复现的根因** |

`instrumented/noise/exact/events.jsonl`：外部输入 at_ns=27879248474700，
commit2=27879279283200，读取 generation2 且 truth=False=27879279630200，
旧 persistent retirement=27879279739100。请求、进程退出及断言原件均保留，
不是仅由 render count 不变得出的分类。

## Deterministic timeline

原件、逐行事件与请求时间位于 `artifacts/reload-rootcause-20261004/`；
`deterministic-timelines.json` 保留所有 T0–T11 的精确 monotonic 时间／UTC 请求时间。
表中“未发生”不是零时间，也不是漏掉事件后推断成功。

| 时间点 | 正常旧测试对照 | 污染失败对照 | 修复后污染对照 |
|---|---|---|---|
| T0 daemon ready | 当前 PID、dry_run 状态 | 同左 | 同左 |
| T1 generation 1 committed | 有 | 有 | 有 |
| T2 old persistent render | 有 | 有 | 有 |
| T3 old one-shot created/render | 有 | 有 | 有 |
| T4 reload request | 有 | 前一条外部输入 health=100 | 外部输入被现有 simulation ownership 忽略 |
| T5 candidate generation 2 prepared | 有，factory probe 随后销毁 | 有，probe 不是运行实例 | 有 |
| T6 generation 2 committed | 有，render owner thread | 有，render owner thread | 有 |
| T7 effect reconciliation | True | **False，target_generation=2** | True |
| T8 old persistent retired | generation replacement | **condition_not_true** | generation replacement |
| T9 new persistent created | 有 | **未发生** | 有 |
| T10 first new persistent render | commit 后 1.5049 ms | **未发生** | commit 后 1.7730 ms |
| T11 old one-shot semantic cancellation | 有 | 断言先失败，未执行该阶段 | 有 |

T9 创建先于 T8 旧实例销毁；这是创建成功后替换的既有实现顺序，表格编号不是伪造严格排序。
污染对照 commit 后仍有多个 old one-shot render，排除“主循环停止 tick”。
上述 latency 只是各对照一次软件观测，不是生产性能承诺。

## 最小修复：测试夹具，不修改 production

永久修改仅两个测试文件：

1. `tests/test_automation_reload_daemon.py`
   - 使用已有 `/api/gsi/simulation`，按 `applied_sequence` 确认 render owner 已接收输入；
     heartbeat 保持 health=10 与 freshness，前台使用既有模拟输入。
   - 永久加入外部 health=100 请求，确认请求仍正常返回200但不会覆盖夹具输入；
     不要求用户关闭游戏，不杀任何外部输入源。
   - daemon ready 同时验证 PID 与 dry_run，避免把仅 listener ready 当作 owner ready。
   - 按已接受 config 日志、实例 identity、render 和 destruction 事件同步。
     4秒是失败 deadline；每10ms检查事件，**不是新增固定等待窗口或 retry-until-pass**。
   - 保留原 old/new render count 断言，并强化：cosmetic 不取消原实例、
     reload 后 old one-shot 与唯一 new persistent 同时渲染、旧 persistent 已销毁、
     semantic 修改销毁原 one-shot 且发生在自然完成前、取消后 new persistent 继续渲染。
2. `tests/fixtures/automation_lifecycle_fixture.cpp`
   - 可选测试 journal，记录 marker、单调 instance identity、created/render/destroyed、elapsed。
   - 原 `old_render/new_render` trace 与 ABI/lifecycle 回调行为保留；
     validation probe 可与真正渲染的 persistent 区分，不用任意总 count 代替实例身份。

原 candidate-preparation/failure-retains-valid-generation、persistent replacement、
one-shot survival 和 semantic cancellation 的生产实现没有改变。
临时 `ReloadAudit` 及其所有 production callsites 已复制到 ignored evidence 目录后移除；
恢复的是本轮开始时文件的原始 bytes，没有 reset/checkout 或丢弃累积工作。

## Reload publication / scheduler 审计

- `PluginPublicationQueue::Submit` 在 queue mutex 内发布；owner 用同一 mutex 取出。
- owner frame boundary 调用 `PublishReload`，registry 受锁保护，commit 后才完成 promise。
- 当前 main loop 自然 tick 会依次 publication、EvaluateAutomation、Consume、Tick。
  API200说明 publication 接受，不意味着条件不成立的 effect 必须创建。
- instrumented clean/failure 都证明 commit 与 reconciliation 可达；失败中 generation2 是 active，
  但 False 条件不允许 persistent creation。没有发现需要新增 wake 的生产证据。
- 不修改 generation ordering、registry、one-shot ownership、effect graph 或调度 cadence。
- `GetGeneration` 每次从受 mutex 保护的当前 registry 读取；持久 recipe identity 包含
  generation ID，成功构建新实例才替换旧实例。one-shot 的 shared generation owner
  保留旧 DLL 的 lifetime；失败候选不会替换旧 registry。相关 native/daemon suites
  覆盖无效候选、replacement、survival 与 cancellation。
- publication queue 没有 condition_variable wake；此 daemon 是持续运行的 frame loop，
  dry_run 仍每帧执行 effect processing。干净和受控失败的 audit 都实际观测到 owner
  tick/reconciliation，未发现“已 commit 却缺少 wake”机制缺陷。

## alpha.7 / HardwareSlot 因果与 bool contract

| 检查 | 结论 |
|---|---|
| frame admission mutation mutex | 在 Tick 后限制物理帧提交；对照继续正常 tick，无 generation publication 因果证据 |
| ObserveHardwareSlot | 非 HardwareSlot／automation disabled 早退；夹具没有槽位激活或 HID访问 |
| AuraAdapter IsConnected / reconnect | dry_run；matching render 事件持续，不触发真实 reconnect |
| shutdown | 断言发生在 daemon 仍运行时，不能由后续终止解释 |
| ownership suppression bool | false 只表示此帧未提交；未进入 PushFrameAdmitted 的错误计数路径 |
| main-loop caller | frame_pushed 仅用于提交／latency 诊断；reconnect 由 IsConnected=false 分支触发 |

补充调用链边界：shutdown blackout 忽略未提交结果后按既有路径释放资源。
`ForceReset` 会把 PushFrame=false 记录为推流失败，但 daemon 唯一 startup callsite
在安装 ownership callback 前执行；该调用没有由 ownership callback 返回 false 的路径，
它不构成本次运行中“ownership suppression → transport failure”的生产路径。

**HardwareSlot、M605、51 00、BasicInfo、lighting ownership、reconnect production 均未修改，
与本次可复现 reload 失败 NOT RELATED。** 不将 suppression 当成 HID failure，不为本轮扩展 bool 架构。
旧物理验收依然是 alpha.6-labelled binary 的历史证据，本轮不重复硬件或篡改版本记录。

## HISTORICAL CHECKPOINT / SUPERSEDED — 首次根因验证与软件停止点

此节的canonical FAIL属于20:27:10Z开始的旧运行。
原日志/TRX已封存于`artifacts/command9-closure-20261004/ci-before/`和上一轮证据ZIP；
下表中的原`artifacts/ci`路径当时指该旧运行，如今该目录保存最新完整PASS。

| 验证 | 结果 | 原件 |
|---|---|---|
| 原 failing binary exact case | 首次 PASS，随后连续10/10 PASS | `baseline/summary.json`、`baseline/ten-repeats/summary.json` |
| 原 case + 受控外部 GSI | 3/3 FAIL，原 count assertion 不变 | `baseline/controlled-noise/` |
| 临时 instrumentation 对照 | clean PASS、noise FAIL、fixed-noise PASS | `instrumented/`、`deterministic-timelines.json` |
| 修复后的 exact case stress | **50/50 consecutive PASS**，0 failure/error/skip | `stress-50/summary.json`、逐次 stdout/request/fixture journal |
| 相关 native reload/plugin suites | 5/5 PASS | `related-ctest.log` |
| reload/effect/retrigger daemon suites | 4/4 PASS | `related-daemon.log` |
| fresh canonical CTest | 25/25 PASS | `artifacts/ci/ctest.log` |
| fresh canonical daemon integration | 45/45 PASS，包括原 failing case | `artifacts/ci/daemon-integration.log` |
| Aura.Tests | 176/176 PASS，0 skip | `artifacts/ci/dotnet-test.log` |
| WinUI Release build | PASS，0 error；1条已有 WUI4001 warning | `artifacts/ci/winui-build.log` |
| AsusPlatform.Tests | **57 PASS / 1 FAIL**，0 skip | `artifacts/ci/asus-platform-test.log`、`asus-platform.trx` |
| frontend test/build | **NOT RUN**，因前一阶段失败停止 | `artifacts/ci/ci-summary.json` |

stress 使用 `build/reload-rootcause-20261004/Release/aura_daemon.exe`，SHA256：
`3b579b5ddf90c79b7ab447931db5b8b5061a40d4cd11cbb5c1a662c38850b6fc`。
50次开始于20:21:05.673549Z，结束于20:22:14.816203Z（北京时间2026-10-04）。
这是完整 CI 前的已执行、逐次封存记录；本次继续阶段核验原件与当前测试修复，
并实际执行下一次完整 canonical CI，不把已有 stress 记录冒称为另一次 rerun。

完整命令：`pwsh ./tools/ci/run-ci.ps1`，exit=1。
开始2026-10-03T20:27:10.290975Z，结束20:34:22.0845398Z；
build：`build/ci/20261003-202710-9bfc0267/Release`。
`overall=failed`，17 stages中14 PASS、1 FAIL、2 NOT RUN。
`complete_required_run=true`只表示请求了完整范围，**本轮不是 full CI PASS**。
Gate A、enumeration、MTA triage、lighting-backend 等 software stages 均 PASS；
没有执行真实 vendor/HID trial。

### Historical blocker — CLOSED by command9 audit and fixture correction

`AsusPlatform.Tests.ProtocolTests.StableWireEnums`，
`tests/AsusPlatform.Tests/PlatformTests.cs:87`：

- expected：`1,2,3,4,5,6,7,8`。
- actual：`1,2,3,4,5,6,7,8,9`。
- 当前 `src/AsusPlatform/Ipc/Contracts.cs:7` 包含 `GetCoolingReadOnlySnapshot = 9`。
- 该断言失败后，同一个方法的后续 enum assertions 未执行；不推断其结果。

这是已有 ASUS platform wire-contract/fixture 不一致，不是 plugin render regression。
按“CI仍FAIL：STOP with exact blocker”停止，没有修改 enum/test、重试 CI、放宽断言、
跳过阶段或扩展本轮 scope。旧失败 CI 在 `canonical-before/` 中另存，未覆盖历史。

### Historical version 与交付边界 — superseded by final software closure

VERSION 仍为 `0.1.0-alpha.7`；本次 fresh canonical 已构建 daemon 与 WinUI。
由于完整 CI 未 PASS，**CI PASS 后重建及完整 VERSION/daemon/WinUI/diagnostics/package
metadata source 一致性检查未执行**，不能宣称该版本 gate 已闭环。
`version_smoke.py` 是准备好的 software-only 检查脚本，尚未运行；不作为 PASS 证据。
没有额外 final-binary stress、包提取 smoke、clean startup、alpha.6 upgrade 或 hosted CI。

交付包含两份报告、historical checkpoint 标记、两个测试文件的最小 patch、
原始请求/进程退出/时间戳/事件链、50次 stress、完整 canonical 日志与 hash manifest。
生产源码与 `source-before/` 的相应 bytes 一致；`source-scope-audit.json` 可核验。
patch基于当前 HEAD，依赖已有累计 alpha.7 worktree，不冒称完整独立发行源码。
历史停止点：**SOFTWARE_CI_BLOCKED**；已由本文件顶部最终SOFTWARE_CI_PASS状态取代。
不 commit / push / tag / package / release，不使用 Computer Use，不执行硬件操作。
