# HISTORICAL CHECKPOINT / SUPERSEDED

This alpha.7 investigation records its original device state and acceptance boundary. Later physical acceptance and package acceptance supersede its release status; historical failures and observations are retained unchanged. Current truth: [HardwareSlot closure](../../../release/HARDWARE_SLOT_RELEASE_CLOSURE.md) and [package candidate](../../../release/PACKAGE_CANDIDATE_AUDIT.md). Raw captures/private evidence remain local and are not committed.

# Fresh AP Bare Selector Result — alpha.7

## 后续 checkpoint：2026-10-03 12:54–12:58

Owner-only WebHID policy 下已成功恢复 Gear Link 网页连接检查。官方选择 slot1 后又捕获未经本任务授权的 slot3→slot6，BasicInfo6，UI仍1。没有启动 Aura daemon或执行新的 bare selector5；浅触发反馈无法归属slot1。本轮停止原因是 runtime drift，非权限弹窗。详见 [FRESH_AP_CONNECTED_RETEST_RESULT.md](FRESH_AP_CONNECTED_RETEST_RESULT.md)。下面保留原单次 Aura test 的历史证据；其 INCONCLUSIVE 结论未改变。

## 最终结论

**INCONCLUSIVE / INVALID — UNEXPECTED PROFILE SELECTORS BEFORE PHYSICAL CHECK**

本轮不能标为 `BARE_51_00_AP_PASS`，也不能标为
`BARE_51_00_AP_FAIL`。唯一一次 Aura 产品激活本身通过 USB / BasicInfo
验证，但等待物理确认时发生了另外两次未经本任务授权的 selector。
Owner 明确确认没有操作 Profile，且 A 的深触发是在这些额外切换之后测试的。
该物理结果因此不能归属于目标槽位 5。

这不是 companion 必要性的证据，也不是 51 00 物理失败的证据。
原问题 A（旧 bank 内容不再符合参考）与 B（缺少 load companion）仍未区分。
**保留最窄生产协议；整个 HardwareSlot MVP 仍未获得完整物理接受。**

本轮只执行了一次当前 Aura HardwareSlot 激活，没有重复测试、自动抢回
槽位 5、追加官方 companion、修改 production、修改 bank 内容，或开展新的
getter / persistence / ManualHold / reconnect 实验。

## 可信 marker 与 slot1 physical PRE

- 上一轮新建并物理确认的 marker：slot5 Key A = 3.2 mm，第一次按下较深；
  slot1 A 为浅触发。只使用 A，不使用 RT / DKS / lighting / host cache 判据。
- 本轮刚进入现有 Gear Link session 时，BasicInfo 读到 slot6；这是实验起点
  状态，未将旧 UI selector 1 当成硬件 authority。
- 在官方已连接 device instance 的原始队列上调用 `changeProfile(1)`，并执行
  之前已审计的官方 RT gate / SpeedTap / BasicInfo / polling refresh 方法。
  没有自写 HID sender、磁轴 setter、配置编辑或 Sync。
- 官方 BasicInfo 于 UTC `2026-10-02T20:52:21.749Z` 确认 active slot1。
  页面仍显示 Profile1；截图 `screenshots/official-slot1-pre.jpg`。
- Owner 对“请按一下 A，确认现在是不是浅触发。”回复 **“是”**。
  **SLOT1 SHALLOW PRE = USER PHYSICAL PASS**。

## Gear Link isolation

Owner 确认浅触发后，关闭唯一 Gear Link 标签 `871192270`，终止该页面的
WebHID session。重新列出的同一 Edge browser 标签仅有键盘测试页，无
Gear Link 标签。没有关闭 Edge、恢复浏览器 session 或停止 ASUS services。

证据：`state/gear-link-isolation.json`。这证明本任务控制的 Gear Link 前端
已关闭，**不证明所有可能的外部 writer 已隔离**。后续额外 selectors 的
发送进程仍 UNKNOWN；USBPcap 本身不能确定 process sender。

## 当前 Aura 产品路径

直接后台启动命令被自动审批拒绝且没有执行。随后使用现有当前工作树 WinUI
“设置 → 重新连接”，由正式 `DaemonSupervisor` 启动当前 daemon。
确认其 binary / command line / parent identity；没有绕过安全策略或改变配置。

- Binary：`build/dev-winui/Release/aura_daemon.exe`。
- Binary SHA256：`179F474E315C56AF8F211B0DEA866FD2194F3BFD0B3C2245ACB561827FF6910B`。
- 使用原先隔离 acceptance data；lighting config 没有编辑。
- Device Profile automation 原本 disabled、bindings 空；保持原样。
- 只读取已有唯一 target Profile GUID
  `ae368645-1ced-43fa-a899-e90b6abc7723`，确认 backend=hardware_slot、slot=5。
  没有因另一个 Profile 名字叫 HW Slot1 就误用其实际 slot3 字段。
- 实际调用正式 `POST /api/device-profiles/activate`，reason=Manual，
  expected_revision=23。不是 audit-only raw sender，也不是 Gear Link JS。
- 成功 response revision=24、selected=active=该 GUID、dirty=false；
  requested=observed=5、selector_sent=true、verification=confirmed。
- magnetic plan / effective target 为空；M605 staged transactions=0。
- API processing=111.8756 ms；runtime activation=109.3544 ms；
  selector 到首次匹配 BasicInfo5=107.056 ms。
  **这些是 host / USB 时间，不是物理感知延迟。**

## USB sequence 与额外切换

