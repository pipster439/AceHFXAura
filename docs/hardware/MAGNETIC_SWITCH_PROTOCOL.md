# ASUS ROG Falchion Ace HFX (M605) 磁轴高级功能与压力感应协议逆向规范

> **文档说明**：本文保留驱动静态逆向研究，未验证的报文和 Interface 3 深度流均为研究假设。alpha.5 Phase 1 的写入依据见 [已验证运行时协议](M605_RUNTIME_PROTOCOL.md)。

---

## 1. 协议通信通道与调度模型

### 1.1 USB 接口分工架构

ROG Falchion Ace HFX (`VID: 0x0B05`, `PID: 0x1B7E`) 作为复合 USB HID 设备，在硬件驱动层划分为多通道独立通信管道：

| 通道名称 | 对应 USB 接口 | 端点类型 | 数据方向 | 报文长度 | 功能职责 |
|---|---|---|---|---|---|
| **Direct Lighting Pipe** | Interface 0/1/2 | Interrupt / Output Report | Host -> Device | 64 Bytes | Aura Direct RGB 流式直推 (`0x81C0`) |
| **Control & Config Pipe** | Interface 0 | Control / Feature / Output | 双向 | 64 Bytes | 磁轴配置、DKS、RT、SpeedTap、死区下发 (`0xXX51`) |
| **Vendor High-Speed Pipe** | **Interface 3** | Interrupt In (Streaming) | Device -> Host | 64 Bytes | **实时键程/霍尔 ADC 压力感应流、矩阵触发行程上报** |

### 1.2 调度中枢与回执机制

所有磁轴配置指令均由中枢调度函数 `AacM605::SetFunction`（RVA `0x08BF70`, 地址 `0x18008BF70`）统一分发处理：
1. **报文提交**：通过 `AacKbFunction` 下属的传输对象 `*plVar + 0x1070` 执行 `WriteReport(buffer, 0x40)`。
2. **异步回执校验**：指令下发后，DLL 内部以 10ms 为粒度轮询状态标志位（偏移 `+0x138`）：
   - 当设备返回 `0xFF 0xAA` 时判定为操作成功（日志标记 `[AacM605Function][...] FF AA`）。
   - 轮询等待超时上限为 **1500ms**（汇编常量 `0x5DC` / `0x5DB`，对应 150 次 `Sleep(10)`）。
   - 若超时未收到应答，触发 `Timeout` 保护并返回错误码。

---

## 2. 磁轴高级功能控制报文格式 (Control Pipe: `0xXX51`)

所有磁轴配置报文统一采用 64 字节定长格式，报文 Command 标识固定为 `0x51`（ASCII `'Q'`），Sub-Opcode 位于 Byte 1。

```
Byte Offset:   0     1     2     3     4     5     6     7     8 ... 63
             ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬────────┐
Field:       │ CMD │ SUB │        Payload (依功能定义)        │  0x00  │
             └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴────────┘
Value:        0x51  Opcode
```

---

### 2.1 触发键程配置 (Actuation Point)

键程数值以 **0.1 mm** 为最小步进单位。调节范围为 `0.1 mm ~ 4.0 mm`，对应整型数值 `1 ~ 40` (`0x01 ~ 0x28`)。

#### (1) 全局键程 (SetActuation_AllKey, Function ID `0x29`)
- **报文头**：`0x5051` (`Byte 0 = 0x51`, `Byte 1 = 0x50`)
- **汇编证据**：
  ```assembly
  18008c937: MOV dword ptr [RBP + -0x50], 0x5051  ; 报文头 0x5051
  18008c92c: MOVZX EAX, byte ptr [RSI + 0x4]      ; 获取输入行程值 (0.1mm 单位)
  18008c930: MOV byte ptr [RBP + -0x4c], AL       ; 存入报文 Byte 4
  ```
- **报文载荷结构**：
  - `Byte 0`: `0x51` (CMD)
  - `Byte 1`: `0x50` (Sub-Opcode)
  - `Byte 2..3`: `0x0000`
  - `Byte 4`: 全局触发键程数值（`0x01` ~ `0x28`，对应 0.1mm ~ 4.0mm）
  - `Byte 5..63`: `0x00`

