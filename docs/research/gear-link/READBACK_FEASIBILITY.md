# Typed Readback Feasibility

## Production gate

本轮不把参数 readback 接入 Aura。仅有 public SDK getter 加页面数值不足以成立 production readback。Normal UI 未观测到目标参数 query，且后续实验发生 active-bank 串扰；没有安全的参数回复 fixture。

| 特性 | Request source | 真机 response bytes | UI correlation | 重复 / bank / cold start | 当前可行性 |
|---|---|---|---|---|---|
| Actuation | shared getActuation / V2 | 无对应 pair | A/common 显示有 host cache 来源 | 未验证 | STATIC candidate；不能辨 effective vs override |
| Deadzone | shared getDeadZone / V2 | 无对应 pair | B/common 显示有 host cache 来源 | 未验证 | STATIC candidate；缺 override/ownership 证据 |
| Per-key RT | shared 25 06/A6 decoder | 无对应 pair | selected list 与 press/release 是保存意图 | 未验证 | STATIC candidate；不等于 gate2500 |
| DKS/key mode | shared 25 02/A2、25 29 detail | 无对应 pair | V“默认”是 UI fallback | 未验证 | STATIC candidate；不能推断 Unknown=Standard |

SDK 解析位置与 offsets 见 [Typed Readback Audit](TYPED_READBACK_AUDIT.md)。连续模式只在相关 V2 decoder 中出现，不据 V1 省略推断 firmware continuous=false。RT decoder 的 selector 判断也不是 per-key 51 54 写命令里的“独立参数 bit”证明。

## 下一步证据门槛

1. 排除其它实验写入，官方选测试槽位并从 BasicInfo 验证当前 bank。
2. 若正常 UI 仍不产生 query，保持 STATIC；只有另行获得 owner 的 controlled read-only replay 授权后，才能设计独立实验。本任务不执行 replay。
3. 真实 response 必须关联 request、设备重连身份、bank、UTC；多次确认 Standard/DKS、RT enable0/1/press/release、common/per-key differences。
4. 对 AP/DZ 必须另外确定是否有 override flag；单纯 effective value 不能授权 Aura relinquish ownership。
5. 错误、长度、未知 selector 与未解析字段全部 fail closed；先做离线 parser fixture，之后再独立决定生产化。

USB echo 仅说明某 packet 被接受/返回，不是状态 readback或物理/NVM验收。没有足够证据时，Aura 现有 trusted prior state guard 应保留。

## 新授权 / current recovery outcome

Owner已批准controlled official getter replay，不再需要重复请求该许可。实际受[slot drift](EXTERNAL_WRITER_RECOVERY.md)阻塞，因此尚未执行；[结果矩阵](READBACK_RESULT_MATRIX.md)记录每项已确认的static request/decoder与未验证字段。当前不能据未测试结果选择VIABLE/PARTIAL/NOT USEFUL。唯一先行缺口是可信、无非预期切槽的known-state窗口；之后才可取得参数reply并评价cold-state acquisition。没有production readback、全键enumeration或放宽prior-state guard。

## Controlled-getter authority precheck — 2026-10-02

Corrected full-init Window D remains VALID PASS (no initialization drift in that prior bounded trial); previous drift remains unexplained. The current connected Gear Link UI still5, but one exact official BasicInfo audit call and its captured IN report active1. Five completed BasicInfo samples all1; no51 00 was captured, so transition time/source is unknown. Stopped before any DKS/RT/AP/DZ getter: magnetic replay0, no recovery selection, no setting changes, no W/V physical/power test. Source-attribution gate is necessary again; no escalation/service kill undertaken here. Parameter repeatability, ownership/override and cold-acquisition feasibility remain NOT VERIFIED, not evidence of getter failure/uselessness.

All owned capture/worker exited. See [Typed Readback Audit](TYPED_READBACK_AUDIT.md), latest checkpoint, and `audit_artifacts/controlled-getter-audit/REPORT.md` for exact request/reply, SDK/UI evidence, five-sample timeline, current-source audit, SHA256 and fresh tests.

## Controlled getter recovery checkpoint — 2026-10-02 15:21–15:25 UTC

