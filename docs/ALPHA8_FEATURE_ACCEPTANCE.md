# alpha.8 Feature Acceptance

当前状态：**ALPHA8_FEATURE_ACCEPTANCE_PASS / READY_FOR_VERSION_FREEZE**。实现、完整 canonical、mock 与全部必须 Owner 验收闭合；尚未执行 version freeze。

## Checkpoints

| 项目 | 本地 commit |
|---|---|
| AI MVP | `616c05af8efcdfffab571a8039e4ad9b11aa877f` |
| Studio Tooling | `6070e6232c5eab3c76f86ac4155468d03a4f92da` |
| Studio Polish | `53cdbb68df0673d2fca977dfcf6aa97bcc2aae16` |
| Conversational Assistant | `310b164b09a0019cbb661924f3fa0aeddb09ebc9` |
| Conversation Compatibility | `3954c3d819ed308fe16e29f406bb079934e86456` |

## 必须项

| Gate | 状态 | 证据 |
|---|---|---|
| CONVERSATIONAL_STUDIO_ASSISTANT_PASS | PASS | 既有 multi-turn/tool/stale/explicit-Apply/Undo/cancel 安全层及当前回归 |
| CONVERSATIONAL_PROVIDER_COMPAT_PASS | PASS | C# Studio 103/103、frontend Studio 45/45、native mock 17/17 |
| canonical CI | PASS | `build/ci/20261006-111459-b2c31f3c`，18/18 required PASS，0 unexpected NOT RUN；480 输入无漂移 |
| REAL_CONVERSATIONAL_LLM_ACCEPTANCE_PASS | PASS | Connection、自然解释、multi-turn proposal/Diff/Apply/Bench/comparison/Undo、错误对话与 Owner exact-key hygiene 均通过 |
| REAL_ERROR_CONVERSATION_PASS | PASS | JSON Object 两轮正常解释实际历史 response/tool 诊断并说明工具限制，无 proposal/Apply/Publish |
| REAL_KEY_HYGIENE_PASS | PASS | Owner 隐藏输入 exact 自检 187 payloads / 0 matches，并回复 PASS；Agent 不读取 key |
| NATIVE_COMMAND_PALETTE_SHORTCUT_PASS | PASS | Owner 在 native composer 实际 Ctrl+Shift+P 弹出，Esc 正常关闭 |
| REAL_FILE_PICKER_PASS | PASS | Owner 真实 Save/Open、摘要/显式确认生成 owner_effect_import；验证通过、尚未构建、未发布 |
| NATIVE_125_DPI | NOT RUN — OPTIONAL | 不阻塞；未改变 Windows 缩放 |

## Provider 与实际多轮验收

Host `api.deepseek.com`，model `deepseek-flash`，当前 response mode JSON Object。Owner 手动输入的既有 protected credential 继续使用，Agent 未读取、导出或截图 key，没有自动重试真实 provider。

此前严格 schema HTTP 400 和非法 proposal 均正确拒绝；只有 Owner 明确授权后才显式重试。合法提案仅修改 n6 color_cycle/PERIOD_SEC：当前 3.0→2.6 秒；尚未 Apply 的 follow-up 调整为 2.8 秒，实际 Diff 3.0→2.8。RGB (0,180,255)/(220,0,160)、continuous/fade_out_ms=0 保持，validation/simulation 通过。Owner 批准具体提案才 Apply；pre-AI snapshot 保留 3.0。Owner 播放单键轻按，原生状态完成、400/400 ms；实际差异回复区分未应用的 2.6。Undo 恢复 3.0 和原 RGB，持久配置未改变。

本轮兼容性修复后，用 prompt 引用实际历史原生 response/tool 校验消息，模型正常解释协议响应拒绝与有效工程的区别，读取 validation 结果为空；“能帮我修吗？”正常说明安全工程工具不能修改客户端解析协议，拒绝无关周期/颜色修改。两轮均 text-only，没有协议 fatal error、candidate、Apply 或 Publish。这里不声称真实 compiler-error fixture 验收。

