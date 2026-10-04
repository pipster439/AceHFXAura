# Agent-operated Gear Link / HFX Audit Log

## Latest checkpoint — 2026-10-03 CST

**本次slot5 power persistence：Actuation / RT / DKS / firmware lighting USER PHYSICAL PASS；Deadzone NOT PHYSICALLY VERIFIED。**

连续1057.167939秒capture贯穿基线和拔插，无post-reconnect target setter/Apply；仅官方reselect5及polling companions。T1后1015/1015completed BasicInfo5。历史drift source仍UNKNOWN。2秒explicit monitor在拔插挂起，有记录缺口，USBcapture不中断；不得宣称完整monitor cadence PASS。详情见本文件最新章和`audit_artifacts/slot5-power-persistence-20261003/REPORT.md`。以下旧BLOCKED/NOT VERIFIED段落保留为历史，不能当最新结果。


日期：2026-10-02；工作树 `G:\Aura`；研究范围，未修改 Aura production。

## 执行记录

1. 用户提供正确 Gear Link URL，去掉链接末尾中文逗号，复用 Edge 中的官方 zh-cn 页面。native computer path此前未能可靠判断URL；当前通过已知URL的浏览器 Computer Use控制，不报告native DevTools已打开。
2. preflight重新读取实际UI：ROG魔导士ACE HFX、6个Profile、当前默认槽位；官方选择5后保存`preflight-profile5.jpg`。descriptor VID0B05/PID1B7E、bcdDevice0159；与固件1.00.59边界一致。不可从模块“当前版本1.00.28”当成firmware。
3. 建单槽 ledger5可写、其它DO_NOT_TOUCH；官方UI名称不修改。保存测试槽位 host baseline，不导出完整storage或实际serial。
4. 已安装USBPcap1.5.4.0 usage经本机检查；隐藏elevated worker实际ready并能capture，用户没看到UAC不等于捕获失败。禁止第二capture进程；停止仅针对本worker验证 executable+pipe的实例。
5. 完成 AP / DZ / RT / DKS 四个正常页面 readback用例；精确case矩阵见 [Typed Readback](TYPED_READBACK_AUDIT.md)。目标参数query没有升级证据等级。
6. 单槽准备AP/DZ、onlyW RT、V DKS，每次操作记录UTC及before/after。官方低DZ警告显示后明确应用到测试B .1；没有点击整体reset。
7. Owner显式允许灯光“解除同步”。点击一次未解除可见warning；官方owner页显示设备灯效已选，停该case，不绕过。
8. 离线检查发现未计划的bank3/1切换与并行ownership实验；停止后续writes、物理与lighting sweep，不将污染case算PASS。没有kill其它实验或修改ASUS service。
9. 用户说明不在电脑前：继续离线解析、case scope checker、失败清理mock tests、hash索引和本文档；所有物理确认延期。

## Browser / evidence privacy

界面由`cua_repl`操作；基于可见文本、AX控件与当前DOM。没有用shell/UIAutomation替代点击。CDP仅做只读Network观察和限定slot5字段的storage投影；没有通过CDP点击/写入/调用HID。

Network只保存 request/WS次数及必要transport元信息，不保留WS frame内容、请求header、cookie、token、完整storage。未确认native DevTools Preserve Log，不能将CDP观察当成完成native DevTools设置。

`usb/*.pcap`是采集时privacy-filtered的原始留存classic pcap，不是完整控制器pcap/pcapng。普通键盘输入、serial query/string descriptor、其它设备及未经审查body在内存丢弃。保留的报文bytes与timestamps不改；derived target pcap、CSV、JSON、payload sequence各case独立。

Frame编号是留存capture编号；只对allowlist内opcode的零计数做判断。移除类别不可称“不曾发送”。具体SHA256、长度、opcode counts、scope与相对路径见`audit_artifacts/computer-use-audit/capture-index.json`。

## 工具与fresh验证

- `tools/research/gear_link_capture_worker.ps1`：被动USBPcap4 worker，自己实例的identity校验，拒绝已有capture，idle退出；无HID API。
- `tools/research/gear_link_audit_session.py`：拒绝覆盖case，采集precheck→记录→停止→hash复核→离线解析→classification；启动失败安全停止自己的capture/collector并保留失败证据。
- `tools/research/gear_link_case_scope.py`：offline BasicInfo/BankSelection检查；foreignbank污染，零异常不能自动升级VERIFIED，不做process归因。
- `tests/test_gear_link_computer_audit.py`：12项offline safety regression，含真实bank观测/污染fixtures、阻止未解决scope继续开始用例；加已有reference13项，共25项fresh PASS，详见当前`logs/research-tests.log`。

