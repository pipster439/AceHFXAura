# Device Profile Automation state table — Phase 4B

BindingEngine resolves only. Coordinator offers stable decisions to Runtime
authority. Selected/active/dirty stay Runtime truth.

| Input / result | Coordinator | Hardware |
| --- | --- | --- |
| Debounce / unknown foreground | WaitingForDecision | none |
| Disabled / configuration unavailable | WaitingForDecision | none; no reset |
| No match without fallback | WaitingForDecision | none; keep current |
| Invalid target / deleted target | TargetMissing | none; no lower rule/fallback |
| ManualHold | WaitingForDecision | none until stable known foreground change |
| Stable Match / explicit Fallback | Preflight → Activating | existing Runtime after final gate/safety/freshness checks |
| Consumed successful identity | retained result / Idle | no duplicate call |
| Fully submitted clean target | no-op | zero operations after Runtime checks |
| Stale revision/sequence/foreground/manual/generation token | stale | no new stage |
| Keyboard/transport unavailable | Deferred | accepted desired selection; no false success |
| Quarantine/unhealthy | Blocked | no clear/ack/timed retry loop |
| Plan success + final shadow verification | succeeded → Idle | known host submission, active target/clean |
| Failed/partial apply | failed → Blocked | Runtime unknown/dirty/safety remains authoritative |
| Stop before admission | cancelled / Shutdown | none |
| Stop/focus change after admission | finish safe plan then shutdown/new stable decision | no staged interruption/fake rollback |

## Diagnostic fields

| Field | Meaning |
| --- | --- |
| foreground_process / committed_foreground_process | current normalized observation / last stable identity |
| decision_foreground_process, sequence, timestamp | decision provenance, not active state |
| configuration_document_revision | conservative config/Profile revision for admission |
| foreground_observation_sequence | genuine normalized observation epoch; catches A→B→A freshness races without changing semantic target dedup |
| manual_action_sequence, manual_hold(_foreground) | existing session-local accepted manual intent |
| hardware_activation_allowed | attached production coordinator enabled; not health permission |
| hardware_block_reasons | policy/result blockers; no phase-disabled reason in production |
| coordinator_state | WaitingForDecision / Preflight / Activating / Deferred / Blocked / Idle / Shutdown |
| activation_state / outcome / error | attempted result; deferred/failed are not success |
| activation_target / decision_sequence / matched_rule / foreground | attempted GUID/rule/basename |
| activation_attempt | cumulative Runtime-seam attempts, NOT HID count |
| last_attempted_decision_sequence / last_successful_decision_sequence | dedup evidence |
| activation_started_at / completed_at / retry_at | process-local monotonic ms; retry null if unscheduled |
| authoritative_preflight | Runtime-gate result context; transport preflight remains inside Runtime |
| stale_decision_count | tokens rejected stale |
| authority_context | informational cache only |

## Native UI mapping

| State | Chinese status |
| --- | --- |
| Enabled, waiting | 自动应用已启用，等待稳定匹配 |
| Activating | 正在应用：名称 |
| succeeded / no-op | 已提交到键盘：名称 |
| Deferred | 等待键盘可用后重试应用 |
| ManualHold | 自动应用已暂停：你刚刚手动选择了配置文件 |
| Quarantine | 键盘处于安全隔离，请使用现有恢复流程处理 |
| Failed | 自动应用失败；配置文件页保留具体错误 |
| Disabled / no target | 当前键盘配置保持不变 |

Would-select is distinct from last submitted/actual Profile state. Submitted is
host intent, not firmware readback or effective RT for all keys. Hardware RT gate
OFF/ON does not change desired per-key enable/shadow, dirty or decision sequence,
and never schedules an activation. No physical-switch control exists.

## Validation boundary

Fake-clock policy and real Runtime/mock transport tests cover debounce, duplicate
calls, A/B/A, revision/target/manual/generation races, backoff/health wake, failures,
pure status GET during a paused transaction, shutdown, RT gate independence.
Existing lighting/GSI/safety/CRUD suites remain required.

Owner acceptance: stable Desktop→CS2→Desktop; ManualHold in CS2; unavailable and
backoff; existing safe quarantine recovery only; RT gate OFF with configured
enabled keys; one mixed Actuation/Deadzone/DKS/RT Profile. Do not create an unsafe
transaction merely to test quarantine. No physical Phase 4B PASS is implied.
Reconnect replay, background lifetime, Lighting Ownership and 51 53 are separate.
