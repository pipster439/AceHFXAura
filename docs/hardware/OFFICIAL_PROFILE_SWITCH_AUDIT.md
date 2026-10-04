# Official Armoury Crate Magnetic Profile Switch Audit

2026-10-02 · Ace HFX firmware **1.00.59** capture · firmware **1.00.58** static analysis

## 1. 结论与证据边界

本次已保存的官方 **FIRMWARE_PROFILE A/B** 切换应分类为 **D — Device-side profile bank select**。官方发送短的 `51 00` 槽位选择，再发送查询/配套命令；确认的 A/B 序列最后有一次 `50 55`。切换时没有 Actuation、Deadzone、DKS 或 RT 逐项重写。用户确认设置随 A/B 切换恢复，清空 B 的 RT 列表后再次确认对照行为，物理 RT 开关为 ON。

- **OFFICIAL USB CAPTURE PASS**：两份完整 USBPcap；选择 A=6 / B=1 的字节、echo、Apply 和所有接口 OUT 序列。
- **STATIC CONFIRMED**：官方 frontend 切换接口、SDK Profile 参数解析、HAL 选择报文；1.00.58 固件 `51 00` 接收并进入 Profile 加载状态机。
- **USER PHYSICAL BEHAVIOR PASS（用户确认范围）**：请求中的 A/B 对照可以复现；用户没有分别提供键程测量、DKS 事件记录或物理完成时戳。
- **INFERRED**：1.00.59 内部实现沿用 1.00.58 的 bank 加载机制；USB + 官方 host Profile 类型 + 用户观察共同支持分类 D。
- **NOT VERIFIED**：1.00.59 固件逐条内部执行、NVM/power-cycle、其它槽位/层、官方 SOFTWARE_PROFILE 路径、隐藏 bank override flag 的具体存储布局。

本轮没有修改 DeviceProfileRuntime、planner、WinUI、P4B、M605、NativeHid allowlist、settle 或 quarantine；没有 replay、自定义 HID 写入、51 2C、51 53 放行、commit/push。新增代码只是离线文件解析器及其测试。

## 2. 环境和实际操作修正

目标 VID/PID `0B05:1B7E`，USBPcap4，**本次 bus=4 / address=7**，不是历史 address=10。每份 capture 由注入的设备与配置 descriptor 识别地址/接口；device descriptor `bcdDevice=0x0159`，与已确认 firmware 1.00.59 对照。

所有 Ace HFX interfaces 均保留：MI_00 IN81，MI_01 OUT0D/IN85，MI_02 IN8C，MI_03 IN8E，MI_04 OUT0F。没有仅筛选磁轴接口。USBPcap 1.5.4；完整 root capture 使用已安装程序的 `-d \\.\USBPcap4 -A --inject-descriptors -o <unique file>`，Ctrl+C 停止。Aura/daemon 已正常退出；只有官方软件负责设置写入。

**主抓包实际是 B→A→B**，而不是计划中的三次切换。用户原话：“已完成，初始配置是B，我先切换到A后切换到B”。随后确认名称 A/B 及“设置都能随配置文件切换”。

只读官方保存值发现：主抓包时 B 仍启用默认 **W/S/D/A**，统一 RT .2。因此不能将该抓包写成 W enabled→disabled。用户随后说明：“B是默认WASD键启用RT的，我现在全都取消了”。清空操作发生在两份抓包之间；没有混进主抓包。

补充 capture 在 B RT 列表为空后启动。用户按一次完整阶段做 A/B 对照并回复：“确认可以，RT开关开启”。实际线包选择序列为 **3→1→3→1→6→1**。槽位 3 名称/额外选择原因未确认，不能把它改标成 A；最后 6→1 是独立有效的 A→B 清 RT 对照。保留这些额外选择及没有 Apply 的序列，未删除异常或重做抓包。

## 3. Exact A/B 保存配置

配置来自只读官方 ASUS 保存文件，证据 `official-host-settings-after-b-rt-removal.json`，采样 08:23:09.837981 UTC、补充 capture 之前。只提取 A/B 的相关字段；**official saved host intent，不是 firmware readback**。未导出设备序列号、完整 remap/macros 或其它 Profile。

