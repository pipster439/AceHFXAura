# alpha.7 真机验收：诊断字段对应表

Phase 4A 增加独立的 `automation` 决策诊断区，详见[决策字段与状态表](../architecture/DEVICE_PROFILE_AUTOMATION_STATES.md)。它不代表 selected/active，也不会应用配置、探测 HID 或解除隔离。自动硬件切换在本阶段始终禁用。

## 获取方式与证据边界

在原生 WinUI 的「配置文件 → … → 高级诊断…」中点击「复制 JSON」或「导出文件…」。每次「重新读取」只请求 `GET /api/device-profiles/diagnostics`；不保存 Profile、不应用设置、不打开/探测 HID、不 acknowledge quarantine。

API envelope 为 `status / api_version / diagnostics`；UI 导出的是 `diagnostics` 对象。`diagnostic_schema_version = 1`，`captured_at_utc` 是导出时的 UTC 时间。缺失/未知信息用 `null` 表达。

Phase 4A.5 新增 `daemon.process_instance_id`（每次 daemon 生命周期生成一次的随机 GUID）和 `daemon.started_at_utc`；`product_version` 保持原有含义。同一进程多次导出应相同，重启后 GUID 应变化。它不包含机器、用户、PID 或路径信息。比较累计 timing 前先确认进程 GUID 相同。

这是 **host observation / host-submission only**。数字不是 firmware readback；命令提交成功也不是物理行为 PASS。请分别保存「操作前」「操作后」两份文件，再记录实际按键行为/延迟/灯效观察。

诊断不主动发现拔插。`connected_now`、`fresh_session_now` 固定为 `null`，因为导出没有执行当前物理 presence probe。正常 runtime 的已有 lifecycle 检查观察到变化后，诊断才反映该变化。不要把未变化的缓存当成键盘仍在线。

## 字段对应表

以下路径均相对导出的 JSON 根对象。