研究验证包括解析所有11份本阶段pcap、hash匹配、privacy复验、PowerShell脚本syntax和scoped patch whitespace；未运行/冒用软件CI或hardware acceptance。

没有改DeviceProfileRuntime、M605、NativeHid、P4B、Profile schema、WinUI生产、RT bytes、quarantine、timing、reconnect。没有生产readback或51 00 writer；51 53仍blocked。没有commit/push/release。

## 当前交付与剩余工作

offline readback feasibility完整交付；single-slot persistence与lighting被明确block，不宣称整个实验已完成。界面截图、case状态、host baseline和污染序列保留，可在owner回电脑后从可信bank确认恢复。报告与scoped patch不包含其它未提交产品工作。

## Recovery checkpoint — 2026-10-02 21:25 CST drift

Owner已回电脑，并新增exact official read-only getter授权。停止两个已确认前端、重新选5并取得两次相隔超过10秒的BasicInfo5；随后逐项官方提交AP/DZ/W RT/V DKS。解除同步仍被官方UI阻止，未更改灯效。官方reload/reconnect后留存未计划51 00 slot3→1，BasicInfo和UI最后为1。Owner确认没有手动操作。已停止capture/worker；不再设置、查询或抢回5。具体来源未知，不能称只剩单一writer。

本次10份privacy-filtered original pcap已重新hash/隐私复验、解析；见recovery-capture-index.json。前阶段25项日志仅属于旧checkpoint，新测试与报告另存recovery-*，不覆盖旧证据。完整power/getter闭环未完成。详见[恢复审计](EXTERNAL_WRITER_RECOVERY.md)和[readback矩阵](READBACK_RESULT_MATRIX.md)。

## Corrected full-init D checkpoint — 2026-10-02

The prior landing-page-only window is invalid for complete device initialization. Corrected D completed official Connect, owner HID authorization and verified HFX device page, then145.053s with no UI input. UI remained5; nine BasicInfo samples5; no51 00 in full D. Previous drift remains unexplained, no initialization/source attribution claim. Native HID chooser must be handed to owner immediately; no landing-page idle substitution. No setting edits/getter/persistence/process escalation/service stops. Owned passive worker exited.

See [PROFILE_DRIFT_ATTRIBUTION](PROFILE_DRIFT_ATTRIBUTION.md), corrected full-init section; `audit_artifacts/profile-drift-attribution/full-init-d/REPORT.md`. Earlier capture files/hashes remain preserved.

## Controlled-getter authority precheck — 2026-10-02

Corrected full-init Window D remains VALID PASS (no initialization drift in that prior bounded trial); previous drift remains unexplained. The current connected Gear Link UI still5, but one exact official BasicInfo audit call and its captured IN report active1. Five completed BasicInfo samples all1; no51 00 was captured, so transition time/source is unknown. Stopped before any DKS/RT/AP/DZ getter: magnetic replay0, no recovery selection, no setting changes, no W/V physical/power test. Source-attribution gate is necessary again; no escalation/service kill undertaken here. Parameter repeatability, ownership/override and cold-acquisition feasibility remain NOT VERIFIED, not evidence of getter failure/uselessness.

All owned capture/worker exited. See [Typed Readback Audit](TYPED_READBACK_AUDIT.md), latest checkpoint, and `audit_artifacts/controlled-getter-audit/REPORT.md` for exact request/reply, SDK/UI evidence, five-sample timeline, current-source audit, SHA256 and fresh tests.

## Controlled getter recovery checkpoint — 2026-10-02 15:21–15:25 UTC

Revised owner authority policy applied: startup UI5/BasicInfo1 classified STALE_UI_OR_EXTERNAL_BANK_STATE; reload→official Connect→ownerHID authorization→devicepage restored UI1/BasicInfo1. Official select5 then two BasicInfo5 separated2seconds passed. authority_recovery_count=1, success_count=1, unexpected_runtime_drift_count=0. Previous drift remains UNEXPLAINED; correctedWindowD retains VALID PASS.

One official V DKS `getKeyTrigger(1026)` executed via exact official mapping/queue, with permitted12 12 dependency. OUT97/100/105/110 exact64bytes `25 02 00 00 31 00`+58zeros; no matching25 02 IN. Pre/postBasicInfo5. `{source:1026}` is not readback: official queue resolves synthetic timeout sentinel rather than throwing; no type/fields verified. Automatic3retries do not satisfy independent2-test repeatability. Standard contrast, RT, AP and DZ not executed.

