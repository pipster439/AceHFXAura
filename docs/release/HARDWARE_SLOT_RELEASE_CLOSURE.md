# alpha.7 current closure: HARDWARE_SLOT_RELEASE_CLOSURE

**PACKAGE_CANDIDATE_PASS — NOT FINAL RELEASE.** Candidate `49fc2019c37099ca` was accepted on 2026-10-04.
Final source reconciliation, commit, push, exact-commit hosted CI and clean artifact rebuild are now Owner-authorized; tag/release/public publication remain pending Owner approval.

The candidate was built from an uncommitted cumulative worktree. Its SHA256 is `290bc0d08b36998b3119de955483923411dfdeb881b318d2e7631e4c7c155b71`.
Eight later FanWorker validation/diagnostic and regression-wiring inputs were explicitly authorized for alpha.7; they require fresh complete software validation and exact-commit rebuild. Candidate acceptance does not validate those later inputs.

Current related closure: [HardwareSlot](HARDWARE_SLOT_RELEASE_CLOSURE.md), [plugin reload](PLUGIN_RELOAD_ROOT_CAUSE.md), [ASUS command9](ASUS_COMMAND9_RELEASE_CLOSURE.md), [package candidate](PACKAGE_CANDIDATE_AUDIT.md).

HardwareSlot physical gates, candidate 1→5→1, packaged reconnect and CS2 smoke remain retained historical PASS evidence. Plugin fixture root cause is closed; production reload lifecycle was unchanged. Command IDs 1–8 remain unchanged, ID9 is additive and protocol version stays1.
Known limits: external unauthorized selector sender UNKNOWN; reconnect mismatch branch has software coverage only; NVM mechanism/wear UNKNOWN; no hidden hardware-bank authoring.

## HISTORICAL CHECKPOINT / SUPERSEDED — preserved investigation and acceptance record

All dated stop points and unexecuted-stage statements below describe their original checkpoint. They do not supersede the current status above or claim final hosted CI/runtime acceptance. Original evidence references remain retained locally.

# HardwareSlot alpha.7 Release Closure

日期：2026-10-04（Asia/Shanghai）。证据时间统一使用 UTC，北京时间为 UTC+8。

## 结论与停止点

**SOFTWARE_CI_PASS — READY_FOR_PACKAGE_CANDIDATE。STOP，等待Owner review；不是final release。**

HardwareSlot 核心功能的已测试路径取得验收证据。Plugin reload 的受控失败已定位为
fixture 未隔离真实 GSI 输入，最小测试修复后50/50 stress、相关 suites和最新完整 CI
中的45/45 daemon cases均 PASS；生产 reload/lifecycle 调度未改变。
原StableWireEnums blocker经command9实现、service/client/UI链条与protocol兼容性审计后确认
为stale fixture，已修复并强化。**existing IDs1–8 unchanged；new ID9 additive，Protocol.Version仍为1。**
最新完整canonical CI17/17 stages PASS、0 NOT RUN，frontend test/build均已执行。
本轮保留Owner授权纳入验证的并行cooling工作，不将它们混入task-only patch。
详见 [PLUGIN_RELOAD_ROOT_CAUSE.md](PLUGIN_RELOAD_ROOT_CAUSE.md) 与
[ASUS_COMMAND9_RELEASE_CLOSURE.md](ASUS_COMMAND9_RELEASE_CLOSURE.md)。

保持最小生产协议：BasicInfo PRE → 必要时一个 typed `51 00` → BasicInfo 确认。
不添加官方 refresh companions，不增加 bank authoring，不继续 getter 研究。
此前fresh A深浅对照已证明 **FRESH_BARE_51_00_AP_PHYSICAL_PASS**；原canonical smoke、
真实ManualHold、同槽拔插reconnect、板载灯效与A深浅共同变化的历史证据全部保留。
本轮没有重新观察设备或重复任何physical验收。