#### (2) 单键键程 (SetActuation_PreKey, Function ID `0x2C`)
- **报文头**：`0x4F51` (`Byte 0 = 0x51`, `Byte 1 = 0x4F`)
- **汇编证据**：
  ```assembly
  18008ca2d: MOV dword ptr [RBP + -0x50], 0x4f51  ; 报文头 0x4F51
  18008ca40: MOV EDX, dword ptr [RAX + RCX*0x4]  ; 从矩阵表提取 16 位硬件 KeyID
  18008ca47: MOV byte ptr [RBP + -0x4c], DL       ; KeyID Low -> Byte 4
  18008ca54: MOV byte ptr [RBP + -0x4b], DL       ; KeyID High -> Byte 5
  18008ca4a: MOV byte ptr [RBP + -0x4a], AL       ; 行程数值 -> Byte 6
  ```
- **报文载荷结构**：
  - `Byte 0`: `0x51`
  - `Byte 1`: `0x4F`
  - `Byte 2..3`: `0x0000`
  - `Byte 4..5`: 硬件 Key ID（16-bit Little-Endian，由矩阵计算得出）
  - `Byte 6`: 单键触发键程数值（`0x01` ~ `0x28`）
  - `Byte 7..63`: `0x00`

---

### 2.2 快速触发配置 (Rapid Trigger)

Rapid Trigger (RT) 支持动态触底重置与抬起重置，灵敏度精度为 0.1 mm。

#### (1) 全局快速触发 (SetRapidTrigger_AllKey, Function ID `0x2D`)
- **报文头**：`0x5351` (`Byte 0 = 0x51`, `Byte 1 = 0x53`)
- **汇编证据**：
  ```assembly
  18008cb66: MOV word ptr [RBP + -0x50], 0x5351   ; 报文头 0x5351
  18008cb27: MOV EAX, dword ptr [RSI + 0x14]      ; 特性标志/开关 (16-bit)
  18008cb2a: MOV byte ptr [RBP + -0x4e], AL       ; Flags Low -> Byte 2
  18008cb30: MOV byte ptr [RBP + -0x4d], AL       ; Flags High -> Byte 3
  18008cb37: MOV byte ptr [RBP + -0x4c], AL       ; 下压触发灵敏度 -> Byte 4
  18008cb3e: MOV byte ptr [RBP + -0x4b], AL       ; 抬起复位灵敏度 -> Byte 5
  18008cb45: MOV byte ptr [RBP + -0x4a], AL       ; 顶部死区 -> Byte 6
  18008cb4c: MOV byte ptr [RBP + -0x49], AL       ; 底部死区 -> Byte 7
  ```
- **报文载荷结构**：
  - `Byte 0`: `0x51`
  - `Byte 1`: `0x53`
  - `Byte 2..3`: RT 特性模式标志（16-bit LE，`0x0001` 为使能 Continuous RT）
  - `Byte 4`: 下压灵敏度 Press Sensitivity（`0x01` ~ `0x28`，对应 0.1mm ~ 4.0mm）
  - `Byte 5`: 抬起灵敏度 Release Sensitivity（`0x01` ~ `0x28`）
  - `Byte 6`: 上死区 Top Deadzone（`0x00` ~ `0x14`）
  - `Byte 7`: 下死区 Bottom Deadzone（`0x00` ~ `0x14`）
  - `Byte 8..63`: `0x00`

#### (2) 单键快速触发 (SetRapidTrigger_PreKey, Function ID `0x2E`)
- **报文头**：`0x5451` (`Byte 0 = 0x51`, `Byte 1 = 0x54`)
- **报文载荷结构**：
  - `Byte 0`: `0x51`
  - `Byte 1`: `0x54`
  - `Byte 2..3`: RT 特性模式标志（16-bit LE）
  - `Byte 4..5`: 硬件 Key ID（16-bit LE）
  - `Byte 6`: 下压灵敏度 Press Sensitivity
  - `Byte 7`: 抬起灵敏度 Release Sensitivity
  - `Byte 8`: 单键死区 Deadzone
  - `Byte 9..63`: `0x00`

