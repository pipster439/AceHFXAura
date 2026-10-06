# alpha.8 Conversation Response Compatibility

当前状态：CONVERSATIONAL_PROVIDER_COMPAT_PASS / ALPHA8_FEATURE_ACCEPTANCE_PASS / READY_FOR_VERSION_FREEZE。最终 canonical 18/18、桌面 mock 17/17 与所有必须 Owner gates 通过，本地 checkpoint 已完成。仅准备就绪，尚未 version freeze。

ALPHA8_CONVERSATION_COMPAT_COMMIT：`3954c3d819ed308fe16e29f406bb079934e86456`。Commit 后 480 个冻结输入无变化，已有报告改动未混入 commit。

起点：`310b164b09a0019cbb661924f3fa0aeddb09ebc9`。起点已有三份验收/Polish 报告改动，本轮已按字节和独立 Git tree 记录；本轮 patch 以该 tree 为基准，排除这些既有改动。本轮无真实 HID、协议改动、SSE、聊天持久化、版本号变更、产品 package、push/tag/release。

## 结论与主要变更

- 独立 `AssistantText` 与 `TypedToolCall`。普通 provider text 不需要 envelope，仅作为聊天文字，不能变成 proposal、Apply 或 Publish。
- 完整 envelope 仍通过原有严格 parser、字段白名单、工具白名单与参数限制。一个动作非法则该 envelope 全部工具不执行，安全且唯一的 `message` 可显示；原生 UI 显示动作拒绝通知，并取消任何同轮已准备的候选。
- 不从 prose、markdown code block 或“把 3 改 2”文字提取动作。损坏 JSON、二进制/控制字符、无效文字编码、过大响应仍报错。有效文字可含拒绝说明；本地原有密钥/路径过滤仍生效。
- Settings 增加 Auto / JSON Object / JSON Schema；默认 Auto。旧 StructuredOutput=false→Auto、true→JSON Schema，新保存仅写显式 mode。服务 endpoint/凭据 target 不变，迁移不读取、删除或重写密钥。
- JSON Object 只使用通用 `response_format: json_object`，JSON Schema 保留已有严格 schema；没有 DeepSeek 特判。HTTP 400 响应模式错误提示用户切换，不自动发第二次请求。
- 请求/响应/工具大小上限、最多四轮工具、指纹 stale 检查、独立候选验证、显式 Apply、pre-AI snapshot、Undo、用户 Publish 和 session-memory 边界保持。