| 字段 | A (官方 id=6, FIRMWARE_PROFILE) | B (官方 id=1, FIRMWARE_PROFILE) |
|---|---|---|
| Common Actuation | raw10 = 1.0 mm | raw30 = 3.0 mm |
| A键 Actuation | raw20 = 2.0 mm | normal.actuation raw30 |
| Common DZ top/bottom | raw2 / raw2 = .2/.2 mm | raw1 / raw1 = .1/.1 mm |
| B键 DZ | raw5 / raw5 = .5/.5 mm | preKeyDeadZoneList 空 |
| RT enabled list（补充时） | 仅 W logical1793 | 空 |
| RT press/release | raw5 / raw15 = .5/1.5 mm | 保存 raw2/raw2；没有启用键 |
| RT separate editing / continuous | 1 / 0 | 0 / 0 |
| V DKS | trigger_type=3，四 slot | trigger_type=0，normal Standard |

A 的 V DKS 四项（共同 start=10/end=36）：slot1 function0 target1026 position3320；slot2 function1 target1536(LeftCtrl) position1000；slot3/4 function0 target1026 position0000。其完整精确 JSON 在 targeted host snapshot；未将未触发 slot 当成实际物理动作。

B 的 W/A/V/B 保存对象均为 normal、actuation30；这只证明官方保存值，不单凭 JSON 声称 firmware override flags 已清。主抓包时 B 的 RT enabled list 为 [1793,1794,769,1538]；该初次只读输出没有单独保存原 XML 快照，后续 correction notes 明确记录，未伪造旧文件 hash。

命名空间：W logical0x0701/1793 → wire0012；A logical0x0602/1538 → wire001F；V logical0x0402/1026 → wire0031；B logical0x0403/1027 → wire0032。**key1026 是 V，不是 W。**

## 4. 原始 capture 与 SHA-256

原始文件只读取，没有重命名或修改；完整 root capture 保留。派生 all-interface pcap 是额外文件。原始 capture 包含同 hub 其它设备，作为本地私有 evidence，勿提交 Git 或公开上传。

### 主抓包

路径：`F:\USBPcap\official_profile_switch_20261002_161152.pcap`

SHA-256：`baa2b3c7a2dd37b4cc513049d5a22c8e0d385bb1954ab57492847be52bbcce6d`

5,123,835 bytes；80,191 records；Ace HFX 73 records；实际 OUT 14。EOF 完整，snapshot truncation=0，目标 URB status error=0。classic pcap 不含 kernel drop 统计，不能宣称捕获绝对无丢包。

### B RT 清空后的补充抓包

路径：`F:\USBPcap\official_profile_switch_20261002_162310.pcap`

SHA-256：`5f9683bb40cbef206776d06323bbf534ecdaf43275876097bcf223fe62fb6ed1`

12,299,487 bytes；235,962 records；Ace HFX 374 records；实际 OUT 40。EOF 完整，snapshot truncation=0，目标 URB status error=0。classic pcap 不含 kernel drop 统计，不能宣称捕获绝对无丢包。

## 5. 全 OUT / opcode counts

次数按实际 target OUT submission 统计，不重复计 completion。EP0 的 setup+data 保留原格式；不把 `21 09` 误认成 vendor opcode。

| Opcode | 主抓包 | 补充抓包 | 角色 |
|---|---:|---:|---|
| `51 00` | 2 | 6 | Profile selection, captured + static |
| `51 50` | 0 | 0 | 磁轴配置命令，未观察到 |
| `51 4F` | 0 | 0 | 磁轴配置命令，未观察到 |
| `51 58` | 0 | 0 | 磁轴配置命令，未观察到 |
| `51 59` | 0 | 0 | 磁轴配置命令，未观察到 |
| `51 52` | 0 | 0 | 磁轴配置命令，未观察到 |
| `51 53` | 0 | 0 | 磁轴配置命令，未观察到 |
| `51 54` | 0 | 0 | 磁轴配置命令，未观察到 |
| `51 23` | 0 | 0 | 磁轴配置命令，未观察到 |
| `50 55` | 2 | 2 | Apply gate |
| `25 01` | 2 | 6 | 配套命令；详细作用 NOT VERIFIED |
| `12 03` | 2 | 6 | 配套查询候选；详细作用 NOT VERIFIED |
| `12 00` | 2 | 6 | 状态查询候选；回复有 Profile index correlation |
| `22 01` | 2 | 6 | 配套命令；详细作用 NOT VERIFIED |
| `12 12` | 2 | 6 | 配套查询候选；详细作用 NOT VERIFIED |
| `21 09` | 0 | 2 | EP0 HID SET_REPORT setup+data；独立 control traffic |

