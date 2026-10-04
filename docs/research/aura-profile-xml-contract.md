# Aura profile XML contract — M2.10

本文件记录安装版 ASUS caller 实际构造的最小格式，**不是写接口规范或可执行 payload**。
物理寻址、完整参数范围和恢复契约仍未成立。最终决策为
`SERVICE_MEDIATOR_WRITE_BACKEND_REJECTED`，保留 ServiceMediator 只读发现。

证据：`audit_artifacts/phase3-m2.10/profile-xml-contract.json`、`configuration-snapshot.json`。
调用链见 [lifecycle](aura-profile-lifecycle.md)，恢复见 [restore analysis](aura-profile-restore-analysis.md)。

## 证据边界

| 模块 | FileVersion | SHA-256 |
|---|---|---|
| ArmouryCrate.AuraPlugin.dll，x64，base 0x180000000 | 6.5.10.0 | 88eefb3e5116c070fa5ece2b709ad1162e95d863c3d6f4b182f1dac6be464751 |
| LightingService.exe，x86，base 0x400000 | 3.10.12.0 | a711b9776ac0279256d22389478d44e3a51a1909c75a3bf03158cf9d5b91d3db |

本次重新核对安装文件版本/hash，与 M2.9 一致。只分析这两个模块中已定位调用链；未执行
vendor 方法、修改二进制或读取进程内存。以下地址均为 **RVA**，不混用两个模块的 image base。
CONFIRMED_STATIC 表示机器码/反编译支持的构造或分支；不代表已完成 live write 验证。

## 实际构造路径

AuraPlugin `0x234C40` 从当前 helper 对象及全局 LED vector 新建 XML 文档，序列化、转换为宽字符串，
向 `0x24A5E0` 传入常量 `"1"` 和这份 XML。旁路还把生成文档写入 plugin 配置目录下的
`AuraDlgSetProfile.xml`；这不是先读取上一份 XML 再执行回滚。helper 的 effect state 已在上游更新。

`"1"` 的来源是 UTF-16 常量 RVA `0x3DF5C4`。相同值用于普通 Group 更新、入组及重置请求。
服务 `0x2D9340` 转到共享 manager `0x2C1F20`、解析 XML 的 `0x2CFAD0`。
因此 **可确认它不是 caller 为九个 runtime records 分配的不同唯一值**；其完整协议角色仍为
UNKNOWN，不将其命名为物理设备 ID、锁 ID 或事务 ID。

## 最小已见格式

下面列的是节点路径和数据来源，不提供一份可直接用于写入的完整 XML。

| 路径（相对 root） | 已见值/来源 | 限制 |
|---|---|---|
| header、version | `ASUS_AURA`、普通 builder `1.1` | 固定 reset XML 则为 `1.0`；saved status/profile 为 `1.2`，不是同一文档版本 |
| funcid | helper +0x00 字符串；V1_ENGINE 上游设为 `4` | command 路由，不是 effect 或设备索引 |
| device/@key | 普通 builder `Group` | 整体同步逻辑集合，不能当作一个物理设备 |
| device/isenabled | helper +0x20 字符串 | 开关语义；无瞬态保证 |
| device/scene/state | 普通 builder `S0` | saved profile 另有 scene/@key、Non-S0；勿自行补全 builder |
| scene/mode/@key | helper +0x40 整数转十进制字符串 | 完整 effect enum、支持范围和 safe subset 未恢复 |
| mode/iscolorsynced、singlecolor | builder `0`、helper flag 生成值 | 不能据名称推导每个物理灯的同步关系 |
| mode/color_type | `Plain` / `Gradient` 分支 | 此格式不是 SetLedMatrix 的 UI4 packing |
| mode/start_end_color_cycle_start/end/range | builder `0.000000` / `1.000000` / `1.000000` | 当前持久配置可有不同值，不能当默认恢复值 |
| mode/start_to_end_or_end_to_start | builder `1` | 数值到可见方向的完整映射 UNKNOWN |
| mode/effect_speed、music_index | helper 数值转字符串 | 没有证实全范围/单位 |
| mode/thermal_threshold_one/two、thermal_value_type | helper 数值；`na` / `Thermal` / `CPU_Loading` / `VGA_Thermal` 分支 | 仅静态字段识别；未执行传感器、fan 或控制调用 |
| mode/led/@key | 全局 vector 中的字符串，record stride 0x50 | 不等于 QueryAllDevice 的 index，也不等于物理 LED 位置 |
| led/color | builder scalar 字符串 | 未证实 packed RGB/alpha/channel 顺序 |
| led/hue、saturation、lightness | vector +0x40/+0x44/+0x48 float，以 `%f` 序列化 | 未证实全部范围或 setter 对实际硬件的转换规则 |
| led/speed、direction、hashue2、hue2 | builder scalar/flag 字符串 | 不能直接当作 RGB frame 元素 |

