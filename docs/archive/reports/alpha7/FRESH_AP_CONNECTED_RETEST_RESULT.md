# HISTORICAL CHECKPOINT / SUPERSEDED

This alpha.7 investigation records its original device state and acceptance boundary. Later physical acceptance and package acceptance supersede its release status; historical failures and observations are retained unchanged. Current truth: [HardwareSlot closure](../../../release/HARDWARE_SLOT_RELEASE_CLOSURE.md) and [package candidate](../../../release/PACKAGE_CANDIDATE_AUDIT.md). Raw captures/private evidence remain local and are not committed.

# Fresh AP Connected Retest — 2026-10-03

## 结论

**UNAUTHORIZED_HOST_SELECTOR_CONFIRMED；SENDER=UNKNOWN。**

Owner subsequently explicitly confirmed no manual action occurred during the observed slot1→3→6 drift window. `OWNER_MANUAL_SWITCH=EXCLUDED`。实际 host OUT selector 已捕获，不能将它仅归结为 UI stale；UI1/BasicInfo6 mismatch 是后续表现。bare selector physical test NOT EXECUTED。

按 Owner-only WebHID policy 恢复网页检查，成功确认 Gear Link 已进入 HFX 设备页。没有检查、读取或操作浏览器原生权限弹窗。本次停止原因是实际捕获的 bank drift，与权限弹窗无关。

唯一授权动作是官方网页选择配置文件 1。之后发生未经本任务授权的 `51 00 slot3 → slot6`，且 BasicInfo 确认 6。Aura daemon 本轮未启动，Aura HardwareSlot activation 调用次数为 0。

Owner 回复“是，浅触发”。保留此物理观察，但无法证明观察时仍为槽位 1，不能作为可信 slot1 physical PRE。没有执行槽位 5 裸 selector，不能判定 BARE_51_00_AP_PASS 或 FAIL。

## 连续证据

目录：`artifacts/fresh-ap-connected-retest-20261003-125433/`。

- Capture 从官方连接已经完成后、选择 slot1 之前开始；不是覆盖 WebHID 授权/初始化的 capture。
- 不中断 capture 覆盖读 BasicInfo、官方选 slot1、等待物理 PRE、发现 drift。
- Worker started UTC `2026-10-03T04:54:34.6824025Z`；exited UTC `2026-10-03T04:58:08.1546797Z`。
- privacy outcome=complete；60 retained records，58 reviewed vendor packets，含 safe descriptors；普通按键、serial 与未审查流量不落盘。
- SHA256：`61d978bc3cfa4e81e06ded2d48d70eba2e4ef520fc55e940b35d4da4217885d7`。
- C0 81 不在本 collector 保留范围，不能声称 RGB OFF。

## 时间线（北京时间）

| 时间 | 证据 | 结论 |
|---|---|---|
| 12:55:08.593010 | retained frame3，OUT `51 00 ...06` | 官方 slot1 selection 前的额外 selector，sender UNKNOWN |
| 12:55:15.447284 | matching BasicInfo IN=6 | 页面“默认配置文件”与 actual6 一致 |
| 12:55:49.960408 | frame9，OUT `51 00 ...01` | 本任务唯一授权的官方网页选择 |
| 12:55:50.017476 | BasicInfo IN=1 | 选择后硬件 identity=1 |
| 12:56:01.698504 | 显式 official getter，BasicInfo1 | 保存 physical PRE authority |
| 12:56:27.407575 | completed BasicInfo1 | 此时仍为1 |
| 12:57:18.967657 | frame47，OUT `51 00 ...03` | 未经本任务授权，停止依据 |
| 12:57:20.352927 | frame49，OUT `51 00 ...06` | 未经本任务授权，sender UNKNOWN |
| 12:57:30.896524 | 显式 official getter，BasicInfo6 | 确认 authority 丢失 |
| 最后网页检查 | selector仍显示配置文件1 | UI stale，不能用于硬件 authority |

Selector exact payload 为 `51 00 00 00 <slot>` 后续59 bytes全零。BasicInfo64-byte vendor response 的 offset10 为 active slot；使用当前官方 `getDeviceInfo(0)` 与官方 send queue，未手写 HID sender。

## OUT counts

`12 00`×15；`51 00`×4（6、授权1、额外3、额外6）；`25 00`×3；`25 01`×3；`51 31`×4。后面三类出现在官方正常选择的 refresh chain，不是 Aura 裸 selector。

保留范围内 `50 55`、AP/DZ/RT/DKS/firmware-lighting content setters 均为0。没有 Sync、reset 或设置编辑。完整已保留 timeline/payload：`parsed/final-usb.json`。

## 归因与边界

- Preflight 未发现运行中的 Aura daemon、AuraOwnershipExperiment 或 ArmouryCrate.exe 前端；当前 Aura WinUI窗口保留但 daemon未启动。
- ASUS background services 未停止。Gear Link 页面保持连接。
- USB 能证明额外 selector 实际存在，不能确认 process sender。Owner 手动操作已排除；不能归因给 Gear Link、Edge、ASUS服务或 Aura。
- Owner 本轮随后明确确认：slot1 建立到 3→6 drift 期间完全没有任何手动操作。该 window 的 Owner manual switch 已排除。
- 之前约190秒稳定 gate 不覆盖本轮；本轮现在确有 runtime drift。

## STOP

被动 capture 已停并封存。没有重新抢回slot1、没有激活slot5、没有重复 physical request、没有修改 production/allowlist/timing。需要 source attribution/隔离后才能恢复 fresh bare discrimination。权限 policy 已正确遵守，不是当前 blocker。

只做离线分析与文档校验，不重跑 canonical CI。无 commit/push/package/release。