当前源码版本为 `0.1.0-alpha.7`。完整CI后native/WinUI再build PASS；已审阅并执行现有
version_smoke.py，VERSION、daemon、WinUI、diagnostics、packaging metadata source一致性PASS。
本报告不将未执行的发行包安装、升级或 GitHub-hosted CI 写成 PASS，也不宣布可以发布。
本次没有 commit、push、tag、创建发行包或 release。

| Gate | 结果 | 证据边界 |
|---|---|---|
| bare selector 的 AP 物理行为 | PASS | fresh slot1 浅 / slot5 A=3.2 mm 深；历史独立实验 |
| canonical hardware-slot smoke | PASS | 真正 canonical runner，非 audit copy；实时 BasicInfo、去重与规则恢复 |
| ManualHold | PASS | 真 WinUI 手动 Apply、真实 Notepad/CharMap 前台、真实槽位；无额外 selector |
| reconnect（actual=desired） | PASS | Owner 真拔插，generation 1→3，BasicInfo5，active 恢复，dirty=false，selector=0 |
| reconnect（actual≠desired） | SOFTWARE PASS | 本次实际设备回到5；不虚构 mismatch 真机结果 |
| HardwareSlot firmware lighting | OWNER PHYSICAL PASS | slot1 红色恒亮/浅 A；slot5 彩虹/深 A；返回1；C0 81=0 |
| drift detect + warn / no reclaim | SOFTWARE PASS | 10 秒 bounded observation、共享 gate、失败无 burst、无自动抢回；历史 sender UNKNOWN |
| plugin reload root cause / minimal fixture fix | PASS | 受控失败D类；production unchanged；50/50 stress、相关5 native+4 daemon cases、最新daemon45/45 |
| command9 IPC contract | PASS | v1兼容additive read-only command；原1–8不变；wire/dispatcher/authorization专项回归 |
| canonical software CI | **LATEST PASS** | 17/17 stages、0 NOT RUN；CTest25/25、daemon45/45、Aura.Tests176/176、AsusPlatform84/84、WinUI、frontend、全部software gates PASS |
| alpha.7版本一致性 | PASS | CI后再build及software-only runtime/diagnostics check；未生成package |
| package / clean install / alpha.6 upgrade | NOT EXECUTED | 留给 Owner 的下一轮发行包授权 |
| exact final commit hosted Windows CI | NOT EXECUTED | 本次未 commit/push |

## 证据根目录与完整性

根目录：[`artifacts/hardware-slot-release-20261003-080612/`](../../artifacts/hardware-slot-release-20261003-080612/)。
该目录被忽略，不进入发布产物或 source patch。

连续被动 capture 从约 `08:15:57.609Z` 持续至 `08:50:47.425Z`，覆盖 BasicInfo gate、
canonical smoke、ManualHold、拔插、官方灯效准备、Aura 最终灯效测试；中途未 stop/start。

- 文件：`usb/hardware_slot_closure.pcap`。
- SHA256：`cc70aa4e05d98ef3a9a26cb6a91cd2f35e53ce63f91d353594005a0e72da2e5c`。
- 收集器读取 2,172,493 records，保留322；包含两个设备地址的目标 identity/configuration descriptor。
- 这是 **privacy-filtered reviewed-family capture**，不是完整 controller capture。
  普通输入、字符串描述符/serial、RGB bodies 未保存；不捕获账号凭据。
- C0 81 的 OUT submission 在丢弃 body 前计数，manifest Counter 中缺失项表示0；
  该计数路径有永久测试。不能仅凭 pcap 中没有 RGB body 声称没有 RGB 写入。
- classic PCAP 没有 kernel-drop 统计；零计数限定为本收集器观察到的范围。
- 最终离线分段：`parsed/closure-phase-audit.json`；不要用较早解析快照的 hash 当作封存文件 hash。

