# PROFILE DRIFT SOURCE ATTRIBUTION

## Previous checkpoint — before owner completed the HID chooser

2026-10-02（北京时间）。**EXTERNAL WRITER remains; PROCESS ATTRIBUTION NOT VERIFIED; STOP**。

## 结论与当前gate

非预期slot3/1不只发生于reload：官方选5后不reload也出现；Gear Link页面关闭时也出现同样两笔OUT。不能将其定性为Gear Link reload初始化行为。此次未停止任何ASUS服务或candidate进程。getter replay=0，power persistence未执行；Aura production未修改。

C阶段尝试正常重开官方页面，浏览器连接变化后原生Computer Use安全检查无法可靠确定当前Windows browser URL，明确要求停止本轮UI操作。已停止自有capture和ETW，D未执行；没有改用其它方式绕过检查。整体任务部分完成，不宣称source attribution成功。

## A/B/C/D与起点

| Case | Duration(s) | 51 00 | BasicInfo active | Result |
|---|---:|---|---|---|
| attribution_start | 35.159 | 5, 3, 1 | 5, 5, 5, 5, 3 | 5→3→1；未reload |
| window_a | 107.321 | 0 | 1, 1 | 无新增selector；已漂移至1，不满足slot5 stable |
| window_b | 79.192 | 0 | 无 | 不足90s预观察，不能作为B完成证据 |
| window_b_90 | 128.904 | 3, 1 | 无 | Gear Link页面关闭仍出现3→1 |
| window_c | 167.418 | 0 | 1, 1, 1, 1 | 部分重开；页面连接未确认；无selector |
| D | — | 未执行 | — | Computer Use安全停止，不能判reload correlation |

起点截图`initial-slot5.jpg`显示ROG HFX/UI5；BasicInfo四次active5，随后约8秒内3/1。A从这次漂移之后开始，虽然观察超过90秒且没有新的51 00，但实际稳定在1，**不能称STABLE_OPEN_WINDOW slot5 PASS**。

B先有不足90秒预观察，后补完整>=90秒capture，期间Gear Link标签已关闭且ASUS服务保持。此时没有额外BasicInfo主动查询器；页面心跳停止，B无BasicInfo。没有用未知HID/raw replay补采，故无法声称B持续读取了active bank。51 00是filter保留类别，B的两笔OUT可查。

关闭最后一个受控页面后浏览器连接3失效；在14:04:10.192Z可见新Edge连接的新建标签页，Gear Link尚未重开。这个时间与B的第一笔selector相近，是额外browser lifecycle/foreground混杂因素，**不是发送进程证明**。不记录extension instance ID、用户browser历史或PID。

C normal reopen到官方连接页，点击连接后设备connected/ProfileUI未可靠确认；捕获BasicInfo1但不能凭报文推断UI已完成。D的UI5+BasicInfo5前置和reload均未执行；`RELOAD_CORRELATED`保留NOT VERIFIED。

## Selector时间线

| Case / retained frame | UTC | Command / slot |
|---|---|---|
| attribution_start / 3 | 2026-10-02T13:56:51.916028Z | 51 00 / 5 |
| attribution_start / 37 | 2026-10-02T13:56:59.894682Z | 51 00 / 3 |
| attribution_start / 41 | 2026-10-02T13:57:02.087231Z | 51 00 / 1 |
| window_b_90 / 3 | 2026-10-02T14:04:10.165610Z | 51 00 / 3 |
| window_b_90 / 5 | 2026-10-02T14:04:10.613112Z | 51 00 / 1 |

完整64-byte hex、echo、方向、endpoint、Apply与时间间隔在`parsed/<case>/`的JSON/CSV/payload_sequence.txt，BasicInfo列表见selector-timeline.json。SHA256与绝对pcap路径见capture-index.json。这些是隐私过滤后的原始留存pcap，不是全bus文件；不根据被丢弃类别声称absence。

## Process-level observer

本机已安装WPR/logman；限定路径搜索未找到Procmon/Handle。使用已安装Visual Studio TraceEvent2.0.77，运行Microsoft-Windows-Kernel-File realtime metadata session；不保存raw ETL、其它文件路径或HID内容。只输出HFX VID/PID匹配设备事件，隐藏instance串与PID。

只读查询设备接口symbolic link，解析8个HFX内核device alias用于匹配；未打开HID配置接口/写入、未修改registry。首次collector没有alias；A→B期间按记录重启ETW后使用alias；存在collector切换间隙，不伪装连续trace。