DeepSeek 官方 JSON Output 指南明确提供通用 JSON Object 模式和 JSON prompt 要求；真实验收优先使用该模式，但不由文档推断实际验收已通过：[官方指南](https://api-docs.deepseek.com/guides/json_mode/)。

## 验证

- targeted C# Studio：103/103 PASS，0 skip。
- targeted frontend Studio：45/45 PASS，0 skip。
- WinUI Release x64：0 warning、0 error；既有 WinUI analyzer 保留。
- 本轮冻结 build inputs：480 个，包含新增兼容性测试。
- 桌面 mock：17 phases / 17 loopback requests PASS。发现并修复原始 JSON 路径过滤破坏边界的问题；新增 provider-through-orchestrator 过滤回归，safe message 保留且动作拒绝。最终 canonical 18/18 PASS，0 unexpected NOT RUN；build 20261006-111459-b2c31f3c，480 个输入一致。385 .NET tests、25 CTest、45 daemon integration PASS；frontend 97 PASS / 1 既有 portable frame harness skip，未新增 skip。最终构建 desktop mock 17 phases / 17 requests PASS。初次 canonical 与首次失败 mock 均为 preliminary，不用于最终验收。

新增测试覆盖纯文字、错误解释/工具结果后文字、limitation、prose 假 Apply、markdown 假动作、非法动作保留文字、未知工具与合法 sibling 全部拒绝、超大参数、路径/多余字段、重复消息键、损坏/二进制/过大/无效编码响应、三种 mode、legacy 迁移、HTTP 400 单请求无重试和原有 secret/path 过滤。原有 max-round/cancel/stale/color-lock/regression tests 保留。

桌面 mock 新增：纯文字错误说明、文字+合法 typed proposal、同轮验证候选后返回非法动作+安全文字；看见聊天文字并拒绝动作，草稿与 durable state 不变，17/17 phases PASS。刻意取消连接时 mock server 的 Win10053 是预期 fixture 日志；native result error=null 与取消断言仍必须通过。

## 风险与未解决事项

模型普通文字可能错误声称已修改；文字永远不赋予权限，实际草稿和卡片才是变更依据。纯文字回复不保证内容质量，不从文字恢复非法 proposal。Malformed 结构没有可验证说明时只显示通用拒绝；不显示原始危险参数。

真实 error chat/follow-up、Owner 原生快捷键、exact-key local hygiene、生产 Save/Open bundle pickers 均通过。错误对话使用 prompt 引用的真实历史 response/tool 诊断，不声称 compiler-error fixture 验收。Native Windows 125% DPI 可选，不阻塞。

Owner 密钥自检脚本只由 Owner 本地运行，隐藏输入，不调用 Credential Manager，不输出匹配内容；只记录计数与 PASS/FAIL。Agent 不读取真实 key、Authorization header、Owner data-root 全量内容或带密钥设置截图。

## 证据位置

`audit_artifacts/alpha8_conversation_compat`：baseline、480 输入哈希、targeted TRX/日志、canonical、桌面 mock、Owner 脱敏观测与最终交付。交付 patch 必须只含本轮增量；证据 ZIP 从独立 staging 构建，仅包含报告、日志、manifest、hashes、截图及必要小型证据，无源码仓库、构建二进制、依赖、缓存、历史 archive 或 Owner 私有 data-root；SHA-256 放在 ZIP 外。

## Real compatibility acceptance update

REAL_ERROR_CONVERSATION_PASS：api.deepseek.com / deepseek-flash，原生设置 JSON Object，Owner 既有凭据继续可用，未重新输入、读取或导出 key。两轮普通聊天正常显示，解释真实历史响应结构诊断，再说明现有安全工程工具不能修改客户端解析规则；没有为了修复而改周期/颜色，没有 proposal/Apply/Publish，当前 config SHA-256 与原始基线一致。这里的诊断是 prompt 引用的真实历史原生 response/tool 校验消息，不能宣称真实 compiler-error fixture 验收。

一个手工加入跨工作室积木的独立错误 fixture 在切换时导致编辑器空白/上下文超时，未进入 provider 请求；该 fixture 已撤回、原配置恢复，随后用干净 isolated data-root 及相同 protected credential target 完成上述真实对话。该 unsupported fixture 现象尚未定位，不纳入 PASS 证据；正常支持的工程/生产 picker 验收仍需独立确认。

原生快捷键已由 Owner 实际按键确认弹出并正常 Esc 关闭（NATIVE_COMMAND_PALETTE_SHORTCUT_PASS）。生产 Save/Open picker 已通过：Owner 导出并导入，显式确认生成 owner_effect_import；原生状态验证通过、尚未构建、未发布。原工程与其他配置字段未改变；config 仅增加新草稿记录。真实 key 本地 exact 自检已由 Owner 完成并回复 PASS：187 个 payload、0 命中，包含本轮及此前 Owner 数据根、真实导出 bundle 与本轮/此前交付证据。Agent 未读取 key，未访问 Credential Manager。最终 ALPHA8_FEATURE_ACCEPTANCE_PASS / READY_FOR_VERSION_FREEZE。


证据打包时仅脱敏 TRX/日志中源码 DataRow 的已知合成凭据测试字面值，记录原文件 SHA-256 和脱敏计数；保留测试结论与所有断言，不放宽密钥模式检查。


## Final Owner hygiene and stop

REAL_KEY_HYGIENE_PASS：Owner 隐藏本地输入，exact 检查 187 payloads / 0 命中，真实 bundle SHA-256 `f3b6f3f2239dffc374b86b7a4dbc68feb1dc46cd7575fd2f0d4f00c9b5040e45`。只保留计数/布尔/SHA-256 receipt，未读取或记录 plaintext key。最终报告与 receipt 仅由公开状态和计数生成，不添加真实聊天、key 或 headers。Native 125% DPI 为 NOT RUN（optional），unsupported 手工错误 fixture 的空白现象仍列作后续诊断项；正常支持的生产 bundle 路径通过。

ALPHA8_FEATURE_ACCEPTANCE_PASS / READY_FOR_VERSION_FREEZE。停止，等待 Owner 单独授权 alpha.8 version freeze；没有 VERSION bump、产品 package、push/tag/release、SSE、chat DB、alpha.9 或硬件输出。