STOP conditionsC/D: recovery selection also emitted `51 31 00 00 03`+59zeros (OUT57/65/71/78), exact official setPollingRate family; these were detected during subsequent capture review, after the first V attempt, and precede it in the timeline. No further query followed discovery. Therefore whole-run no-side-effect PASS must not be claimed. Profile selection authorization does not authorize companion setters. No Apply/reset/magnetic setter/unexpected selector observed in reviewed classes; no runtime bank drift. Process sender not confirmed. No speculative mapping/query alternate, no service shutdown, no persistence/physical test, no production edits.

Readback/cold-acquisition remains NOT ESTABLISHED. Hardware bank power persistence DEFERRED. Latest sampled5 is not a current live-state claim after own observer exit. Detailed request/response limitations, timeline, SDK fallback and source in `audit_artifacts/controlled-getter-recovery/REPORT.md`, dks-result.json, source audit and captured payload sequence.43offline tests PASS (9new evidence regressions); no hardware/production-readback PASS.


## Controlled AP/DZ matrix and W baseline drift — 2026-10-02 15:40–15:57 UTC

Corrected Window D retains historical VALID PASS; previous drift remains UNEXPLAINED; no reload/sender attribution claimed. New recovery count1/success1; four preselection3/1 selectors kept separately. Authorized selection5 + BasicInfo5 x2 passed; permitted51 31companion stayed in recovery capture.

Four independent official calls: APcommon25 05, A25 04, DZcommon25 0A, B25 09. Each pre/post5, four OUT (official retry), matching parameter IN0, reviewed setter/Apply/selector0. SDK771 and3/3 rejected as synthetic timeout decoder artifacts. DKS not retested; prior25 02 four OUT/IN0 retained. RT25 06/A6 NOT EXECUTED: actual HFX route remains unresolved, frontend only reads25 00hardwaregate plushostcache. Cannot declare entirefamilyunsupported, effective values readable or override metadata known.

Owner confirmed original slot5 DKS, A depth and firmware lighting physically; WRTnotverified. Owner then explicitly authorized onlyW unified0.1mm, separately captured. Actual bank unexpectedly5→3→1 before numeric write; UI remained5. Agent numeric submission used stale precheck without immediate bank confirmation. Wraw1 requests therefore cannot establish slot5baseline; non-testbankimpact cannot be excluded (activevseditbankunknown). Postcheck1 triggered STOP, no automatic restore, recovery or power test. Four unexpected selector frames29/31/49/51 at15:53:49.456237,15:53:51.235820,15:57:04.152080,15:57:04.571796UTC. Sender NOT ATTRIBUTED. This does not invalidate earlier separately clean AP/DZ request/timeout windows.

Power persistence NOT EXECUTED/NOT VERIFIED. Further hardware activity gated on PROFILE_DRIFT_SOURCE_ATTRIBUTION. Lastobservedactual1/UI5; liveauthorityUnknownafterobserverexit. Detailed report, exact64byte requests, percaseTIMELINE/CSV/payloadsequence, source audit, hashes, screenshots andtests at `audit_artifacts/controlled-getter-matrix/REPORT.md`. No production changes.


## Fresh baseline attempt — 2026-10-02 16:45–16:49 UTC

Owner accepted prior189.999s clean slot5 window as operationalgate, with historical source stillUNKNOWN. Official newconnection entered UI1/BasicInfo1; authorized Profile5 selection and two5 samples passed. Before first AP baseline write, immediatePRE found UI5/BasicInfo1. No numericblur/write followed. The baseline capture has only5 BasicInfoOUT and5 completedIN all1; selector/settings/Apply0. Bank changed across the gap after last5/before first1; no new5→3→1 timeline or sender was captured. STOP_AUTHORITY_MISMATCH; no auto recovery, magneticgetter, physicalcheck or powertest. Freshbaseline and hardwarepowerpersistence remain NOTESTABLISHED/NOTVERIFIED. Historicalcleanwindow remainsPASS; source isolation notclaimed.

Exactcase/hash/timeline, cache-onlysnapshot and focused-onBlurfield caution: [Slot5 Fresh Baseline](SLOT5_FRESH_BASELINE.md), `audit_artifacts/slot5-fresh-persistence/REPORT.md`, resumable `audit_session_state.json`. Further active work requires source-attribution review and continuous capturecoverage during preparation.