ETW最终状态与event数见trace/trace-status.json；未捕获能对应51 00发送时刻的目标Write事件，不能升级PROCESS ATTRIBUTION CONFIRMED。数百万普通metadata事件只在内存处理、无内容导出；零target match仅说明此provider/name-map没有取得有效HID覆盖，**不证明没有writer**。事件header process不能直接替代driver issuing-thread归因，当前也没有匹配事件可判。

观察到GearLink_KBProcess/KBAgent/KBService等原生程序；KBProcess加载GearLink_KBApi、GearLink_SwAgentDll、GearLink_ScenarioProfile模块。只能确认原生组件/功能存在，不证明这些组件发了slot3/1。LightingService、ArmouryCrate.Service等同样未被归责或停止。

## 静态cross-reference

官方main1.00.28美化代码约6038行区分host setCurrentProfile与约6045行device.setProfile；约12419/12437行接收device/native Profile index事件更新host，约12400行changeProfileType1通知native服务。host/native存在双向状态路径，但尚无具体本次调用栈或写时刻证据，不将minified符号或module name当协议契约。

## Future audit rule

1. 建立5后的任何非预期selector都使setting/getter/persistence gate失败；本轮允许继续的只是owner明确授权的passive归因窗口。
2. reload/reopen/关闭最后标签页/浏览器重建均视为可能改变authority的生命周期事件；之后重新观察，不能把reload自动归类read-only，也不能说已证明reload必切槽。
3. A仅稳定在1、B后台仍有selector，所以当前不能进入getters。
4. 下一gate需要有HFX设备路径和写时刻覆盖的进程trace，例如正确配置的Procmon或适配实际HID/IOCTL的ETW；随后才可审查安全隔离。不得逐个kill服务碰运气。

## 工具范围、验证与交付

新增tools/research/gear_link_drift_trace.cs、gear_link_drift_worker.ps1，仅realtime observer。C# compile与路径redaction自检已执行；真实collector启动/停止成功。没有完整software CI/physical PASS。本轮不把观察工具当production功能。

scoped.patch仅含本次新增观察工具、本文与EXTERNAL_WRITER_RECOVERY的追加说明；基线是前一个recovery未提交checkpoint，未包含其它累计产品修改。现有report/patch/ZIP保持原样。没有commit/push/package/release。

## Previous landing-only D — INVALID for complete init — 2026-10-02 14:16–14:20 UTC

Owner completed the browser HID chooser and explicitly reported entering the device page without editing settings. Computer Use re-read the official page: ROG 魔导士 ACE HFX, UI Profile1, extension version1.00.28 (not keyboard firmware). No assumed current slot from the earlier checkpoint.

Only official Profile5 selection and ONE ordinary reload were performed. No AP/DZ/RT/DKS/lighting edit, Sync, reset, getter replay or power test. No candidate process/service was stopped. A/B/C captures above remain immutable historical evidence.

| Case | Capture duration(s) | UI / BasicInfo | 51 00 | Result |
|---|---:|---|---|---|
| resumed_slot5_authority | 50.689 | UI1→5; five BasicInfo samples5 | one authorized slot5 | No unexpected selector; first/last BasicInfo5 span10.123s |
| window_d_reload | 117.467 | reload→connection landing page; four BasicInfo samples5 | 0 | 109.277s after reload; 5→3→1 NOT REPRODUCED |

Reload action UTC14:18:00.438Z; the most recent pre-reload BasicInfo5 was14:17:16.136592Z, 44.301s old. At the last sampled post-reload BasicInfo14:19:02.030512Z hardware reported5. This is not a current continuously monitored slot claim after observers stopped. Device descriptors identify controller4/address11/VID0B05/PID1B7E/bcdDevice0x0159.

The ordinary reload returned the connection landing page. No click on Connection and no additional HID chooser interaction occurred during D. Therefore D tests this reload and idle window, **not a full reconnect/permission-completed device-page initialization**. Reopening/connecting can have different behavior; NOT VERIFIED. Earlier C UI ambiguity is superseded by the resumed preflight screenshot, but its historical initialization capture is not retroactively complete.

Retained frame numbers / UTC: authority selector frame3 at14:17:05.897207Z, identical echo frame4 after98.609ms. Authority BasicInfo frames10/18/27/35/38 all5. D BasicInfo frames4/8/10/12 at14:18:01.889511Z /14:18:02.015514Z /14:18:02.077513Z /14:19:02.030512Z all5. Full64-byte payload, endpoint, direction, echoes and timing are in each parsed timeline/payload_sequence/CSV. D OUT counts12 00×4,12 12×1; no51 00. No extra replay was used to obtain these BasicInfo observations.

