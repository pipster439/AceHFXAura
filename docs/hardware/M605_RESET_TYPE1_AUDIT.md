# alpha.7 — `51 52 resetType 1` Actuation reset 只读审计

审计日期：2026-09-30。工作树基线 HEAD：`01786b1bdc368d6b9fc625cfbcf2eda8c8ac79f4`，保留进入本轮时全部未提交变更。

## 1. 结论与生产门槛

**静态证据支持 type 1 是 Actuation 专用的覆盖清理及 common/base 更新命令。证据尚未形成官方实发报文和物理行为闭环，不能加入生产写路径。**

- 它遍历所选 layer 的完整当前运行键记录表，写入给定 Actuation raw，并清除每键 Actuation override 的 bit 15。不是只处理 UI 当前选中的键。
- override flag 清除后，固件扫描逻辑确实读取当前固件 Profile 的 global/common Actuation，因此支持恢复 inheritance 的静态结论。
- 它同时更新固件当前 Profile 的 common/base Actuation；不是无参数的纯“删除标志”操作。官方传入当前全局值，不是固定出厂值。
- **“全部”有范围限制**：官方前端固定 `layer=0`；固件此值只清 bank 0。不能宣称它清理 bank 1、所有 layer、所有板载存档或其它固件版本。
- type 1 的 field mask 是 `1`，跳过 RT 与 Deadzone 重置分支；已追踪的命令和 helper 中未发现 DKS、SpeedTap、remap 配置重置路径。存在反馈/灯光缓存与通用待处理状态副作用，详见第 6 节。
- 未捕获新的 type 1 USB OUT 或 DEVICE_RX；没有点击官方重置，没有加载 DLL 执行 setter，没有发送实体报文。
- 本轮只新增本报告与隔离审计材料。未增加 builder、runtime API、NativeHid allowlist，未改 reconnect/quarantine 或 verified Deadzone type 4 路径。Actuation Profile 恢复路径仍按现有安全门槛阻止未验证 reset。

| 问题 | 当前回答 | 证据等级 |
| --- | --- | --- |
| 清除全部 per-key Actuation override flags/table？ | 对所选 layer 的 `5 × N` 当前配置记录逐条清 bit 15，保留记录本身及其它配置字段；官方 layer 0 不覆盖 bank 1 | 静态确认，物理待验 |
| 恢复 global Actuation inheritance？ | 扫描器在 flag=0 时从 current Profile common 字段取值；reset 自己也写这个 common 字段 | 静态确认，物理待验 |
| 精确报文布局？ | HAL 64-byte vendor buffer 和 65-byte host HID framing 已定位；type 1 的实际参数值与真实 OUT sequence 未捕获 | 静态布局确认 |
| 不影响 RT/DZ/DKS 等？ | 静态没有对应配置写入；不能以此替代运行设备的保留性验证 | 静态支持，物理待验 |
| 官方入口？ | “单键触发点设置：编辑”→“单键触发点设置”弹窗→“重置” | 前端与中文资源确认，未实际点击 |
| 可以被动 USB 抓包？ | 可对官方 UI 操作被动记录，但当前环境检查范围内无 USBPcap 工具/驱动；本轮未安装或执行捕获 | 捕获待准备 |

## 2. 输入身份与可复核性

下列 SHA-256 由本轮实际文件计算；Ghidra session 的占位 hash 不作二进制身份依据。

| 输入 | 身份 |
| --- | --- |
| 安装的 `View/7038/index.js` | `708e9049cb6fb61c489d47dcb15a0ff038b96ee1774aeeb57655ab7172a2eab4` |
| 安装的 `5744-bundle-f78f7.js` | `54147a0a9f9d8c329044fe5accf734a9096d2be35eef8ac250069e44acf43892` |
| `ArmouryKbSDK.dll` 3.0.97.0，x86 | `66bb6e08a116c7670e79649c6ac93cac97b8310ad7cea1a3ce2b1f5b00fe7d0d`；Ghidra 使用的历史副本与安装文件逐字节相同 |
| `drivers/AacKbHal_x64.dll` 1.3.46.0，x64 | `52d575bf942b7551b3f120c446bf0d853e36f9225c6b9a17407a80e0b1829f04` |
| 官方 `M605_V01_00_58.bin`，507904 bytes | `6d410ee0a54f640b4ab016cdb973f08e3d3d0ab7a716c7368167e562e0e19f1d` |
| `M605_1_00_58_core2_readonly.bin`，124756 bytes | `8fe68a13d7a0cd3bf8b4fd0dbe0575c0b7e67c1d4373e8d63970cf248684e668`；验证为上述完整文件从 `0x21000` 开始的精确切片，分析基址 `0x18000000` |

