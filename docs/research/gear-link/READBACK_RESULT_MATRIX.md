# Controlled Readback Result Matrix

2026-10-02 recovery checkpoint。当前官方设备模块1.00.28；设备固件1.00.59。**Replay 未执行，不能选择 READBACK VIABLE / PARTIAL READBACK / NOT USEFUL 中任一实验结论。** bank authority 重新失效，完整研究被阻塞，不代表 getter 无用。

| Subsystem | Official query | 已准备 host target | 实机 reply | Effective value | Override | Enabled / mode | Side effects | Confidence |
|---|---|---|---|---|---|---|---|---|
| DKS | 25 02 + wire | V DKS→J；对照Standard键尚未验证 | 无 | 未验证 | 不适用 | 官方解析 mode0 Normal、2 DKS；仅STATIC | 未测试 | STATIC CONFIRMED |
| RT | 25 06 / 25 A6 + wire | W press.5/release1.5、enabled、continuous OFF | 无 | 未验证 | 不可推断inheritance | V1 enable9；V2 continuous10/enable11，仅STATIC | 未测试 | STATIC CONFIRMED |
| AP common | 25 05 | common2.0 | 无 | 未验证 | decoder未提取flag | 不适用 | 未测试 | STATIC CONFIRMED |
| AP key | 25 04 + wire | A3.0；无override对照未物理确认 | 无 | 未验证 | 未验证；effective≠override | 不适用 | 未测试 | STATIC CONFIRMED |
| DZ common | 25 0A | top.3/bottom.2 | 无 | 未验证 | decoder未提取flag | 不适用 | 未测试 | STATIC CONFIRMED |
| DZ key | 25 09 + wire | B top.4/bottom.1 | 无 | 未验证 | 未验证 | 不适用 | 未测试 | STATIC CONFIRMED |

## Exact request 的静态来源

原件 shared bundle SHA256：`be3360d6d6c6d13f6325df9c6383bf2117fb6235a3581638a33a9471eae9e657`。

当前已加载官方 getter 的函数文本保存于 `state/recovery/official-getter-functions.json`，只做 `.toString()` 读取，没有调用 getter。shared `makeCommand`（美化文件14183行）零初始化 payload，然后写 `[family, subcommand, selector low, selector high, ...body]`。这些 getter selector固定0，key body是 `wire LE16`，common body为空或零wire。HFX64-byte vendor payload：`25 xx 00 00 [wire LE16 or zeros] +58 zeros`。host report framing不包括在此payload内。

官方映射 helper `getFWKey/getKeyCode`（15058/15062）使用 keyboard nation 映射。nation 未缓存时，`getKeyboardNation`（15048）会调用官方 `getDeviceInfo(KeyboardLayoutNation)`，所以**不能声称 getter 始终只有一笔USB request**。这个依赖是读路径；正式 replay 前仍须审查其完整 request/response 和报告 framing。DKS response解码也可能调用这个映射。

已核对 getter 本体只有 makeCommand/getFWKey/sendCommand/解码；没有50 55、51 00、reset、save或setter。queue默认500ms、3次retry是官方 host 实现，不授权改Aura safety。没有运行这些函数，所以副作用、错误处理、重连后布局依赖均未实测。

## Static response decoder（不是 firmware fixtures）

- AP V1：reply4..5 LE16；DZ V1：bottom4/top5。
- RT V1：selector2..3，统一/press4..5、release6..7、enable9；不返回continuous。
- RT V2：selector2..3、values6..7/8..9、continuous10、enable11。
- DKS V1：mode2..3；Normal target4..5/AP6..7；DKS start4..5/end6..7，四target8..9/11..12/14..15/17..18，packed positions10/13/16/19。

未知字段不命名，不把SDK函数存在视为设备支持，不把 saved host intent 当reply。AP/DZ decoder没读取override flag，尚不能证明回复中有或没有该信息。除现有BasicInfo/gate证据外，本次没有参数readback升级。

## 后续门槛

先解决 [slot drift](EXTERNAL_WRITER_RECOVERY.md)。之后只在5建立可信known-state对照，执行少量官方exact getters，关联OUT/IN、bank、UI和物理观察，检查query后无setter/Apply/slot drift。首次有效响应才允许建立reply fixtures；不枚举68键、不生产化。

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

| Case | Query | OUT / matching IN | pre/post | SDK artifact | Classification |
|---|---|---|---|---|---|
| Common AP | 25 05 | 4 / 0 | 5 / 5 | `771` rejected | NO_MATCHING_RESPONSE |
| A AP | 25 04 | 4 / 0 | 5 / 5 | `771` rejected | NO_MATCHING_RESPONSE |
| Common DZ | 25 0A | 4 / 0 | 5 / 5 | `{'bottom': 3, 'top': 3}` rejected | NO_MATCHING_RESPONSE |
| B DZ | 25 09 | 4 / 0 | 5 / 5 | `{'bottom': 3, 'top': 3}` rejected | NO_MATCHING_RESPONSE |
