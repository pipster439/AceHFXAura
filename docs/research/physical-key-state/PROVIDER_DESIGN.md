# PhysicalKeyStateProvider 离线接口设计

**设计文档，未实现，未接入生产。** 当前报告协议不足，不提供 vendor byte parser、真实键 fixture 或 physical state-machine PASS 声明。此设计定义未来 parser 获准后的状态契约，不为任何未知字节赋予含义。

## 候选类型契约

```text
PhysicalKeyId       = 显式审计的实体开关身份；不可由 HID usage / LED ID 推算
DeviceIdentity      = 精确设备/TLC绑定，进程内使用；不向日志暴露serial/path
SessionGeneration   = 每次连接、身份重绑或状态重置递增
KeyEvent            = {PhysicalKeyId, Press|Release, monotonic_timestamp,
                       local_sequence, DeviceIdentity, SessionGeneration}
ProviderStatus      = Unknown | Ready | Stale | Disconnected
ResetEvent          = {reason, DeviceIdentity, old_generation, new_generation}
Snapshot            = {status, known_pressed_ids, known_released_ids,
                       unknown_ids, observed_at, generation}
```

时间戳为 host monotonic receipt time；不称设备扫描/机械接触时间。timestamp 不是可靠 report ordering 的替代品。设备有已验证序号时另携带 device_sequence；没有则只保证单接收线程的 host receipt order，无法检测 USB/固件内部丢失或乱序。每键状态允许 Unknown，未知集合不等于 released。

Wire ID 可作为候选审计输入，当前68键白名单是**配置映射**证据；必须另确认输入使用同一 identity namespace。Fn009F、Copilot/RightCtrl0040、LED位置、HID usage 不互相换算。

## 状态机验收设计

| 输入情形 | 预期行为 | 后续离线 fixture 要覆盖 |
|---|---|---|
| 启动 / 新连接 | Unknown；不能假定全键释放 | 启动前已有按住键 |
| 已验证完整 snapshot | 为覆盖范围建立基线；未知键保持 Unknown | 部分覆盖、空快照与 malformed区别 |
| 已知 released→pressed | 一次 Press、递增本地序号 | 新press及并发键 |
| 已知 pressed→released | 一次 Release | 释放与多键独立性 |
| 同状态重复报告 | 不重复发边沿，但可以刷新有效快照时间 | 大批重复及长按 |
| 只有 delta，无启动基线 | 未涉及键保持 Unknown；不宣称完整快照 | 首事件是Up、缺失早期Down |
| device_sequence 逆序/缺口（如果确有该字段） | 丢弃旧包/使覆盖范围 Unknown，按已审序号规则处理 | wrap、重复、gap、乱序 |
| 无设备序号，仅时间倒退/并行交付 | 拒绝不满足本地序列契约的输入；不能声称检测设备丢包 | 不同线程、同时间多个事件 |
| malformed / 长度或ID不符 | 不解析；相关状态Unknown或拒绝来源，理由独立记录 | 边界长度、未知ID、批量dwCount overflow |
| disconnect / identity改变 | 清空held；Reset/Disconnected；递增generation | 按住时拔插、handle重用、双设备 |
| stale / 丢失 / 停止 | 清空可信状态并发Reset；不伪造物理Release | 部分报告、焦点取消、超时 |
| 新generation收到旧报告 | 拒绝，绝不恢复旧held | 重连后延迟包 |

Reset 表示取消可信状态，不证明实体刚释放；消费者未来必须单独处理 cancellation。stale 不得按“每隔N毫秒没有报告”任意触发：事件驱动按住不变时可能没有报告。需已证明的刷新/heartbeat、接收健康或生命周期失效证据；否则数据年龄仅为信息，不推断 released。必要时明确状态 Unknown，而不是发虚假的 Press/Release。

## 下一轮 parser 的前置门槛

确立确切 interface/TLC/Report ID、framing、版本、源身份映射、快照/delta属性、完整释放和恢复机制后，才能用真实证据生成解析器测试，再用明确标为合成的 fixture 验证上述状态机。不能从现有标准usage位图或 HAL回调名称臆造物理协议。

本轮只对缓存能力解析工具进行了离线测试；本文件没有生产 runtime 所有者、灯效接入、Hook切换或自动fallback方案。集成决定见[关闭的集成门槛](INTEGRATION_PROPOSAL.md)。