中文资源来自安装的 `resources/Simplified Chinese-ded15.xml`，其 hash 与全部文件路径见 [输入身份](../../audit_artifacts/alpha7_reset_type1/input_identities.json)。固件结论仅针对 1.00.58；历史 type 4 抓取报告中的 1.00.59 不构成 type 1 或版本等价证据。

## 3. 官方前端与 UI 调用链

`5744-bundle-f78f7.js` 的 literal `resetType:"1"` 位于字符 offset `91278`（解码后字符串偏移，不是文件 byte offset）。`Vt` 对话框中的 `L()` 回调执行：

```javascript
// 从已安装官方 bundle 摘录；本轮没有执行。
t = { resetType: "1" };
u.call((0, de.OP)(t));
```

`index.js` 导出 `OP → ie`，`ie` 即日志标识为 `resetActuationPreKey` 的 thunk（字符 offset `611939`）。完整函数已存入 [前端证据](../../audit_artifacts/alpha7_reset_type1/frontend_type1.json)。

1. 从 `buttons.analogTrigger.actuation` 取当前全局 raw `x`。
2. 克隆整个前端 keyList，把存在的 normal/toggle Actuation 属性改成 `x`，并更新 Redux UI 状态。
3. 生成 `{profileID: selectedProfile.id, data: {actuation:x, resetType:"1", layer:0}}`。
4. 向 `settings.url + "/actuation"`（另带 deviceSN/dongleSN query）发送 JSON PATCH，timeout 60000 ms。
5. 只有 live 模式 `t && !isEditMode` 才发送；离线编辑模式只改官方 UI 草稿。成功后刷新官方配置；失败时恢复先前前端模型。

明确入口：当前设备的触发点/模拟设置区域，“单键触发点设置：”旁“编辑”按钮 `row_251` 打开 `Vt`；弹窗标题 `row_2107` 为“单键触发点设置”；其 **“重置”按钮 `row_78`** 调用 `L()`。该按钮不受是否有已选键限制，selection list 也没有加入 reset payload。

“取消全选”`row_2112` 只清 UI selection；“完成”`row_352` 关闭弹窗。它们不是 type 1 触发入口。整体 Analog Reset 的 type 3、Deadzone 弹窗的 type 4 也不是本次入口。

官方资源 `row_2101` 还明确写“全局设置将排除以下按键：”，支持 global Actuation setter 不等于清除 per-key overrides 的既有物理发现。

**调用链剩余动态边界**：本轮定位了前端 JSON、SDK XML parser/dispatch 与 x64 HAL 的同一 FunctionID/参数契约；没有录到本次 REST→XML→HAL 的运行调用栈。SDK 是 x86、目标 HAL 是 x64，不能把这两份静态程序当成已经证明的同进程直接函数调用。官方中间进程/代理如何桥接、是否在 reset 前后另外选择固件 Profile 或提交 Apply，仍需运行时或完整 USB sequence 关联。

## 4. SDK → HAL → host HID framing

SDK `ExecuteFunction` 在 `0x1000A2A8` 检查 selector `0xCE`，`0x1000A2E7` 调用 `FUN_10044DE0`。它调用 XML parser `FUN_1004E810`（caller `0x10044EEF`）。

