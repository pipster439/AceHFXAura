# 工作室：光效与 Automation v2

Studio 由本地 Web 服务提供，也通过 WinUI WebView2 使用。硬件默认 auto（Native HID 优先，保留 gated legacy_hal 回退）。

## 制作与发布光效

保存草稿只保存编辑内容，不改变运行版本。发布显式选择 continuous 或 one-shot：stageEffect → 原生编译 → daemon 确认加载与生命周期 → revision 检查及原子配置写入。失败保留旧发布引用。旧有效连续工作区继续运行；Plugin ABI v1、generation pinning 和无生命周期插件的 LegacyEnvelope 均保留。

等待、循环、变量和逐键渲染属于单光效脚本。等待不阻塞硬件线程；现有每帧指令预算与 sequence machinery 不变。编辑界面的 Effect Preview 用本地 JS 输入预览单个效果，不证明 Automation 行为。

## 作者流程

在 Automation（或工作室的自动化编辑区）使用一个根节点连接规则，连接顺序就是配置顺序。三种 WHEN 分别是“当条件成立期间”“当条件首次成立时”“当事件发生时”。播放光效只负责选择光效、优先级、合成方式和一次性重触发。事件只在 WHEN 选择一次。

state 播放光效是 while_true；rising/event 是 one_shot。Activate Profile 为独立 state 动作，DND 为规则元数据。目录来自后端 profile recipes 和已发布插件；草稿须先发布。无法解析的引用显示不可用并阻止保存。保存直接提交 V2 记录，服务端统一验证全部记录并以一个 revision 事务原子写入；冲突需重新加载并审阅。

初始 config.example 展示 cs2.exe Activate Profile、低血量持续层和击杀一次性层。基础方案在命中规则中取配置顺序最前者；合成为 Base → persistent → transient，每类内部按 priority、rule_order、instance_sequence 排序。one-shot 保留 restart/ignore_while_active/stack/queue。

## 全 Automation 模拟

页面明确显示 REAL GSI / SIMULATION。开启模拟后默认前台为 cs2.exe；可以改变血量、护甲、炸弹、回合阶段和击杀数。“+1 kill”修改真实计数，经 daemon 的事件检测、RuleEngine、AutomationEffectRuntime、合成和既有输出路径执行。

默认心跳 1000 ms，并按 freshness 缩短。暂停心跳可验证过期和恢复。真实 CS2 上报仍正常收到 2xx，但模拟开启时不进入权威状态。进入和退出会重建来源基线，不生成假事件或 rising，不回放切换期间的事件。开启模拟时退出单效果硬件预览；不要用 Effect Preview 的本地输入判断 Automation。

## 验证

frontend npm test/build 覆盖真实 Blockly 工作区和发布链；CMake/CTest 覆盖真实 V2、ABI/lifetime、generation/reload、合成和模拟输入链；Python 启动真实 dry-run daemon 验证接口和发布事务。WinUI Automation 作者入口通过 Studio 使用，本地旧 Application Rules 页面与 API 已删除。

自动化结果不等于真实键盘或真实 CS2 验收。详见 [Automation v2 架构](../architecture/AUTOMATION_V2.md) 与本次退役报告。
