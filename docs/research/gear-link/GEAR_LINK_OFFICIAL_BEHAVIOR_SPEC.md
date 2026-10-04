# Gear Link Official Behavior Spec — HFX

本规范描述当前官方产品的可观察契约，不要求 Aura 克隆。版本、source anchors、capture hash 和证据等级见 [索引](README.md)。`STATIC` 和 USB echo 不替代物理验证。

## Profile

用户选择硬件 Profile 槽位；本机六个，最后一个为“默认 配置文件”。slot string/number 是硬件身份，不是 GUID 或可任意增加的 host Profile。Profile 包含灯效、磁轴、按键功能、RT选择、SpeedTap、触控条、polling rate 的 host 意图。选择既影响当前键盘行为，也改变随后编辑的槽位。

普通 4↔5 选择：用户报告磁轴和灯光一同恢复；capture 不见对应逐键/灯效重放。host UI 则从选中 Profile cache 加载大部分 editor 值。不能把 cache 显示当成对键盘整个槽位重新读取。

## 编辑 / 保存 / 应用

用户确认设置编辑实时生效。多数 handler 在状态更新后写 typed vendor command，再自动 `50 55`；无需等一个统一的 Profile Apply 按钮。DKS dialog/阈值编辑包含自己的提交动作。host cache/native 文件的更新是另一种持久化，不与 USB Apply 相等。

“同步设置”是全槽操作：逐槽选择、重置该槽、按 host 数据重放，然后返回原槽。它不是无害的 reload，也不是只保存当前编辑页。具体掉电/Flash写入范围未完整研究，不能承诺无限次安全同步。

## Actuation

当前 capability 范围 .1–4.0 mm，.1步进。共同参数和逐键值都可编辑，多选是编辑便利。空 host per-key list 表示 host 没有该项；不证明 device override flag 已清除。

页面单键移除在源码中写回当前共同 AP/DZ，然后删 host list。产品显示恢复共同数值，**是否真能跟随后来的共同改值未在本轮证明**。整体 AP reset 的 resetType1 路径是另一回事。Aura 不能用“写成相同值”冒充 inheritance。

## Deadzone

top/bottom 独立值，当前 capability 0–.5 mm，.1步进。共同/逐键编辑，wire 中 bottom 在 top 前。单键移除与 AP 绑定同一个 removePerKey 路径，可能同时重写两类值；不是仅 UI 删除。resetType4 仅静态路径，本轮无51 52。

## Rapid Trigger

核心交互是选择哪些键启用，统一或独立编辑按压/抬起灵敏度；范围 .1–2.5 mm。全区群组、per-key list 是 host UI 表示，均经过 M605 `51 54`。独立模式发 selector1/2，不是 separate bit。enable0 是关闭该键 RT，不是回 firmware common。

物理 RT 开关只读观察，与上述配置独立。MI_02 event 更新 observed gate；不删除所选键或覆写 press/release。此前实机证据与当前源均支持该分离。本轮捕到 OFF/ON event，用户没有逐键再给 gate AND矩阵的独立物理结果，不重复宣称完整验收。

DKS、ModTap、Toggle 冲突键从 RT 可选集合排除；Fn等不可用键来自页面/布局限制。用户本轮明确确认启用 DKS 后不能添加该键 RT。没有证据允许静默 RT→DKS Standard 转换。

Continuous 字段/开关存在；ON的完整行为本轮未验收。不得只凭字段存在在 Aura 扩大产品/协议范围。旧 `51 53` builder 存在于库，但普通 HFX UI 没走它，本次count0。

## DKS

一个 source key 有四动作 slot，按按压/抬起位置设置触发，阈值 UI、目标键选择与预览是独立产品状态。完整配置提交四个 `51 23` slot，然后 Apply。Standard 与空 host object 不可泛化等同：官方 hardware bank 可以保存自己的完整模式；Aura unmanaged state 没有这个前提。

## SpeedTap / Remap / Macro

SpeedTap 表示选定键对和 enable；本轮只在 sync 段捕到 pair写入，无独立冲突/物理优先级测试，不推断所有 SOCD策略。按键页另有 Standard/remap、Fn组合、MT、连发、媒体/快捷操作、宏分类。宏及软件分配依赖 Companion；本轮未导出用户宏、未验证其完全固件持久化。

## Lighting

当前实际十种入口：恒亮、呼吸、彩色循环、彩虹、涟漪、触发、星空、流沙、电流、雨滴。每种有适用的颜色、亮度、速度/方向/梯度/背景字段；不能要求所有 effect 都有全部字段。模拟灯效仅部分模式有入口。

用户确认没有独立逐键/灯条编辑，不能因键盘图可视化就认定可点击逐 LED。Profile selection 可恢复该槽灯效；mode editing 会写参数并 Apply。退出 Aura Sync 的 SW-mode协调和 restore 是独立流程，尚不能定义成 generic reset所有灯层。

## 连接 / 状态

用户报告需要手动连接；拔插后回默认 Profile。源码有 rediscovery、连接事件和延迟 reinitialize，这不等于本机浏览器无需用户重新授权/选择，也不等于自动恢复原槽位。实时 gate事件与 keyboard config不混写。

initial unavailable / connection lost 应是设备状态；cached Profile 可以展示编辑意图，但不能以它宣称硬件已经应用。当前官方内部某些 refresh 会写polling rate，Aura diagnostics必须保留自己的 pure-read contract。

## 不能从本规范导出的结论

- 所有状态均可固件 readback。
- Gear Link 缓存就是固件权威或云端备份。
- globalKeyList 就是固件globalRT/inheritance。
- 单键移除、空列表、Profile reset 是同一种操作。
- echo 等于持久化或物理效果。
- vendor generic库支持的功能全都对 HFX可达。
- 官方 retry、timing、bulk command 可直接成为 Aura 生产策略。
