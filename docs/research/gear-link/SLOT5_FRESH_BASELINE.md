# Slot5 Fresh Baseline / Hardware Bank Power Persistence Audit

## Latest checkpoint — 2026-10-03 CST

**本次slot5 power persistence：Actuation / RT / DKS / firmware lighting USER PHYSICAL PASS；Deadzone NOT PHYSICALLY VERIFIED。**

连续1057.167939秒capture贯穿基线和拔插，无post-reconnect target setter/Apply；仅官方reselect5及polling companions。T1后1015/1015completed BasicInfo5。历史drift source仍UNKNOWN。2秒explicit monitor在拔插挂起，有记录缺口，USBcapture不中断；不得宣称完整monitor cadence PASS。详情见本文件最新章和`audit_artifacts/slot5-power-persistence-20261003/REPORT.md`。以下旧BLOCKED/NOT VERIFIED段落保留为历史，不能当最新结果。


## Outcome

**STOP_AUTHORITY_MISMATCH. Fresh hardware baseline NOT ESTABLISHED. Power persistence NOT EXECUTED / NOT VERIFIED.**

The owner accepted the previous 189.999-second clean window (20/20 explicit BasicInfo5, no selector/settings/Apply) as a bounded operational gate. That historical window remains PASS. It never proved historical source isolation; source remains UNKNOWN. This attempt establishes that the bounded gate is insufficient to assume later authority without immediate PRE checks.

No Aura production files, protocol, planner, timing, quarantine, user Profile documents, or ASUS services were changed. No magnetic getter, power test, reset, Sync Settings, lighting edit or unknown sender was used. The official Profile5 selection and its existing polling/init companions were explicitly allowed by the current task.

## Timeline (UTC; local Beijing time adds 8 hours)

| Stage | Observation | Interpretation |
|---|---|---|
| Official connection | Original tab was absent. Opened official zh-cn page, clicked Connect, owner completed HID chooser. Actual HFX page showed Profile1 and BasicInfo1. | Startup state; not a captured drift event. |
| Official Profile5 selection | Two explicit BasicInfo5, 16:47:14.527Z and 16:47:16.551Z, approximately2seconds apart; UI5. | Authority established at those samples. |
| Editor preparation | Opened AP/DZ page and inspected official input blur semantics; host cache commonAP20/A30, DZ3/2/B4/1 retained. | Cache is host intent, not firmware parameter readback. |
| Baseline capture start | First captured completed BasicInfo at16:49:10.967266Z already1 (frame4). | Change occurred after last captured5 at16:47:18.091606Z and before this capture's first sample. |
| Immediate mutation PRE | At16:49:17.449Z explicit BasicInfo1 with UI5 (captured frame6). | Required PRE5 failed; no mutation authorized. |
| Stop | No blur/Tab dispatched; capture ended. All5 completed BasicInfo IN were1; only12 00 OUT. | Immediate authority guard prevented a new misattributed setting write. |

**No new 5→3→1 selector sequence was captured.** Do not attribute it to Gear Link reload, AuraOwnershipExperiment, ASUS services or a specific process. The actual slot changed from last5 to first1 across an unobserved capture gap. The exact transition, intermediate banks and sender remain UNKNOWN. No automatic reselect/restore was attempted.

The live passive guard also independently recorded BasicInfo1 and `stop_required=true`; its result is in `state/slot5_actuation_baseline-live.json`. The synchronous PRE independently failed before the requested mutation. The source attribution task must resume before further active testing.

## Exact authority and mutation accounting

Common AP target2.0 was already displayed. Its textbox was filled with the same2.0 to prepare a controlled onBlur submission. Official source `preload...js` lines2661–2705 calls `onChangeBlur` from blur; main source lines20678–20687 routes it to AP setter. Input preparation was not the intended mutation and generated no setter. PRE failed, so the planned Tab/blur was never executed. **The focused field must not be blurred/navigated as an automatic cleanup action**, since even the same value can invoke that callback.