| SDK 解析结构偏移 | XML 元素 | HAL 七个 dword 参数 |
| --- | --- | --- |
| `0x210` | **`type`** | `[0] resetType` |
| `0x214` | `actuation` | `[1] actuation raw` |
| `0x218` | `rapid_trigger` | `[2] RT raw` |
| `0x21C` | `layer`，缺省 0 | `[3] layer` |
| `0x220` | `dead_zone`，缺省 -1 | `[4] single DZ` |
| `0x224` | `dead_zone_top`，缺省 -1 | `[5] top DZ` |
| `0x228` | `dead_zone_bottom`，缺省 -1 | `[6] bottom DZ` |

`DAT_101843F4` 实际 ASCII 为 `type\0`，不能把 JSON 名称 `resetType` 当成 SDK XML 元素。此前文档使用 `resettype` 的概括不精确，本报告以实际 parser 为准，不修改上一轮文件。

SDK `0x10044EF4..0x10044F6E` 把这七个 dword 复制到连续参数块，push 该参数块及 FunctionID `0x2B`，通过 vtable `+0x0C` 分派。反编译器在这里丢失了部分参数，应以归档汇编为准。

HAL `FUN_18008BF70` 的 `param_2 == 0x2B` 分支调用 `FUN_180022100`：

- 零初始化 64-byte vendor buffer；写 header `51 52` 与小端 type word `01 00`。
- type 1 分支 `0x1800221A0..0x1800221AC` 只从参数 `[1]` 取低 byte，写 vendor offset 4；**不取 RT 参数**。
- 公共尾部 `0x18002221A` 取 layer 参数 `[3]` 低 byte，写 vendor offset 5。
- 通过 transport vtable `+8` 提交 `0x40` bytes。

`KbControl` vtable 基址 `0x180151088`，`+8` 指向 `FUN_180014110`。该函数从实例 `+0x1088` 读取 ReportID，前置到 vendor buffer，然后 `WriteFile(..., 0x41, ...)`。ReportID 来自枚举/constructor 参数，不是 reset 函数的任意新常量。历史同设备官方 type 4 echo 的 host ReportID 为 0；**本次 type 1 尚无实发证据确认该运行时 transport 实例**。

### 完整 65-byte host HID 布局（静态候选，不是捕获 packet）

| Host byte（含 ReportID） | Vendor offset | 值与含义 |
| --- | --- | --- |
| 0 | 不属于 vendor payload | ReportID；既有 M605 路径/历史 echo 为 `00`，本次实发待捕获 |
| 1 | 0 | `51` vendor command family |
| 2 | 1 | `52` reset 子命令 |
| 3 | 2 | `01` resetType low byte；1.00.58 handler 用此 byte 分派 |
| 4 | 3 | `00` resetType high byte；HAL 生成的 type word 高 byte |
| 5 | 4 | 当前 global/common Actuation raw；官方 Slider 为 1–40，即 0.1–4.0 mm，以 raw/10 显示 |
| 6 | 5 | layer；官方明确传 `00` |
| **7–64，每个 byte** | **6–63，每个 byte** | HAL 零初始化后未写入，全部 `00`；本 type 没有 RT、DZ、logical key ID、Profile GUID 参数 |

示意：`00 51 52 01 00 [Actuation_raw] 00 [58 个 00]`。这里的 raw 必须来自可信 host desired/common 值；不猜成 0、10 或“出厂默认”。firmware 存储只使用低 7 bits，不能从它缺少严格 range check 推导可允许其它输入。

65 bytes 指 host `WriteFile` buffer，不能未经检查 USB descriptor 就认定 USBPcap 总线数据也包含同一个 ReportID prefix。应保留原始 bus payload，并据实际 report framing 归一化比较。

HAL reset function 自己没有额外 `50 55`；它有官方反馈状态 polling（Sleep 10 ms，约 1500 ms 上限）、timeout 与 FF AA 错误处理。本轮未更改任何 settle。是否由官方外层补发 Apply/其它操作、实际先后顺序和报文间隔，必须捕获整段 sequence，不能仅依据函数内没有 Apply 就删掉 Aura 已验证的 transaction gate。

## 5. firmware 1.00.58 处理链与 inheritance

原始 Thumb bytes 通过 Capstone 逐地址复核；helper 用独立 scratch Ghidra project 反编译。range dump 中可能有 literal pool，不能将线性反汇编的每行都当执行路径。以下路径使用明确跳转目标，而非给 handler 中间位置伪造独立 C 函数。

