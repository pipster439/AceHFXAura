# 集成门槛记录

**NOT AUTHORIZED / EVIDENCE GATE CLOSED。** 总判定 INCONCLUSIVE，因此本轮不提出生产集成架构、不实现或切换 PhysicalKeyStateProvider，不移除 Hook，不改 Reactive/Ripple。

当前可考虑的研究方向是设备来源过滤，但其逻辑输入价值不能包装为物理矩阵读取。`HardwareRtGateMonitor` 已有独立的 vendor Raw Input 接收窗口；不能向同进程添加第二个同 usage pair 注册，以免覆盖该窗口。未来生产注册所有权如何统一，必须等可行性与完整生命周期证据闭合后再设计。

门槛为：[物理可行性](PHYSICAL_KEY_FEASIBILITY.md)必要证据 + [Stage C 矩阵](PHYSICAL_ACCEPTANCE_MATRIX.md) + [隐私审查](PRIVACY_REVIEW.md)全部必需项通过。用户恢复 Stage B 本身也不授权设备配置变更、主动使能或发布版本。

[Provider 类型/状态机契约](PROVIDER_DESIGN.md)只是无协议假设的离线设计。下一次任务需先作 VIABLE/PARTIAL 判定，确认 parser 输入身份与证据，再决定是否提出正式集成方案；本文件仅记录当前阻止集成的理由。
