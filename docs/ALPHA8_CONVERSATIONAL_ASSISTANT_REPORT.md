# alpha.8 Conversational Studio Assistant

Status: **CONVERSATIONAL_STUDIO_ASSISTANT_PASS / CONVERSATIONAL_PROVIDER_COMPAT_PASS**. Current canonical 18/18 and native mock 17/17 PASS. Real error chat, native shortcut and production Save/Open picker PASS; Owner exact-key hygiene PASS (187 payloads / zero matches). Final status: ALPHA8_FEATURE_ACCEPTANCE_PASS / READY_FOR_VERSION_FREEZE; version freeze has not been executed.

ALPHA8_AI_MVP_COMMIT: `616c05af8efcdfffab571a8039e4ad9b11aa877f`
ALPHA8_TOOLING_COMMIT: `6070e6232c5eab3c76f86ac4155468d03a4f92da`
ALPHA8_POLISH_COMMIT: `53cdbb68df0673d2fca977dfcf6aa97bcc2aae16`
ALPHA8_CONVERSATIONAL_AI_COMMIT: `310b164b09a0019cbb661924f3fa0aeddb09ebc9` (`feat: add conversational Studio assistant`). Hook changed no build input; post-commit working tree was clean and diff check passed.

Phase 0: all 25 previous Polish source/report hashes and 468 build inputs matched accepted evidence; diff clean. C# Studio 64/64 and frontend Studio/Polish 62 PASS / 1 existing skip. Authorized local checkpoint hook changed no build inputs, then clean working tree. Current task patch starts at this Polish checkpoint, excluding all previously dirty Phase 3 files.

## Architecture and boundaries

Retained native WinUI right panel now shows one conversation with user/assistant messages, multiline composer, Send/Cancel/explicit Retry/Clear and ordinary prompt suggestions. There is no action or preset mode selector, no per-turn modal, and provider configuration stays in existing Settings. Enter sends and Shift+Enter inserts a newline. Scroll follows newest messages unless the user moves away. A source-only candidate card displays summary, node/field before/after, independent validation/simulation, base SHA-256 fingerprint (short view + full tooltip), stale status, local Preview, explicit Apply/Dismiss and Test Bench. One-step AI Undo and pre-Apply snapshots still use existing paths.

StudioConversationSession is memory-only per logical project (max 20 projects, max 60 visible messages/project). Requests contain at most 24 recent user/assistant messages = 12 conversational turns, each locally bounded to 2000 characters. Notice messages are not sent. There is no conversation database, autosave extension, recovery payload, export or bundle entry.

StudioConversationOrchestrator uses provider-neutral results through existing OpenAI-compatible chat/completions. Ordinary AssistantText is read-only and may be plain text. Structured tools require the exact local envelope and strict action validation. The adapter uses Auto / JSON Object / JSON Schema; the legacy StructuredOutput bool remains read-compatible without changing credential target. Endpoint-native function calling is not required. Internal AssistantMessage / TypedToolCall / StudioAssistantTurnResult types isolate UI from provider JSON. Response <=16 KiB, arguments <=8 KiB, tool results <=16 KiB, transport request <=64 KiB, existing provider response <=128 KiB. Too-large input stops with a clear error. No provider network retry; explicit Retry starts exactly one user turn (which may include bounded tool rounds).

Allowed reads: get_current_effect_summary, get_capabilities, get_validation_errors, get_build_errors, get_active_proposal, selected get_preset. Candidate-only operations: propose_effect_change, validate_candidate, simulate_candidate. Max 4 tool rounds, at most 4 calls per round. No Apply, Publish, file, shell, git, process, HID, DLL, compiler or arbitrary topology/code tool exists. Numeric color edits and template replacement are rejected if recent conversation requires preserved colors, including the exact Owner acceptance phrase "不要改变颜色". Preset replacement requires empty edits: current-draft node IDs cannot identify preset nodes. Native and JS guards both enforce this boundary. Prompt/diagnostic/tool content never authorizes more capabilities.

JS StudioConversationTools reuses StudioProposalSession and the existing independent Blockly/C++/JS simulation validator; no second renderer. Full project graph/name/publication SHA-256 binds each turn and card. Tools recheck the current graph before executing. External edits mark a pending proposal stale, preserve conversation and supply latest draft next turn. Project switch isolates previous proposal and conversation. A pending proposal can be used as follow-up context without applying it. Apply still requires an owner button click and exact original draft equality; one-step Undo requires actual applied-draft equality. Candidate/tool data cannot write config or publish. Complete final parse and current fingerprint are required before enabling Apply. Cancel or incomplete/invalid turn invalidates candidate.

