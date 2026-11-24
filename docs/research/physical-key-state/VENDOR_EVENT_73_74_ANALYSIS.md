# Vendor Event 0x73 / 0x74 — Static Analysis

日期2026-10-09。**STATIC ONLY / INCONCLUSIVE**；没有真实73/74报文。本轮只沿既有固件/HAL入口做有界追踪，没有重启Hall ADC项目，没有执行厂商代码、打开设备或发送查询。

## 映像与证据约束

固件本地文件 `audit_artifacts/alpha6-phase4-3/M605_1_00_58_core2_readonly.bin`，SHA256 `8fe68a13d7a0cd3bf8b4fd0dbe0575c0b7e67c1d4373e8d63970cf248684e668`，124756 bytes，Thumb基址18000000。HAL `drivers/AacKbHal_x64.dll`，SHA256 `52d575bf942b7551b3f120c446bf0d853e36f9225c6b9a17407a80e0b1829f04`，x64 PE。当前重算一致；Ghidra读出的44C0有界字节与该固件逐字节一致，HAL虚表由本地PE核验。Ghidra session/project的空image hash不作为映像身份。

既有 `fw_scan_gate.txt`、`fw_event_emit.txt`、HAL notification/transport归档继续有效但须保留反编译不确定性。1.00.58静态分析不能无条件外推到历史实体1.00.59或未查询的当前Profile/固件。新增可复现脚本、关键反汇编及出处hash在本轮静态证据中。

## 产生条件：未证明普通Down/Up

`FUN_18004a7e`遍历传感/键表，并按per-key配置结构、阈值、状态和计数分支。73/74的六个调用点由历史反编译与xref共同定位：

| Call site | 内部事件 | 直接条件 / 证据等级 |
|---|---|---|
| 18004E1C | 73, key-byte, 1 | 阶段状态低nibble=1；对应计数=0；`_DAT_18005208[1] !=0`；STATIC CONFIRMED |
| 18004FC8 | 73, key-byte, 2 | 阶段=2，同类计数/非零门控；STATIC CONFIRMED |
| 1800514C | 73, key-byte, 3 | 阶段=3，同类条件；STATIC CONFIRMED |
| 18005304 | 73, key-byte, 4 | 阶段=4；另一per-key计数=0；`_DAT_18005678[1] !=0`；STATIC CONFIRMED，反编译r12恢复有警告 |
| 180053C6 | 74, key-byte, 0 | per-key类型cVar1=2、传感值<6、计时阈值达到分支；STATIC，变量实际语义UNKNOWN |
| 180054F4 | 74, key-byte, opaque | 同一类型分支的不同状态/配置条件；可见参数2路径，但参数全域与feature命名未闭合 |

四阶段、阈值、计时与动作列表是DKS/特殊功能的 **INFERRED** 候选；没有官方语义名称、活动配置或实际报文，不能标为“DKS已确认”。这些调用不在所有普通键的无条件make/break出口；`73=Down/74=Up`、持续监听默认开启、覆盖68键皆UNKNOWN。门控可能是feature使能、计数、连接或其它条件，不能把“非零”自动称为订阅开关。

本轮没有找齐门控字段写入者，也没有证明某个具体HID命令能使能73/74。若后续静态资料发现配置/主动命令线索，只记录为待审，当前被动授权不允许尝试。零A报告与“当前配置未满足条件”相容，但没有因果证明。

## 内部封装与RT对照

`0x180044C0`不是已定义Ghidra函数，本地有界Thumb代码直接确认：

| 内部4字节record offset | 写入 |
|---|---|
| 0 | 事件marker |
| 1 | 传入key值低字节 |
| 2 | mode0写key>>8；mode1写9F；其它mode此路径未写入 |
| 3 | opaque阶段/参数 |

它写到literal `18004560 → 18023EA8` 的4字节slot，控制块 `18004514 → 1801E708` 的+70/+74游标，环形slot范围4B。**STATIC CONFIRMED的是内部记录，不是USB Report ID/长度/endpoint。** mode1的9F与配置Fn标识相容，却不能推广为普通物理key标识；其它mode未写高字节会留下旧内容的风险也不能当成已发生的USB故障。

RT producer是 **1800687E..180068E6**：GPIO函数读取0x47，经反相、bit5状态比较与计数防抖，更新配置bit20，再以R0=76、R1=0、R2=0、R3=状态调用18004498。旧 `fw_switch.txt` 的18006800属于另一个GPIO0x46路径，不能单独拿它证明RT producer。本轮新增有界字节证据纠正了这一定位；未改旧报告。