| 阶段 | BasicInfo OUT | selector OUT | companions 25 00/25 01/51 31 | magnetic/DKS/reset | firmware lighting | 50 55 |
|---|---:|---:|---|---:|---|---:|
| 初始 read-only gate | 2 | 0 | 0/0/0 | 0 | 0 | 0 |
| canonical + ManualHold | 69 | 4：1,5,1,5 | 0/0/0 | 0 | 0 | 0 |
| reconnect 至第一 Aura 退出 | 2 | 0 | 0/0/0 | 0 | 0 | 0 |
| 官方灯效准备 | 25 | 1：1 | 4/4/1 | 0 | 51 2c ×10 | 2 |
| 正式灯效窗口前额外 Aura manual actions | 9 | 4：5,1,5,1 | 0/0/0 | 0 | 0 | 0 |
| 正式 Aura 灯效窗口 | 5 | 2：5,1 | 0/0/0 | 0 | 0 | 0 |

整份 capture：selector=11、BasicInfo=113；Direct RGB C0 81 submission=0。
`51 50/4f/58/59/52/53/54/23` 均为0，`51 2d`、`50 40` 均为0。
官方准备阶段的 `51 2c` 与两个 `50 55` 是授权建立灯效对照的写入，**不是 Aura HardwareSlot
activation 的行为**。不能用全 capture 的 Apply=2 否定正式 activation 的 Apply=0。

### 额外四次 manual activation 的处理

`08:46:41.815`、`08:46:47.777`、`08:46:50.127`、`08:47:09.489` 有四次 selector。
第二个 Aura daemon 的 selector counter 增量、Manual reason / manual-action sequence 与之相符；
谁操作了这些 manual actions 没有独立确认。不能声称都是 Agent 的正式 stage，也不归因给 Owner。
它们在 `08:47:18.555` 正式灯效窗口之前；正式窗口无额外 selector。
总数守恒：第一 daemon4 + 官方准备1 + 第二 daemon6 =11。没有将这些动作误写为历史未知 sender 的漂移。

## canonical smoke 与 ManualHold

真实执行命令：

```powershell
pwsh ./tools/hardware/run-hardware-slot-smoke.ps1 `
  -AllowHardwareWrites -SlotA 5 -SlotB 1 -ManualHold `
  -OutputDirectory artifacts/hardware-slot-release-20261003-080612/canonical-smoke
```

`hardware-smoke-summary.json`：overall=passed，restoration=restored，exit=0。
Profile GUID：HW Slot5=`ae368645-1ced-43fa-a899-e90b6abc7723`；
HW Slot1=`b43db7b1-e783-457c-8365-6af3545347e1`。
原隔离测试 document 的 HW Slot1 错映射到3，通过真实 WinUI Save-only 修正到1；
没有复制或覆盖真实用户配置。

| 真实 stage | BasicInfo | selector counter / 行为 |
|---|---:|---|
| Notepad → HW5 | 5 | fresh-query same-slot no-op，0 selector |
| CharMap → HW1 | 1 | 一个 selector1 |
| Notepad → HW5 | 5 | 一个 selector5 |
| Aura WinUI 手动 Apply HW1 | 1 | 一个 selector1；manual_action_sequence=1 |
| 返回 Notepad | 1 | ManualHold 阻止抢回；0 selector |
| CharMap 稳定 | 1 | hold 清除；query-confirmed no-op |
| 再返回 Notepad | 5 | 一个 selector5 |

真实手动 Apply 完成在 `08:28:13.658Z`；timeline 中 `08:23:45 MANUAL_APPLY_UI_PRE`
仅是较早准备动作，不能当作成功的 Apply 时间。
Aura 前台停留超过 freshness 窗口后，hold 先采用 `AwaitingStableExternal`，
返回 Notepad 时绑定 notepad.exe 并保持 HW1；切换不同稳定外部 context 后正常解除。
保留既有 ManualHold/control-surface 语义，没有重写 P4B 或添加第二 activation path。

