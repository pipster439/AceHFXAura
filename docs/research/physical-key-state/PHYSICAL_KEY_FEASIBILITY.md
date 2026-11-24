# 物理按键读取可行性

**总判定：INCONCLUSIVE。** 设备专属输入表面已确认，完整物理身份协议及实际行为没有确认。本轮不选择 VIABLE，也不以有限负面证据宣称所有接口 NOT VIABLE。PARTIAL 描述部分来源过滤价值，但不作为本轮物理状态研究的总判定。

## “物理状态”的验收定义

本任务需要独立实体开关的数字激活/释放身份，至少不被输出键码宏、重映射、DKS、SpeedTap 合并或抹除。Hall 键的数字激活依赖触发点、RT 与迟滞，不能等同手指接触、机械行程、完全触底或 ADC 深度。本轮完全排除模拟行程读取。

普通 A/W/S/D 默认配置下，标准 usage 与实体键通常同名；这样的测试无法证明报告位于固件重映射/仲裁之前。不同物理键可能产生相同逻辑 usage，一个物理键也可生成多个 usage。数字位图、NKRO、设备 hDevice 都不自动保留物理因果关系。

## 已知能力与限制

| 要求 | 现有证据 | 结果 |
|---|---|---|
| 不使用全局 Hook 的来源过滤 | 两个 Keyboard TLC 和 Raw Input 设备身份可枚举 | 架构能力存在，输入送达未在本轮测试；有机会解决多键盘混淆 |
| 完整物理 Key ID | MI_03 声明标准 Keyboard usage；HAL73/74 有 Wire 表反查 | UNKNOWN；标准 usage 不等于物理 Wire ID |
| Copilot 独立身份 | Wire 表中 `0x050A → 0x0040`；生产 Hook 将 F23/APPS 映射为 COPILOT | 配置身份已知，输入身份 UNKNOWN |
| 真 Win/Shift + Copilot 并发 | 无受控物理捕获 | UNKNOWN；不能用时间窗口去重当物理证明 |
| 左右修饰键 | MI_00 缓存 E0..E7；MI_03 无该范围 | 描述符能力已知；物理来源、跨 TLC 合并与释放 UNKNOWN |
| Fn 独立按下/释放 | Wire 表中 Fn `0x0508 → 0x009F` | 仅配置身份；输入 UNKNOWN |
| DKS/SpeedTap/重映射下的源身份 | 已验证配置协议 + FW58 条件通知/输出构建分支 | UNKNOWN；有源标识的阶段事件也可能缺失普通释放 |
| 重复/乱序/漏报/重连恢复 | RT Gate 现有生命周期范式、后续 Provider 设计 | 全键数据可靠性 UNKNOWN；未实现或测试 physical state machine |
| 8000 Hz/物理端到端延迟 | 旧模式文字和历史 bInterval | UNKNOWN；未测量速度、接触时间或 host scheduling |

## Copilot 串亮为什么不能提前宣布解决

`foreground_monitor.cpp` 使用 `WH_KEYBOARD_LL`、vkCode/flags，事件没有设备来源。`key_input_hub.h` 在 COPILOT Down 时删除 50ms 内队尾 L_WIN/L_SHIFT Down，并清除 held。这是根据逻辑事件序列作的启发式，不证明能在真实 Win/Shift 同时按下时区分来源；消费者也可能已读走前置事件。本次没有改该逻辑。

即使 MI_03 携带 F23，而 MI_00 携带 Win/Shift，也不能把两通道的时间接近认定为 Copilot-only。真正满足替换条件的输入必须输出 Copilot 物理 ID，同时没有把宏生成的 Win/Shift 误认成实体键；按住真实 Win/Shift 再操作 Copilot 时，真实状态和释放都必须保留。

Wire `0x0040` 在当前白名单注释为 RightCtrl/Copilot 的**同一物理键位**；`calibrated_keymap.json` 对该键位标识为 COPILOT。不能另造一个独立 RightCtrl 实体键测试结果。未来若需把该键位配置为逻辑 R_CTRL，属于另一次明确授权的配置实验，本次不执行。

## 最值得后续证伪的假设

| 假设 | 支持 | 反证/缺口 | 最小下一步 |
|---|---|---|---|
| MI_03 本身是物理矩阵 | 数字变量位图 | TLC/usage 为标准键码，未声明物理 ID；无修饰键范围 | 将其当逻辑候选，对 Copilot/真实修饰键受控对照，不凭 A/W/S/D 判定 |
| 73/74 是完整源键事件 | HAL Wire 映射、两个不同回调 | FW58 有 enable-like 条件、阶段参数、有限队列；没有当前被动报文与全键覆盖 | Stage B 恢复后，仅观察已确认 vendor TLC 是否自然出现，先做少量选定键对照 |
| 70 是全键状态快照 | HAL 存在 bitset 消费者 | 已审消费者只扫描前5字节、转换热键/配置事件 | 停止直接矩阵解释；没有进一步普通键快照证据就不建 parser |
| 用 Raw Input 消除全局 Hook 即解决物理混淆 | 可按 hDevice 过滤 | 来源过滤不能逆转固件输出合成 | 单列 device-specific logical provider 的产品价值，物理命名仍禁用 |

若 vendor 事件只有通过 Feature/Output/未验证查询使能才能获得，本次授权下停止；若被动报告只有 DKS 阶段/热键而没有完整源键状态，则其可用范围最多 PARTIAL。无数据不等于接口不存在，尤其既有隐私过滤捕获的 allowlist 会排除未知事件。

## VIABLE 的必要证据门槛

必须绑定接口、TLC、Report ID、长度、版本和身份映射，证明 [全部身份与生命周期矩阵](PHYSICAL_ACCEPTANCE_MATRIX.md)通过；任何未覆盖键保持 Unknown。需要独立的源键真值、宏/重映射对照、真实修饰键并发、完整释放、启动时已按住、断连及 stale 恢复证据，不能只展示“按 A 有一个 bit”。

达到门槛后，才可实现真实 parser、硬件来源 fixture 和离线状态机测试。当前仅有[接口设计](PROVIDER_DESIGN.md)，[生产集成门槛](INTEGRATION_PROPOSAL.md)关闭。