This bounded window is clean, but **the earlier closed-page3→1 and no-reload5→3→1 remain unexplained**. RELOAD_CORRELATED not established; PROCESS ATTRIBUTION NOT VERIFIED. ETW saw1,306,042 metadata events, zero target matches, zero decode failures; the provider has no proven HID Write/IOCTL coverage. Zero matches is inconclusive. No native Gear Link/ASUS process is accused based on module names or temporal association.

Safety gate remains BLOCKED_SOURCE_ATTRIBUTION. Getters=0; persistence NOT VERIFIED. No full-key enumeration or production edits. Need adequate process-level device-path/write-time evidence before isolation and later experiments. Both owned capture worker and realtime ETW verified stopped. Official page is left at its connection landing page.

New artifacts: `audit_artifacts/profile-drift-attribution/resume-device-page/REPORT.md`, capture-index/combined-capture-index.json, per-case parsed evidence, screenshots, trace-status, fresh34-test offline parser/gate log, scoped.patch and evidence.zip. Earlier report/patch/zip and raw captures are preserved. Patch baseline is the pre-resume document checkpoint, only this report and EXTERNAL_WRITER_RECOVERY append changed; product source untouched.

## Corrected Window D — full reconnect/device initialization (2026-10-02)

**VALID full-init trial; previous drift remains unexplained.** Old `window_d_reload` returned the connection landing page without Connect/HID permission/device-page entry. It is **INVALID for complete reconnect/init verification**, regardless of its idle duration or BasicInfo samples. Its original pcap/hash/report are preserved, with a separate correction record; it cannot be used to infer complete device initialization behavior.

This trial: normal connection and owner HID authorization → official slot5 selection → UI5 + BasicInfo5 → stable precheck → one ordinary reload → official Connect → owner completes HID chooser → actual ROG HFX device page verified → no UI input for 145.053s. Device page entry was required before the90s clock began. Start/end screenshots both show Profile5.

| Event | UTC | Beijing (UTC+08) |
|---|---|---|
| Reload | 2026-10-02T14:32:29.998Z | 2026-10-02T22:32:29.998+08:00 |
| Connect successful dispatch | 2026-10-02T14:32:55.534Z | 2026-10-02T22:32:55.534+08:00 |
| HID completion — owner acknowledgment | 2026-10-02T14:33:21.711Z | 2026-10-02T22:33:21.711+08:00 |
| HFX device-page entry confirmed / idle begins | 2026-10-02T14:33:29.882Z | 2026-10-02T22:33:29.882+08:00 |
| Final UI read after idle | 2026-10-02T14:36:05.885Z | 2026-10-02T22:36:05.885+08:00 |
| Capture stopped | 2026-10-02T14:35:54.934860+00:00 | 2026-10-02T22:35:54.934+08:00 |

Connect at14:32:39.939Z hit a detached node while the landing DOM finished loading; no successful input was established. Re-read the current accessibility state, targeted the visible Connect at14:32:55.534Z, then immediately handed off the HID chooser. No cancellation, bypass, repeated reload, or landing-only idle classification. Owner replied“好了”. The permission timestamp is **owner acknowledgment**, not an instrumented native click time; exact OS dialog completion is NOT VERIFIED. Device page entry timestamp is the subsequent fresh AX confirmation, not guessed from BasicInfo.

Precheck BasicInfo5 samples14:31:43.426380Z and14:32:02.022995Z span18.597s. Before reload the last sample was27.975s old, UI still5. Precheck OUT:51 00×1 (authorized5),25 00×1,25 01×1,12 00×2,51 31×1; companion traffic originated through official selection, not an audit raw sender. No setting control edited.

Full D OUT:12 00×9,12 12×2,12 03×1,27 00×4; **51 00×0**,50 55×0. Nine completed BasicInfo samples all active5; frames4/8/10/12/22/24/26/28/30. Post-device-page samples14:33:31.914621Z,14:34:31.915749Z,14:35:31.909869Z all5. At final UI read14:36:05.885Z, device page stillProfile5. Polls are discrete; no continuous/current-slot claim after collector stop. `27 00` is recorded as observed companion traffic, not given a new production contract.

**Classification:** full browser HID permission + actual device init completed, no5→3→1 in this trial. Therefore **previous drift remains unexplained**. Do not mark GEAR_LINK DEVICE INITIALIZATION CORRELATED or sender confirmed. One clean trial cannot erase the earlier unexpected selectors in no-reload/closed-page windows. No process tracing escalation or service stop was attempted.