Model context: recent conversation, effective numeric nodes, graph-derived relevant capabilities, current fingerprint and minimal active proposal summary. Selected diagnostics and single preset metadata enter only through their allowed read tool. Preview RGB frames are retained locally and omitted from model history. No repository/files/arbitrary logs/local absolute paths/hardware serial/credential/unrelated GSI/all preset catalog. Existing exact-key/path redaction applies before HTTP and display. Generated templates are existing bounded presets only.

## Streaming decision

SSE is deliberately deferred as the requested optional capability. Current UI has in-flight status, cancellation and final rich-card rendering, compatible with a future incremental text callback. Endpoints without streaming remain usable through existing non-streaming transport. No partial response can become Applyable. SSE chunk/split/disconnect tests are NOT RUN because SSE was not implemented; existing transport limits/truncation/network/cancellation tests remain.

## Automated checks

Accepted canonical: `canonical-verified/ci-summary.json`, build `20261006-072550-d1e937a3`; **18/18 required stages PASS, 0 unexpected NOT RUN**. All 479 frozen inputs matched before/after tests and checkpoint. WinUI build: 0 warnings / 0 errors. Native CTest: 25/25; daemon integration: 45/45; .NET: Aura 256, AsusPlatform 84, FanTypeLibValidator 23 = 363 PASS. Frontend: 97 PASS / 1 pre-existing portable native-C++ frame harness skip (98 total); no new skip or CI stage omission.

Current-build desktop conversation smoke: **14 phases PASS, 12 loopback mock requests**, error=null. Existing Polish/tooling regression: crash 12, recover 5, clean 3 phases PASS; bundle roundtrip via isolated fixture picker delegate, summary + explicit import, command filtering/arrows/Enter/Esc + Validate, deterministic key tap and CS2 health, pause/step/reset, autosave/crash restore, pre-AI snapshot/Apply/restore, clean restart and pre-Publish failure snapshot with no reload. Production Save/Open picker and native-focus shortcut remain Owner gates.

UX: native current monitor rasterization 150%; normal/empty chat, proposal card and long model/error layouts captured. Dark/light, 840/1520 width, 125/150% WebView device-scale emulation ran all 8 cases for Bench + palette. Emulation does not prove native Windows 125% DPI. Representative proposal/empty chat/light narrow palette screenshots inspected; controls and composer remain usable.

| Conversational gate | Result / evidence |
|---|---|
| One chat / no mode selection / first-run prompts | PASS — native smoke + rewritten native panel |
| Multi-turn speed follow-up / color preservation | PASS — C# + frontend 900→650→725 ms; native 3→2→2.5 s before Apply |
| Project revision / stale / project-session isolation | PASS — full graph fingerprint, C# + JS tests |
| Typed tools / max 4 rounds / bounded request | PASS — parser/orchestrator tests, native mock tool metadata |
| Proposal / Apply / Publish user-only | PASS — authority checks, snapshots, illegal Publish rejection |
| Test Bench / actual applied comparison / Undo | PASS — current-build desktop |
| Actual error conversation / validation follow-up | PASS — C2039 selected diagnostic carried; unsupported native-member repair explicitly limited |
| Injection / unknown/apply/filesystem/shell tools / oversize | PASS — both parser/tool test layers |
| Exact-key redaction / no conversation persistence | PASS — synthetic tests + isolated JSON/bundle/journal/authority checks |
| Cancel mid-tool/in-flight / explicit Retry / Clear | PASS — C# cancellation + native received-request cancellation and one explicit Retry |
| Streaming | DEFERRED optional; no SSE implementation or SSE PASS claim |
| Node/effect references | Field/node labels in Diff; click-to-highlight deferred, non-blocking |

Targeted checks on final source: Studio C# 81/81; new frontend conversation tests 8/8. Both test layers cover 900 → 650 → 725 ms follow-up before Apply, unchanged color fields, exact Apply/Undo and stale/cancel isolation. Final canonical and current-build native smoke results are recorded above.

Desktop local mock covers explanation → proposal → unapplied follow-up → Apply → Test Bench → comparison with actual applied draft → Undo → actual C2039 diagnostic tool → bounded proposal → Publish tool rejection → one explicit Retry → cancellation after the local server received the in-flight request → Clear, with no durable/published/runtime authority changes. Provider requests are loopback only and synthetic credential is removed normally. All persisted fixture JSON is checked for the synthetic credential and no conversation store is written. A loopback mock socket can report Win10053/ConnectionAbortedError when the deliberate Cancel closes the connection; this is an expected fixture diagnostic, and the authoritative native result must still have error=null and cancellation/authority assertions PASS.

## Historical Owner real-provider acceptance before compatibility fix