未知命令没有被过滤。补充中的 EP0 SET_REPORT：frame27632，08:23:26.925075Z，`21 09 00 02 00 00 01 00 03`；frame223020，08:25:02.320956Z，同 setup、data=01。setup 是8 bytes、data是1 byte，wIndex0；它们不在已确认 A/B 切换序列内。其 LED/state 意义不凭猜测命名。

## 6. Exact payload / echo / framing

64-byte USB vendor payload（没有把 Windows dummy ReportID 当成 USB byte）：

```text
A selection: 51 00 00 00 06 + 59 zero bytes
B selection: 51 00 00 00 01 + 59 zero bytes
Apply:       50 55          + 62 zero bytes
Companions:  25 01 / 12 03 / 12 00 / 22 01 / 12 12 + 62 zero bytes
```

Confirmed A/B ordering:

```text
51 00 <selector at byte4> → identical IN echo
25 01 → identical echo
12 03 → identical echo
12 00 → non-identical status reply
22 01 → identical echo
12 12 → non-identical reply
50 55 → identical IN echo
```

All vendor OUT = MI_01 EP0D，64 bytes；responses = MI_01 EP85，64 bytes。full hex、每个 raw frame、UTC(ns)、length、direction、interface、endpoint、inter-OUT delay、echo frame/delay 已保存在每份派生目录的 `timeline.json/csv`、`all_target_out.json`、`payload_sequence.txt`、`capture_analysis.json`。IN query response 没有冒充 identical echo。

主抓包 `12 00` reply：A `12 00 00 00 59 00 01 00 06 04 06` +53 zeros；B `12 00 00 00 59 00 01 00 06 02 01` +53 zeros。byte10 与6/1相关，但这些未知字段未建立 production parser/readback contract。`12 12` reply `12 12 00 00 01 06` +58 zeros。不能凭 query 名称声称全磁轴 readback。

## 7. A/B 时间线

以下全部 UTC。时间是 URB submission/completion，不是 UI click / physical settled。主抓包对应 B→A、A→B；补充最后两个选择对应 B→A、A→B。其余 selector3/1 单列，不声称物理结论。

### 主抓包

目标 selector 6，group 1：

| Raw frame | UTC | OUT prefix | 前一 OUT 后 ms | Echo frame / ms |
|---:|---|---|---:|---|
| 49347 | 2026-10-02T08:13:21.670469Z | `51 00 00 00 06` | — | 49386 / 89.404 |
| 49725 | 2026-10-02T08:13:21.839187Z | `25 01 00 00 00` | 168.718 | 49743 / 1.690 |
| 49898 | 2026-10-02T08:13:21.859434Z | `12 03 00 00 00` | 20.247 | 49912 / 1.458 |
| 50110 | 2026-10-02T08:13:21.885007Z | `12 00 00 00 00` | 25.573 | non-identical response; see timeline |
| 50237 | 2026-10-02T08:13:21.899690Z | `22 01 00 00 00` | 14.683 | 50251 / 1.203 |
| 50367 | 2026-10-02T08:13:21.914548Z | `12 12 00 00 00` | 14.858 | non-identical response; see timeline |
| 50699 | 2026-10-02T08:13:21.953191Z | `50 55 00 00 00` | 38.643 | 51603 / 107.733 |

Select→Apply OUT **282.722 ms**；select→final identical Apply echo **390.455 ms**。

目标 selector 1，group 2：