User interaction rule: upon official Connect, if native HID chooser appears, immediately pause and ask only“Gear Link 的 HID 设备选择弹窗已经出现。请选 ROG Falchion Ace HFX 并点击连接，完成后回复‘好了’。” Resume only after owner reply AND verifying the real device page. Never count landing-page idle as full initialization.

No AP/DZ/RT/DKS/lighting edits, Sync/reset/delete, getter replay, unplug/persistence, new process attribution, production changes, commit/push/package/release. Getter count0; persistence NOT VERIFIED. Owned passive worker verified exited. The task stops here for review.

Fresh offline Gear Link parser/gate tests:34 PASS. Capture validity assertions PASS. Scope patch applies to pre-task research document snapshots; no product source changes. New artifacts: `audit_artifacts/profile-drift-attribution/full-init-d/REPORT.md`, capture-index.json, prior-window-correction.json, UI timeline, per-case full retained payload/CSV/JSON, screenshots, fresh logs, scoped.patch and evidence.zip.


## Controlled AP/DZ matrix and W baseline drift — 2026-10-02 15:40–15:57 UTC

Corrected Window D retains historical VALID PASS; previous drift remains UNEXPLAINED; no reload/sender attribution claimed. New recovery count1/success1; four preselection3/1 selectors kept separately. Authorized selection5 + BasicInfo5 x2 passed; permitted51 31companion stayed in recovery capture.

Four independent official calls: APcommon25 05, A25 04, DZcommon25 0A, B25 09. Each pre/post5, four OUT (official retry), matching parameter IN0, reviewed setter/Apply/selector0. SDK771 and3/3 rejected as synthetic timeout decoder artifacts. DKS not retested; prior25 02 four OUT/IN0 retained. RT25 06/A6 NOT EXECUTED: actual HFX route remains unresolved, frontend only reads25 00hardwaregate plushostcache. Cannot declare entirefamilyunsupported, effective values readable or override metadata known.

Owner confirmed original slot5 DKS, A depth and firmware lighting physically; WRTnotverified. Owner then explicitly authorized onlyW unified0.1mm, separately captured. Actual bank unexpectedly5→3→1 before numeric write; UI remained5. Agent numeric submission used stale precheck without immediate bank confirmation. Wraw1 requests therefore cannot establish slot5baseline; non-testbankimpact cannot be excluded (activevseditbankunknown). Postcheck1 triggered STOP, no automatic restore, recovery or power test. Four unexpected selector frames29/31/49/51 at15:53:49.456237,15:53:51.235820,15:57:04.152080,15:57:04.571796UTC. Sender NOT ATTRIBUTED. This does not invalidate earlier separately clean AP/DZ request/timeout windows.

Power persistence NOT EXECUTED/NOT VERIFIED. Further hardware activity gated on PROFILE_DRIFT_SOURCE_ATTRIBUTION. Lastobservedactual1/UI5; liveauthorityUnknownafterobserverexit. Detailed report, exact64byte requests, percaseTIMELINE/CSV/payloadsequence, source audit, hashes, screenshots andtests at `audit_artifacts/controlled-getter-matrix/REPORT.md`. No production changes.

| frame | UTC | Selection |
|---|---|---|
| 29 | 2026-10-02T15:53:49.456237Z | 51 00 slot 3 |
| 31 | 2026-10-02T15:53:51.235820Z | 51 00 slot 1 |
| 49 | 2026-10-02T15:57:04.152080Z | 51 00 slot 3 |
| 51 | 2026-10-02T15:57:04.571796Z | 51 00 slot 1 |


## V2 owned-writer inventory and bounded clean window — 2026-10-03

见 [Profile Drift Source Attribution V2](PROFILE_DRIFT_SOURCE_ATTRIBUTION_V2.md)。本轮未发现或停止项目writer；官方select5后20次BasicInfo在189.999秒连续窗口均5、10秒cadence，整份idle capture只有12 00，无51 00/setter/Apply。不能据此归因AuraOwnershipExperiment或声称source已隔离。只读handle snapshot发现LightingService/asus_framework的MI_01 candidate，handle不是sender；枚举partial。未启动Procmon/ETW、未停ASUS服务、未继续getters或power test。上轮W0.1污染未恢复、full hardware state仍UNKNOWN。phase=STOP_SOURCE_UNKNOWN；此clean窗口仅满足有界稳定观察门槛，等待owner review。
