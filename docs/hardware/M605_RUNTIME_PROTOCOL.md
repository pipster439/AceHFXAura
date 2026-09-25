# M605 已验证运行时协议（alpha.5 Phase 1–3）

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

## Phase 2：已验证的单键 RT 与 Deadzone

单键 Rapid Trigger 对同一已验证 Wire ID 连续发送 Press 与 Release 两份 stage，**两份 stage 之间没有等待或 Apply**：

| stage | 报文前缀 | 数值 | 启用位 |
|---|---|---|---|
| Press | `00 51 54 01 00 [Wire低] [Wire高] [press_raw] 00 [flag]` | `round(press_mm * 10)` | Byte 9 |
| Release | `00 51 54 02 00 [Wire低] [Wire高] [release_raw] 00 [flag]` | `round(release_mm * 10)` | Byte 9 |

两值只允许 0.1–2.5 mm（raw 1–25）。`flag=1` 表示启用；禁用使用**相同的两份报文**并令 `flag=0`。禁用 API 必须显式收到来自权威应用配置的继承 Press/Release 值，不把 V 上已验证的 0.4/0.2 mm 当成通用默认值，也不假称从设备读取到继承值。V 的已验证启用例为 Press `... 31 00 08 00 01`、Release `... 31 00 06 00 01`；禁用例为 `... 31 00 04 00 00` 与 `... 31 00 02 00 00`。

单键 Deadzone 覆盖使用 `00 51 59 00 00 [Wire低] [Wire高] [Bottom_raw] [Top_raw] 00...`，两值均为 0.0–0.5 mm（raw 0–5）。**Byte 7 是 Bottom；Byte 8 是 Top。** 旧资料中相反的次序已被受控 A/B 和 HAL 字段偏移证据推翻。V 的 Top 0.2 / Bottom 0.3 mm 必须为 `00 51 59 00 00 31 00 03 02 ...`。

`ResetAllPerKeyDeadzoneOverrides` 是**清空全部单键 Deadzone 覆盖表**的破坏性操作，绝非删除指定键。唯一允许的 resetType 是 `0x04`，Falchion Ace HFX 仅支持 dual-deadzone 的 layer 0：`00 51 52 04 00 [全局Bottom_raw] 00 [全局Top_raw] 00...`。调用者显式提供权威应用配置中的全局值（raw 0–5）；后端不读取设备或自动导入 ASUS XML。Bottom 4 / Top 3 的官方 A/B 报文是 `00 51 52 04 00 04 00 03 ...`。若权威值为 Bottom 1 / Top 0，builder 产生 `00 51 52 04 00 01 00 00 ...`；此精确 Top=0 报文已在生产路径实体 smoke 中发送，操作者确认恢复全局继承行为。

较早观察到的 `00 51 52 04 00 01 00 02 ...` 实际编码 Top=2。ASUS 前端的 `deadZoneTop = O || W` 在 `O=0` 时错误回退到默认 `W=2`；本实现不复制这个前端行为。早期把 Byte 7 称为 Deadzone Mode 的说法也不适用。匹配的设备 echo 已被观察到，但不能据此宣称存在独立的 MCU ACK 确认协议。

Phase 2 的所有事务仍遵守完整生产时序：连续发送全部 stage → **最后一份 stage 后**等待 210 ms → 恰好一次 Apply → 等待 400 ms → 更新应用侧影子状态并完成 future。共享设备写锁覆盖全程；任一 stage 或 Apply 传输失败都进入 `IndeterminateStagedState`。影子状态新增单键 RT 启用位及 Press/Release raw、单键 Deadzone Top/Bottom raw；reset-all 成功后才清空运行时已知 Deadzone 覆盖表。以上仍不是设备 readback。