| 地址 | 可复核作用 |
| --- | --- |
| `0x18001FBE` / `0x18001FD6` | 通用命令 parser 入口；`r6=1` |
| `0x1800201C..2020` | vendor family `51` 跳至 `0x1800248E` |
| `0x180024EC`，table `0x180024F0` | `52 - 50 = 2` 的 table entry 跳到 `0x180026E8`，再跳 `0x18002C1A` |
| `0x18002C1A..2C24` | reset type low byte 分派；type 1 entry 指向 `0x18002C38` |
| `0x18002C38..2C48` | Actuation 参数 offset=0，field mask=1；layer 参数 offset=1 |
| `0x18002C6C..2C9E` | 读取 vendor offset 5；layer 0→mask 1，1→mask 2，2→mask 3，其它值错误返回 |
| `0x18002C8A..2C98` / `2DC0..2DCE` | `N` 来自配置计数；循环全部 `5 × N` records（不是 UI 的 68 个 logical key 清单） |
| `0x18002CAC..2CC2` | bank 0 的每个 record `+8`：写 raw 低 7 bits，`BIC #0x8000` 清 override flag |
| `0x18002CCC..2CE8` | bank 1 的对应字段 `+0xD8C`；只有 layer mask 包含 bit 1 才执行 |
| `0x18002DD8..2DF0` | 更新 `0x18024EE0 + currentProfileIndex*4` 的 bits 9–15，并调用 `FUN_18006D10(2,0)` |
| `0x18002DF8..2DFC`、`2EB8..2EBC` | type 1 跳过 RT post-processing 与 DZ common 更新 |
| `0x18002EEE..2EF8` | 构造 reset echo，进入公共响应函数 `0x18000A70` |

Actuation 配置 bank 基址 `0x180202AC`；record stride `0x20`；另一 bank 位移 `0xD84`。清理表达式为 `newWord = ((oldWord & ~0x7F) | (raw & 0x7F)) & ~0x8000`，保留 bits 7–14。它清 override **flag** 并填入 common raw，既不是删除整条键记录，也不是清 RT word。

独立消费者证据：扫描器 `FUN_18004A7E` 的 `0x18004B18..4B30` 读取当前 bank record `+8`：

- bit 15 为 1：取该键低 7 bits。
- bit 15 为 0：取当前固件 Profile 的 common word bits 9–15（`0x18024EE0`，index 来自 `0x1801E6A4+6`）。

因此 inheritance 恢复有实际扫描分支的静态支持，不是仅从 reset 函数名称推断。当前 hardware Profile index 的 common 被写；此命令没有循环清其它 common/Profile slot，也不承载 Aura stable GUID。两个 layer bank 与板载 Profile slots 是不同维度，不能混用。

**官方 host 模型与固件范围的差异**：前端会把 normal/toggle 属性都改成 common，但所发 layer 固定为 0；固件此命令只清 bank 0。前端显示被恢复不证明另一个 bank 已恢复。未来必须明确产品管理的 layer，或先取得其它 layer 的官方 packet 与行为证据；本轮不试发 layer 1/2。

## 6. RT / Deadzone / DKS / SpeedTap / remap 与其它副作用