Owner specified host `api.deepseek.com`, model `deepseek-flash`. Owner entered the key manually and reported Test Connection success; targeted native status also read connection success. Settings saved normally without agent key access. First real explanation returned HTTP 400 while StructuredOutput=true. DeepSeek Chat Completions documentation lists text/json_object; the optional strict JSON schema checkbox was turned off and saved. Owner explicitly authorized one retry: explanation PASS, accurately describing the 3-second cycle, both current RGB triples and no GSI. The next natural speed-change response was rejected by strict response/tool parsing; no candidate/Apply occurred. Owner explicitly authorized a new proposal: PASS, n6 color_cycle/PERIOD_SEC 3→2.6 seconds, only that field changed, colors/publication unchanged, validation/simulation PASS. Before Apply, natural follow-up slowed the previous 2.6-second suggestion to 2.8; actual card showed current-draft Diff 3→2.8 with base fingerprint prefix 809d5fc759aa. Owner explicitly approved this concrete proposal; Apply produced draft 2.8 with a before_ai_apply snapshot at 3. Owner manually completed single-key-tap Test Bench because the UI automation ownership guard rejected the cross-process WebView Play target; the guard was not bypassed. Native UI confirmed complete, 400/400 ms. Actual-draft comparison accurately explained 3→2.8 and distinguished the un-applied 2.6 suggestion. Undo restored 3 and both RGB triples. Durable config bytes/SHA-256 remained unchanged throughout. Invalid model output correctly refused is not itself a product blocker. Error chat is not yet accepted: two natural requests concerning the actual prior response-structure diagnostic were rejected by response/tool parsing, without modifying the draft. Real-key hygiene and native manual gates also remain pending. No automatic provider retry occurred.

Owner must enter the API key manually in native Settings, never in chat/evidence. Agent may fill host/model only and stops before the key. Owner manually tests Connection. Record host/model/status only, no secret/header/credential dump/screenshot while key is being entered. A guarded Owner UI entry opens Studio only, verifies an already-owned offline dry-run core, and has no automatic prompts, mock delegates, provider calls or Apply. Native production bundle file pickers remain active. Its launcher never accesses credential APIs and the isolated Owner data-root is excluded from delivery evidence.

A–E: connection success; explain in natural chat; speed up without color changes; follow-up slow slightly without first applying; inspect typed Diff/validation; Owner confirms and Apply; run deterministic Test Bench; ask what actually changed; Undo or pre-AI snapshot restore; safe actual diagnostic/error chat; verify secret hygiene across logs/diagnostics/config/project/bundle/journal/snapshots/recent/evidence (no chat export). Real invalid proposals correctly refused are not product failures.

Native manual gates: focus a native WinUI control and Owner presses Ctrl+Shift+P; real Export Save picker and Import Open picker, inspect summary/explicit confirmation/new draft/no compile/publish. Actual native 125% DPI optional; 150% current machine plus emulated matrix are distinguished in previous report. No system DPI changes without Owner action.

## Risks, corrections and deferred work

- Real provider/model quality, native-focus shortcut and production file picker are required Owner gates, not inferred from mocks; all have now been accepted, including Owner exact-key hygiene. Historical failed attempts are retained below the historical heading.
- SSE, cross-restart chat history, rich node click/highlight, new topology/code generation, hardware, cloud/marketplace and alpha.9 are deferred.
- First native fixture failed at Preview because an AutomationId lacked a matching FindName; x:Name was added. Next fixture proved diagnostic/tool data but final mock text omitted its error code; mock final answer was corrected, assertion retained. Accepted evidence must use only final current-build runs.
- Retained project uses localized native event wiring; chat business/tool policy is in testable Services/JS helpers. Lists are explicitly bounded rather than adding a database/workspace manager.
- No VERSION bump/package/push/tag/release. Two local checkpoints are authorized; final feature freeze still requires Owner acceptance.

## Evidence and deliveries

Root: `audit_artifacts/alpha8_conversation`. Report/logs/TRX/CI manifest/input hashes/owned screenshots and mock request metadata only. Task-only patch excludes the pre-existing Polish checkpoint. Evidence archive is built from a fresh independent staging directory; no nested archives, full repository, dependency/cache/build binaries, unrelated historical output or credentials. External SHA-256 accompanies archive and patch.

## Original conversational checkpoint changed files (historical)

17 implementation/test files plus three report updates. The conversational checkpoint contains implementation/tests only; acceptance/report files are subsequently reviewable documentation changes. Task patch starts at the Polish checkpoint, so none of the original uncommitted Phase 3 Polish changes are mixed into it.