Runner 修复：安装临时规则前确认真实目标 foreground 稳定；阶段以 accepted decision identity
（decision/config/manual-action/foreground sequence + target GUID）和实际 selector counter 判唯一性。
same-slot query observation 可增加 attempt 诊断，不等于重复 selector；真正多余 selector 仍 FAIL。
`finally` revisioned 恢复原规则/fallback；本次恢复为 automation disabled、bindings=[]、无 fallback。
默认没有 -AllowHardwareWrites 仅取缓存；CI/GITHUB_ACTIONS hard refuse 写模式。

Runner 自己只声明 host/device identity 观察；Owner 的 AP/灯效实体验证在独立阶段记录，
不将脚本 JSON 的 `hardware_behavior=NOT VERIFIED` 偷改成物理 PASS。

## reconnect 闭环

Owner 执行 unplug/replug，回复“好了”。desired5、actual5；session generation 1→3。
新地址13的首个命令是 `12 00`，匹配真实 IN slot5；没有自动 `51 00`、setting replay 或 `50 55`。
active HW5 被重新确认、dirty=false、第一 daemon selector_count 保持4。
证据：`state/reconnect-result.json` 与对应 first-state/diagnostics、完整 USB。

实现上的必要修复：即使 Direct RGB admission 被关闭，Native transport 的被动 interface-change
标志也能让 Aura reconnect lifecycle 识别旧 handle，释放并按既有 backoff 重连。
这没有修改 M605 staged transaction、allowlist、parser、settle 或 quarantine contract。
Profile state 对新 transport generation 最多执行一次 read-only observation，不自动 reapply。

本次实际 reconnect 时临时规则已恢复、automation disabled；actual≠desired、automation enabled
时的故障分支由生产-backed mock regression 覆盖，未单独进行第二次物理拔插。
不把一个 matching physical case 写成全部 reconnect 分支的真实验收。

## Lighting Ownership

生产采用 **停止发送 Direct RGB**，没有创造 release/stop HID command。
Profile mutation gate 与 frame admission 共用，同一 lock order 为 mutation gate → DeviceWriteMutex：

- 保存的 selected backend=HardwareSlot 时，从启动第一帧开始抑制 C0 81。
- HardwareSlot activation 在 query/select 前保守保留 FirmwareBank ownership；失败/defer、漂移、断连仍抑制。
- 正常渲染与 shutdown blackout 都经过 admission；被抑制不当作 HID failure。
- 成功 HostManaged activation 才恢复 Aura DirectRGB ownership，单纯 Save 不切 ownership。
- 快速 backend 转换与帧发送串行化，防止已确认 bank 后晚到的 Aura RGB frame 覆盖灯效。

真机准备仅经官方灯效 UI：slot5=Rainbow100%，slot1=Static red100%，不修改 AP/DZ/RT/DKS。
曾按已有授权执行官方“解除同步”；query 无匹配响应，不能把 SDK fallback/UI 状态写成 firmware truth。
正常灯效控件可用，实际 setter/Apply 已捕获；只有这一准备阶段有 firmware lighting writes。
第二个 Aura daemon 在准备中仅承担 BasicInfo observer，automation OFF、FirmwareBank admission，
未进行 activation；Gear Link `08:45:40.857Z` 已关闭，正式窗口仅 Aura activation。

正式 Aura 1→5→1：

| stage | Runtime total_ms | selector→completed BasicInfo（USB） | Owner 对照 |
|---|---:|---:|---|
| 1：slot1 | 2.2182，fresh-query no-op | 无 selector | 浅 A、红色恒亮 |
| 2：slot5 | 118.3994 | 116.522 ms | 深 A、彩虹 |
| 3：slot1 | 118.1475 | 115.697 ms | 浅 A、红色恒亮恢复 |

Owner 在统一确认问题后回复“确认”；原回复与问题保存于 `state/lighting-owner-result.json`。
AP/灯效共同变化为 OWNER PHYSICAL PASS，精确实体按键时间未知。
canonical 中四次 selector→completed BasicInfo 为104.866/104.878/104.432/104.461 ms；
这是查询观测延迟（包含既有验证等待），不是 firmware 最小切换延迟或整机 foreground 延迟承诺。