`DeviceName`、`type`、`effect_path_order` 在当前 saved status/profile 中存在；并非全部由上述 builder
写出。没有在普通 builder 中恢复到唯一 USB/PCI/serial、model、type、index 组合。
`AURA_Brightness` 在上游 `SetAppProfile` 的另一段操作中出现，不能宣称 `SetProfile` XML 已包含
完整亮度状态。`GetProfile(deviceId)` 在此版服务为 E_NOTIMPL。

入组 builder `0x236BA0/0x2373C0` 采用 `funcid=7`、`devicelist/device/@key` 及 `ingroup`。
它们修改同步集合，不能用来无副作用地选出单设备。固定重置请求 `0x13A550` 使用 funcid8、
Mainboard/resetall；不是普通效果更新的前置步骤或安全恢复步骤。

## XML selector → lookup → topology

服务 `0x2CE5A0` 遍历 XML device 节点，Group 的 S0 分支进入 `0x2CA0E0`；读取 enabled、scene、mode，
更新 currentMode 并转到 `0x2CCC00`。Mainboard 路径显式检查 `Mainboard_Master` 并获取相关对象集合。
这是实际解析分支，**不证明任意 led key 均有隔离的单灯写语义**。

| M2.9 runtime record | XML/config 候选 | lookup / correspondence | 置信度与缺口 |
|---|---|---|---|
| DRAM index0 | Group led25–40，DeviceName=GSkillDram | Group 解析；16 keys 为该 family 的配置关联 | STRONG family；单 DIMM 分界 UNKNOWN |
| DRAM index1 | 同一组 keys | 两个记录共享 family/model，不能分配25–32/33–40 | 单 record selector UNKNOWN |
| GPU/Vga 1 | Group led2–24，DeviceName=Vga | 配置23 entries 与 runtime23 slots 关联 | STRONG；逐 physical LED routing UNKNOWN |
| ROG STRIX LC III SERIES | Group led44，DeviceName=WaterCooler | 1配置 zone 对应 runtime4 slots | STRONG family；1→4 fan-out/物理范围 UNKNOWN |
| WDL keyboard | connecteddevice WDL_Keyboard，profile 无对应 LED | profile/status 只提供 family 状态 | 唯一写 selector UNKNOWN；不打开 LampArray |
| motherboard | Group led0/1，Mainboard section | Mainboard→Mainboard_Master 分支 CONFIRMED_STATIC | family STRONG；Back Plate1/2 物理连线 UNKNOWN |
| ARGB header index0 | Group led41–43，DeviceName=AddressableStrip | 3 keys 与3 records 的 family 关联 | individual index mapping UNKNOWN |
| ARGB header index1 | 同一组 keys | 无独立已证实 index→key 路由 | UNKNOWN |
| ARGB header index2 | 同一组 keys | capacity120/500 不证明附接长度 | UNKNOWN |

EXACT 仅适用于同一 XML 文件内的 key/DeviceName 记录和已见解析分支，不能提升为 EXACT physical
output。不按名称合并 WDL/Falchion，不借助 M605 HID 验证，不挑选整组或未知 ARGB strip 作为 S1。
九个 runtime records、519 slots、Group1122 和45显式 entries 的 M2.9 原始观察保留；它们不是
可互换的 payload length。Back Plate-1/2 的标签没有证明它们控制哪两路可见灯。

结论：最小构造格式已恢复，但**一个可隔离、唯一寻址的物理 target/zone 和精确临时 effect scope
尚未建立**。不把这份部分 schema 变成 AceHFXAura 的写 API。
