# ROG FALCHION ACE HFX 物理按键硬件校准全貌详细报告

- **设备型号**: ROG FALCHION ACE HFX (PID: `0x1B7E`, Model: 7038)
- **总校准按键数**: 68 键
- **导出时间**: 2026-08-29 13:52:26
- **通讯协议**: 原生底层 USB HID 接口 3 帧直控 (`0x81 0xC0`)
- **安全等级**: 易失性 RAM 高速推流 (零 Flash 磨损)

---

## 物理按键与硬件 LED ID 详细对照总表

| 排号 | 排内序号 | 按键标识 | 键帽字符 | 分类 | 硬件 LED ID | 电气矩阵 (Col, Row) | 校准状态 |
| :---: | :---: | :--- | :---: | :---: | :---: | :---: | :---: |
| 1 | 01 | **ESC** | Esc | Function | `1` | `(0, 1)` | VERIFIED |
| 1 | 02 | **1** | 1 | Numeric | `9` | `(1, 1)` | VERIFIED |
| 1 | 03 | **2** | 2 | Numeric | `17` | `(2, 1)` | VERIFIED |
| 1 | 04 | **3** | 3 | Numeric | `25` | `(3, 1)` | VERIFIED |
| 1 | 05 | **4** | 4 | Numeric | `33` | `(4, 1)` | VERIFIED |
| 1 | 06 | **5** | 5 | Numeric | `41` | `(5, 1)` | VERIFIED |
| 1 | 07 | **6** | 6 | Numeric | `49` | `(6, 1)` | VERIFIED |
| 1 | 08 | **7** | 7 | Numeric | `57` | `(7, 1)` | VERIFIED |
| 1 | 09 | **8** | 8 | Numeric | `65` | `(8, 1)` | VERIFIED |
| 1 | 10 | **9** | 9 | Numeric | `73` | `(9, 1)` | VERIFIED |
| 1 | 11 | **0** | 0 | Numeric | `81` | `(10, 1)` | VERIFIED |
| 1 | 12 | **-** | - | Symbol | `89` | `(11, 1)` | VERIFIED |
| 1 | 13 | **=** | = | Symbol | `97` | `(12, 1)` | VERIFIED |
| 1 | 14 | **BACKSPACE** | Backspace | Function | `105` | `(13, 1)` | VERIFIED |
| 1 | 15 | **INS** | Ins | Navigation | `113` | `(14, 1)` | VERIFIED |
| 2 | 01 | **TAB** | Tab | Function | `2` | `(0, 2)` | VERIFIED |
| 2 | 02 | **Q** | Q | Alpha | `10` | `(1, 2)` | VERIFIED |
| 2 | 03 | **W** | W | Alpha | `18` | `(2, 2)` | VERIFIED |
| 2 | 04 | **E** | E | Alpha | `26` | `(3, 2)` | VERIFIED |
| 2 | 05 | **R** | R | Alpha | `34` | `(4, 2)` | VERIFIED |
| 2 | 06 | **T** | T | Alpha | `42` | `(5, 2)` | VERIFIED |
| 2 | 07 | **Y** | Y | Alpha | `50` | `(6, 2)` | VERIFIED |
| 2 | 08 | **U** | U | Alpha | `58` | `(7, 2)` | VERIFIED |
| 2 | 09 | **I** | I | Alpha | `66` | `(8, 2)` | VERIFIED |
| 2 | 10 | **O** | O | Alpha | `74` | `(9, 2)` | VERIFIED |
| 2 | 11 | **P** | P | Alpha | `82` | `(10, 2)` | VERIFIED |
| 2 | 12 | **[** | [ | Symbol | `90` | `(11, 2)` | VERIFIED |
| 2 | 13 | **]** | ] | Symbol | `98` | `(12, 2)` | VERIFIED |
| 2 | 14 | **\** | \ | Symbol | `106` | `(13, 2)` | VERIFIED |
| 2 | 15 | **DEL** | Del | Navigation | `114` | `(14, 2)` | VERIFIED |
| 3 | 01 | **CAPS** | Caps | Function | `3` | `(0, 3)` | VERIFIED |
| 3 | 02 | **A** | A | Alpha | `11` | `(1, 3)` | VERIFIED |
| 3 | 03 | **S** | S | Alpha | `19` | `(2, 3)` | VERIFIED |
| 3 | 04 | **D** | D | Alpha | `27` | `(3, 3)` | VERIFIED |
| 3 | 05 | **F** | F | Alpha | `35` | `(4, 3)` | VERIFIED |
| 3 | 06 | **G** | G | Alpha | `43` | `(5, 3)` | VERIFIED |
| 3 | 07 | **H** | H | Alpha | `51` | `(6, 3)` | VERIFIED |
| 3 | 08 | **J** | J | Alpha | `59` | `(7, 3)` | VERIFIED |
| 3 | 09 | **K** | K | Alpha | `67` | `(8, 3)` | VERIFIED |
| 3 | 10 | **L** | L | Alpha | `75` | `(9, 3)` | VERIFIED |
| 3 | 11 | **;** | ; | Symbol | `83` | `(10, 3)` | VERIFIED |
| 3 | 12 | **'** | ' | Symbol | `91` | `(11, 3)` | VERIFIED |
| 3 | 13 | **ENTER** | Enter | Function | `107` | `(13, 3)` | VERIFIED |
| 3 | 14 | **PGUP** | PgUp | Navigation | `115` | `(14, 3)` | VERIFIED |
| 4 | 01 | **L_SHIFT** | Shift | Modifier | `4` | `(0, 4)` | VERIFIED |
| 4 | 02 | **Z** | Z | Alpha | `20` | `(2, 4)` | VERIFIED |
| 4 | 03 | **X** | X | Alpha | `28` | `(3, 4)` | VERIFIED |
| 4 | 04 | **C** | C | Alpha | `36` | `(4, 4)` | VERIFIED |
| 4 | 05 | **V** | V | Alpha | `44` | `(5, 4)` | VERIFIED |
| 4 | 06 | **B** | B | Alpha | `52` | `(6, 4)` | VERIFIED |
| 4 | 07 | **N** | N | Alpha | `60` | `(7, 4)` | VERIFIED |
| 4 | 08 | **M** | M | Alpha | `68` | `(8, 4)` | VERIFIED |
| 4 | 09 | **,** | , | Symbol | `76` | `(9, 4)` | VERIFIED |
| 4 | 10 | **.** | . | Symbol | `84` | `(10, 4)` | VERIFIED |
| 4 | 11 | **/** | / | Symbol | `92` | `(11, 4)` | VERIFIED |
| 4 | 12 | **R_SHIFT** | Shift | Modifier | `100` | `(12, 4)` | VERIFIED |
| 4 | 13 | **UP** | ↑ | Arrow | `108` | `(13, 4)` | VERIFIED |
| 4 | 14 | **PGDN** | PgDn | Navigation | `116` | `(14, 4)` | VERIFIED |
| 5 | 01 | **L_CTRL** | Ctrl | Modifier | `5` | `(0, 5)` | VERIFIED |
| 5 | 02 | **L_WIN** | Win | Modifier | `13` | `(1, 5)` | VERIFIED |
| 5 | 03 | **L_ALT** | Alt | Modifier | `21` | `(2, 5)` | VERIFIED |
| 5 | 04 | **SPACE** | Space | Space | `53` | `(6, 5)` | VERIFIED |
| 5 | 05 | **R_ALT** | Alt | Modifier | `77` | `(9, 5)` | VERIFIED |
| 5 | 06 | **FN** | Fn | Modifier | `85` | `(10, 5)` | VERIFIED |
| 5 | 07 | **COPILOT** | Copilot | Modifier | `93` | `(11, 5)` | VERIFIED |
| 5 | 08 | **LEFT** | ← | Arrow | `101` | `(12, 5)` | VERIFIED |
| 5 | 09 | **DOWN** | ↓ | Arrow | `109` | `(13, 5)` | VERIFIED |
| 5 | 10 | **RIGHT** | → | Arrow | `117` | `(14, 5)` | VERIFIED |

---
*由 ROG Falchion Ace HFX Per-Key Calibrator GUI 自动生成导出*
