# Aura vs Gear Link Gap Matrix

比较基于本轮源码读取与capture；不是对历史doc陈述照单全收。Aura baseline：当前工作树d567de6，保留已有未提交Profile/P4B/平台研究。以下 **Action仅建议，不实施**。

| Feature | Gear Link reference | Current Aura | Gap / priority | Follow-up |
|---|---|---|---|---|
| Profile selection | finite activehardwarebank，多个产品领域一起恢复 | stableGUID + daemonhostdesired/sparseownership | **High**：名称相似但未管理域不会自动清理外部状态；这是scope契约差异 | owner先决定完整desired还是明确partialmanaged，依赖trustedbaseline，不删除Unknownguards |
| RT model | groupedkeyeditor最终逐54完整状态 | ExplicitPerKeyState，legacy53blocked | **Low**：production方向一致 | 保持disabled/unmanaged/gate分离，完善authoring测试 |
| RT/DKS UX | DKS/MT/Toggle键禁选RT | UnknownStandardblock + explicitStandard authoring | **High UX**：用户需可解释地建立Standard，disabled不可被剪枝 | 显式draftmutation、具体keycount/reason；不silentStandardize |
| RT physical gate | event/query只更新hardwareobservedstate | passiveMI02typedgate +submission shadow | **Low**：需持续确保身份/provenance/Unknown | 只读模型回归，OFF不dirty/disable |
| AP inheritance | common+perkey；移除写当前common | verifiedtype1 reset +exceptions | **不应回退** | Gear移除不是更强证据；保留Aura真正inheritance |
| DZ inheritance | 相似hostlist/remove当前值 | verifiedtype4reset/common/exceptions | **不应回退** | 同上；独立物理gate保护 |
| Full readback | 少量query +大量cache；genericgetter存在 | SessionAppliedhostshadow明确非readback | **Medium**：可研究typedgetter，但不能把cache升格 | queryexactfixtures/范围/overrideflag验证后才接readonlyAPI |
| Diagnostics | 官方refresh可能writepolling | pure read，无connect/HID/quarantine变更 | **Aura契约应保持** | 禁止直接复用Gearrefresh |
| Lighting effects | 十种firmwaremode +部分analog | softwareDirectRGB +effect0modifierprereq | **High**：模式接管/退出/restore缺闭环 | LightingOwnership专项，先typedmode，再error/restore/persistence；不是直接开51 2C |
| Lighting bank | selection可恢复lighting | ProfilelightingLegacyUnmanaged | **High architecture** | 明确跨lighting/magneticownership先于hardwarebank优化 |
| Per-key RGB/lightbar | 当前网页无独立editor/mapping | directRGBindices；独立lightbar映射仍不完整 | **Medium research** | 不从generic83/15术语补造表；只扩有完整mapping证据的范围 |
| Batch efficiency | multi54/4DKS/multieffect→Apply，队列retry | 每typedop已有conservative事务settle | **Low/后置** | correctness、generation、partialfailure先于性能；不降低210/400ms |
| Reconnect | 用户需手动连接；本次回default6；源码有reinit | stalehandlepreflight/generation，无automaticreapply | **Medium**：产品重连策略不同 |先分别定义observedbank与hostdesired，再决定owner-gatedreapply；不照搬隐式写 |
| Sync/reset | 全槽50 40重置+hostreplay | revisionedSave与Apply分离；quarantine保护 | **不可复制** | 不把allslotsync当普通Save或repair |
| Macro/remap/cloud | Companion软件功能，部分directkeybuilder | 非Aura当前目标 | **Low/documentonly** | 不把Aura扩成Armouryclone，不导入账户/宏敏感资料 |
| Multi-device | publicPIDmanifest +devicefactoryspecialization | M605typedboundary和独立平台接口 | **Medium design** | capabilities与protocolauthority分离；别复用newergenericRTlayout |

## 推荐依赖顺序

1. **Profile产品契约**：明确“管理范围”与officialfullbank差异。先审查已有disabledRT pipeline及完整desired设计，保持prior/restorationguards。新需求不能用“未配置就是Standard”偷偷实现。
2. **Authoring / provenance**：禁选/冲突可视、可信Standard显式操作、gate与configured/effective明确、缓存来源标注。可从officialUI规则借鉴，不需要unknownprotocol。
3. **Lighting Ownership独立gate**：固件Static/analog/其它mode与DirectRGB的接管、清理、restore、失败状态，main/lightbar范围。当前packet参考足够设计测试，不足以自动打开productionwriter。
4. **只读typedquery研究**：按feature小范围fixture验证，不引入officialrefresh中polling写，不假设overrideflags可读。
5. **Reconnect产品策略**：把activebank、durableGUIDdesired与generation分开；保留quarantine和显式Apply，自动reapply需新授权。
6. **Hardware bank与batch优化**：完成ownership/metadata/NVM/多产品恢复gate后再评估。53bulk始终独立physicallyvalidatedgate，不用于性能捷径。

## 直接不复用

未知值fallback、全槽reset修复、syntheticFFAA当成功、同opcode响应关联并行、unsafe staged retry、plaincache冒充firmwaretruth、rawsender、arbitraryreset/layer、minifiedsymbolAPI、physicalRTsoftwaremaster控制。Aura已有更强safe contract的地方，官方实现是风险参考而非替换理由。