**Lighting Ownership 的当前 release blocker 已在已测试 HardwareSlot 使用路径闭环。**
并非证明所有历史 latched DirectRGB 状态只停帧即可清除；先有活跃 HostManaged RGB streaming
再切 HardwareSlot 的单独真机转换本次未执行，其串行化/恢复逻辑有 software regression。
外部 ASUS writer/Aura Sync 仍可能覆盖灯效，Aura 不宣称控制或停止这些服务。

## external-slot-drift release policy

历史 unauthorized `51 00` sender 仍 UNKNOWN；不归因给 LightingService、ASUS framework、Gear Link 或 Aura。
本次不继续 Procmon/getter 无限归因，也没有停止 ASUS Windows services。

alpha.7：connected + selected HardwareSlot + automation enabled + Clean/non-quarantine 时，
每10秒最多一次 BasicInfo observation；busy gate 跳过、失败占用同一 interval，不 burst。
desired 与 observed 分离；mismatch 清 active、dirty=true、中文提示外部操作，不猜 sender。
观察自身不生成 automation decision、不产生 success event、**不主动 reclaim**。
后续正常 foreground decision 仍可激活；ManualHold/admission/freshness 继续约束它。
automation OFF 时依赖 reconnect/state path 或显式刷新；观测不是实时且永久不变的保证。
auto-reclaim 默认 OFF 的可选产品暂不实施，避免与 ASUS 服务互相抢 bank；可留后续版本。

## 软件验证与版本

### 当前软件 checkpoint — 2026-10-04

完整执行`pwsh ./tools/ci/run-ci.ps1`（无Skip），exit=0，overall=passed。
2026-10-03T21:13:05.2182871Z → 21:21:26.8423307Z；fresh build：
`build/ci/20261003-211305-d5aec59b/Release`。
17/17 stages PASS，complete_required_run=true，0 NOT RUN。
CTest25/25、daemon45/45、Aura.Tests176/176、AsusPlatform84/84、Gate A、enumeration、
MTA triage、lighting-backend、WinUI、frontend test/build全部执行并PASS。
frontend42 PASS+1原有intentional skip；WinUI在canonical内0errors/1条已有WUI4001 warning，
CI后再build0errors/0warnings。没有新增skip、retry-until-pass或放宽语义断言。

AsusPlatform第一轮targeted58/58 PASS；Owner保留并行cooling更新后，最终targeted84/84 PASS（之前81/81记录另存）。
原StableWireEnums整个方法完成，包括PlatformError、CapabilityState和ExecutionState断言。
第一轮本任务CI曾因新增fixture依赖实时SCM状态失败，已按最小fixture修正封存；
详见command9报告，不能将该失败记录删除或伪装成一次无失败的运行。

CI后native/WinUI再build与已审阅的version_smoke.py PASS；21:22:10.604704Z确认
VERSION、daemon product_version、WinUI版本、diagnostics与packaging metadata source均为0.1.0-alpha.7。
新daemon仅dry-run软件启动；旧物理验收仍是alpha.6-labelled binary，不篡改或重复。
证据：`artifacts/command9-closure-20261004/`、最新`artifacts/ci/`与本轮交付ZIP。

本轮未修改plugin production或HardwareSlot/lighting ownership/M605/reconnect源码。
并行cooling变更由Owner明确授权保留并纳入CI；本轮patch只包含本轮测试/文档改动。
前一完整PASS之后cooling validation又变更，触发当前84例与完整CI重新验证；
最终完整CI所有已记录native/protected和managed输入从开始到最终核对均hash一致。
前一PASS及source drift记录保留于ci-pass-before-drift，不作为最终源码验收替代。