Owner 用生产 Save/Open picker 导出后导入，显式确认生成 owner_effect_import。原工程与其他配置字段相等；config 只新增独立工程草稿记录，不把新增 draft 误报成 published 状态，也不要求整个 config 文件 hash 保持。

## 当前架构与边界

普通 AssistantText 是只读文字；完整结构化 TypedToolCall/ProposedChange 仍经过严格白名单、参数大小、候选图/模拟验证和 fingerprint。非法动作保留安全说明文字但不执行，任何同轮候选取消；不从 prose 或 markdown 猜测动作。Apply/Publish 均由用户显式操作。Auto / JSON Object / JSON Schema 使用通用 provider capability；兼容旧 StructuredOutput 设置，不改变 credential target，不因 HTTP 400 自动第二次付费请求。最多四轮工具和所有原有 request/response limits 保持。

Context 只包含 bounded recent chat、相关当前 nodes、graph-derived capabilities、fingerprint、active proposal summary，以及按工具需要选取的诊断或单个 preset。不发送整个仓库、任意日志、路径、hardware serial、API key、无关 GSI 或全部 presets。聊天仅 session-memory，不持久化、不导出、不进入 bundle/journal/snapshot。

## 验证与风险

Final canonical：18/18 required PASS、0 unexpected NOT RUN；385 .NET、25 CTest、45 daemon PASS；frontend 97 PASS / 1 既有 portable frame harness skip。当前最终构建 native mock 17 phases / 17 loopback requests PASS。480 个源码输入与 commit 后均无漂移，diff check PASS。Preliminary CI 与首次失败 mock 不作为最终 PASS 证据。

手工注入跨工作室积木的 unsupported 错误 fixture 曾在切换时产生空白/上下文超时，未进入 provider；已撤回并验证原配置恢复。原因未定位，保留为后续诊断项，不作为真实错误对话 PASS 证据。正常支持的生产 bundle 导入路径此次验证通过。

模型文字可能错误声称已修改，实际草稿/提案卡片/authority 才是依据。正确拒绝非法模型提案不是产品 blocker。Owner exact-key 自检已独立确认；合成测试和结构扫描不是该实际验收的替代。

## 交付与停止边界

本轮兼容性报告：`docs/ALPHA8_CONVERSATION_COMPAT_REPORT.md`。单独 task-only patch 和证据 ZIP 在 `audit_artifacts/alpha8_conversation_compat/delivery`，以任务开始的 private Git tree 排除已有未提交报告改动；当前源码 checkpoint 包含 10 implementation/test files。证据从 fresh staging 打包，含报告、最终日志/TRX/manifest/hashes/mock screenshots 与本地自检脚本；不含 Owner data-root、原始真实聊天、凭据、构建二进制、依赖、旧嵌套 archive。SHA-256 在包外。

没有真实 HID、hardware protocol change、VERSION bump、产品 package、push、tag、release、SSE、chat DB 或 alpha.9。全部必须项闭合后只声明 readiness，等待 Owner 单独授权 version freeze。


## Final Owner hygiene and stop

REAL_KEY_HYGIENE_PASS：Owner 隐藏本地输入，exact 检查 187 payloads / 0 命中，真实 bundle SHA-256 `f3b6f3f2239dffc374b86b7a4dbc68feb1dc46cd7575fd2f0d4f00c9b5040e45`。只保留计数/布尔/SHA-256 receipt，未读取或记录 plaintext key。最终报告与 receipt 仅由公开状态和计数生成，不添加真实聊天、key 或 headers。Native 125% DPI 为 NOT RUN（optional），unsupported 手工错误 fixture 的空白现象仍列作后续诊断项；正常支持的生产 bundle 路径通过。

ALPHA8_FEATURE_ACCEPTANCE_PASS / READY_FOR_VERSION_FREEZE。停止，等待 Owner 单独授权 alpha.8 version freeze；没有 VERSION bump、产品 package、push/tag/release、SSE、chat DB、alpha.9 或硬件输出。