| 字段或行为 | type 1 静态路径 |
| --- | --- |
| RT master / press / release / separate / top-bottom | field mask 1 不进入 `2CEC..2D42` 的 RT word 写入，也不进入 `2DFE..2EB6` 的 RT 恢复逻辑；common 更新只替换 bits 9–15，其余 bits 保留 |
| Deadzone top/bottom 与 override flags | mask 1 不进入 `2D42..2DC0` 的 DZ 清表或 `2EBE..2EEA` 的 DZ common/helper 更新 |
| DKS enable/type、阈值与 event/action records | 不写 record `+4`、DKS 阈值/action 区域；不调用 DKS Standard/Reset 路径。Actuation 字段仍会对所有记录清理，不因某键是 DKS 就跳过 |
| SpeedTap | 不进入 SpeedTap `51 56/57` 配置分支，已追踪 helper 不写其配置表 |
| Remap / macro / logical key mapping | 不进入这些 command branches；官方前端修改的是 Actuation 属性，不是 remap bindings |
| 其它 Actuation bits | record bits 7–14 保留；common 的其它 bits 保留 |
| 调节反馈 / 灯光缓存 | `FUN_18006D10(2,0)` 可调用 `FUN_1800CE1E`，刷新键反馈 buffer；helper `FUN_18008F9C` 清反馈缓存。特定运行模式还会尾调用 `FUN_1800D3B0` 写 feedback/color buffer。不能宣称零灯光副作用 |
| 通用待处理状态 | parser 前置 `FUN_1800FC16` 在配置 magic 条件成立且 vendor family 为 `51` 时将 `0x18026778` OR 4；这是共享待处理标志，不能断言该操作完全不影响后续保存/刷新调度 |

`6D10` 的参数 2 跳过“增加/减少 Actuation”和 mode-switch 分支；参数 0 避免普通反馈尾部条件的扩展操作。部分 Ghidra helper 包含 unreachable-block 警告与 shared-tail 合并；原始汇编、调用参数及 bit mask 是主证据。上述配置保留结论限于已定位的 1.00.58 静态路径，**不等同真实设备 RT/DZ/DKS preservation PASS**，也不承诺 NVM/power-cycle persistence、调节模式、正在按键时的瞬时行为完全不变。

## 7. 历史捕获与本轮只读 USB 捕获可行性

[扫描记录](../../audit_artifacts/alpha7_reset_type1/historical_capture_scan.json) 对 12 份相关历史 event JSONL 的 `raw_hex` 实际记录进行检查：只发现 `00 51 52 04 00 ...`，**type 1 为 0**。扫描目录为 `hall_protocol_*`、`alpha6-phase4*`、`analog_lighting_*`；另一个 JSONL 是 ACL recovery 日志，不是 HID capture。`audit_artifacts` 内未发现 `.pcap/.pcapng`。

历史 Stage 5A type 4 样本是 DEVICE_RX echo，不能称为完整 USB HOST_OUT。没有用文档中的候选 hex、脚本生成的 packet 或测试 fixture 冒充捕获。

当前安装 Armoury frontend 与官方后台进程存在；**现有官方软件提供明确 type 1 UI 入口，适合后续被动捕获**。但本轮 inventory 检查中：USBPcapCMD/tshark/dumpcap 均不在 PATH，常见安装路径和 USBPcap.sys 不存在，Win32_SystemDriver 未返回 USBPcap driver；现有 x64dbg session 列表为空。这不是全盘软件不存在的保证，而是本轮工具准备不足的具体证据。未安装 kernel driver，未注入/attach 官方进程，未启动捕获。

“只读”指采集者不提交 HID writes。**点击官方“重置”本身会修改硬件**；不能借“被动抓包”名称把它当成纯读查询。本轮未执行该操作。

### 后续一次受控官方捕获需包含

1. 先记录实际设备固件版本、当前板载 Profile/layer、官方 global Actuation、已知 per-key override，以及 RT/DZ/DKS/SpeedTap/remap 的可恢复配置；由用户确认有可恢复值后操作。
2. 使用可用 USBPcap 工具定位 `VID 0B05 / PID 1B7E` 的 USB controller、device address 和接口；采集 OUT、IN 与完整时间戳，保留 descriptor/report framing。停止 Aura 的自有写入参与，保留官方软件产生的完整 sequence。
3. 捕获窗口中，由用户在 live 模式进入上述“单键触发点设置”弹窗，点击一次“重置”；采集者只监听，不调用 SDK/自定义 setter、不重放 packet。
4. 保留点击前后若干秒，包括可能的 `51 52`、`50 55`、echo、Profile/layer selection、配置刷新与其它 write；不能只导出筛选后的单包而丢失前置事务。
5. 将真实 host-view/USB-view 对齐，检查 type、raw、layer、每个 reserved byte、接口、顺序与 timing；做一组已知不同 common 值的复核，以排除固定默认值误判。
6. 验证外部软件留下的 M/W per-key override 清理后随 global/common 再次变化；检查未选中键、RT press/release/master/separate、DZ top/bottom、DKS、SpeedTap、remap 保留。其它 layer 和 power-cycle 另列验证，不能附带宣称通过。

