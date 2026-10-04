# Continuous Profile Drift Navigation Audit

## 结论

本次 **slot5 建立后导航漂移 NOT REPRODUCED**：T1 选择后 284 次显式 BasicInfo 与 867 个捕获到的完成响应均为5；T2–T7导航及最后 149.120 秒静置没有新的51 00。历史5→3→1的发送进程仍 **UNKNOWN**，不能宣称来源已隔离。

连续捕获在 Edge 重开/会话恢复阶段确实留存了 **51 00 slot3、slot1**（frame9/11），发生在本轮官方 Connect 点击之前。此阶段开始实际硬件是1，因此本次不能描述为再次观察到5→3→1。仅有 **EDGE REOPEN / SESSION RESTORE TEMPORAL CORRELATION**，没有进程级写证据，不可命名发送方为Gear Link、Edge或ASUS服务。

**没有执行磁轴、灯效、DKS配置编辑、Apply、Sync、reset、getter replay或拔插。** 但官方Profile5选择自身生成了 **51 31×4 polling-rate companion**；因此不将整个捕获夸大为“只有选择命令/没有任何setter”。这部分是官方UI副作用，不是Aura写入或新增审计setter。后续实验需先接受/界定这个官方选择副作用。

## 聚焦字段安全恢复

原页面 Common AP 输入准备值2，focus为INPUT。原生单渲染器 Page.crash 请求被接口拒绝，未执行；经owner明确批准强制结束全部Edge后，核验剩余Edge为0。没有正常关页、Tab、blur、提交或页面导航。用户重开后旧页恢复为connection landing，旧输入JS会话已终止。

执行强制终止开始时间见 `state/edge-hard-termination.json`（17:17:52.8554873Z），完成见 `state/edge-hard-termination-post.json`，恢复结束17:18:26.1180682Z。完整capture复核未见目标配置setter。**FOCUSED_FIELD_HARD_EXIT_PASS**。