### HISTORICAL CHECKPOINT / SUPERSEDED — 2026-10-04首次reload closure CI

以下旧FAIL保留于`artifacts/command9-closure-20261004/ci-before/`与上一轮证据ZIP。
这里的原artifacts/ci路径已被最新完整PASS替代，不再是当前结果。

完整执行 `pwsh ./tools/ci/run-ci.ps1`（无 Skip），exit=1，overall=failed。
开始2026-10-03T20:27:10.290975Z，结束20:34:22.0845398Z；
fresh build 为 `build/ci/20261003-202710-9bfc0267/Release`。
原件：`artifacts/ci/ci-summary.json`，并收录于本次 reload closure evidence ZIP。
17 stages中14 PASS、1 FAIL、2 NOT RUN；完整范围请求不代表完整通过。

CTest25/25、daemon integration45/45（含原 failing reload case）、Aura.Tests176/176、
WinUI0errors/1条已有WUI4001 warning、lighting/software gates PASS。
AsusPlatform.Tests57/58 PASS；`StableWireEnums`在 `PlatformTests.cs:87` 失败，
当前Command新增 `GetCoolingReadOnlySnapshot=9`，而断言仍期待1–8。
不据此改 wire contract 或放宽测试；后续frontend test/build均NOT RUN。

原 failing binary 首次和随后10次自然环境均PASS；外部health=100受控污染使原断言3/3 FAIL。
deterministic timeline证明generation2已经committed、graph正在reconcile、persistent条件False，
old one-shot仍render；根因分类D（fixture输入前提）。
修复使用现有simulation输入ownership与instance/lifecycle事件同步，保留原语义断言；
CI前连续50/50 PASS，相关native5/5与daemon4/4 PASS。
历史失败发送者仍未知，不虚构历史归因。详细过程、时间戳和源文件一致性见根因报告。

VERSION仍为0.1.0-alpha.7。CI未PASS，未执行条件性的再build、完整版本一致性runtime smoke，
也未产出任何发行包。本次没有HardwareSlot/lighting ownership/M605/reconnect生产修改，
没有重复物理验收；既有alpha.6-labelled physical evidence原样保留。

### HISTORICAL CHECKPOINT / SUPERSEDED — 2026-10-03 软件记录

以下原失败及物理前PASS记录保留作历史，不覆盖上面的最新软件checkpoint。

物理测试使用完整 CI PASS 的 Release build：
`build/ci/20261003-080612-0c5e3e40/Release`。
当时二进制版本仍显示 alpha.6；这不能当成新 alpha.7 二进制的真机启动证据。
之后只更新 VERSION 为 alpha.7、补文档/离线归档，并再次执行 canonical CI。
更新后没有另行硬件写入或 Computer Use。

物理前 canonical CI：CTests25/25、daemon integration45/45、Aura.Tests176、AsusPlatform.Tests58、
frontend42 PASS+1 intentional skip；WinUI Release 0errors、1条已有 WUI4001 warning。
附加 CI infrastructure11/11、privacy/audit22/22。
第一轮 daemon plugin reload case 有一次 `persistent did not swap to new DLL`（count1→1）失败；
同 binary 单项复跑及下一次完整 CI PASS。原失败日志保留在 `ci-first-attempt/`。
版本后 fresh full CI 再次失败于同一 case（count0→0），因此不能再描述为已闭环的偶发问题。
没有修改产品/测试来绕过，不声称已经找到或修复根因。

版本后的最终记录：`state/final-software-validation.json`、`ci-alpha7-failed/ci-summary.json`。
命令是完整 `pwsh ./tools/ci/run-ci.ps1`，exit=1，overall=failed；
CTests25/25 PASS，daemon integration44 PASS/1 FAIL，后续 dotnet/WinUI/AsusPlatform/frontend NOT RUN。
`complete_required_run=true` 表示请求完整范围，不表示所有 stage 已成功执行。
build 为 `build/ci/20261003-085942-7681a1e0/Release`。

