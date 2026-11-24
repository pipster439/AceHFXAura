# HID 拓扑与输入数据源矩阵

结论标签：**CONFIRMED** 表示指定证据层已直接确认，必须同时标明是源码、缓存、历史 USB 或静态二进制；**INFERRED** 是尚需实体测试的推论；**UNKNOWN** 是无证据/不可读取；**DISPROVEN** 只否定表述的具体假设，不表示其它未知路径不存在。

目标 VID `0x0B05` / PID `0x1B7E`。2026-10-08 当前缓存枚举 8 个 TLC，覆盖 5 个 USB interface。[快照](evidence/TOPOLOGY_SNAPSHOT.json)不含按键事件；当前固件版本 UNKNOWN。端点来自既有 1.00.59 调查的 USBPcap **注入配置元数据**，不是本次硬件读回。Windows preparsed data 是缓存的解释结果，并非 USB report descriptor 正文字节。

## Interface / TLC / Report

| Interface / Collection | TLC Usage Page / Usage | Windows In / Out / Feature bytes | Input Report IDs | Interrupt IN / max packet | 角色与证据状态 |
|---|---|---|---|---|---|
| MI_00 / 单 TLC | 0001 / 0006 | 9 / 2 / 0 | 0（无显式 ID） | 81 / 8 | 标准 Boot Keyboard，缓存与历史 class/subclass/protocol=3/1/1 CONFIRMED；6KRO 模式角色 INFERRED |
| MI_01 / 单 TLC | FF00 / 0001 | 65 / 65 / 0 | 0（无显式 ID） | 85 / 64 | vendor 配置/echo 通道；缓存与 Native HID 匹配 CONFIRMED；非全键物理源 |
| MI_02 / Col01 | 000C / 0001 | 4 / 0 / 0 | 1 | 8C / 21（接口共用） | Consumer 输入；缓存 CONFIRMED，物理源关系 UNKNOWN |
| MI_02 / Col02 | 0001 / 0080 | 2 / 0 / 0 | 2 | 8C / 21（接口共用） | System Control：81/82/83；缓存 CONFIRMED |
| MI_02 / Col03 | FFC0 / 0001 | 21 / 0 / 0 | 3 | 8C / 21（接口共用） | vendor 输入；缓存 CONFIRMED；RT Gate `03 76` 既有物理证据 CONFIRMED |
| MI_02 / Col04 | UNKNOWN / UNKNOWN | UNKNOWN | UNKNOWN | 8C / 21（接口共用） | TLC 存在 CONFIRMED；Raw Input type=Mouse CONFIRMED；Generic Desktop Mouse 角色 INFERRED，能力访问失败 |
| MI_03 / 单 TLC | 0001 / 0006 | 20 / 0 / 0 | 0（无显式 ID） | 8E / 19 | 标准 Keyboard 数字位图；缓存 CONFIRMED；NKRO 角色 INFERRED，物理身份 UNKNOWN |
| MI_04 / 单 TLC | 0059 / 0001 | 0 / 0 / 51 | 无 Input | 无 Interrupt IN | LampArray feature-only 能力 CONFIRMED；配置有 OUT 0F / 64，不是按键输入端点 |

MI_02 的各 TLC 共用接口端点，不能为每个 TLC 发明独立 endpoint。Col04 的 Report ID 4 是容易产生的猜测，本次没有可信能力证据，因此仍为 UNKNOWN。

Report ID 0 的 Windows HID report buffer 通常保留 0 前缀；历史 MI_00、MI_01、MI_03 的 USB max packet 分别为 8/64/19，而缓存 In 长度为 9/65/20，不能在总线 payload 前再寻找一个实际 00 ID。MI_02 的 IDs 1/2/3 是显式编号，RT Gate 21 字节真实 report 已含 03。RAWKEYBOARD 不提供 HID report framing。

## 历史配置描述符

| Interface | 声明 report descriptor 长度 | IN bInterval 原值 | OUT endpoint / packet / bInterval |
|---|---:|---:|---|
| MI_00 | 68 | 1 | 无 interrupt OUT；有缓存键盘 LED output 能力，未操作 |
| MI_01 | 34 | 1 | 0D / 64 / 4 |
| MI_02 | 182 | 1 | 无 |
| MI_03 | 23 | 4 | 无 |
| MI_04 | 327 | 无 IN | 0F / 64 / 4 |