`Page.crash` 原始意图依据[官方CDP协议定义](https://github.com/ChromeDevTools/devtools-protocol/blob/master/json/browser_protocol.json)，实际方法为Windows强制终止已确认Microsoft Edge进程；失败的尝试与实际执行方法分别保存，未将拒绝当成功。

## 连续捕获与监测边界

- 文件：`G:/Aura/audit_artifacts/continuous-profile-drift/usb/continuous_slot5_navigation_drift.pcap`
- SHA256：`d4e9e7df23ef76ef6a7b0a88190b75f0597ecf57d16af262af07633245c68857`
- 单次capture worker/collector启动、单次停止，无分段重启。case生命周期：2026-10-02T17:15:06.451799+00:00 → 2026-10-02T17:32:54.555221+00:00，1068.103422s；包含启动确认/停止解析开销。精确有流量区间另列，不把case时长伪装成每毫秒均有留存报文。启动/停止见logs、capture-command、case记录。
- 首/末留存vendor报文：2026-10-02T17:15:10.960826Z → 2026-10-02T17:32:47.220199Z，跨度 1056.259373 秒。此跨度是有报文留存的区间，不把无流量空白当capture停止。
- 总留存records 1870；全部BasicInfo OUT 913，含官方页面自然轮询及explicit monitor。
- 本轮显式官方 `currentDevice.getDeviceInfo(0)` 2秒监测：2026-10-02T17:22:57.690Z → 2026-10-02T17:32:47.176Z，295次。间隔 min/median/max = 1.986/2.000/2.015s；错误0。
- **限制**：强制退出Edge至重新授权期间无法在终止的官方页面运行2秒getter。USB capture连续覆盖此段；2秒monitor在T1前已启动并覆盖全部导航/idle。没有伪称恢复全过程都有2秒BasicInfo。
- BasicInfo观察值在vendor payload offset10；64bytes、12 00 header校验；未把其它字段或host cache当bank truth。
- 采集时隐私过滤，不留账号/token/cookie/序列号/普通键盘输入；unknown vendor bodies移除。所有absence结论只涵盖已reviewed保留opcode，不能证明总线没有未知协议。

## 动作时间线（UTC）

| 动作 | 时间 | 行为/边界 |
|---|---|---|
| T0 official Connect (before) | 2026-10-02T17:21:05.371Z | 记录原始时间，不补造时序 |
| T0 official Connect (after) | 2026-10-02T17:21:05.682Z | 记录原始时间，不补造时序 |
| T0 owner authorization acknowledged / device page inspection (after) | 2026-10-02T17:22:27.301Z | 记录原始时间，不补造时序 |
| T1 open official Profile selector (before) | 2026-10-02T17:22:57.694Z | 记录原始时间，不补造时序 |
| T1 open official Profile selector (after) | 2026-10-02T17:22:57.974Z | 记录原始时间，不补造时序 |
| T1 select official Profile5 (before) | 2026-10-02T17:23:17.493Z | 记录原始时间，不补造时序 |
| T1 select official Profile5 (after) | 2026-10-02T17:23:17.787Z | 记录原始时间，不补造时序 |
| T2 open Magnetic AP-DZ main page (before) | 2026-10-02T17:24:02.029Z | 记录原始时间，不补造时序 |
| T2 open Magnetic AP-DZ main page (after) | 2026-10-02T17:24:02.304Z | 记录原始时间，不补造时序 |
| T3 open per-key AP-DZ editor (before) | 2026-10-02T17:24:21.557Z | 记录原始时间，不补造时序 |
| T3 open per-key AP-DZ editor (after) | 2026-10-02T17:24:21.907Z | 记录原始时间，不补造时序 |
| T4 select A in per-key editor (selection only) (before) | 2026-10-02T17:28:37.982Z | 记录原始时间，不补造时序 |
| T4 select A in per-key editor (selection only) (after) | 2026-10-02T17:28:38.247Z | 记录原始时间，不补造时序 |
| T5 inspect visible Deadzone section shared with AP; no separate tab; no input focus (observation) | 2026-10-02T17:29:00.398Z | 记录原始时间，不补造时序 |
| T6 navigate Rapid Trigger (before) | 2026-10-02T17:29:14.522Z | 记录原始时间，不补造时序 |
| T6 navigate Rapid Trigger (after) | 2026-10-02T17:29:14.800Z | 记录原始时间，不补造时序 |
| T7 navigate key assignment DKS entry (before) | 2026-10-02T17:29:41.179Z | 记录原始时间，不补造时序 |
| T7 navigate key assignment DKS entry (after) | 2026-10-02T17:29:41.457Z | 记录原始时间，不补造时序 |
| T7 DKS available in key assignment; mode-changing DKS button intentionally untouched (observation) | 2026-10-02T17:30:18.044Z | 记录原始时间，不补造时序 |
| Idle window completed; stop own BasicInfo interval (after) | 2026-10-02T17:32:47.164Z | 记录原始时间，不补造时序 |

T1两次authority为17:23:17.901Z、17:23:19.955Z，均UI5+BasicInfo5。主键盘与下方per-key键盘用当前DOM及可见位置区分，选择的是下方A。官方静态路径：key selection只更新选中列表/local editor；setter仅 numeric blur / slider change-end 等路径可达。本轮numeric control始终未focus，最后focus为BODY。

T5没有独立Deadzone导航tab：AP/DZ共用当前编辑区，因此只观察已可见顶部/底部死区；不虚构一次切页。T7进入“按键设置”，可见“动态按键(DKS)”入口；该按钮会改变模式，本轮没有点击，也未开启一个新的DKS配置。因此 **DKS模式编辑内部导航未覆盖**。

## 全部51 00及前后BasicInfo

| frame | UTC | slot | phase | 前/后BasicInfo |
|---|---|---:|---|---|
| 9 | 2026-10-02T17:19:37.744603Z | 3 | STARTUP_BEFORE_CONNECT | frame8 2026-10-02T17:17:10.961567Z slot1 / frame14 2026-10-02T17:19:41.964029Z slot1 |
| 11 | 2026-10-02T17:19:38.336574Z | 1 | STARTUP_BEFORE_CONNECT | frame8 2026-10-02T17:17:10.961567Z slot1 / frame14 2026-10-02T17:19:41.964029Z slot1 |
| 107 | 2026-10-02T17:23:17.786424Z | 5 | AUTHORIZED_T1 | frame106 2026-10-02T17:23:17.696369Z slot1 / frame110 2026-10-02T17:23:17.886623Z slot5 |

每个selector的完整64-byte payload：

frame9：
```text
51 00 00 00 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

frame11：
```text
51 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

frame107：
```text
51 00 00 00 05 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

frame9/11离T0 Connect（17:21:05.371Z）分别约87.6/87.0秒，不能归因于该点击。Edge tab重新打开元数据约17:19:37.903Z与selector窗口邻近；非进程写事件证据。两个selector有echo，但没有slot3期间的BasicInfo采样，因此只能证明slot3 selection submission，不能证明该短暂slot的固件effective状态。T1后first_drift=null，无mid-test selector。

## 目标配置与其它官方报文

| opcode | OUT count | 分类 |
|---|---:|---|
| 12 00 | 913 | BasicInfo read |
| 51 00 | 3 | startup3/1 + authorized5 |
| 12 12 | 2 | Keyboard nation read |
| 12 03 | 1 | official initialization read |
| 27 00 | 4 | official SW ownership getter, not 74 setter |
| 25 00 | 8 | official status queries, including RT page entry |
| 25 01 | 1 | official query |
| 51 31 | 4 | official selection polling-rate write companion |

51 31位于frame117/121/125/129，17:23:17.909409–17:23:19.428666Z，payload4=03，其余reserved为0。官方 `changeProfile → refreshProfileSettings → refreshPollingRate` 可达。27 00×4在Connect初始化时出现，官方shared `getSWModeStatus` builder为39/0/0/[]，setter另用116(74)；本轮74 00=0。

目标write列表51 50/4F/58/59/52/54/53/21/23/2C/2D、50 40/55均0。完整OUT/IN及timestamps在parsed/timeline.json、timeline.csv、payload_sequence.txt。未执行任何磁轴参数getter replay。

## 进程 / handle evidence

T1建立后约21秒启动只读process/handle snapshot；`state/t1-process-handles/hid_handle_owners.json`完成时间17:23:55.459597Z。Edge持有MI01及MI02句柄，GearLink_KBProcess.exe持有MI02 COL03。**handle owner != packet sender**。部分系统进程无法DuplicateHandle，coverage有记录，不宣称穷尽。没有本轮process-level WriteFile/ETW归因。

Procmon/handle CLI未在PATH找到，此次未安装工具、停ASUS服务或升级process tracing。startup selector发生时没有对应process-write记录；不能拿较晚的handle snapshot反向证明发送方。T1后没有first drift，故未生成虚构的first-drift snapshot。

## 判定与下一 gate

- 聚焦字段恢复：PASS，目标setter0。
- UI导航及149秒idle：**Case D NOT REPRODUCED（有界）**，无slot5→其它观察，无51 00 runtime。
- startup3/1selector：**CAPTURED，SOURCE UNKNOWN**；不属于T1建立之后的新drift，但仍是待归因异常。
- BANK_CHANGED_WITHOUT_CAPTURED_51_00：本轮未观察到；未据此宣称51 00是唯一切bank机制。
- 历史drift发送方/根因：NOT VERIFIED。没有声明reload correlated、Gear Link sender confirmed或source isolated。
- 本轮停止于报告。没有恢复fresh baseline、没有power persistence、没有production changes。后续如要恢复baseline，必须继续 immediate PRE5 → one mutation → POST5，保留unexpected selector stop gate，并先明确官方selection companion边界。

## 自动复核

`finalize_report.py`只离线读取已脱敏pcap解析与UI时间线：检查SHA、>=120s idle、284/284 explicit5、全部post-authority BasicInfo5、runtime selector0、目标setter0、监测无error且已停止。既有research offline suites结果见logs/offline-tests.log；不把offline结果当physical PASS。

所有数值/截图是本次证据；RT页面W0.1仅host/UI cache，不据此认定哪个hardware bank曾保存该参数。NVM persistence/physical RT均未验证。


### 本次验证结果

- Research offline tests：53 PASS，fresh `logs/offline-tests.log`。
- Offline evidence assertions：PASS，`state/offline-evidence-check.json`。
- `git diff --check`：PASS，`logs/diff-check.log`。
- 只改本轮研究文档；没有运行完整软件CI、产品build、硬件smoke或其它硬件写入。
- capture worker exited，interval/被动guard停止；只读观测已结束。


### 原始scope分类保持

旧通用case classifier按整份capture判CONTAMINATED（startup slot3/1和初始slot1），原始case.json、bank-scope.json保留不改。当前新phase-aware-summary.json只对T1成功后导航区间作NOT REPRODUCED分类；没有删除历史selector、硬改旧guard或把初始模板中的Profile5当硬件证据。