- `frontend/src/components/EffectStudio.jsx`
- `tools/ci/run-studio-ai-smoke.py`
- `tools/ci/run-studio-polish-smoke.py`
- `tools/ci/run-studio-tooling-smoke.py`
- `winui/App.xaml.cs`
- `winui/Pages/StudioPage.Assistant.cs`
- `winui/Pages/StudioPage.xaml`
- `winui/Validation/StudioAiValidation.cs`
- `winui/Validation/StudioPolishValidation.cs`
- `winui/Validation/StudioToolingValidation.cs`
- `frontend/src/utils/studioConversation.js`
- `frontend/tests/studio-conversation.test.mjs`
- `tests/Aura.Tests/StudioConversationTests.cs`
- `tools/ci/studio_conversation_mock.py`
- `winui/Services/StudioConversation.cs`
- `winui/Validation/StudioConversationValidation.cs`
- `winui/Validation/StudioOwnerAcceptance.cs`

## Conversation response compatibility follow-up

本轮增加独立只读 AssistantText 和 Auto / JSON Object / JSON Schema 模式，严格动作验证保持；非法动作可保留安全说明文字，任何同轮候选取消。原始 JSON 的密钥/路径过滤改为结构保留的字符串过滤，以免破坏 envelope。损坏/二进制/超限仍拒绝，不从 prose/markdown 提取动作。

Targeted C# Studio 103/103、frontend Studio 45/45、WinUI 0 warning/0 error；桌面 mock 17 phases/17 requests PASS。本轮最终 canonical 18/18 PASS，0 unexpected NOT RUN，480 输入无变化；最终构建 native mock 17/17 PASS。ALPHA8_CONVERSATION_COMPAT_COMMIT：`3954c3d819ed308fe16e29f406bb079934e86456`，commit 无输入漂移。真实 error chat、native shortcut、Owner exact-key hygiene 与真实 picker 均通过。详见 ALPHA8_CONVERSATION_COMPAT_REPORT.md。

## Real compatibility acceptance update

REAL_ERROR_CONVERSATION_PASS：api.deepseek.com / deepseek-flash，原生设置 JSON Object，Owner 既有凭据继续可用，未重新输入、读取或导出 key。两轮普通聊天正常显示，解释真实历史响应结构诊断，再说明现有安全工程工具不能修改客户端解析规则；没有为了修复而改周期/颜色，没有 proposal/Apply/Publish，当前 config SHA-256 与原始基线一致。这里的诊断是 prompt 引用的真实历史原生 response/tool 校验消息，不能宣称真实 compiler-error fixture 验收。

一个手工加入跨工作室积木的独立错误 fixture 在切换时导致编辑器空白/上下文超时，未进入 provider 请求；该 fixture 已撤回、原配置恢复，随后用干净 isolated data-root 及相同 protected credential target 完成上述真实对话。该 unsupported fixture 现象尚未定位，不纳入 PASS 证据；正常支持的工程/生产 picker 验收仍需独立确认。

原生快捷键已由 Owner 实际按键确认弹出并正常 Esc 关闭（NATIVE_COMMAND_PALETTE_SHORTCUT_PASS）。生产 Save/Open picker 已通过：Owner 导出并导入，显式确认生成 owner_effect_import；原生状态验证通过、尚未构建、未发布。原工程与其他配置字段未改变；config 仅增加新草稿记录。真实 key 本地 exact 自检已由 Owner 完成并回复 PASS：187 个 payload、0 命中，包含本轮及此前 Owner 数据根、真实导出 bundle 与本轮/此前交付证据。Agent 未读取 key，未访问 Credential Manager。最终 ALPHA8_FEATURE_ACCEPTANCE_PASS / READY_FOR_VERSION_FREEZE。


## Final Owner hygiene and stop

REAL_KEY_HYGIENE_PASS：Owner 隐藏本地输入，exact 检查 187 payloads / 0 命中，真实 bundle SHA-256 `f3b6f3f2239dffc374b86b7a4dbc68feb1dc46cd7575fd2f0d4f00c9b5040e45`。只保留计数/布尔/SHA-256 receipt，未读取或记录 plaintext key。最终报告与 receipt 仅由公开状态和计数生成，不添加真实聊天、key 或 headers。Native 125% DPI 为 NOT RUN（optional），unsupported 手工错误 fixture 的空白现象仍列作后续诊断项；正常支持的生产 bundle 路径通过。

ALPHA8_FEATURE_ACCEPTANCE_PASS / READY_FOR_VERSION_FREEZE。停止，等待 Owner 单独授权 alpha.8 version freeze；没有 VERSION bump、产品 package、push/tag/release、SSE、chat DB、alpha.9 或硬件输出。