## Continuous navigation audit — 2026-10-03 CST

详见 [Continuous Profile Drift Audit](CONTINUOUS_PROFILE_DRIFT_AUDIT.md)。聚焦字段经owner授权强制退出全部Edge，无目标setter。单capture留存startup selector frame9/11(slot3/1)，早于Connect；sender UNKNOWN。T1后284次explicit BasicInfo均5，导航及149.120s idle无新selector。官方选择含51 31×4，未宣称全程没有setter。停止观察器，不继续baseline/getter/persistence。


# Fresh baseline / power persistence — completed 2026-10-03

## 当前结论 — 2026-10-03 北京时间

**Actuation / RT / DKS / firmware lighting：HARDWARE BANK POWER PERSISTENCE PASS，限本次槽位5、目标状态与一次拔插。**

Owner 对拔插前和拔插后的四项检查均回复“确认”。连续 USB 证据显示重新枚举后没有下表中的配置重放；只通过官方 UI 重选槽位5，含允许的 polling companion。结合物理确认，支持 **Profile bank contents survive keyboard power removal/reconnect**，并从板载槽位保留状态恢复行为。不是 Aura 硬件 bank writer 的生产化验证，也不是参数数值的 firmware readback。

Deadzone **NOT PHYSICALLY VERIFIED**。没有要求 owner 用主观体验强行判断死区。Flash/RAM staging、延迟提交、50 55 与 NVM commit 的关系、wear leveling / NVM wear 均 **NOT VERIFIED**。

## 历史 gate 与 scope

前一轮189.999秒 bounded clean window 与后续导航稳定窗口仍为 PASS；历史5→3→1来源仍 **UNKNOWN**。Edge reopen/session restore 只有时间相关性，未证明发送者。Corrected Window D 的完整 reconnect/init 有效试验没有复现 drift，不能写 reload correlated 或 Gear Link sender confirmed。

本轮保留同一个 Gear Link tab/session；没有 close/reopen、restore、reload、duplicate tab、getter replay、Sync、reset 或其它槽位配置修改。测试槽位5正常 UI 选择被授权；`51 31` polling companion 被授权。Owner 先前明确允许“解除同步用于实验”，因此基线阶段执行一次官方解除同步；这是 lighting ownership 的已授权跨配置例外，不是其它 bank 内容编辑。拔插后未再解除同步或编辑设置。

当前 capture 起点 BasicInfo1/UI5；T1 官方选择5建立新 authority。起点1没有被隐藏。旧 W0.1 写入受影响 bank仍 UNKNOWN，本轮没有猜测恢复它。

## 单一连续 acquisition

- 路径：`audit_artifacts/slot5-power-persistence-20261003/usb/slot5_power_persistence_continuous.pcap`
- SHA256：`30f82edeb6ed45807d908e9156016bc11091756f2019add058f7255d61d0b874`
- 时间：2026-10-02T17:44:42.967740Z → 2026-10-02T18:02:20.135679Z（UTC；北京时间 +8小时）
- 连续时长：1057.167939秒；保留记录2261。
- 总线4；VID0B05/PID1B7E；firmware1.00.59；USB address11→12。
- 重新枚举：frame1865，2026-10-02T17:55:04.451926Z。
- 这是原始 **privacy-filtered acquisition**，不是完整控制器 dump。账号/token/cookie/serial、普通键输入和未审查 body 不落盘。不存在命令的结论仅适用于审查并保留的 family；不能声称所有未知协议都不存在。
- Frame编号是 retained capture编号。完整 payload、USB endpoint、IN/OUT、时间线和 privacy manifest 保存在 usb/、parsed/。

## 新基线

| 子系统 | 确认提交的测试意图 |
|---|---|
| Common Actuation |2.0mm|
| A per-key Actuation |3.0mm；wire31|
| Common Deadzone |top.3 / bottom.2；保留本测试槽 B override top.4 / bottom.1|
| W Rapid Trigger |enabled；press.5 / release1.5；continuous0；wire18|
| V DKS |重新提交已有简单 J action；V logical1026/wire49，J logical773/wire37；start/end1.0，保留四槽位置|
| Firmware lighting |呼吸 / Breathing，effect1；非 Direct RGB streaming|

V DKS 是同值官方应用，未构造新 DKS shape。RT independent editor是操作模式，不是新增 firmware separate bit；实际51 54 selector1 raw5、selector2 raw15、continuous0、enable1。Common AP/DZ 官方回放已有逐键 override 属正常 companion，完整保留。