---

### 2.3 死区控制 (DeadZone Control)

死区用于滤除由于震动或初始接触产生的误触抖动。

#### (1) 全局死区 (SetDeadZone_AllKey, Function ID `0x34` -> `FUN_180021e40`)
- **报文头**：`0x5851` (`Byte 0 = 0x51`, `Byte 1 = 0x58`)
- **报文格式**：
  - 分离死区模式（`param_2[1] == -1`）：
    - `Byte 0`: `0x51`, `Byte 1`: `0x58`, `Byte 2..3`: `0x0000`
    - `Byte 4`: 上死区 Top Deadzone（来自于 `param_2 + 0xc`）
    - `Byte 5`: 下死区 Bottom Deadzone（来自于 `param_2 + 0x8`）
  - 对称死区模式：
    - `Byte 4`: 对称死区数值（`0x01` ~ `0x14`）

#### (2) 单键死区 (SetDeadZone_PreKey, Function ID `0x35` -> `FUN_180021f90`)
- **报文头**：`0x5951` (`Byte 0 = 0x51`, `Byte 1 = 0x59`)
- **报文格式**：
  - `Byte 0`: `0x51`, `Byte 1`: `0x59`
  - `Byte 2..3`: 硬件 Key ID（16-bit LE）
  - `Byte 4..5`: `0x0000`
  - `Byte 6`: 上死区 / 对称死区
  - `Byte 7`: 下死区（仅分离模式非零）

---

### 2.4 四段动态键程 (DKS - Dynamic Keystrokes)

DKS 允许单次物理按键在下压与回弹的全生命周期内按不同行程触发多达 4 个动作。

- **函数地址**：`FUN_180021bf0` (Function ID `0x27`)
- **报文头**：`0x2351` (`Byte 0 = 0x51`, `Byte 1 = 0x23`)
- **汇编证据**：
  ```assembly
  180021c3b: MOV word ptr [RBP + -0x78], 0x2351   ; 报文头 0x2351
  180021c45: MOV local_76, DL                     ; 主按键 KeyID Low -> Byte 2
  180021c49: MOV local_75, DH                     ; 主按键 KeyID High -> Byte 3
  180021c4d: MOV local_74, param_2[2]             ; 行程触点 1 -> Byte 4
  180021c51: MOV local_73, param_2[4]             ; 行程触点 2 -> Byte 5
  180021c5b: MOV local_72, DL                     ; 辅助按键 KeyID Low -> Byte 6
  180021c5f: MOV local_71, DH                     ; 辅助按键 KeyID High -> Byte 7
  ```
- **四段动作编码位掩码（Byte 8 算法）**：
  在循环 `180021cf0` 中，DLL 对输入的 4 个段位状态逐项映射：
  - `'0'` (`0x30`) -> 映射为二进制字符串 `"00"`（无按键动作）
  - `'1'` (`0x31`) -> 映射为二进制字符串 `"01"`（动作按键 1 触发）
  - `'2'` (`0x32`) -> 映射为二进制字符串 `"10"`（动作按键 2 触发）
  - `'3'` (`0x33`) -> 映射为二进制字符串 `"11"`（动作 1 与 动作 2 联合触发）
  4 个阶段依次拼接为 8 字符二进制字串，通过 `strtol(str, 0, 2)`（RVA `0x10EAC8`）转换为单字节写入 **Byte 8**。
