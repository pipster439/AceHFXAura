# ROG FALCHION ACE HFX 硬件级独立单键 RGB 控制系统

本工具专为 **ROG FALCHION ACE HFX**（魔导士 ACE HFX，PID: `0x1B7E`, Model: `7038`）深度定制开发，直接通过华硕原生底层驱动通讯层实现**完全独立的单键 RGB 控制**。

---

## 核心设计特性

1. **绝对安全（零 Flash/EEPROM 磨损）**：
   - 100% 运行于硬件易失性高速 RAM 帧推流模式，彻底杜绝固件 EEPROM 擦写，键盘寿命无任何隐患。
2. **完全独立单键控制（True Per-Key RGB）**：
   - 彻底告别传统波浪（Wave/Pattern）滚动的局限，每个按键拥有独立的物理色彩通道。
   - 所有物理坐标已全部通过硬件实测点对点校准（包括 WASD、ESC、空格、上下左右方向键、Lightbar 指示灯等）。
3. **彻底抛弃 Windows 动态照明 (WDL)**：
   - 不依赖 Windows 11 LampArray 服务，不受系统动态照明冲突或覆盖干扰。
4. **低资源占用**：
   - 采用原生 Ctypes 挂载 `AacKbHal_x64.dll` 的 `Set_L_STD_SINGLE_XY` 核心指令，单帧推流耗时低于 1 毫秒，CPU 占用几乎为 0%。

---

## 物理按键与功能键名称对照表

所有按键名称**均不区分大小写**（例如 `space`、`Space`、`SPACE` 均完全相同）：

| 功能分类 | 支持的按键名称（直接在 `--key` 后使用） | 对应物理按键 |
| :--- | :--- | :--- |
| **空格键** | `SPACE` 或 `SPACEBAR` | 空格长键 |
| **退出键** | `ESC` 或 `ESCAPE` | Esc 键 |
| **回车键** | `ENTER` 或 `RETURN` | Enter 回车键 |
| **退格键** | `BACKSPACE` 或 `BS` | Backspace 退格键 |
| **制表 / 大小写** | `TAB`、`CAPS` 或 `CAPSLOCK` | Tab 键、Caps Lock 大小写锁定 |
| **Shift 键** | `SHIFT`（默认左 Shift）、`L_SHIFT`、`R_SHIFT` | 左 Shift、右 Shift |
| **Ctrl 键** | `CTRL`（默认左 Ctrl）、`L_CTRL` | 控制键 |
| **Win 键** | `WIN`、`L_WIN` 或 `WINDOWS` | Windows 徽标键 |
| **Alt 键** | `ALT`（默认左 Alt）、`L_ALT`、`R_ALT` | 左 Alt、右 Alt |
| **快捷与专属** | `FN`、`COPILOT` | 华硕 Fn 键、微软 Copilot 键 |
| **导航控制** | `DEL`（Delete）、`INS`（Insert）、`PGUP`、`PGDN` | 右侧 4 个独立导航键 |
| **独立方向键** | `UP`、`DOWN` / `LEFT`、`RIGHT` 或整组 `ARROWS` | 上、下、左、右独立物理方向键 |
| **常用字母连写** | `tygf`、`wasd`、`qwer`、`12345`（自动逐字识别） | 任意多个字符组合连续赋值 |

---

## 使用示例

脚本路径：[`g:\Aura\set_per_key.py`](file:///g:/Aura/set_per_key.py)

### 示例 1：点亮功能按键（空格+回车+退格+Tab）
```powershell
python g:\Aura\set_per_key.py --key SPACE 0 200 255 --key ENTER 255 0 0 --key BACKSPACE 255 255 0 --key TAB 0 255 0
```

### 示例 2：点亮所有修饰键（Shift + Ctrl + Alt + Win + Fn + Copilot）
```powershell
python g:\Aura\set_per_key.py --key SHIFT 255 0 255 --key CTRL 0 255 255 --key ALT 255 255 0 --key WIN 0 0 255 --key FN 255 100 0 --key COPILOT 0 255 150
```

### 示例 3：点亮右侧导航键与四个方向键
```powershell
# 方向键整组一键点亮（黄色），导航键分别点亮（青色）
python g:\Aura\set_per_key.py --key ARROWS 255 255 0 --key DEL 0 255 255 --key PGUP 0 255 255 --key PGDN 0 255 255
```

### 示例 4：字母连写 + 功能键组合
```powershell
# tygf 连写设为纯蓝，空格设为冰蓝，回车设为红色
python g:\Aura\set_per_key.py --key tygf 0 0 255 --key SPACE 0 200 255 --key ENTER 255 0 0
```

### 5. 一键熄灭所有按键
```powershell
python g:\Aura\set_per_key.py --preset off --duration 1
```

---

## 🖥️ 纯鼠标可视化图形校准工具 (GUI Calibrator) ⭐ 强烈推荐

工具路径：[`g:\Aura\gui_calibrator.py`](file:///g:/Aura/gui_calibrator.py) 或 双击 [`g:\Aura\run_gui_calibrator.bat`](file:///g:/Aura/run_gui_calibrator.bat)

```powershell
python g:\Aura\gui_calibrator.py
```
- **纯鼠标操作**：界面渲染完整的 65% 紧凑键盘按键面板。
- **左键点击**：键盘某键亮起时，鼠标直接点击屏幕上对应的按键，瞬间绑定硬件 ID 并自动切换到下一个！
- **右键测试**：鼠标右键点击任意按键，真机键盘会立即单独点亮该键进行验证。
- **空引脚跳过**：遇到未通电空引脚，直接点击 **[○ 此引脚未亮 (跳过)]** 即可。
- **自动保存与详细导出**：每次点击都会实时保存并更新 JSON 档案与 Markdown 对照总表！

---

## 🛠️ 终端命令行校准工具 (CLI Calibrator)

工具脚本：[`g:\Aura\calibrate_keys.py`](file:///g:/Aura/calibrate_keys.py)

### 1. 启动交互式逐键校准（支持实时探针微调）
```powershell
# 从第 1 排按键依序巡检至第 5 排
python g:\Aura\calibrate_keys.py

# 遇到没亮的情况？
# 直接按 [+] 或 [-]（或方向键 ↑ / ↓）进行实时探针微调，灯光会在键盘上逐个引脚跳动！
# 看到目标键亮起后，按 [Enter] 即可瞬间锁定！
# 如果该按键暂无灯珠，直接按 [N] 键标记为未亮 (UNLIT) 并跳过。
```

### 2. 仅校准指定排
```powershell
python g:\Aura\calibrate_keys.py --row 4   # 专门检测第 4 排 (ZXCV 底部字母)
python g:\Aura\calibrate_keys.py --row 5   # 专门检测第 5 排 (空格、修饰与方向键)
python g:\Aura\calibrate_keys.py --start 30 # 从第 30 个按键开始
```

### 3. 全引脚扫描模式 (Hardware ID Sweep)
```powershell
# 从 ID 0 到 127 逐一点亮真机引脚，没亮的空引脚按空格秒过，亮起的键按回车快速录入
python g:\Aura\calibrate_keys.py --mode sweep
```

### 4. 详细导出成果
校准过程中随时按 `Q` 退出或全部完成后，系统将自动生成：
- **完整 JSON 数据库**：[`g:\Aura\calibrated_keymap.json`](file:///g:/Aura/calibrated_keymap.json)
- **可视化对照总表报告**：[`g:\Aura\calibrated_keymap.md`](file:///g:/Aura/calibrated_keymap.md)