每项 PRE紧邻一次 official UI mutation，没有在 PRE 后导航；POST5。数字框由官方 blur 提交，同值也会写。除 Common AP 首项外，POST在官方正常settle后取得。Common AP最早 POST 为0.306秒后，早于完整重试结束，另存 settled POST 17:48:09.264Z；期间持续采样/连续USB均5。不能把最早POST冒充完整transaction settle证据。

| Mutation | PRE UTC | POST UTC | active slots |
|---|---|---|---|
| common-actuation | 2026-10-02T17:47:19.387Z | 2026-10-02T17:47:19.693Z | 5 / 5 |
| common-deadzone-top | 2026-10-02T17:48:09.330Z | 2026-10-02T17:48:11.686Z | 5 / 5 |
| common-deadzone-bottom | 2026-10-02T17:48:20.883Z | 2026-10-02T17:48:23.246Z | 5 / 5 |
| key-a-actuation | 2026-10-02T17:48:39.840Z | 2026-10-02T17:48:42.198Z | 5 / 5 |
| w-rt-independent-editor | 2026-10-02T17:49:06.393Z | 2026-10-02T17:49:09.035Z | 5 / 5 |
| w-rt-press | 2026-10-02T17:49:18.845Z | 2026-10-02T17:49:21.211Z | 5 / 5 |
| w-rt-release | 2026-10-02T17:49:21.330Z | 2026-10-02T17:49:23.679Z | 5 / 5 |
| v-existing-dks-reapply | 2026-10-02T17:50:21.907Z | 2026-10-02T17:50:24.510Z | 5 / 5 |
| authorized-release-aura-sync | 2026-10-02T17:50:44.836Z | 2026-10-02T17:50:47.451Z | 5 / 5 |
| firmware-breathing-effect | 2026-10-02T17:51:23.547Z | 2026-10-02T17:51:26.135Z | 5 / 5 |

所有10项PRE/POST均5；最后两次baseline确认5：17:51:26.228Z、17:51:28.251Z。逐项 before/after截图、日志和JSON均保留。`fresh_slot5_baseline.json` 是 host intent + submission evidence，不冒充 parameter readback。

## Power / reconnect 时间线

| 时间 UTC / frame | 事件 |
|---|---|
|17:45:36.300833Z /5|唯一pre-power授权selector5：`51 00 00 00 05 00` +58zeros|
|17:54:17.893Z|before-unplug explicit BasicInfo5，effect1|
|Owner回复“好了”|确认按要求拔掉键盘、等3秒、插回；没有独立电源仪测量|
|17:55:04.451926Z /1865|目标设备新USB address12 descriptor|
|17:55:04.862303Z /1868|首次完成的新设备BasicInfo：slot5、effect1；早于本任务post-power reselect|
|17:55:39.171Z→17:55:39.463Z|同一tab官方Connect点击；未reload|
|17:56:22.093Z|Owner HID chooser完成回复“好了”|
|17:56:22.154Z|首次agent有效explicit reconnect BasicInfo5|
|18:00:00.665256Z /1948|仅official Profile5 reselect；51 31×4 companion|
|18:00:00.765Z /18:00:02.789Z|两次explicit BasicInfo5，约2秒间隔|
|18:02:19.587Z|Owner四项post-power结果回复“确认”|
|18:02:19.622Z|final explicit BasicInfo5，effect1|

T1之后全部1015次captured completed BasicInfo均5；post-reconnect 168次均5。整个capture仅两笔selector，都是授权5；unexpected selector0。没有再次出现3→1，不能据此宣称历史source isolated。

## Host replay guard

区分重新枚举frame1865前后，OUT submission计数，排除OUT completion重复。下表覆盖全部要求family及辅助禁用/ownership/bulk族。

| Opcode | Pre-reconnect | Post-reconnect |
|---|---:|---:|
| `51 50` | 4 | 0 |
| `51 4f` | 5 | 0 |
| `51 58` | 8 | 0 |
| `51 59` | 12 | 0 |
| `51 52` | 0 | 0 |
| `51 54` | 15 | 0 |
| `51 23` | 7 | 0 |
| `51 2c` | 2 | 0 |
| `51 2d` | 0 | 0 |
| `50 40` | 0 | 0 |
| `50 55` | 13 | 0 |
| `51 21` | 0 | 0 |
| `51 55` | 0 | 0 |
| `51 56` | 0 | 0 |
| `74 00` | 0 | 0 |
| `51 53` | 0 | 0 |