| Raw frame | UTC | OUT prefix | 前一 OUT 后 ms | Echo frame / ms |
|---:|---|---|---:|---|
| 63341 | 2026-10-02T08:13:34.071840Z | `51 00 00 00 01` | 12118.649 | 63375 / 89.324 |
| 63409 | 2026-10-02T08:13:34.254501Z | `25 01 00 00 00` | 182.661 | 63410 / 0.718 |
| 63417 | 2026-10-02T08:13:34.262226Z | `12 03 00 00 00` | 7.725 | 63420 / 0.936 |
| 63423 | 2026-10-02T08:13:34.264435Z | `12 00 00 00 00` | 2.209 | non-identical response; see timeline |
| 63427 | 2026-10-02T08:13:34.266439Z | `22 01 00 00 00` | 2.004 | 63430 / 0.722 |
| 63433 | 2026-10-02T08:13:34.268403Z | `12 12 00 00 00` | 1.964 | non-identical response; see timeline |
| 63447 | 2026-10-02T08:13:34.289770Z | `50 55 00 00 00` | 21.367 | 63487 / 103.397 |

Select→Apply OUT **217.930 ms**；select→final identical Apply echo **321.327 ms**。

### 补充已确认 A/B

目标 selector 6，group 5：

| Raw frame | UTC | OUT prefix | 前一 OUT 后 ms | Echo frame / ms |
|---:|---|---|---:|---|
| 157265 | 2026-10-02T08:24:31.397187Z | `51 00 00 00 06` | 16663.298 | 157324 / 89.182 |
| 157354 | 2026-10-02T08:24:31.568101Z | `25 01 00 00 00` | 170.914 | 157355 / 1.272 |
| 157360 | 2026-10-02T08:24:31.574982Z | `12 03 00 00 00` | 6.881 | 157361 / 1.388 |
| 157366 | 2026-10-02T08:24:31.577063Z | `12 00 00 00 00` | 2.081 | non-identical response; see timeline |
| 157370 | 2026-10-02T08:24:31.579049Z | `22 01 00 00 00` | 1.986 | 157371 / 1.321 |
| 157376 | 2026-10-02T08:24:31.581064Z | `12 12 00 00 00` | 2.015 | non-identical response; see timeline |
| 157387 | 2026-10-02T08:24:31.602957Z | `50 55 00 00 00` | 21.893 | 157431 / 106.417 |

Select→Apply OUT **205.770 ms**；select→final identical Apply echo **312.187 ms**。

目标 selector 1，group 6：

| Raw frame | UTC | OUT prefix | 前一 OUT 后 ms | Echo frame / ms |
|---:|---|---|---:|---|
| 207628 | 2026-10-02T08:24:50.422316Z | `51 00 00 00 01` | 18819.359 | 207904 / 118.936 |
| 208571 | 2026-10-02T08:24:50.629149Z | `25 01 00 00 00` | 206.833 | 208582 / 1.113 |
| 208649 | 2026-10-02T08:24:50.638026Z | `12 03 00 00 00` | 8.877 | 208661 / 1.237 |
| 208672 | 2026-10-02T08:24:50.640377Z | `12 00 00 00 00` | 2.351 | non-identical response; see timeline |
| 208694 | 2026-10-02T08:24:50.642388Z | `22 01 00 00 00` | 2.011 | 208703 / 0.882 |
| 208714 | 2026-10-02T08:24:50.644365Z | `12 12 00 00 00` | 1.977 | non-identical response; see timeline |
| 208924 | 2026-10-02T08:24:50.668648Z | `50 55 00 00 00` | 24.283 | 210158 / 230.605 |

Select→Apply OUT **246.332 ms**；select→final identical Apply echo **476.937 ms**。

### 补充的额外选择（不隐藏）

| group | selector | select frame | UTC | Apply observed |
|---:|---:|---:|---|---|
| 1 | 3 | 94970 | 2026-10-02T08:24:09.816241Z | False |
| 2 | 1 | 101072 | 2026-10-02T08:24:11.721508Z | False |
| 3 | 3 | 108257 | 2026-10-02T08:24:13.435899Z | False |
| 4 | 1 | 111721 | 2026-10-02T08:24:14.490933Z | False |
| 5 | 6 | 157265 | 2026-10-02T08:24:31.397187Z | True |
| 6 | 1 | 207628 | 2026-10-02T08:24:50.422316Z | True |

前四组同样包含选择和五条 companions，但没有在下一次选择前观察到 `50 55`。不能把最后的两个 Apply 分配给前四组、不能填补“应该存在”的 Apply，也不能据此擅自省掉 Aura Apply。其用户触发路径与槽位3身份尚未确认。