- **报文载荷结构**：
  - `Byte 0`: `0x51`
  - `Byte 1`: `0x23`
  - `Byte 2..3`: 物理触发源按键 Hardware Key ID（16-bit LE）
  - `Byte 4`: 行程触点 1 (Downstroke 1 Distance，0.1mm 单位)
  - `Byte 5`: 行程触点 2 (Downstroke 2 Distance，0.1mm 单位)
  - `Byte 6..7`: 次按键 / 动作按键 2 Hardware Key ID（16-bit LE）
  - `Byte 8`: 四段动作位掩码 `Action_Mask` (8-bit)
    - `Bits [1:0]`: Stage 1 (Down 1)
    - `Bits [3:2]`: Stage 2 (Down 2)
    - `Bits [5:4]`: Stage 3 (Up 1)
    - `Bits [7:6]`: Stage 4 (Up 2)
  - `Byte 9`: DKS 配置标志位（如是否保持单次触发）
  - `Byte 10..63`: `0x00`

---

### 2.5 急停与对冲优先 (SpeedTap / SOCD)

SpeedTap 用于在 FPS/竞技游戏中实现双键对冲裁决（如 A 键与 D 键急停优先）。

#### (1) 按键绑定与模式设定 (SetSpeedTap, Function ID `0x30`)
- **报文头**：`0x5551` (`Byte 0 = 0x51`, `Byte 1 = 0x55`)
- **报文载荷结构**：
  - `Byte 0`: `0x51`, `Byte 1`: `0x55`, `Byte 2..3`: `0x0000`
  - `Byte 4..5`: 优先对冲第一键 Hardware Key ID（16-bit LE，如 Key A）
  - `Byte 6..7`: 优先对冲第二键 Hardware Key ID（16-bit LE，如 Key D）
  - `Byte 8`: SOCD 裁决模式：
    - `0x00`: Last Input Priority（后输入优先 / 急停）
    - `0x01`: Neutral Mode（中性 / 同时失效）
    - `0x02`: First Input Priority（先输入优先）
  - `Byte 9..63`: `0x00`

#### (2) 重置 SpeedTap (ResetSpeedTap, Function ID `0x31`)
- **报文头**：`0x5651` (`Byte 0 = 0x51`, `Byte 1 = 0x56`)
- 全载荷其余字节均为 `0x00`。

#### (3) 开关状态切换 (SwitchSpeedTap, Function ID `0x32` -> `FUN_1800222d0`)
- **报文头**：`0x5751` (`Byte 0 = 0x51`, `Byte 1 = 0x57`)
- **Byte 4**: `0x01`（启用） / `0x00`（停用）

---

### 2.6 复位控制与通知使能

#### (1) 恢复默认键程与 RT (Reset_Actuation_RapidTrigger, Function ID `0x2B` -> `FUN_180022100`)
- **报文头**：`0x5251` (`Byte 0 = 0x51`, `Byte 1 = 0x52`)
- **Byte 2..3**: 复位子模式
  - `0`: 全部复位（Default Actuation + Default RT）
  - `1`: 仅复位触发键程
  - `2`: 仅复位 RT
  - `3`: 带死区完整复位
  - `4`: 仅复位死区

#### (2) 动态数据上报使能 (SetDKS_Notify, Function ID `0x2A`)
- **报文头**：`0x5151` (`Byte 0 = 0x51`, `Byte 1 = 0x51`)
- 静态分析中的候选通知命令；尚无已验证的连续逐键 Hall 行程数据流。

---

## 3. Interface 3 实时压力感应与行程流协议

**验证边界**：以下是旧版反编译推断，已测试的运行时 USB 路径未发现连续逐键 Hall 行程值。不得据此实现 `GetTravelMm`、`GetHallDepth` 或 `RawHallValue`。

### 3.1 监听架构 (`FUN_18001ef70` & `FUN_18001fc00`)

旧版反编译将 `InitReadEvent_interface3`（RVA `0x01EF70`）解释为后台读取监听；以下分类未获得物理硬件闭环验证：

```
Byte 0 Packet Identifier:
  's' (0x73) -> 实时按键模拟行程 / 霍尔 ADC 深度数据包 (Analog Key Depth Stream)
  't' (0x74) -> 按键回弹/复位行程事件包
  'q' (0x71) -> 17 字节全键矩阵实时按下/抬起位图流 (136 Keys Bitfield)
  'p' (0x70) -> 板载配置档案 / 组合快捷键切换事件
  'a' (0x61) -> 扩展状态变更
  'r' (0x72) -> 侧边滑条物理按键事件
  'w' (0x77) -> 触控滑条 (Touch Slider) 连续位置/手势流
```