| Target | Intended state | Fresh authoritative submission | Physical / persistence |
|---|---|---|---|
| Common AP |2.0mm|NOT EXECUTED: PRE1|NOT VERIFIED|
| A AP |3.0mm|NOT EXECUTED|NOT VERIFIED|
| DZ |safe distinct values, existing host.3/.2|NOT EXECUTED|NOT VERIFIED|
| W RT |enabled,.5/1.5|NOT EXECUTED|NOT VERIFIED|
| V DKS |previously validated simple action|NOT EXECUTED|NOT VERIFIED|
| Firmware lighting |distinct test effect|NOT EXECUTED|NOT VERIFIED|

There is no fresh final-baseline screenshot/JSON asserting PASS. `baseline/host-cache-before.json` is explicitly cache-only. `screenshots/stopped-ui5-authority1.jpg` shows staleUI5, while captured BasicInfo proves1. Prior contaminated W0.1 affected bank remains UNKNOWN; no guessed restoration was made.

## Capture matrix and SHA256

All files are **original privacy-filtered acquisitions**, not unfiltered bus dumps. Serial/token/cookie/typing data were excluded at acquisition. Absence claims apply to reviewed retained command families only. Full payload sequences, endpoint/timestamps and privacy manifests are kept beside each parsed case.

- `usb/official_connection.pcap`: SHA256 `bfd80ddcf2140d35a3838b69404667ce1815e6f963bf1a23649b21865c6193af`; OUT `{'12 00': 12, '12 03': 4, '27 00': 4, '12 12': 4}`; completed BasicInfo slots `[1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]`.
- `usb/authorized_slot5_selection.pcap`: SHA256 `a50023a768b67a09d6910718c7f556214be9bc96d02f15d9298f967a5bf11ad8`; OUT `{'12 00': 10, '51 00': 1, '25 00': 4, '25 01': 4, '51 31': 4}`; completed BasicInfo slots `[1, 5, 5, 5, 5, 5, 5, 5, 5, 5]`.
- `usb/slot5_actuation_baseline.pcap`: SHA256 `3493264001eaabcc2c6bab0139961d7affdffe6c7c5c77a1c750f64e706a1274`; OUT `{'12 00': 5}`; completed BasicInfo slots `[1, 1, 1, 1, 1]`.

Official selection includes exactly one `51 00` selecting5, with `25 00`, `25 01` and `51 31` polling companion. The task explicitly permits those known selection/init companions. The attempted baseline has `51 00=0`, `50 55=0`, magnetic/lighting/DKS setters0. This is not evidence that no bank write happened in the gap.

## Persistence / replay acceptance

Pre-power physical check was not requested. Unplug/replug and post-power physical check were not requested. No post-reconnect replay capture exists, so the following cannot be established: no host replay, restoration from onboardbank, AP/RT/DKS/lighting power survival, startupdefaultbank, NVMcommit timing/wear.

Parameter-getter historical results stay unchanged: DKS25 02 and AP25 05/04 and DZ25 0A/09 each4OUT/0matchingIN; SDK synthetic fallback is not readback. RT25 06/A6 remains NOT EXECUTED. None were retried here.

## Safety / next gate

This attempt complied with the new immediate PRE policy and stopped before writing. Research capture/helpers are stopped through ownership-checked control; no ASUS/Windows service is stopped. The retained browser is marked for handoff, with focused numeric field caveat in resumable state.

Future attribution observation needs continuous passive coverage between selection and baseline preparation to capture any transition; per-case evidence boundaries alone left a gap. This is an evidence-tooling recommendation, not a production change or a new authority bypass. Before future mutation: source-attribution review, official selection5, fresh immediate PRE/action/POST and continuous unexpected-selector monitoring.

NVMwear, RAM staging, delayedcommit and whether everyApply writesflash remain NOT VERIFIED.

## Offline verification / cleanup

53 existing research tests PASS; `git diff --check` PASS. Artifact evidence assertions check sole authorized selector5, all5 failed-precheck IN1 samples, no baseline setter, no executed mutation, and owned capture worker `exited`/pipe null. These are software/evidence checks, not hardware acceptance. Fresh logs: `logs/research-tests.log`, `logs/diff-check.log`; structured validation: `state/validation.json`.


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
