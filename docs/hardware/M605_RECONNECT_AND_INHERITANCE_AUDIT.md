# alpha.7 M605 重连与单键继承审计

日期：2026-09-30。区分用户真机发现、当前只读静态证据、软件/mock 验证；不把静态推断写成实体 PASS。

## 结论与生产限制

1. 用户真机确认：`51 50` 更新 Actuation common/base，**不清除** Armoury Crate 留下的 `51 4F` 单键覆盖。因此 Global 成功不能清空单键 shadow，也不能证明 Profile 继承完整生效。
2. `51 58` 不作为 Deadzone 单键覆盖删除机制；使用既有实体闭环验证的 `51 52 resetType 4` 清表，再提交 common/base 与保留的单键例外。
3. 生产仍只 allowlist resetType 4。本次没有增加 resetType 0/1 builder、runtime setter、generic reset API 或未知 HID 报文。
4. **触发点 Profile 继承仍是 release blocker**：只要 effective target 管理 Actuation（包括已知 baseline 的 inherit），activation 在首份 HID 报文之前明确失败，selected 保留、active unknown、dirty=true。不能让未知外部单键表被默认为 clean。
5. 重连实现的是 transport/session 安全边界，不是自动 Profile reapply 或 firmware readback。真正 unplug/replug 时序仍须实体复验。

## Transport 生命周期实现