保留诊断复现：`reload-investigation-short/` 使用同一失败 binary、原测试、仅保存 stdout/request/trace。
reload API 返回200，daemon 日志确实记录 candidate generation2 prepared；原150ms检查窗口内无 new_render，FAIL。
`reload-investigation/` 的 audit-only observation 对照将原150ms等待改为800ms，仍 new_render=0、FAIL。
该对照没有改永久测试或生产，**不是 PASS，也不证明某个具体根因**；只说明简单延长等待不能解决本次复现。
全部 daemon 均 `--dry-run`，真实 HID 未挂载。

## 发行前最小剩余项

1. 软件gate已完成：plugin reload fixture root cause CLOSED、command9 stale fixture CLOSED、
   完整canonical17/17 PASS与alpha.7版本一致性PASS；STOP at READY_FOR_PACKAGE_CANDIDATE。
2. Owner review 本报告、本轮task-only patch与原物理证据，确认是否授权发行包准备。
3. 在正式包装路径产出 alpha.7 candidate 后，验证完整提取目录的 packaged runtime、clean startup，
   daemon/WinUI/manifest/archive 版本一致，不依赖 checkout、SDK、audit path。
4. 使用隔离 alpha.6 配置副本做真实升级保留检查；legacy HostManaged 和 HardwareSlot Save/load
   当前有软件测试，不能替代已提取包的升级实测。
5. 执行产物 inventory/hash 检查，确认无 captures、真实 config/profile、实验 writer、debug/evidence 文件。
   当前 source guards PASS 不等于实际 ZIP 已完成此检查。
6. 若进入提交/推送阶段，再取 exact final commit 的 GitHub-hosted Windows CI PASS；本次无 push。

不新增 getter、bank authoring、NVM wear 研究、hardware-bank writer redesign 或未知 companions。
prior power evidence 可证明测试 bank 内容跨断电保留，不证明每次 setter 都直写 Flash 或 wear/deferred commit。
**NVM mechanism仍UNKNOWN；external unauthorized selector sender仍UNKNOWN；no auto-reclaim。
reconnect mismatch physical branch仍只有SOFTWARE PASS。** 本轮不扩大这些历史结论。

## 变更与交付范围

原硬件closure的累计变更涉及 Direct RGB admission/reconnect lifecycle、HardwareSlot observer/reconciliation、
runner 的 identity/counter 判断、privacy counter 与永久 regressions、中文说明及版本源。
没有改生产 selector protocol、NativeHid command allowlist、M605 timing、magnetic planner/prior guard
或 P4B decision/ManualHold semantics。

原硬件证据包中的`scoped.patch`是**从当时HEAD生成的指定文件累计diff**，包含此前alpha.7 MVP改动；
因同一 worktree 累积高度交织，没有虚构仅本轮的干净 patch，也没有重写历史/丢弃修改。
文件清单、patch base/hash 和证据 manifest 在 evidence 根目录。补丁排除 config、capture、logs、build outputs。
它是 review 范围补丁，依赖当前累计 MVP worktree，不能当作可独立应用到干净 HEAD 的完整发行源码快照。

本轮`alpha7-command9-task-only.patch`以任务开始时snapshot为基线，仅含PlatformTests、
M1 additive note与三份closure报告，不混入既有未提交或并行cooling变更。
patch-scope.json记录before/after hashes与隔离apply验证；依赖既有累计alpha.7工作区。

当前停止点为 **SOFTWARE_CI_PASS / READY_FOR_PACKAGE_CANDIDATE**。
完整software gate已经PASS；等待Owner review后才进入package candidate及剩余发行验收。
上一物理checkpoint的Aura/daemon已由Owner正常退出、capture封存、worker退出、设备slot1记录
仍是历史事实；本轮未重新观察设备、自动恢复/抢回或发出新的硬件命令。
没有commit/push/tag/package/release。等待Owner review；包、安装、升级与hosted CI均未执行。