**Phase 2 后端验收：PASS（补充生产路径 0.1/0.1 mm RT 诊断后接受）。** 原脚本的 V RT Press 0.8 / Release 0.6 mm 报文和 API 成功，但操作者未能明确感到 RT 效果，故原脚本实体标记未通过。用户随后明确授权用同一生产 API 设置 Press/Release 0.1/0.1 mm，操作者清晰确认 RT 效果，之后又确认禁用并恢复继承值。单键 Deadzone 的 Bottom/Top 报文及效果、Top=0 的 reset-all 报文及继承恢复也获得实体确认。额外诊断使实际 HID 输出报文总数为 **16**，不能表述为原定恰好 10 份的严格脚本通过。未来需要易于实体辨别的 RT smoke 向量时，优先考虑本次已确认的较小 RT 距离，并如实记录具体设置和写入总数。完整时间线与哈希保存在 `audit_artifacts/alpha5-phase2-production-smoke/PHASE2_PRODUCTION_SMOKE_REPORT.md`。

目前没有安全的公开单键 Deadzone 删除 API。未来选择性删除必须先持有权威的完整目标覆盖集，再作为一个序列化事务 reset-all 并重放保留项。All-Key RT/Deadzone、DKS、Hall 遥测、持久化与 UI/HTTP 不在本阶段。

## Phase 3：已验证的 SpeedTap 运行时操作

SpeedTap 继续使用同一 MI_01 端点、65 字节报文、共享写锁及完整的 **stage → 210 ms → 一次 Apply → 400 ms** 事务时序。下列每个 API 调用各自是一笔事务；没有证据允许把两个不同语义操作合并为一次 Apply。

| 操作 | 精确 stage 前缀 | 语义 |
|---|---|---|
| `SetSpeedTapPair(key1, key2)` | `00 51 55 00 00 [Wire1低] [Wire1高] [Wire2低] [Wire2高] 01 00...` | 启用指定有序键对 |
| `DisableSpeedTapPair(key1, key2)` | 同上，Byte 9 为 `00` | 只禁用该指定键对，不重置其他键对 |
| `SetSpeedTapMaster(true/false)` | `00 51 57 00 00 [01/00] 00...` | 独立开启／关闭 SpeedTap 引擎；Master OFF 不代表删除键对 |
| `ResetSpeedTapRuntimeToProfile()` | `00 51 56 00 00 00...` | 将运行时键对状态恢复到活动配置基线；**不是清空全部键对** |

SpeedTap 映射仅增加经过实体验证的 A：逻辑 `1538 / 0x0602` → Wire `0x001F`，D：`769 / 0x0301` → `0x0021`，W：`1793 / 0x0701` → `0x0012`，S：`1794 / 0x0702` → `0x0020`；既有 V/C 映射也可使用。SpeedTap 专用映射查找不扩大 Phase 1/2 单键设置的允许范围。相同键的配对、未知映射、非 0/1 标志和非零保留字节均被拒绝。官方 UI 不允许一个键同时参与多个键对，但尚不能据此宣称固件普遍禁止重叠键对；本后端暂不增加推测性的冲突判定。

Stage 6B 在 Master ON 且 A+D 为活动配置基线、W+S 为运行时新增键对时，单独发送 `0x51 0x56` 并 Apply 后观察到 **W+S 停用、A+D 仍有效**。因此较早把 `0x56` 称作“无条件清空到空表”的解释已被推翻。本实现不提供 `ClearAllSpeedTapPairs()`。若未来需要“保持 Master ON 但删除所有键对”，必须另行取得权威当前键对集合并定向禁用，不能借用 `0x56` 的名字或效果。

官方 Reset UI 的观察序列是 `0x56` + Apply，**然后** `0x57` Master OFF + Apply。本后端保留两笔独立、串行的事务。运行时影子仅记录 AceHFXAura 提交过的键对启停和独立的 Master 状态；reset-to-profile 成功后清除这些运行时提交记录，并把键对知识标记为 `ProfileBaselineUnknown`，绝不把空 map 当成设备空表。没有完整键对读回、活动配置存储介质或断电持久性结论。Stage 6B 观察到匹配的设备 echo；`0xFFAA` 与 echo 均不在此处被称为经证明的 MCU ACK。

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

已测试的运行时 USB 协议未发现连续逐键 Hall 行程值；不要提供伪造的 `GetTravelMm`、`GetHallDepth` 或 `RawHallValue` API。DKS、固件操作和持久化写入均不属于已实现的 Phase 1–3 范围。
