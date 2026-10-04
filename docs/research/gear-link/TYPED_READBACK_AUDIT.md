# Gear Link Typed Readback Audit

日期：2026-10-02；设备固件证据边界：1.00.59；官方网页设备模块：1.00.28。

## 结论

本轮用 Computer Use 打开官方页面及编辑器，选择测试键，未主动调用隐藏 getter 或构造 query。**下表目标 query 没有在这些用例的留存报文中出现，不能升级为 OFFICIAL USB CAPTURE PASS。** 页面显示的参数有对应的已保存 host Profile；显示数值不构成固件参数 readback。

| 官方 UI 操作 | 用例 | 留存 OUT | 指定 query | 结果 |
|---|---|---|---|---|
| 触发点/死区页，打开逐键编辑器，选 A，未改值完成 | actuation_readback | 12 00 ×1 | 25 05、25 04：0 | 参数 getter STATIC ONLY |
| 同一编辑器选 B 查看死区，未改值完成 | deadzone_readback | 12 00 ×1 | 25 0A、25 09：0 | 参数 getter STATIC ONLY |
| RT 页，查看 W，打开/关闭空 group 编辑器 | rt_readback | 25 00 ×3、12 00 ×1、50 55 ×1 | 25 06、25 A6：0 | 硬件 gate query 与参数 query 分开 |
| 按键页选 V，显示“默认 / 指定为 V”，未选择 DKS 功能 | dks_readback | 12 00 ×1 | 25 02：0 | 默认 fallback 不是 Standard readback |

退出未改值的 RT group 编辑器仍发送一次 Apply。不可称这段为“零 HID”或将 Apply 当成 parameter query；没有观察到该段的 51 54 stage。

## 已审查的静态契约

共享公开文件：`chunk-1.00.28-7038-1784769136-06d08f-BI62m6xD.js`；美化派生路径在 `audit_artifacts/gear-link-full-audit/js/beautified/`。原件 hash 在前阶段 resource manifest。以下是官方 host decoder 的行为，**不是本机响应 fixture**。

| getter 位置 | 静态请求 | 官方代码解析 |
|---|---|---|
| getActuation，16520 | common 25 05；key 25 04，wire 放请求 4..5 | response 4..5 LE16 value；未提取 override 标志 |
| getActuationV2，16531 | common 25 A5；key 25 A4 | response 6..7 LE16 value；不能据此认定 ownership |
| getDeadZone，16705 | common 25 0A；key 25 09 | response byte4 bottom、5 top |
| getDeadZoneV2，16723 | common 25 AA；key 25 A9 | response 4..5 key、6 bottom、7 top |
| getRapidTriggerPerKeySettings，16646 | 25 06 + wire | response selector 2..3；values 4..5 / 6..7；enable9 |
| RT V2，16665 | 25 A6 + wire | response selector 2..3；values 6..7 / 8..9；continuous10、enable11 |
| getKeyTriggerV2，16398；getDKSKeyInfo，16469 | 25 A2 mode；25 29 DKS detail | mode-specific fields；四 slot target/packed positions 来自 detail |

旧 mode query 25 02 与 V2 25 A2 不可混用；共享 getter 的存在也不证明 HFX 页面调用它。通用 RT setter 与 M605 specialization 的字段不同，不复用通用 setter 当作 HFX 契约。

**采集覆盖限制**：本轮四用例的 filter 覆盖用户指定的 25 05/04/0A/09/06/A6/02，因此这些零计数可审查；25 A2、25 29、25 A5、25 AA 当时未在留存集合内。不能声称这些新版 query 没有发送，也不能从丢弃统计重建响应。未来若需要捕获它们，先单独审查隐私保留范围；本轮不补发。

## 已成立与未成立的 readback

BasicInfo 实际回复包含 Profile count、active slot、current effect；物理 RT gate 有独立 25 00 / MI_02 status 路径。它们的已捕获字段不意味着 AP、DZ、per-key RT、DKS table 完整可读。

对于目标参数，request/response pair、稳定重复、Profile dependence、cold-start 行为、override 存在性、未知字段、错误响应均 **NOT VERIFIED**。没有制造“OFF fixture/ON fixture”的参数回复，也没有将 host saved intent 伪装为 device reply。

精确 bytes、UTC、端点、echo 和 SHA-256 见 `audit_artifacts/computer-use-audit/parsed/<case>/` 及 `capture-index.json`。

## Controlled replay 授权与停止状态

本次owner已授权exact official read-only getters。当前加载官方方法已读取源码文本（未调用），与shared bundle一致：25 02、25 06/A6、25 05/04、25 0A/09。解析表与依赖审计见[Readback Result Matrix](READBACK_RESULT_MATRIX.md)。键映射可能先查询keyboard nation，不能省略这个依赖的安全证明。

实际replay零次：slot5重新出现非预期3/1 drift，在query阶段之前停止。此前“本任务不执行replay”描述是旧阶段权限边界，不覆盖本次新授权；本次未执行的原因是authority gate失败，不是缺少用户许可。没有新参数reply/override/Standard/enable fixtures，仍STATIC ONLY。

## Latest controlled-getter checkpoint — 2026-10-02 (Beijing)

**STOP: authority precheck failed before parameter replay.** Corrected Window D remains VALID PASS for its bounded full reconnect/device-init trial: no51 00/50 55, nine BasicInfo5 samples,145.053s device-page idle, final UI5. It does not prove later bank authority or explain previous drift. No reload/init correlation or sender confirmation is claimed.