所有表内 frame 为脱敏 pcap 的 retained record 编号。UTC 表后附北京时间，
避免混淆。Vendor payload 为 64 bytes；host transport Report ID 不在这 64 bytes 内。

| Retained frame | UTC | OUT / completed IN | 含义 |
|---:|---|---|---|
| 3 / 4 | 20:55:29.523843 / .525280 | 12 00 query / BasicInfo1 | Aura startup 只读检查 |
| 5 / 6 | 20:55:45.354001 / .355742 | 12 00 query / BasicInfo1 | 显式只读 PRE |
| 7 / 8 | 20:56:25.183990 / .185102 | 12 00 query / BasicInfo1 | 激活前紧邻 PRE |
| 9 / 10 | 20:56:25.225260 / .227102 | 12 00 query / BasicInfo1 | 产品 activation 内部 PRE |
| 11 / 12 | 20:56:25.227304 / .317356 | 51 00 slot5 / echo5 | **唯一授权 selector** |
| 13 / 14 | 20:56:25.332702 / .334360 | 12 00 query / BasicInfo5 | 产品 verification PASS |
| 15 / 16 | 21:26:08.355198 / .397365 | 51 00 slot3 / echo3 | **未经本任务授权，sender UNKNOWN** |
| 17 / 18 | 21:26:09.057885 / .118870 | 51 00 slot6 / echo6 | **未经本任务授权，sender UNKNOWN** |
| 19 / 20 | 21:26:49.005991 / .007249 | 12 00 query / BasicInfo6 | 确认最终 authority 已丢失 |

Exact reviewed OUT payloads（全部 remainder 为 zero）：

- BasicInfo：`12 00` + 62 zero bytes。
- 授权 selector：`51 00 00 00 05` + 59 zero bytes。
- 额外 selector：`51 00 00 00 03` / `51 00 00 00 06` + 59 zero bytes。
- BasicInfo response active-slot 字段为 vendor payload offset10。

北京时间 `2026-10-03 04:56:25`：Aura slot5 selector 与验证。
`05:26:08–09`：额外 selectors 3、6。`05:26:49`：最终 BasicInfo6。
两次额外 selector 发生在首次验证后的约 29 分 43 秒；当时本任务没有再次
调用 activation，也没有进行浏览器或 Profile 操作。

## Companion / setter counts

| Reviewed command | 完整独立 capture OUT 数量 |
|---|---:|
| 12 00 | 6（6 个 matching completed IN） |
| 51 00 | **3**：授权 5×1；额外 3×1、6×1 |
| 25 00 / 25 01 / 51 31 | 各 0 |
| 50 55 | 0 |
| 51 50 / 51 4F / 51 58 / 51 59 | 各 0 |
| 51 52 / 51 53 / 51 54 / 51 23 | 各 0 |
| 51 2C / 51 2D / 50 40 | 各 0 |

初始 activation 窗口及提问前 live parse 只有 1 个 selector，BasicInfo1→5，
所以 transport gate 当时 PASS。不能用该旧 live snapshot 冒充整个最终窗口。
最终 offline guard 正确 non-zero：exactly-one-selector 和 slot authority 失效。
全部 capture 自开始到结束没有 stop/start gap。

Collector 只保留 HFX reviewed vendor/status packets 与不含 serial 的必要
descriptors。普通按键、serial query/string descriptors、其它设备与未审查
vendor bodies 被丢弃；C0 81 不在本 collector 的保留范围内。
**不声称 RGB streaming OFF 或 C0 81=0，也不声称此文件是完整总线包清单。**
Direct RGB 不作为 A 触发深度判据。

## Owner physical POST 与失效原因

Owner 对 A 是否恢复明显深按回复 **“确认”**。
发现额外 selectors 后，仅做一次澄清，未要求重做：

> 北京时间 05:26:08–09，你是否手动切过配置文件或按过 Profile 快捷键？
> 刚才 A 的深触发是在这之前还是之后测试的？无需再操作键盘。

Owner 回复：**“没有动过，在此之后测试的”**。

因此：Owner 确实观察到深触发，但观察发生在额外 slot3 / slot6 selection
之后，最终 BasicInfo 又确认 slot6。无法把它作为 fresh slot5 bare activation
的物理闭环。不可强行按二选一判 PASS/FAIL；不得声称原始失败只因 stale bank，
也不得声称 companion 已证明必要。

## 封存、验证与下一步

- Capture：`artifacts/fresh-ap-bare-selector-20261003-0452/usb/fresh_ap_bare_selector_test.pcap`。
- SHA256：`531674cecc377727f0fc51bb8461007852e914bf7e54f88ccc50c96082fec8cc`。
- Worker started UTC `2026-10-02T20:53:22.5135841Z`，
  stopped UTC `2026-10-02T21:27:31.5480642Z`；privacy outcome=complete。
- 当前实验 daemon 通过 identity-validated 私有正常停机 event 退出；未粗暴
  kill 未知进程，ASUS services 未动，capture stopped/finalized。
- `analyze_bare.py` 只读离线分析，无 HID API；最终 evidence guard **FAIL**
  正是预期保护结果，不能改成绿。Source attribution 未执行。
- 没有生产代码变化；没有重跑 canonical CI，也没有用旧 CI 代替本轮验证。

**STOP_WAIT_OWNER_REVIEW — AUTHORITY_LOST_BEFORE_PHYSICAL_CHECK**。
后续若 Owner 批准，先归因/隔离本次无 Gear Link 前端时的 slot3→slot6 writer，
再考虑短窗口重复该单一 discrimination。当前不做 companion minimization，
不恢复 ManualHold/reconnect，不提交、推送、打包或发布。