`NativeHidBackend` 在枚举前注册 HID device-interface arrival/removal 通知，记录所选 MI_01 path。回调只标记该 path 已变化；open 尚未选择 path 时对 interface 变化保守拒绝本次 open。回调不关闭 handle、不发送报文、不等待事务。Disconnect 在回调外注销通知，避免对象销毁后回调继续访问。[Windows interface 通知说明](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/registering-for-notification-of-device-interface-arrival-and-device-removal)、[注销说明](https://learn.microsoft.com/en-us/windows/win32/api/cfgmgr32/nf-cfgmgr32-cm_unregister_notification)。

每次新事务在 latch Arm 前：检查通知标记、旧 handle endpoint identity、当前 path 能否以 access=0 打开并通过 VID/PID/MI_01/Usage/65-byte input/output 校验。旧 session 失效则关闭、清 shadow、generation++；重新枚举验证 open 成功后再次 generation++。所有这些动作在共享 DeviceWriteMutex 下；不会增加 HID 并发或缩短 30/210/400 ms 等待。

状态查询只在设备锁可立即取得且 runtime Clean 时检查旧 session；不打开新 transport、不发送报文。Profile 在 planning 前准备 fresh session，在操作间及最终 commit（包括 no-op）检查 generation。一个 plan 不能跨 epoch 继续被认定为完整提交。

移除发生在 stage 已可能提交之后，或 Apply 后 settle 尚未完成时：关闭 transport、清 shadow、进入 indeterminate、保留 durable latch。首个 stage WriteFile 1167 仍按原安全规则处理，不泛化为可重试错误。

### Persistent quarantine 恢复

arrival/replug/app restart 不自动清 latch：暂无证据证明 USB replug 能清 MCU staging RAM。现有磁轴页的外部恢复确认仍要求操作者先用外部方式恢复已知良好状态；确认调用现在必须在 idle 状态重新 open 并校验 M605 transport。offline/busy/open failure/clear-latch failure 保留隔离。fresh open 本身不是恢复证据，确认操作发送 **0 HID reports**。这是保守的人工 resynchronization 路径，不是自动 rollback。

## resetType 0 只读证据

### 输入身份

| 输入 | 身份 |
| --- | --- |
| `M605_1_00_58_core2_readonly.bin` | SHA-256 `8fe68a13d7a0cd3bf8b4fd0dbe0575c0b7e67c1d4373e8d63970cf248684e668`；Thumb base `0x18000000`；1.00.58 样本，不证明当前实体固件与其相同 |
| `drivers/AacKbHal_x64.dll` v1.3.46.0 | SHA-256 `52d575bf942b7551b3f120c446bf0d853e36f9225c6b9a17407a80e0b1829f04` |
| 安装的 `ArmouryKbSDK.dll` | SHA-256 `66bb6e08a116c7670e79649c6ac93cac97b8310ad7cea1a3ce2b1f5b00fe7d0d` |
| `View/7038/index.js` | SHA-256 `708e9049cb6fb61c489d47dcb15a0ff038b96ee1774aeeb57655ab7172a2eab4` |
| `View/7038/5744-bundle-f78f7.js` | SHA-256 `54147a0a9f9d8c329044fe5accf734a9096d2be35eef8ac250069e44acf43892` |

### Official frontend → SDK → HAL

`index.js` 的 `resetAnalogTriggerAll` 包含 type 0 分支，会同时处理 Actuation、RT 与 per-key RT list；这证明候选调用能力存在，不证明实际 UI 发出了 type 0。

SDK XML parser `0x1004E810` 解析 `resettype / actuation / rapidTrigger / layer / deadZone / deadZoneTop / deadZoneBottom`，分别进入 offset `0x210..0x228` 的七个 dword。layer 缺省 0，三个 deadzone 参数缺省 -1。SDK `0x10044EF4` 包装这七个参数并通过 FunctionID `0x2B` 分派至 HAL `FUN_180022100`。

HAL type 0 分支（`0x18002216D` 起）把 actuation、rapidTrigger、layer 参数低字节写到零初始化的 64-byte vendor buffer 的 offset 4/5/6；vendor header 为 `51 52`、resetType word 为 0。静态得出的 **65-byte 候选**如下，仅限研究，不进入生产：

| HID report byte | 值 |
| --- | --- |
| 0 | `00` Report ID |
| 1–2 | `51 52` |
| 3–4 | `00 00` resetType 0 |
| 5 | Actuation raw（来自调用参数；当前未建立该 reset 的完整有效范围 contract） |
| 6 | RT raw（一个值，不表达 press/release 分离配置） |
| 7 | layer |
| 8–64 | 全零 reserved bytes |

HAL 发送 64-byte vendor buffer 后等待自身状态/FFAA/timeout；这不证明生产 `50 55` 时序、硬件 ACK 语义或 persistence。

**实际官方 UI 调用未闭环**：当前安装的 5744 bundle 搜索只找到 literal resetType 1、4、3。触发点单键重置回调调用 type 1，Deadzone 调用 type 4，整体 analog reset 调用 type 3。未找到明确发出 type 0 的 UI 操作；不可把 type 1/3 的 UI 当作 type 0 证据。后续可优先审计 type 1 是否为更合适的纯 Actuation 恢复能力，但同样须先验证，不能借本次扩大 allowlist。

### Firmware type 0 branch

已保存直接分支 Thumb disassembly，跳表与 literal data 单独标明，不作为指令解码。

| 地址 | 直接分支观察 |
| --- | --- |
| `0x18002C1A` | resetType dispatch（0–4） |
| `0x18002C2A` | type 0 设置 field mask=3，Actuation argument offset 0、RT offset 1、layer offset 2 |
| `0x18002C6C` | 读取 vendor `+6` 的 layer，进入相关 bank 选择；不能把候选 layer 0/1/2 通用化为生产 API |
| `0x18002CB6..2CC2` | 重写 per-key table `+8` Actuation low 7 bits、清 bit 15；另一个 bank 相距 `0xD8C` |
| `0x18002D00..2D3E` | 重写 RT low 14 bits（两个敏感度域来自同一参数），清高两位；不是只改 Actuation |
| `0x18002DD8..2DE8` | 修改 profile-indexed global act metadata 并调用 `0x18006D10`；不证明 flash/NVM persistence |
| `0x18002E04..2EAC` | 按 profile 表对四个选中键设置 RT enable bit `0x4000`；具体键身份尚不作为已验证 contract |
| `0x18002EB4` | 更新 bank/status flag |
| `0x18002EEE..2EF8` → `0x180030C4` → `0x180033A4` | 构建 `51 52` 响应，调用 `0x18000A70` |
| `0x18003548` → `0x18003A6A` → `0x180040C0` | 清 handler 标记并返回 |

type 0 的 mask 没有 Deadzone bit，但 RT 副作用已足以否定“纯 Actuation 清表”。直接分支已追到 epilogue；被调用 helper 的完整语义、其它固件版本、runtime feature/master 状态影响和实体实际结果仍未闭环。不能仅靠这些静态 bit 操作证明“继承表清除后 RT 全部保持不变”。

### 证据等级与下一步

- **PASS — software/mock**：stale session preflight、generation/shadow invalidation、in-flight removal quarantine、resetType 0 allowlist 拒绝、Deadzone reset/base/exception ordering 与失败行为。
- **只读本机检查**：fresh native endpoint open/notification registration/close 成功，0 output reports；没有拔插、reset 或磁轴写入。
- **NEEDS HARDWARE VALIDATION**：真实 unplug/replug 通知时序、DKS 首笔事务、Deadzone inherited/exception 行为、持久 latch 人工恢复流程。
- **BLOCKED BY UNVERIFIED PROTOCOL**：生产 Actuation inheritance reset。resetType 0 当前只有静态候选与 RT 副作用证据，缺 type 0 官方 UI 操作、官方被动 65-byte sequence 捕获、对应实体恢复和 RT preservation。

后续应先用官方 UI 找到确切操作并被动捕获，不调用 SDK/DLL 人工造 type 0，不发送猜测包。若 type 0 无 UI，应考虑审计已存在官方 UI 的 type 1，仍须独立完成精确包、完整时序与物理验证。完成前 Profile Actuation 会明确失败，不提供伪造的 A→B→inherit PASS。

## 本地证据文件

未提交的 `audit_artifacts/alpha7_reconnect_inheritance/` 包含 `ghidra_hal_reset_dispatch.json`、`ghidra_hal_type0_assembly.json`、`ghidra_sdk_xml_parser.json`、`ghidra_sdk_reset_packing.json`、`official_frontend_reset_snippets.json`、`firmware_reset_dispatch.txt`、复现脚本与测试日志。Ghidra project metadata 的空文件 hash 不充当 binary hash；以上输入 hash 按实际文件计算。