| 字段 | 含义 | 真机应观察什么 / 不应推断什么 |
| --- | --- | --- |
| `daemon.product_version` | 编译时项目 VERSION | 记录实际版本；当前开发工作树仍沿用 `0.1.0-alpha.6`，不能单靠这个字符串区分 alpha.7 未提交开发版本。 |
| `daemon.profile_api_version` | Profile API contract，当前 1 | 旧 daemon 缺少诊断 route 时 UI 报后台版本不支持，不回退到本地 Profile writer。 |
| `runtime.document_available / document_error` | daemon 是否已加载合法 document | corrupt document 时仍可导出，document revision / baseline 为 null，原文件保留。没有导出原文件错误中的路径/内容。 |
| `runtime.selected_profile_id` | durable desired Profile GUID | 手动磁轴写入、拔插或失败后保持；切换 Profile 时改变并持久化。 |
| `runtime.active_profile_id` | Profile Runtime 记录的完整提交过的 Profile | 成功且当前 session 有效才可解释为 host 已提交。失败/正常 invalidation 后为 null；不能推断 firmware 状态。 |
| `runtime.dirty` | desired configuration 尚无法确认完整提交 | 成功时 false；manual mutation、正常 session invalidation、失败/延后时 true。 |
| `runtime.deferred` | 最近一次 activation outcome 是否为 deferred | 只是最后一次请求的结果，不代表后台将自动重试；键盘重新可用后仍须手动应用。 |
| `runtime.document_revision` | 文件内容修订号 | 保存/改名/CRUD/selected 改变/成功 manual global baseline 更新时增加；单纯诊断不变。 |
| `runtime.runtime_revision / mutation_revision` | 当前实现中同一 mutation counter 的两个 API 名称 | activation / invalidation / document mutation 后单调增加；不是两个独立计数器。诊断不增加。 |
| `runtime.session_generation` / `m605.session_generation` | 同一个 M605 transport epoch | 仅在本 daemon process 内单调递增，不跨进程持久化，也不是拔插次数。一次 discard + reopen 可能增加两次。 |
| `runtime.profile_observed_session_generation` | Profile owner 最后已消费的 epoch | 与 M605 generation 相同表示已消费。 |
| `runtime.session_invalidation_pending` | cached Profile epoch 与当前 M605 epoch 不同 | true 时旧 active/dirty 缓存尚未由正常 runtime 查询消费；诊断本身不会修改它。 |
| `runtime.active_intent_current_session` | active intent 对当前 session 仍有效，且 health Clean / 未 quarantine | 即使 cached active ID 非空，也必须看这个字段；false 不应报告本 session clean/已应用。仍不是 firmware confirmation。 |
| `m605.health` | Clean / TransactionInProgress / IndeterminateStagedState / PersistentSafetyQuarantine / Stopped | staged failure 仍进入 unknown/quarantine；不因诊断恢复。 |
| `m605.persistent_safety_quarantine` | 当前安全 latch 是否阻止事务 | true 时先保存诊断，遵循已有显式恢复流程；拔插/重启/导出都不是 acknowledge。 |
| `m605.transport.open_at_last_lifecycle_observation` | 最近一次 lifecycle 记录中 M605 handle 是否打开 | true 不代表此刻物理在线。latch-clear failure 时 handle 可仍 open，但 health 不健康。 |
| `m605.transport.last_open_generation` | 上次验证/open transport 时的 epoch | 配合 transitions 看 reopen；不证明 firmware reset、override table 清空或 latch 可清除。 |
| `m605.transport.connected_now / fresh_session_now` | 本次导出没有实时检测 | null 是刻意的 unknown，不能当 false/true。没有借用 lighting connected snapshot。 |
| `m605.session_transitions` | 最近 16 个 process-local generation 变化 | reason 为 `transport_opened`、`stale_transport_discarded`、`transaction_indeterminate`、`external_resynchronization_transport_opened`。最后一种仅记录已有显式恢复路径，不由诊断触发。 |
| `m605.session_applied` | 完整 stage + apply + settle 的 host submission shadow | `source` 明确不是 readback；raw 数值按 0.1 mm。清空表示知识失效，不是设备恢复默认。 |
| `m605.session_applied.per_key_deadzone_table_known` | verified type 4 reset 完整提交后的 host 知识 | 与各单键记录配合观察；不是读回表。 |
| `m605.session_applied.external_override_tables_known` | 所有外部软件 override tables 的完整知识 | 当前 false；尤其不能由 AllKey Actuation 提交推断 per-key override flags 清空。 |
| `durable_magnetic_baseline` | document root `global_defaults` 的已知 desired values | Profile apply 不应污染它。未知为 null，不猜 1.0 mm。忽略未知扩展字段。 |
| `last_activation.resolved_baseline_at_planning` | 上次规划时 root + trusted saved HostProfile fallback | 使用规划时已读过的资料；诊断不重新读 ASUS 用户配置。不是当前文件实时读取，更不是 firmware readback。 |
| `active_profile_effective_values` | 当前 active 的 managed effective target，从现有 applied-intent map 导出 | 只含 managed fields 的类型、logical IDs、值，不是全键盘完整 getter。无 active 则 null；还须检查 `active_intent_current_session`。 |
| `last_activation.reason / outcome / error` | 最近一次完成 activation 的原因及结果 | Manual / Automation / Startup / Reconnect / Restore；succeeded / deferred / failed。导出不触发 activation。 |
| `last_plan.operation_count / operation_kinds / operations` | 最近一次实际生成的 plan | 顺序为真实 planner 顺序；no-op count=0。后续 deferred 未生成新 plan 时保留旧 plan，需检查其 GUID / revision / generation。 |
| `last_activation.plan / effective_target` | 最近这次 activation 的 plan 和目标 | pre-planning deferred 为 null；preflight blocker 的 target 也只是目标，不代表执行。 |
| `last_activation.operations` | 原事务及 best-effort recovery 的逐操作结果 | `prior_intent_resubmission` 区分 recovery；失败不意味着真实 ACID rollback。 |
| `last_failed_operation` | 最近 Profile operation failure，跨后续成功保留 | GUID / mutation revision / session generation 给出历史归属。preflight failure 没有 HID operation 时不会伪造失败操作。 |
| `m605.last_failed_operation` | 最近一个已执行 worker job 的失败，含 manual routes | 对照 logical ID、kind、epoch、Win32；后续成功不会清掉这段历史。不是当前仍在失败的声明。 |
| `last_failed_operation.win32_error` / `m605.last_failed_operation.win32_error` | 从 backend 明确 Win32 marker 提取的数值 | 例如 1167；缺少 marker 为 null。绝不在 HTTP/export thread 调用 GetLastError 猜测。 |
| `m605.last_error / last_error_win32` | M605 当前最近错误文本/显式 Win32 code | 成功可清空 last_error，历史 failure 仍保留；带路径文本整体省略，普通错误最多 512 UTF-8 bytes。 |

## Timing 对应表

所有时间单位均为毫秒，属于软件计时，不是 LED/MCU acknowledgement。mock 的 settle 时间不能替代真机 timing。