### 3.2 模拟键程数据包 ('s' / 0x73) 详细协议

当按键行程产生连续位移时，Interface 3 实时上报 `'s'` 数据包：

| 偏移 | 字段名 | 类型 | 说明 |
|---|---|---|---|
| `0` | Header | `uint8` | 固定值 `'s'` (`0x73`) |
| `1..2` | Raw Key Scancode | `uint16_le` | 原始扫描码，通过 `DAT_1801caef0` 查表反求 `(Row, Col)` |
| `3` | **Current Depth (ADC)** | `uint8` | **实时模拟键程/压力深度（0x00 为未按，0xFF 为完全触底）** |
| `4..63` | Reserved / Timestamp | `bytes` | 状态扩展与时间戳序列 |

**驱动内部解析反编译证据 (`FUN_18001fc00`)**：
```c
if (cVar2 == 's') { // 0x73
    // 逆向遍历 75 列 x 8 行矩阵表 DAT_1801caef0 匹配物理键位
    if ((uint)(ushort)local_68 == *(uint *)(&DAT_1801caef0 + (iVar8 + iVar11 * 8) * 4)) {
        uVar7 = (short)iVar8 << 8 | (ushort)iVar11; // 高字节 Row, 低字节 Col
        local_868 = (uint)local_68._2_1_;           // 取 Byte 3 实时键程深度
        pcVar5 = *(code **)(... + 0xC8);            // 派发至 SwitchKeyLog 注册的深度监听回调
        (*pcVar5)(&event_struct);
    }
}
```

---

## 4. 硬件矩阵寻址映射表 (`DAT_1801caef0`)

在 DLL 数据段 `0x1801CAEF0` 处硬编码了完整的 75 列 × 8 行（共 600 个条目）的硬件扫描码对照表，驱动通过逻辑位置 `Index = Row + Col * 8` 检索对应的 16 位硬件 Key ID。

### 4.1 典型按键硬件 ID 索引摘录

| 按键标识 | 逻辑矩阵 `(Row, Col)` | 偏移索引 | 硬件 Key ID (Hex) |
|---|---|---|---|
| **Esc** | `(1, 0)` | 1 | `0x0001` |
| **Tab** | `(2, 0)` | 2 | `0x0010` |
| **Caps Lock** | `(3, 0)` | 3 | `0x001E` |
| **Left Shift** | `(4, 0)` | 4 | `0x002C` |
| **Left Ctrl** | `(5, 0)` | 5 | `0x003A` |
| **Key 'W'** | `(2, 2)` | 18 | `0x0012` |
| **Key 'A'** | `(3, 1)` | 11 | `0x001F` |
| **Key 'S'** | `(3, 2)` | 19 | `0x0020` |
| **Key 'D'** | `(3, 3)` | 27 | `0x0021` |
| **Space** | `(5, 6)` | 53 | `0x0039` |

---

## 5. 状态查询与回执接口汇总 (GetFunction: `0x18008D250`)

| 查询目标 | Function ID | 出发报文 | 响应首字节 | 结果偏移 | 说明 |
|---|---|---|---|---|---|
| **Rapid Trigger 开关状态** | `0x2F` | `25 00 00 ...` | `'%'` (`0x25`) | Byte 4 | 返回 `0`（关闭）或 `1`（开启） |
| **SpeedTap 开关状态** | `0x33` | `25 01 00 ...` | `'%'` (`0x25`) | Byte 4 | 返回 `0`（关闭）或 `1`（开启） |
| **触控滑条模式 (Lever Mode)** | `0x12E` | `22 01 00 ...` | `'"'` (`0x22`) | Byte 4 | 返回滑条当前的动作模式索引 |
| **无线连接状态** | `0x1A` | `12 07 00 ...` | `0x12 0x07` | Byte 4 | 返回 WDL 无线链路信道质量 |
