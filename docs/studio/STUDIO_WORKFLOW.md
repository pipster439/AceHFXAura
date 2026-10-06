# 工作室：光效与 Automation v2

Studio 由本地 Web 服务提供，也通过 WinUI WebView2 使用。硬件默认 auto（Native HID 优先，保留 gated legacy_hal 回退）。

## alpha.8 Studio 助手与工具

WinUI 原生 Studio 命令、状态、最近工程与 Assistant 围绕现有 WebView2 / Blockly 编辑器工作。Ctrl+Shift+P 打开命令面板；它复用现有命令与验证，disabled 命令会说明原因。

在 AI 服务设置填 OpenAI-compatible Base URL 和模型，手动输入 API key。密钥保存在 Windows 受保护凭据存储，不保存到工程、config 或工程包。默认 Auto 接受普通回复；JSON Object 适合支持 JSON mode 的 endpoint；仅服务明确支持严格 schema 时选择 JSON Schema。模式不兼容时手动切换并显式重试，不自动重复付费请求。Provider 输出质量、兼容性与费用由各 provider 决定，请先测试连接。

直接聊天解释效果、提出调整或分析相关诊断，不用选择 Explain/Modify 模式。普通文字没有修改权限；typed proposal 在独立候选上验证工程和模拟，并在卡片显示 Diff、before/after、基准与 stale 状态。用户明确 Preview / Apply 后才修改草稿；外部编辑使旧建议失效。AI 不能自动 Apply、Publish、编译、操作文件/shell 或硬件。一步 AI Undo 和 pre-AI snapshot 可恢复草稿，恢复不发布。对话仅 session-memory，退出后丢失；SSE 与跨重启历史延后。

Test Bench 默认 LOCAL SIMULATION ONLY，复用效果 JS 模拟与确定性时间，支持 Play/Pause/Step/Reset/Loop，以及按键轻按/长按/连按、CS2 health 等场景。JSON 场景导入严格校验；不会发送真实 HID。能力信息由实际工程图推导，帮助筛选相关 AI context 和推荐场景，不相信模型自称能力。

自动保存使用 data-root（尊重 AURA_DATA_ROOT）和原子写入；恢复提示让用户选择 Restore / Discard，不自动覆盖。AI Apply 和 Publish 前保存 bounded snapshots（每工程最多 10 个）。最近列表也有限保存，缺失工程可正常处理。

`.auraeffect` 仅包含 manifest、Blockly/source 工程及元数据，不含插件 DLL、密钥、日志、恢复 journal、快照或最近列表。导入显示摘要并要求明确确认，再创建独立草稿，不覆盖现有工程、不编译、不 Publish。导出 created_with_version 由实际 WinUI 产品版本写入，源头是 VERSION。

Known limitation：不支持/跨工作室 malformed graph 在手工构造的错误路径上可能产生空白编辑器或 context timeout，而非优雅的不支持工程诊断。正常支持工程和生产 bundle 导入路径已验证；此 robustness backlog 本轮不修。Native 125% DPI 尚未人工验收，可选且不阻塞本次候选。

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