18004498清零21字节buffer，将参数写到offset1..4，再branch到18004362拷贝到21字节队列（+14/+18游标、3slot）。这条RT封装与73/74四字节ring不同。RT封装函数自身不写offset0=03，最终编号/USB route需依赖已有物理通知与拓扑，不能由此函数自行推定。

试图decompile18004362实际返回包含它的FUN18004498；47F8、6196、FC3E无已定义function而失败。没有创建function/重新分析数据库；改用本地边界反汇编，未把最近函数名当请求地址。部分Thumb区域含literal/table数据，解码器会把它们显示成指令；未据此形成语义结论。队列consumer、最终USB提交、73/74实际Report封装仍 **UNKNOWN**。xref查不到18023EA8的reader也不证明没有reader：间接寻址/基址偏移不必有直接xref。

## HAL callback与新增传输链证据

`FUN_18001fc00`收到的param2[0]为marker；73/74从param2+1复制临时数据，以低16位反查 `DAT_1801CAEF0`（最多4B×8项），编码表位置并调用+ C8 / + D0 callback，附带第三数据字节。映射与opaque参数处理为 **STATIC CONFIRMED**；表大小不是68键覆盖证据，配置Wire ID不是事件物理ID证明，callback地址偏移不是Down/Up语义证明。

本轮有界追踪补充：

1. Reader `FUN_18001f070`只在内部日志使用`ReadEvent_interface3`；取`*(param1+8)+1070`的传输对象，调用虚表+30，传入0x40和FUN1FC00。
2. 本地PE虚表基址180151088，slot+30在1801510B8，目标 **180014BC0**。它ReadFile请求param3+1、去掉首字节、把固定param3字节送callback。M605传0x40，因此请求buffer65，callback视图64；函数未把实际returned length推广成固定USB长度。没有执行ReadFile。
3. 包装对象由FUN180086DB0创建；180086E68把选中传输对象存入wrapper+1070。它向FUN1800126B0传mode，再由1800127E6 `MOV R9D,ESI`、1800127F0 call传到FUN180012060。该callee按mode0筛FF00、mode1筛FFC0。反编译FUN126B0遗漏参数，不能以伪签名说只有一个参数。
4. FUN180080A70在180081C47传mode0、180081D1A传mode1调用包装factory；两种对象都存在。当前M605 reader的param1+8究竟选哪个对象，及该对象对应具体Windows路径/endpoint，仍未闭合。

这确认了“接口3/64字节”属于内部reader命名与buffer策略，**不能**直接指认MI_03、MI_02或MI_01。去掉首字节支持观察器的marker-offset1作为候选探针，但仍不足以确认当前73/74 USB framing；较大读取buffer也不能单独证明设备的report一定65字节。

## Endpoint、Collection、Report证据表

| 事件 | Interface / TLC / Endpoint | Report | 等级 |
|---|---|---|---|
| RT76 | 历史MI_02 Col03，FFC0/1；历史配置IN8C/max21 | 03 76 00 00 state +16zeros，21bytes | 历史物理RT报告已验证；本轮NOT RUN；端点数值继承配置元数据 |
| 73/74 | **UNKNOWN** | 内部4byte；HAL去首byte后分发；实际USB ID/length UNKNOWN | STATIC partial chain，不能映射interface名称 |
| control候选 | 历史MI_01 FF00/1、IN85/max64 | 缓存Input65/ID0；RAWHID真实prefix/length未知 | 历史缓存与本轮probe设计；A无报告 |
| notifications候选 | 历史MI_02 Col03 FFC0/1、IN8C/max21 | 缓存ID3/21；73/74来源未知 | 与已知RT route相同只是候选，不是73/74证明 |

## 对物理Provider的影响

没有新物理报文、Key ID或释放证据。73/74更适合作为条件性阶段事件候选，当前不能用于Reactive/Ripple全键状态；没有snapshot、序号或丢失恢复保证。Copilot单键与真实Win/Shift、DKS/SpeedTap/重映射、Fn和68键覆盖继续UNKNOWN。需要先RT投递正控，再以实体反例验证候选是否保持物理来源；时间相关性、配置ID相同和普通A/W/S/D均不构成物理身份验收。