| `last_activation.timing` 字段 | 解释 |
| --- | --- |
| `total_ms` | coordinator 内 activation 总时长，不包含此前等待 coordinator gate 的时长。 |
| `api_processing_ms` | HTTP activation 从 route 入口至 coordinator 返回的时长，包含 gate wait / request parsing；不含响应网络传输。非 HTTP 调用或未记录时 null。 |
| `document_ms` | activation 改变 selected 时的持久化时间；不是所有 CRUD 的累计时间。 |
| `planning_ms` | BuildPlan 耗时。 |
| `profile_overhead_ms` | 现有测量 total 减 M605 / document 的 remainder，包含 planning；不要把它再与 planning 相加。 |
| `planned_operation_count / planned_operation_types` | 该 activation 实际规划数量和类型分布。 |
| `executed_operation_count / executed_operation_type_ms` | 已执行操作数量、按类型累计等待时间；包含 recovery。 |
| `m605_transactions / m605_total_ms` | activation 时间窗口内 worker 完成 job 数量/累计时间。 |
| `queue_wait_ms / device_lock_wait_ms` | M605 队列等待 / 与 lighting 共享 mutex 的等待。 |
| `transport_connect_ms / safety_latch_ms` | worker session/connect 检查 / durable latch I/O。activation 的前置 PrepareTransportSession 不计入 worker connect delta，它落在 total / remainder 中。 |
| `hid_stage_submit_ms / hid_apply_submit_ms` | host stage/apply submit call 时间，不是 firmware ACK。 |
| `inter_stage_settle_ms / pre_apply_settle_ms / post_apply_settle_ms` | 已有强制 settle 计时；本轮没有修改这些间隔。 |

`m605.cumulative_timing` 是 daemon 当前 process 内所有完成 worker jobs 的累计值。不要与来自不同 `daemon.process_instance_id` 的文件做数值差；不收集 PID/进程列表。诊断导出与 Profile apply 共用 coordinator gate，因此读取会等进行中的事务完成，**不是 live progress API**。

## 验收操作对应

| 操作 | 应保存与比较的字段 | 安全预期 |
| --- | --- | --- |
| 空闲导出两次 | revision、generation、shadow、quarantine、plan；允许 timestamp 不同 | 全部业务状态不变，没有 transaction/connect/probe；正常 lifecycle 可能独立变化，不能归因于导出。 |
| 拔插发生在新 transaction 之前 | observed/current generation、transitions、active validity、shadow | 已有 lifecycle 检查 discard/reopen 后 generation 增加，旧 shadow 清空，Profile 正常消费后 active null / dirty true，selected 保留；没有自动 reconnect apply 承诺。 |
| disconnect 发生在 stage 成功之后 | health、quarantine、generation、failed operation、Win32、shadow | unknown/dirty，禁止自动 retry staged sequence。1167 只是错误码，不能一律忽略。 |
| quarantine 后重启 Aura | health、quarantine、selected、active、dirty | generation 从新 process 起算；persistent latch 仍阻止 writes，active 不从上次 session 继承。不要以重启冒充恢复。 |
| 在 Profile 中应用设置 | durable baseline、last plan、outcomes、timing、active validity | 成功后 active=selected / dirty=false / valid=true；durable baseline 不因 Profile default 改变。当前未验证 Actuation inheritance reset 仍 fail closed，不等待/放行 type 1。 |
| 重复 clean Profile apply（通过 API/已有测试，不是主按钮） | plan count、m605 transaction delta、outcome | 已知 clean intent 下 0 operation；不是强制 replay。主 Apply 按钮状态保持原有 CanApply fix。 |
| 成功 manual global mutation | baseline、document revision、mutation revision、selected、active、dirty、shadow | baseline 更新且 document revision 增加；selected 不变、active null、dirty true；shadow 只反映成功 host submission。 |
| 成功 manual per-key mutation / partial batch | mutation revision、selected、active、dirty、worker failure、shadow | selected 不变；active unknown / dirty true；不把 partial batch 当成完整 Profile。document baseline 不被单键写入改变。 |
| 失败 manual global mutation | baseline、health、quarantine、Win32、dirty | baseline 不记录未知结果；事务已进入 stage 时保留 quarantine。 |
| verified Deadzone type 4 路径 | `last_plan.operations`、table_known、per-key shadow、baseline | 原有 reset → common → exceptions 顺序不变；不增加任何新的 reset。 |

## 仍需真机验证

- 原生菜单、对话框、复制、文件 picker 的实际桌面行为（Light / Dark / 窄窗口）。本轮编译与 mock tests 不标 UI pixel-perfect PASS。
- 拔插的实际 detection 时机与 generation 变化、真实 WriteFile 1167 的结果、事务中 removal quarantine。
- 成功/失败 manual mutation 的真实按键行为；Profile apply 的实际延迟和灯效影响。
- resetType 1 的官方实机证据仍独立等待 Owner 审查，本轮没有 builder / allowlist / 实体发送。