## 8. Interaction matrix / 各子系统结论

| Subsystem | A | B（RT清空后） | A→B official operation | B→A official operation |
|---|---|---|---|---|
| Common Actuation | 1.0 | 3.0 | select1 + companions + Apply | select6 + companions + Apply |
| A per-key Actuation | 2.0 | host normal3.0 | 同一次 bank选择，无51 4F/50/type1 | 同一次 bank选择，无51 4F |
| Common Deadzone | .2/.2 | .1/.1 | 同一次 bank选择，无51 58 | 同一次 bank选择 |
| B per-key Deadzone | .5/.5 | host无DZexception | 同一次 bank选择，无51 59/type4 | 同一次 bank选择 |
| W RT | enabled .5/1.5 | enabled列表空 | 同一次 bank选择，无51 54 enable0/51 53 | 同一次 bank选择，无51 54 enable1 |
| V DKS | four-slot DKS | Standard | 同一次 bank选择，无逐键 DKS Standard | 同一次 bank选择，无四slot重写 |

**Actuation**：没有 resetType1，也没有 global/per-key common重写。A override 与 B normal 保存值/行为是选择的 bank 内容，不是同一个正在被清表的 Profile。当前 capture 不能证明 B 从共同 override table 中清除了 A flag。

**Deadzone**：没有 resetType4 / 51 58 / 51 59。用户对照确认；不能把“切换到另一个 Profile 的不同表”说成“旧表已删除”。

**DKS**：V 变 Standard 的官方切换命令是 `51 00 ...01` 所代表的 bank选择，而非 switch path 逐键 Standard/reset。切回 A 同理。普通 DKS editor 四slot packet 与 bank restore 是不同操作路径；本次捕获没有编辑 DKS，不为重建旧 editor证据而再抓包。

**RT**：切 B 的确切 new evidence来自补充最后6→1且 B enabled列表为空。没有51 54 disable或51 53；不是RT common inheritance/reset，bank切换恢复另一个逐键enabled表。主抓包不能支持Wdisable（那时B仍WASD）。

**51 53**：两份 capture count=0，保持 unverified / blocked for production；all-key primitive 的历史 static 名称没有变成用户 global RT。

**Apply/batching**：确认的 A/B 切换每次1个 bank选择、5个 companion、1个Apply。不是68键materialization，也没有证明“多个51 54共用Apply”能在 Aura 通用生产路径直接采用。四组无Apply只如实记录，不扩大契约。

**性能**：确认 A/B 的 select→final echo跨度312.187–476.937ms。只代表USB可测区间；UI click→firstUSB与physicalsettled未测。官方效率主要来自设备已有bank的一次选择；没有证据授权改变Aura210/400ms安全settle，不能将echo等待推成transaction总安全延迟。

## 9. Fresh static chain

每次使用 Ghidra MCP read-only session；未运行 HAL/SDK 写路径、未动态调用驱动或写注册表。现有项目重新读取；本轮导出 fresh pseudocode/disassembly/xrefs。以下文件在本地 evidence 下。静态材料版本与实际 USB 1.00.59 明确分开。

| 输入 | SHA-256 |
|---|---|
| official 7038/index.js | 708e9049cb6fb61c489d47dcb15a0ff038b96ee1774aeeb57655ab7172a2eab4 |
| ArmouryKbSDK.dll | 66bb6e08a116c7670e79649c6ac93cac97b8310ad7cea1a3ce2b1f5b00fe7d0d |
| AacKbHal_x64.dll | 52d575bf942b7551b3f120c446bf0d853e36f9225c6b9a17407a80e0b1829f04 |
| 1.00.58 core2 image | 8fe68a13d7a0cd3bf8b4fd0dbe0575c0b7e67c1d4373e8d63970cf248684e668 |

### Frontend → SDK

7038/index.js，module3270，switchProfile offset685703：`PUT /profile/<id>`，body `{profileID:id, forceOverwrite:t}`，timeout60000。官方 saved config list将A6/B1标为FIRMWARE_PROFILE。旁边PATCH的软件Profile覆盖分支不同，本结论不推广到无限host软件Profile。

