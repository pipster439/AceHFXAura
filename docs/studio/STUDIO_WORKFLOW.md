# 工作室：光效与 Automation v2

Studio 由本地 Web 服务提供，也通过 WinUI WebView2 使用。硬件默认 auto（Native HID 优先，保留 gated legacy_hal 回退）。

## 制作与发布光效

保存草稿只保存编辑内容，不改变运行版本。发布显式选择 continuous 或 one-shot：stageEffect → 原生编译 → daemon 确认加载与生命周期 → revision 检查及原子配置写入。失败保留旧发布引用。旧有效连续工作区继续运行；Plugin ABI v1、generation pinning 和无生命周期插件的 LegacyEnvelope 均保留。

新建光效草稿时选择“持续光效”或“单次光效”，单次光效可设置 0..60000 ms 的序列结束后淡出，写入现有 `publication.mode` / `publication.fade_out_ms`。编辑器顶部常驻显示当前草稿的播放方式、淡出值和发布状态；点击生命周期摘要可修改。单次光效的正文时长由 Blockly 序列中的动作与等待决定，序列结束后才淡出；不另设固定播放时长。

等待、循环、变量和逐键渲染属于单光效脚本。等待不阻塞硬件线程；现有每帧指令预算与 sequence machinery 不变。编辑界面的 Effect Preview 用本地 JS 输入预览单个效果，不证明 Automation 行为。

## 作者流程

在 Automation（或工作室的自动化编辑区）使用一个根节点连接规则，连接顺序就是配置顺序。三种 WHEN 分别是“当条件成立期间”“当条件首次成立时”“当事件发生时”。播放光效只负责选择光效、优先级、合成方式和一次性重触发。事件只在 WHEN 选择一次。

Effect Studio 定义光效怎样播放以及何时结束；Automation 定义何时启动。播放光效积木显示已发布 Studio 生命周期、原生生命周期能力或旧插件的 Host 兼容单次播放摘要。常用字段积木展示中文名称，保存时仍写原始 key；未知字段使用自定义字段积木。字段与事件说明可查看类型、枚举、分类和含义。`event.*` 是一次事件发生对应一次 occurrence，不是持续布尔脉冲。`event.ace` 是 Aura 观察本回合击杀数达到 5 时的推定，并非 CS2 官方独立 ACE 事件。

旧光效工作区中读取 `event.*` 布尔脉冲的积木会在加载时报迁移诊断，不会静默替换成另一个布尔字段；请把触发条件移入 Automation 事件规则。`player.state.helmet`、`player.state.defusekit` 等真实布尔状态仍可在光效里读取。

state 播放光效是 while_true；rising/event 是 one_shot。Activate Profile 为独立 state 动作，DND 为规则元数据。目录来自后端 profile recipes 和已发布插件；草稿须先发布。无法解析的引用显示不可用并阻止保存。保存直接提交 V2 记录，服务端统一验证全部记录并以一个 revision 事务原子写入；冲突需重新加载并审阅。

新安装从空 `orchestration.rules` 开始，不会自动启用 CS2 或演示规则。基础方案在命中规则中取配置顺序最前者；合成为 Base → persistent → transient，每类内部按 priority、rule_order、instance_sequence 排序。one-shot 保留 restart/ignore_while_active/stack/queue。

## 全 Automation 模拟

页面明确显示 REAL GSI / SIMULATION。开启模拟后默认前台为 cs2.exe；可以改变血量、护甲、炸弹、回合阶段和击杀数。“+1 kill”修改真实计数，经 daemon 的事件检测、RuleEngine、AutomationEffectRuntime、合成和既有输出路径执行。

默认心跳 1000 ms，并按 freshness 缩短。暂停心跳可验证过期和恢复。真实 CS2 上报仍正常收到 2xx，但模拟开启时不进入权威状态。进入和退出会重建来源基线，不生成假事件或 rising，不回放切换期间的事件。开启模拟时退出单效果硬件预览；不要用 Effect Preview 的本地输入判断 Automation。

## 验证

frontend npm test/build 覆盖真实 Blockly 工作区和发布链；CMake/CTest 覆盖真实 V2、ABI/lifetime、generation/reload、合成和模拟输入链；Python 启动真实 dry-run daemon 验证接口和发布事务。WinUI Automation 作者入口通过 Studio 使用，本地旧 Application Rules 页面与 API 已删除。

自动化结果不等于真实键盘或真实 CS2 验收。详见 [Automation v2 架构](../architecture/AUTOMATION_V2.md) 与本次退役报告。
