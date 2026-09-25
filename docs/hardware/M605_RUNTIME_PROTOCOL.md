# M605 已验证运行时协议（alpha.5 Phase 1）

目标为 ROG Falchion Ace HFX、固件 1.00.59。仅使用 VID `0x0B05`、PID `0x1B7E`、`MI_01`、UsagePage `0xFF00`、Usage `0x0001`，输入与输出报文均为 65 字节，Report ID 为 `0x00`。

## 身份与映射

逻辑键 ID、磁轴 Wire ID、灯光 LED ID、HID Usage 是不同身份。逻辑 ID 的高字节为 row、低字节为 col；M605 表索引为 `row + col * 8`。当前运行时白名单只含：

| 物理键 | 逻辑 ID | 表索引 | Wire ID |
|---|---:|---:|---:|
| V | `1026 / 0x0402` | 20 | `49 / 0x0031` |
| C | `1281 / 0x0501` | 13 | `48 / 0x0030` |

曾将 V 写作逻辑 `1281`、Wire `0x30` 的记录是错误的；该身份属于 C。其他表项在 Phase 1 中保持 UNKNOWN，不从 LED ID 或 HID Usage 推算。

## 已验证写入

单键触发行程：`00 51 4F 00 00 [WireID低] [WireID高] [raw] 00...`。`raw = round(mm * 10)`，仅允许 0.1–4.0 mm、raw 1–40。V 的 4.0 mm 为 `00 51 4F 00 00 31 00 28...`；1.0 mm 为 `00 51 4F 00 00 31 00 0A...`。V 的 4.0 mm 与 1.0 mm 恢复均有物理按键确认。

硬件 Analog Effect：`00 51 2D 00 00 [EffectID] [Enable] 00...`。Phase 1 只允许已物理验证的 Static／恒亮 `EffectID=0`，`Enable=1` 开启、`0` 关闭。键盘 MCU 本地根据 Hall 深度执行灯效，PC 无须推送逐帧 RGB。

上述 stage 写入完成后先等待约 210 ms，再发送**一次**运行时 Apply Gate：`00 50 55 00...`；Apply 写入成功后，再保留 400 ms 的保守、兼容厂商行为的生效后 settle 时间，事务才结束。这不是已验证的 flash commit、持久化保存或 NVM 写入。断电后是否保留尚未测试。

`WriteFile` 成功仅表示主机传输提交成功，不等于 MCU ACK、设备状态读取或物理生效确认。400 ms 是保守等待策略，不是 ACK 解析。未来若加入经验证的 RX 确认，应据此加强完成策略，而不能把当前等待说成设备确认。

`SetPerKeyActuation` 是一次完整的硬件事务，至少包含约 210 ms 的 Apply 前等待和 400 ms 的 Apply 后等待。未来滑杆不能在每次鼠标移动时调用它；接入 UI 时须在上层设计 debounce／合并策略，本阶段不实现。

## 事务健康状态

- `Clean`：可接受配置请求。新连接会清空应用侧影子状态，因为连接并不是设备读取。
- `TransactionInProgress`：后台 worker 已取得设备写锁；stage、210 ms Apply 前等待、apply、400 ms Apply 后等待均与灯光帧互斥。灯光 `PushFrame` 因此可能延迟约 610 ms，再加上实际 HID 写入时间及锁竞争时间。
- `IndeterminateStagedState`：stage 已成功但 apply 传输提交失败，或 stage 写入本身无法确认。MCU staging RAM 可能留有待生效设置。立即清空影子状态，取消等待队列，并拒绝此运行时对象的后续配置与 apply；重连不会自动恢复。需在外部完成设备状态重新同步后，才能创建新的运行时对象。本阶段没有推测性的恢复写入协议，也不假设 USB 重插会清除 staging RAM。
- `Stopped`：停止接收新任务，未开始的任务以 `false` 完成；已开始的事务允许完整结束。析构调用相同的停止路径。

设备尚未开始 stage 时的连接失败不会产生本对象的 staged 状态，允许后续请求重试。所有状态均不代表断电持久性结论。

## 状态边界

设备完整配置读取路径尚未验证。代码中的 **AceHFXAura Applied Runtime State** 只记录本进程成功提交 stage、完成 Apply 前等待、成功提交 apply、完成 Apply 后等待的设置；它不是 **Known Device Readback State** 或 MCU ACK。写入失败或重新连接时清除该影子状态。ASUS XML 不能充当设备实时读取。

已知后续问题：`IndeterminateStagedState` 目前只在本运行时对象生命周期内有效。未来集成阶段必须定义 daemon 重启／运行时重建后的恢复或持久隔离语义；本阶段不增加配置文件或持久标记。

已测试的运行时 USB 协议未发现连续逐键 Hall 行程值；不要提供伪造的 `GetTravelMm`、`GetHallDepth` 或 `RawHallValue` API。Rapid Trigger、Deadzone、SpeedTap、DKS、固件操作和持久化写入均不属于本阶段。