SDK ExecuteFunction `0x100075f0` FunctionID1 分支日志 `SDK_COMMON_KB_FUNC_ID_PROFILE` → `FUN_10029880` SetProfile → parser `FUN_1001d570` 读取 `profile_function`、`profile_index` integer → HAL virtualcall +0xC。源码片段、解析器、调用assembly分别在 `sdk-execute-profile-100075f0.*`、`sdk-set-profile-10029880.*`、`sdk-profile-parser.*`、`sdk-profile-call-asm.json`。

REST服务如何构造SDK XML、本机x86 SDK→x64 HAL桥接的运行时callstack本轮没有截获；不能声称已经动态逐跳跟踪。但两端Profile参数contract与官方USB布局一致。

### HAL → USB

M605 dispatcher `FUN_18008bf70` selector2委派其function object virtual +0x10；日志证明这是M605，保存于 `hal-m605-dispatch-current.*`。

Common base `FUN_180069030`（日志 AacKbFunction_Base SetProfile）读取参数数组：profile_function4 -> zero64buffer，在byte0写51、byte4写低8bit profile_index；profile_function3 -> 50 55；沿既有64byte transport提交。fresh Ghidra assembly开头bytes与当前DLL原字节核对一致。

注意：具体M605 function object vtable到base writer的完整解析本轮尚未完成，不将虚调用当成已捕获动态调用。中间文件 `hal-m605-profile-dispatch.*` 实际为 `FUN_180089280`，日志 AacM602，**不是M605证据**，在notes明确纠正并保留原文件以便审计。

Common base中的其它 reset分支、Sleep/poll、registry同步只作只读材料；未调用、更未用于Aura生产策略。

### USB → firmware1.00.58

`firmware-profile-dispatch.txt` fresh Thumb disassembly：51家族 subcommand0进入 `0x18002514`，读payload byte4；selector6映射internal0，范围检查后写pending状态。literal `0x18022c34 +0x84` = `0x18022cb8`，state=2、targetindex写22cb9。这是Profileload请求，不是磁轴逐键command。

`FUN_18000d56` Profile状态机：DAT_18000e7c实际指向22cb8；case2消费target并更新activeProfileByte（DAT_18000e6c=1801e68b，+1F=1801e6aa），在子状态中读取各bank数据、校验、初始化/reconcile。选择后使用profileindex参与 `index*0x1000+0x2000`、`index*0x4000+layer*0x1000+0x320000`、`index*0x1000+0x340000` 数据访问；磁轴record RAM按0xD84层跨度。`FUN_1800e33c`创建后端数据load request（source/destination/length），完整底层storage driver语义未全部追踪。

因此 **STATIC CONFIRMED** 是：选择index→state2→activeindex更新→按所选bank加载不同区域；不是宣称已解析完整flash持久化格式。`fw-profile-state-machine.*`、`fw-profile-storage-load.*`、`fw-profile-load-reconcile.*`保留细节。1.00.59没有固件image，不能把1.00.58内存地址包装成1.00.59事实。

## 10. 官方模型分类与 Aura 建议（未实施）

对本次FIRMWARE_PROFILE，分类D已由短USB选择 + 官方host标签 + firmware bankloader + 用户行为闭环支撑。对软件Profile、导入/覆盖、bank上传/持久化未知，可能有Hybrid路径，但本次不推断。

Aura stableGUID不是ASUS有限hardware index。不能从6/1直接创建generic SelectProfile API，不能为加速把Aura GUID暗映射到官方slot、覆盖用户bank、启用51 00生产写入。需要独立正式protocol/ownership验证后另行决定。

建议未来明确选择一种 **Full Desired Magnetic State + diff** 产品契约，允许文档compact/sparse storage，但新契约Resolve对受管理keyboard范围输出完整目标：

- unspecified DKS → explicit Standard；
- unspecified RT → explicit Disabled（敏感度/continuous的合法保留来源也必须定义，不能凭空造值）；
- absent actuation/DZ exception → declaredProfile/base inheritance，通过已验证type1/type4重建实际inheritance；
- 临时覆盖有明确层级，hardwareRTgate只读独立。

**Full-state ≠ Full-write**。已知VStandard→VStandard为0write；ADKS→BStandard只还原V；已知RT不变0write、改变只51 54必要selector；resetcommon后仅显式exceptions。未知SessionApplied不能被当成firmwarereadback；generation/externalwriter不确定时仍走现有fail-closed及ownership要求。不能因完整目标契约随意写DKSUnknown或编造prior。

