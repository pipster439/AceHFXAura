# M605 磁轴产品状态与服务（alpha.5 Phase 5）

WinUI 的现有 68 键页面只编辑草稿。选键、滑杆、开关与动作槽预览都不触发硬件写入；用户明确点“应用”后，页面向本机类型化 `/api/magnetic/...` 路由提交 Logical ID、毫米值、枚举状态或布尔意图。页面不构造 HID、Wire ID、opcode 或时序。HTTP 工作线程等待 `M605Runtime` future，WinUI 异步等待响应；后端继续独占 `DeviceWriteMutex` 完成各阶段、Apply 及 settle。

## 来源与已知范围

| 来源 | 表示什么 | 可在页面怎样显示 |
|---|---|---|
| `SessionApplied` | AceHFXAura 本会话完整提交并等待完成的运行时序列 | “本次会话已应用”；不是设备读回 |
| `HostProfile` | ASUS 活动主机配置文件保存的目标值 | “活动配置保存值”；不保证当前 MCU 一致 |
| `Unknown` | 没有可信来源，或字段不符合已确认结构 | 不填造数值，不把零当缺失 |
| `DeviceReadback` | 仅留给将来真实设备查询的具体字段 | Phase 5 不把 HostProfile 或 session shadow 升级为读回 |
| `HostCache` / `Default` / `Derived` | 分别为缓存、默认、换算结果 | 当前服务不冒充这三种来源为当前硬件值 |

主机 provider 只读 `%ProgramData%\ASUS\Framework\keyboard\ROG FALCHION ACE HFX` 下唯一的 `config_<SN>.xml` 与其中 `currProfile.id` 指向的 `fp_<id>_config_<SN>.xml`。文件名、序列号、活动 profile 必须匹配；歧义、缺失、损坏、越界或不支持的字段都返回 Unknown。解析限定已审计的全局 Actuation、全局 RT Press/Release、逐键 RT 列表、全局 Deadzone Top/Bottom、保存的 SpeedTap 键对与 Static Analog 标记。当前不推断逐键 Actuation、逐键 Deadzone、DKS 动作表、SpeedTap Master 或完整设备键对表的主机字段。主机文件从不由该服务修改。

每个服务状态响应带健康状态、持久隔离位、来源标记和 session shadow。对于同一字段，成功的 `SessionApplied` 优先展示；HostProfile 仍保留为独立保存目标。完整逐键磁轴设备读回尚无已验证路径，故不能把状态页叫“读取设备设置”。

## 安全与操作范围

- 每笔运行时事务首个 HID stage 之前，本地锁存先建立并刷写；若锁存建立失败（Arm 失败），此时尚未执行任何 HID 写入，因此不建立不确定设备变动，不进入 `IndeterminateStagedState`，保留既有 SessionApplied 影子，事务直接返回失败且健康状态保持 `Clean`，`last_error` 明确记录持久安全锁存无法建立且未尝试任何设备写入，后续锁存恢复后可重试。只有所有 stage、Apply、后续 settle 都成功后才删除锁存。正常执行中临时存在的锁存不作为产品/UI 意义上的 `persistent_safety_quarantine`（该字段仅在存在未清除的残留锁存或重启隔离时为 true）。崩溃／失败后重启进入 `PersistentSafetyQuarantine`。WinUI 只显示隔离并禁用操作，不提供普通“解除”按钮。`AcknowledgeExternalResynchronization()` 需要开发者／操作员**明确确认外部物理重同步**，它不是硬件 reset。
- 逐键 RT 禁用使用可信活动 HostProfile 保存的全局 Press/Release 值；缺失则禁用操作，绝不硬编码 0.4/0.2。
- DKS 与逐键 RT 的冲突由服务与 WinUI 模型协同处理。来源优先级严格遵守：1. 本键的 SessionApplied（若显式存在且禁用，则不产生冲突；若显式启用则有冲突）；2. 其次是可信的 HostProfile 逐键 RT 列表（若已知，则按其中是否包含该键判定）；3. 若两者均不可得（Unknown），则判定为存在潜在冲突并要求显式确认。服务依次调用已验证的标准 DKS 重写／RT 禁用，再设置目标功能。两笔是独立事务；第二笔失败不自动回滚，返回健康状态和部分进度，若不确定则影子失效并隔离。
- Deadzone 顶部 `0` 是合法保存值。全键重置必须确认，使用可信活动配置全局 Bottom/Top；它清除**全部**逐键覆盖，没有逐键删除 API。
- DKS 仅有四槽、受验证的四种里程碑状态和标准运行时重写；“恢复标准按键行为”不声称恢复工厂或先前 profile。Fn 键（Logical 0x0508 / Wire 0x009F）经结构化验证可作为 DKS 触发源键（source），但尚未作为 DKS 动作目标（action target）获得独立证明；因此产品层、服务与 WinUI 明确施加动作目标约束：允许 DefaultSentinel 和已验证常规按键作为目标，严禁将 Fn 设为 DKS 动作目标（非法 IPC 请求直接以 422 拒绝且不产生任何运行时调用；UI 目标选择器排除 Fn）。Fn 在键盘选择、Actuation、RT、Deadzone 与 DKS 源键中完全保留不受影响。
- SpeedTap 定向禁用、Master 和配置基线重置分别操作。基线重置后 `ProfileBaselineUnknown` 不等于空键对表。启用新键对必须基于可信基线以排除重叠冲突；若 HostProfile 保存键对未知且无完整权威替代基线，服务拒绝启用新键对并返回 409 说明原因，WinUI 禁用启用按钮并提示基线未知；定向禁用操作仍然允许。不凭空推造固件最大键对上限，不宣称 HostProfile 等于设备读回。
- Analog Effect 只接入已验证的 Static 模式。没有配置持久性、掉电状态或 ASUS 文件同步承诺。

页面保留原键盘几何；后续视觉阶段可处理 DKS 卡片密度、窄窗口滚动与大量目标键下拉体验。Phase 5 自动测试只使用假传输，不访问实体 M605。