post-reconnect其它OUT：`12 00`×168、`12 12`×9、`12 03`×5、`27 00`×4、`25 00`×4、`25 01`×4、`51 00`×1、`51 31`×4。初始化read/status以及授权选择/polling不等于目标磁轴/灯光配置重放。无target setters、Apply、reset、Direct RGB；本轮未执行25 02/04/05/06/A6/09/0A getter研究。

## 每子系统 acceptance

| 子系统 | Pre physical | Matching post-power replay | Post physical | Persistence |
|---|---|---:|---|---|
| A Actuation测试深度 |PASS|0|PASS|USER PHYSICAL PASS|
| W RT |PASS|0|PASS|USER PHYSICAL PASS|
| V DKS |PASS|0|PASS|USER PHYSICAL PASS|
| Breathing firmware lighting |PASS|0|PASS|USER PHYSICAL PASS|
| Deadzone |未可靠物理判断|0|未可靠物理判断|NOT PHYSICALLY VERIFIED|

物理PASS证明测试行为保留，不是用仪器精确测量毫米数值，也不证明所有键/所有effects/所有banks持久化。只有本次槽位5、这些目标状态、一次拔插的闭环。

## 保留的偏差与限制

1. **2秒explicit monitor没有贯穿离线重连。** 原监测器最后有效记录17:54:54.497Z，随后query在拔插时挂起（in_flight=true），未回传异常；到18:00:56.685Z重建仅只读monitor，最终42次/0errors/0drift，旧268次完整保存。没有宣称离线期间或该缺口中持续2秒explicit采样。USBPcap全程未中断，重连IN及目标write缺失审计仍有完整连续捕获。
2. 设备离线时CDP getter异常返回的空对象保留在`reconnect-first-read.json`；它不是slot、有效response或默认状态。首次有效explicit read是17:56:22.154Z；更早boot状态来自真实captured IN。
3. post-power UI显示Aura Sync占用warning，但BasicInfo effect1、owner确认呼吸仍在。本任务没有点击解除同步，不把warning当firmware ownership readback。host metadata `currentLightingEffect`导出时仍0，而可见Breathing和设备effect1：host cache不是device truth；ownership机制仍需另立gate。
4. 通用whole-capture scope classifier包含T1之前BasicInfo1，因而保留`contaminated`原始分类。此任务授权启动不一致→official select5；新增phase analysis从frame5开始核对，证明当次全部mutation/selector/后续BasicInfo属于5。没有修改通用guard，也没有删掉初始1。
5. 没有进程级sender证据；USBPcap cannot identify sender。相同官方packet重试保留，不等于新的用户意图或独立repeatability实验。没有修改Aura settle/quarantine。

## 软件验证与停止

本轮运行现有离线Gear Link regression：**53 tests PASS**；日志`logs/offline-regression.log`。新增artifact离线phase parser在本次完整pcap上校验：两次枚举、无unexpected selector、无post-power target replay；输出`parsed/power-phase-analysis.json`和完整BasicInfo列表。git diff检查另存日志。

本轮只更新这三份research文档及ignored audit artifacts。没有修改production、Profile Engine/M605/NativeHid、51 00 Aura writer或schema。现有worktree其它改动保留；patch相对本轮修改前文档snapshot，不包含其它未提交工作。

本轮capture/worker与两个本轮browser monitor已停止。页面和已授权slot5测试设置保留，未自动恢复未知旧W0.1或其它bank。STOP等待owner review；不继续getter/68键枚举/production实现/commit/push/package/release。

## 证据索引

- `state/mutation-authority.json`；`state/fresh_slot5_baseline.json`；`state/slot5-host-test-state.json`
- `state/pre-power-physical.json`；`state/post-power-physical.json`；`state/post-power-profile5-selection.json`；`state/final-basicinfo.json`
- `timelines/ui-actions.jsonl`；`timelines/complete-explicit-monitor.json`
- `parsed/power-phase-analysis.json`；`parsed/basicinfo-complete.json`；`parsed/slot5_power_persistence_continuous/`完整解析
- `screenshots/fresh_slot5_baseline.png`；`screenshots/post-power-final.png`；逐mutation截图
- `usb/slot5_power_persistence_continuous.pcap`及`.privacy.json`
- `audit_session_state.json`；`slot_ledger.json`；`scoped.patch`；`SHA256SUMS.txt`