Current Computer Use read the already-connected ROG HFX device page, still displayingProfile5. No reload or Profile selection was performed. Re-inspected the official loaded getter methods, builders, send queue and selected Profile host intent; then called only the exact official `currentDevice.getDeviceInfo(0)` BasicInfo getter. Its SDK return and captured completed IN both reported **active slot1**, while UI remained5. Consequently the required initial BasicInfo5 and10s stable gate failed; no second deliberate authority call, no magnetic getter, no recovery selection, no physical/persistence test followed.

| BasicInfo IN UTC | Beijing UTC+08 | Retained frame | Active slot |
|---|---|---:|---:|
| 2026-10-02T14:50:01.896268Z | 2026-10-02T22:50:01.896268+08:00 | 4 | 1 |
| 2026-10-02T14:50:02.410270Z | 2026-10-02T22:50:02.410270+08:00 | 6 | 1 |
| 2026-10-02T14:50:02.925277Z | 2026-10-02T22:50:02.925277+08:00 | 8 | 1 |
| 2026-10-02T14:50:03.440411Z | 2026-10-02T22:50:03.440411+08:00 | 10 | 1 |
| 2026-10-02T14:50:31.921767Z | 2026-10-02T22:50:31.921767+08:00 | 12 | 1 |

One explicit audit BasicInfo invocation; five12 00 OUT and five completed responses observed. The remaining official page/SDK activity is not labeled as five deliberate audit calls or attributed to a process. All sampled responses count6/effect8/active1. The first SDK receipt timestamp14:50:01.896Z aligns with captured frame4 at14:50:01.896268Z. UI still5 at the final fresh AX/screenshot. No51 00 was captured in this window: **bank mismatch is observed; this capture does not contain the transition or identify its sender**. The interval after prior last sampled5 and this first sampled1 was not continuously captured. Do not invent a5→3→1 timeline for this run or blame the BasicInfo query.

### Exact executed read

Vendor payload OUT: `12 00` +62 zero bytes (64 bytes). Official WebHID reportId0; builder controls existing framing, no handwritten HID sender. Request frame3, matching query response frame4, endpointOUT0x0D/IN0x85 on interfaceMI_01, controller4/address11 from current descriptors. Response: `12 00 00 00 59 00 01 00 06 08 01 ff 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00`. BasicInfo offset8 profile_count6, offset9 effect8, offset10 active_slot1. Exact request/response/timing in parsed/authority-request-response.json and payload_sequence.txt; SDK snapshot in state/basicinfo-before.json. This is device status, not magnetic parameter readback.

### Source audit / unexecuted getters

Current loaded official getter function text and host target saved in browser/official-source-audit.json. Official shared original SHA and current text SHA in browser/source-manifest.json. Builder zero-initializes64-byte vendor payload, headerfamily/subcommand/selectorLE16, keywireLE16 in4..5. Getter bodies only build query, send via official queue, decode; no setter/Apply/Profile/reset/save calls. Layout mapping can depend on official12 12 KeyboardLayoutNation query; current cache was unset. That dependency is static-audited read-only and **was not called** because magnetic replay stopped.

| Subsystem | Official request | Desired host test target | Current response | Classification |
|---|---|---|---|---|
| DKS | 25 02 +wire | V non-Standard / Standard contrast | None | STATIC ONLY; NOT EXECUTED due gate |
| RT | 25 06 /25 A6 +wire | W enabled,.5/1.5; disabled contrast | None | STATIC ONLY; NOT EXECUTED |
| Actuation | 25 05 common /25 04 key | common2.0,A3.0; normal contrast | None | STATIC ONLY; effective/override unknown |
| Deadzone | 25 0A common /25 09 key | common.3/.2,B.4/.1; normal contrast | None | STATIC ONLY; effective/override unknown |

Host target is saved intent, not verified current slot5 hardware. Current host V DKS endingPoint10 differs from the earlier recovery intent36; this is recorded without guessing a migration/writer cause. No setting edited to restore a known target. Cannot derive Standard from an absent host object or infer firmware inheritance/override from UI.

No parameter request/reply pair, repeatability, enabled/mode field, or effective/override distinction has been experimentally acquired. Cold acquisition viability remains NOT VERIFIED; **not** READBACK NOT USEFUL, because getters were not tested. Physical/non-mutation acceptance W/V is not requested for a failed authority gate. Power persistence remains NOT VERIFIED, zero unplug/replug, no firmware-lighting claim.

### Safety, validation and artifacts

Only12 00 captured; no51 00,50 55, magnetic setter or parameter getter in retained reviewed categories. Existing privacy filter covers these requested query/select/write families; it discards unrelated/unreviewed traffic, so absence is not extrapolated to every unknown command. Files are privacy-filtered acquisition-original pcaps, not complete bus capture. No account token/cookie/credential/serial/typing, PID or full process paths exported. No process attribution escalation/services stopped.

Capture SHA-256: `5ce66fcc729bae483c1776c029ebdd3a6ea0c12c7fb42582f1db80f5fd41ab8e`. Absolute capture path: `G:\Aura\audit_artifacts\controlled-getter-audit\usb\getter_authority.pcap`. Raw/SDK/UI correlation assertions passed; the **hardware gate itself failed**, as intended fail-closed. Fresh34 offline Gear Link parser/gate tests PASS. No full CI or physical PASS claim. All owned passive capture/worker verified exited. Product source/M605/Profile Engine unchanged; no68-key enumeration, commit/push/package/release. scoped.patch contains only five research-document checkpoint appends against baseline/ snapshots.

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