配置总长度 141，alternate setting 均为 0，全部 HID class=3。`bcdUSB=0x0200` 不等于已确认 High Speed；不能仅由 `bInterval=4` 换算实际 8000 Hz/延迟，设备连接速度、模式及新的总线时序未验证。旧资料的“8000 Hz NKRO endpoint”只保留为历史描述，不升级为本次测量。

## MI_03 与 MI_00 缓存输入项

- MI_03：一个 Input button range，page 0007，usage `0000..0097`（0..151），Report ID 0，flags=0002，即 Data/Variable/Absolute。标准 usage 位图不是磁轴 Wire ID 矩阵。没有缓存 modifier `00E0..00E7` 项。
- MI_00：page 0007 modifier range `00E0..00E7`，flags=0002；另有 `0000..0098`，flags=0000，即 Data/Array/Absolute。不能把 array capability 的 usage 上限当成每份报告支持的键数。
- 不从 button capability 的 Reserved 区或版本相关 ReportCount 读取位偏移/完整布局。USB report 正文不齐，不编造可执行 MI_03 原始解析器。
- MI_01：Input value usage2 / Output value usage3，page FF00，8 bit × 64，Report ID 0。能力上允许输入，不证明它携带键状态。
- MI_02 Col03：page FFC0 usage2，Input button main flags=0，Report ID 3；这是容纳 vendor 数据的声明，不是 20 个独立键的证明。

## 候选输入通道证据表

| 候选 | 直接确认 | 推论 / 未知 | 判定 |
|---|---|---|---|
| 标准 MI_00 Keyboard | TLC、长度、标准 usage、修饰键 | Raw Input 可提供设备来源；固件处理后的输出角色 INFERRED；重映射前身份 UNKNOWN | 可研究来源过滤，不能声称完整物理源 |
| MI_03 NKRO 位图 | 标准 Keyboard TLC 与 usage 位图 | 持续输入、物理/逻辑阶段、与 MI_00 合并、实际 NKRO 上限 UNKNOWN | “MI_03 已验证 vendor matrix” DISPROVEN；是否可还原物理身份仍 UNKNOWN |
| MI_02 Col03 / 03 76 | 已有 RT Gate switch 通知及缓存 ID3/21 bytes | 该通知描述后部 RT 开关，不含按键身份 | 用 RT Gate 当全键状态 DISPROVEN |
| vendor 73 / 74 | HAL 分支 Wire 表反查、C8/D0 回调；FW58 条件调用 | MI_02 Col03 路由 INFERRED；当前实时 framing、使能条件、物理身份与全键 Press/Release UNKNOWN | 有价值的数字候选，未开放 parser |
| vendor 70 bitset | HAL p 分支转 `FUN_180020b80`；前5字节热键/配置表与 Profile 分支 | 有状态样式不能等同全键 bitmap | 将已审消费者解释为完整物理矩阵 DISPROVEN |
| MI_01 vendor input | 当前64字节数据能力；既有 echo/同步及历史 typing 负面结果 | 其它未知数字通知是否经此通道 UNKNOWN；无被动新数据 | 不发查询、不加载写 transport |
| Consumer/System/Mouse | 已枚举与部分标准 usage | 映射/宏源身份 UNKNOWN；Mouse caps 受限 | 不能据此重建全键身份 |
| LampArray | 无 Input，Feature51 | 不查询 feature，不涉及键状态 | 作为已声明 Interrupt IN 键源 DISPROVEN |

不要把 firmware command router 中 `0x73` 的 WinLock **命令**条目，与 HAL notification `0x73` 自动视为同一方向/通道/协议。方向、Report ID、接口及固件版本均需分别绑定。

## 可复现的元数据检查

```powershell
python -B tools/research/physical_key_state_metadata.py
python -B tools/research/physical_key_state_metadata.py --all-hid-cached
```

第一条仅枚举 Raw Input 缓存，不打开 HID 句柄。第二条显式取得共享零访问元数据句柄并立即释放；不会处理输入。任何失败保留 UNKNOWN，不提权、不独占、不更改 report queue。工具在测试前已独立审查，和 RT GateMonitor 没有输入注册或生命周期冲突。

依据：[HIDP_BUTTON_CAPS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidpi/ns-hidpi-_hidp_button_caps)、[HIDP_VALUE_CAPS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidpi/ns-hidpi-_hidp_value_caps)、[获取 preparsed data](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidsdi/nf-hidsdi-hidd_getpreparseddata)。这些 API 返回能力元数据；没有提供物理按键语义保证。