Revised owner authority policy applied: startup UI5/BasicInfo1 classified STALE_UI_OR_EXTERNAL_BANK_STATE; reload→official Connect→ownerHID authorization→devicepage restored UI1/BasicInfo1. Official select5 then two BasicInfo5 separated2seconds passed. authority_recovery_count=1, success_count=1, unexpected_runtime_drift_count=0. Previous drift remains UNEXPLAINED; correctedWindowD retains VALID PASS.

One official V DKS `getKeyTrigger(1026)` executed via exact official mapping/queue, with permitted12 12 dependency. OUT97/100/105/110 exact64bytes `25 02 00 00 31 00`+58zeros; no matching25 02 IN. Pre/postBasicInfo5. `{source:1026}` is not readback: official queue resolves synthetic timeout sentinel rather than throwing; no type/fields verified. Automatic3retries do not satisfy independent2-test repeatability. Standard contrast, RT, AP and DZ not executed.

STOP conditionsC/D: recovery selection also emitted `51 31 00 00 03`+59zeros (OUT57/65/71/78), exact official setPollingRate family; these were detected during subsequent capture review, after the first V attempt, and precede it in the timeline. No further query followed discovery. Therefore whole-run no-side-effect PASS must not be claimed. Profile selection authorization does not authorize companion setters. No Apply/reset/magnetic setter/unexpected selector observed in reviewed classes; no runtime bank drift. Process sender not confirmed. No speculative mapping/query alternate, no service shutdown, no persistence/physical test, no production edits.

Readback/cold-acquisition remains NOT ESTABLISHED. Hardware bank power persistence DEFERRED. Latest sampled5 is not a current live-state claim after own observer exit. Detailed request/response limitations, timeline, SDK fallback and source in `audit_artifacts/controlled-getter-recovery/REPORT.md`, dks-result.json, source audit and captured payload sequence.43offline tests PASS (9new evidence regressions); no hardware/production-readback PASS.


## Controlled AP/DZ matrix and W baseline drift — 2026-10-02 15:40–15:57 UTC

Corrected Window D retains historical VALID PASS; previous drift remains UNEXPLAINED; no reload/sender attribution claimed. New recovery count1/success1; four preselection3/1 selectors kept separately. Authorized selection5 + BasicInfo5 x2 passed; permitted51 31companion stayed in recovery capture.

Four independent official calls: APcommon25 05, A25 04, DZcommon25 0A, B25 09. Each pre/post5, four OUT (official retry), matching parameter IN0, reviewed setter/Apply/selector0. SDK771 and3/3 rejected as synthetic timeout decoder artifacts. DKS not retested; prior25 02 four OUT/IN0 retained. RT25 06/A6 NOT EXECUTED: actual HFX route remains unresolved, frontend only reads25 00hardwaregate plushostcache. Cannot declare entirefamilyunsupported, effective values readable or override metadata known.

Owner confirmed original slot5 DKS, A depth and firmware lighting physically; WRTnotverified. Owner then explicitly authorized onlyW unified0.1mm, separately captured. Actual bank unexpectedly5→3→1 before numeric write; UI remained5. Agent numeric submission used stale precheck without immediate bank confirmation. Wraw1 requests therefore cannot establish slot5baseline; non-testbankimpact cannot be excluded (activevseditbankunknown). Postcheck1 triggered STOP, no automatic restore, recovery or power test. Four unexpected selector frames29/31/49/51 at15:53:49.456237,15:53:51.235820,15:57:04.152080,15:57:04.571796UTC. Sender NOT ATTRIBUTED. This does not invalidate earlier separately clean AP/DZ request/timeout windows.

Power persistence NOT EXECUTED/NOT VERIFIED. Further hardware activity gated on PROFILE_DRIFT_SOURCE_ATTRIBUTION. Lastobservedactual1/UI5; liveauthorityUnknownafterobserverexit. Detailed report, exact64byte requests, percaseTIMELINE/CSV/payloadsequence, source audit, hashes, screenshots andtests at `audit_artifacts/controlled-getter-matrix/REPORT.md`. No production changes.