### Migration / ownership impact

现有 absent=unmanaged / present disabled=managed disabled 仍是当前有效持久化契约。未来改变 absent默认值需versioned opt-in model / schema migration preview，清楚说明设备范围、RT缺省完整参数、baseline来源、处理未知字段；不静默重解释旧Profile、不自动expand legacyglobalRT、不删除 prior-state guard。

managed Profiles之间切换若采用完整目标，新Profile自身定义Standard/Disabled/base，不应依赖先前外部prior来补洞。真正退出Aura管理/temporaryownership恢复仍需要trustedprior，无prior则保守阻止。两种生命周期分别建模，不用本轮bank证据为当前unmanage放宽保护。

性能代价：首次完整ownership在未知state下可能昂贵；后续同session只diff。RT仍只能51 54，多键事务仍按当前verifiedgranularity；51 53优化需要独立physicalgate。官方device-bank选择快不证明Aura普通hostProfile可以省settle或随意batch。

## 11. Unresolved / 下一轮 gate

1. 槽位3身份和前四组选择无Apply的触发原因：未知，保留原始timeline。
2. SDK bridge动态callstack与M605虚表最后一跳未完整证明；command角色仍有HAL exactbytes/firmware/capture独立支撑。
3. 25 01/12 03/22 01/12 12完整roles、12 00字段contract、EP0reportdata3/1语义未完全解析。
4. 1.00.59 bank内部overrideflags、实际NVM/power-cycle、层/其它bank没测试。
5. 软件Profilebank覆盖/导入等路径未捕获，不能推广。
6. UI点击与physicalsettled时间没instrument；不报告end-to-end<1s PASS。
7. DKS触发详细event、各键实际mm没独立记录，只限用户确认范围。
8. Aura fullstate目标contract/migration/priorrestore界限需ownerreview；现有RT unmanaged guard和所有safety保持。

不再要求用户重复已完成capture；如果未来实现banks，需要针对未验证范围提出独立证据gate，而不是replay本轮artifact。

## 12. 离线验证、产物和安全所有权

工具：`tools/hardware/analyze_official_profile_switch.py`，只处理classic USBPcap文件；由descriptor识别地址，保留所有interface/unknownOUT，严格拒绝截断、身份缺失、addressreuse、configurationchange。identicalecho与不同queryreply分开。auditfixtures没有write/replay入口。

本轮结果：**14/14 离线 parser tests PASS**，两份真实capture复核PASS，`git diff --check` PASS；fresh logs在evidence。

测试：`python -B -m unittest discover -s tests -p test_official_profile_switch_capture.py -v`。覆盖真实字节fixture、64byte reserved、descriptor地址变化、所有接口未知OUT、EP0、echo/query区别、missingApply保留、其它设备排除、identityreuse、configchange、端序/纳秒和invalid/truncatedcontainers。两份真实pcap另有独立manifest/计数/echo/尾部检查。

未运行整套C++/WinUI/frontend CI：本轮没有产品修改或构建，validation仅限新离线工具、真实captures复解析及diff/scope检查；不是完整softwareCI/releasePASS。

本地 evidence：`audit_artifacts/official-profile-switch/evidence/`；主派生目录`decoded/`，补充`rt-clean-control/`。原始pcap继续留在FUSBPcap；fullhex/timeline/JSON/CSV/derivedall-interfacepcap/hosttargetedconfig/staticfreshoutputs/testslogs均已保存。报告副本、scopedpatch、zip在同一ignoredaudit目录；Git仅候选docs/parser/tests/字节fixture。

任务开始已经有大量未提交项目修改。scopedpatch相对本次起始workingtree，仅包含本次新docs/parser/tests及hardwareindex新增链接，不包其它累计修改。source-integrity.json逐文件核对已有内容；并行工作对tools/LightingBackendProbe/run.py的改动单独记录，不覆盖、不归入本任务。保护的Profile/M605/WinUI/P4B源码未由本轮改动。

STOP：等待ownerreview；本轮没有实现fullstate resolver、bank writer或新HIDallowlist。