捕获工具可用不等于已取得报文；HOST_OUT 符合布局也不等于物理 inheritance 已验证。

## 8. 尚缺证据与后续生产评审

当前明确缺少：

- 官方 type 1 的真实完整 OUT/IN capture 与执行顺序；尤其 ReportID framing、common raw 来源、reserved bytes、是否额外 Apply/选择板载 Profile。
- 本次运行 REST JSON→SDK XML→实际 HAL/module/interface 的关联；x86 SDK 与 x64 HAL 之间的 bridge 尚未动态证明。
- 与受审计 firmware 1.00.58 对应的物理 inheritance 恢复和其它磁轴/键功能保留；其它固件版本的独立验证。
- 官方 `layer=0` 与产品需要管理的 layer/bank 范围是否吻合。不能静态支持 layer 2 就扩大生产能力。

在上述证据闭环后，先提交 packet/sequence/行为证据供用户审核，再设计范围明确的 typed Actuation reset。它必须显式携带 common desired value，区分固件 common 写入与 durable host baseline persistence；shadow 清表范围只能匹配验证过的 session/layer。静态看 reset 本身已更新 common，有进一步合并 operation 的可能，但本轮不修改 planner、不省略任何已验证 gate，也不创建 typed builder。

持久 quarantine 不因这份审计解除。不得通过 type 1、type 0 或 app restart 擅自清 safety latch。verified Deadzone type 4 保持现有路径。

## 9. 验证与交付

**PASS — software/static/mock**：

- [静态检查](../../audit_artifacts/alpha7_reset_type1/static_verification.json) **33/33**：二进制 hash/切片、副本同一性、两级 opcode/type dispatch、field mask、bank 写入、common 更新、真实扫描器 inheritance 分支、前端 UI 值/入口、SDK tag、transport vtable。另含 262144 个 word/值组合的派生 bitfield 代数检查；这是从指令导出的算术核验，不是独立 firmware emulation 或硬件证据。
- `cmake --build build --config Release --target test_m605_protocol test_magnetic_control test_device_profile_runtime` 成功。
- `ctest --test-dir build -C Release --output-on-failure -R '^(m605_protocol|magnetic_control|device_profile_runtime)$'`：**3/3 PASS**，总时间 1.21 s。验证现有 quarantine/session、verified reset 与 Profile safety regressions；没有加入 type 1 生产测试接口。
- [工作树完整性](../../audit_artifacts/alpha7_reset_type1/worktree_integrity.json) 比较进入审计时的既有 tracked/nonignored 文件 SHA-256：既有源码、测试、配置及文档保持原样。本轮新增仅本文件与忽略目录中的证据/工具。

**NEEDS HARDWARE VALIDATION**：官方 type 1 packet capture、inheritance 清理、其它配置保留、bank/layer 范围、Apply timing、power-cycle 行为。

**BLOCKED BY UNVERIFIED PROTOCOL**：type 1 生产 builder/runtime/allowlist。当前不是 unknown packet guessing，而是已知静态布局尚缺官方实发及物理闭环；生产依然 fail closed。

本轮为只读协议审计和文档新增，未重新运行完整 CTest、Aura.Tests、WinUI Release 或 frontend suites；未用上一轮测试结果冒充本轮执行。

证据目录：`audit_artifacts/alpha7_reset_type1/`。包含 frontend、SDK/HAL 原始 MCP 输出、firmware 指令/反编译、历史 capture 扫描、inventory、hash manifest、静态检查及测试日志。独立 Ghidra scratch session 已关闭，原有 HAL/SDK session 未关闭。审计 scripts 不打开 HID、不调用 vendor DLL setter。

随附 patch **只新增本审计文档**，不是累计 alpha.7 工作树 patch，不包含 reconnect、Deadzone、Profile 或其它已有变更。
