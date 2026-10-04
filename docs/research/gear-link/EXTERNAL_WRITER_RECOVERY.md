# External Writer Recovery — Gear Link slot5

日期：2026-10-02；固件 1.00.59；授权写槽位仅 5。此次 checkpoint 结论：**EXTERNAL_WRITER_STILL_PRESENT；停止硬件实验**。这里的状态名表示隔离条件不成立，并不证明某个具体外部进程负责写入。

## 已完成的清理

13:12:58 UTC 核验并停止 `ArmouryCrate.exe` 和旧 `aura_web_ui.exe` 前端。后者来自此前仓库包输出。没有运行中的 AuraOwnershipExperiment、AuraWorker、Aura 或 aura_daemon；只读检查 AceHFXService 已停止。没有停止不确定的 ASUS 系统服务。

仍见 LightingService、ArmouryCrate.Service、ArmouryCrate.UserSessionHelper、ArmourySocketServer、ArmouryHtmlDebugServer 等 ASUS 进程。名称列表保存在脱敏 evidence 中；**来源归因 NOT VERIFIED**。不能据进程存在就认定它修改了键盘，也不能据停止两个前端就声称只剩 Gear Link writer。

## Authority 与异常时间线

| UTC | 北京时间 | 证据 | 观察 |
|---|---|---|---|
| 13:13:32.469736 | 21:13:32 | authority frame10 | BasicInfo active5、count6、effect0；UI 配置文件5 |
| 13:15:21.780091 | 21:15:21 | stability frame12 | BasicInfo 再次 active5，间隔超过10秒 |
| 13:25:01.087353 | 21:25:01 | reload frame10 | BasicInfo active5 |
| 13:25:53.481351 | 21:25:53 | reload frame11 | 未计划 OUT `51 00`，slot3 |
| 13:25:54.696271 | 21:25:54 | reload frame13 | 未计划 OUT `51 00`，slot1 |
| 13:26:01.029978 | 21:26:01 | reload frame16 | BasicInfo active1、effect8 |
| 13:28:14 | 21:28:14 | worker state | 被动 capture worker 退出 |

用户对 21:25:53–54 是否手动切3/1或按 Profile 快捷键的回答：**“没有操作”**。Agent 没有记录切3/1的 UI 动作。官方 reload/reconnect 与漂移时间相关，**不能据此判定 Gear Link 自动切槽，亦不能归责某 ASUS 服务**。USBPcap 不提供发送进程身份。

最初 slot5 authority 的两次设备观测成立，但只是离散采样。USBPcap pipe 有缓冲，BasicInfo 是官方约一分钟心跳；这不是连续 bank 监控。后续漂移取消了继续实验的授权条件。`SLOT_AUTHORITY_STABLE` 仅对早先采样窗口成立，整个 recovery 不算隔离 PASS。

## 停止前实际提交

通过官方 UI 在已观测 slot5 上重新提交：common AP2.0、A3.0；common DZ .3/.2、B .4/.1；仅W RT .5/1.5、continuous OFF；V DKS targetJ、阈值1.0/3.6。具体 capture、OUT/echo/Apply、完整64-byte payload、时间间隔见 `recovery-capture-index.json` 和各 case 派生文件。

官方“解除同步”按已有单独授权点击一次，留存 `74 00` 四次，均未找到 identical echo；UI 警告和禁用状态未解除。重试来源按官方 host 队列推测，非独立物理结论。没有改变 firmware 灯效、没有点击“同步设置”、没有 reset、没有写其它槽位的设置。意外 slot3/1 selection 仍完整留存，不能被描述成 Agent 授权的设置动作。

## 未执行与当前停止状态

- 没有 pre-power 物理确认，没有拔插，没有 persistence acceptance。
- controlled getters 获授权但 **零次 replay**；漂移之后没有查询、设置、自动抢回5。
- 当前最后确认 UI/BasicInfo 是1。slot5 host cache 是保存意图，不是 bank 内容完整 readback。
- capture/worker 已退出；没有停止未知 ASUS 服务，没有恢复或迁移用户配置。

只在来源查明、同一 slot5 authority 重新成立后才能恢复。当前所缺证据是**无并行/非预期切槽的可信实验窗口**。下一阶段不应以反复选择5掩盖这个问题。

## 工具修正与范围

只修改研究工具：live bank gate 对无观测、超过90秒/未来时间、漂移、未经审查记录 fail closed；离线 scope 检查增加 BasicInfo 漂移判断，允许明确选5之前的 preflight 初始 bank，后来的重新选5不清除漂移。session 只对状态文件暂时不存在做三次有界读取，malformed JSON 直接失败。capture buffer 使用已安装 USBPcap 支持的4096字节以减少证据输送延迟，**不调整 HID/Apply/M605 timing**。

live gate 是独立研究检查器，不是完整实时 observer，也不保证两次 BasicInfo 之间没有 drift。未修改 Aura production、Profile Engine、M605、NativeHid、P4B 或 WinUI。51 53继续 blocked；未扫68键；未commit/push。

相关：[结果矩阵](READBACK_RESULT_MATRIX.md)、[Typed Readback](TYPED_READBACK_AUDIT.md)、[可行性](READBACK_FEASIBILITY.md)。

## Profile drift attribution checkpoint — 2026-10-02

新实验在不reload的官方选5后、以及Gear Link标签关闭的>=90秒窗口内都捕获51 00 slot3/1。因此reload专属解释不成立；来源仍未知。ETW实时元数据没有取得对应HFX Write事件，不能归责native Gear Link或其它ASUS服务。C仅部分完成，native Computer Use因browser URL安全检查停止，D未执行。所有自有观察器已停止；getters/persistence继续blocked。详见[Profile Drift Attribution](PROFILE_DRIFT_ATTRIBUTION.md)。

## Device-page resume / single D reload — 2026-10-02

Owner completed HID permission. Verified official device page/Profile1, selected only authorized5, then observed five BasicInfo5 samples. One ordinary reload followed by109.277s idle did not reproduce drift; four post-reload BasicInfo samples remained5. Reload returned the connection landing page; no connection click in D. Latest sampled bank5 at14:19:02.030512Z, not continuous/current status after stop.

Earlier no-reload and closed-page unexpected3→1 remains unexplained; clean D does not prove source isolation. ETW lacked target write coverage (zero matching events), PROCESS ATTRIBUTION NOT VERIFIED. BLOCKED_SOURCE_ATTRIBUTION remains; zero getter replay/power tests. All owned observers stopped; no ASUS service stopped/settings edited. See the appended resume section of [PROFILE_DRIFT_ATTRIBUTION](PROFILE_DRIFT_ATTRIBUTION.md) and `audit_artifacts/profile-drift-attribution/resume-device-page/REPORT.md`.

## Corrected full-init D checkpoint — 2026-10-02

The prior landing-page-only window is invalid for complete device initialization. Corrected D completed official Connect, owner HID authorization and verified HFX device page, then145.053s with no UI input. UI remained5; nine BasicInfo samples5; no51 00 in full D. Previous drift remains unexplained, no initialization/source attribution claim. Native HID chooser must be handed to owner immediately; no landing-page idle substitution. No setting edits/getter/persistence/process escalation/service stops. Owned passive worker exited.

See [PROFILE_DRIFT_ATTRIBUTION](PROFILE_DRIFT_ATTRIBUTION.md), corrected full-init section; `audit_artifacts/profile-drift-attribution/full-init-d/REPORT.md`. Earlier capture files/hashes remain preserved.


## Controlled AP/DZ matrix and W baseline drift — 2026-10-02 15:40–15:57 UTC

Corrected Window D retains historical VALID PASS; previous drift remains UNEXPLAINED; no reload/sender attribution claimed. New recovery count1/success1; four preselection3/1 selectors kept separately. Authorized selection5 + BasicInfo5 x2 passed; permitted51 31companion stayed in recovery capture.

Four independent official calls: APcommon25 05, A25 04, DZcommon25 0A, B25 09. Each pre/post5, four OUT (official retry), matching parameter IN0, reviewed setter/Apply/selector0. SDK771 and3/3 rejected as synthetic timeout decoder artifacts. DKS not retested; prior25 02 four OUT/IN0 retained. RT25 06/A6 NOT EXECUTED: actual HFX route remains unresolved, frontend only reads25 00hardwaregate plushostcache. Cannot declare entirefamilyunsupported, effective values readable or override metadata known.

Owner confirmed original slot5 DKS, A depth and firmware lighting physically; WRTnotverified. Owner then explicitly authorized onlyW unified0.1mm, separately captured. Actual bank unexpectedly5→3→1 before numeric write; UI remained5. Agent numeric submission used stale precheck without immediate bank confirmation. Wraw1 requests therefore cannot establish slot5baseline; non-testbankimpact cannot be excluded (activevseditbankunknown). Postcheck1 triggered STOP, no automatic restore, recovery or power test. Four unexpected selector frames29/31/49/51 at15:53:49.456237,15:53:51.235820,15:57:04.152080,15:57:04.571796UTC. Sender NOT ATTRIBUTED. This does not invalidate earlier separately clean AP/DZ request/timeout windows.

Power persistence NOT EXECUTED/NOT VERIFIED. Further hardware activity gated on PROFILE_DRIFT_SOURCE_ATTRIBUTION. Lastobservedactual1/UI5; liveauthorityUnknownafterobserverexit. Detailed report, exact64byte requests, percaseTIMELINE/CSV/payloadsequence, source audit, hashes, screenshots andtests at `audit_artifacts/controlled-getter-matrix/REPORT.md`. No production changes.

| frame | UTC | Selection |
|---|---|---|
| 29 | 2026-10-02T15:53:49.456237Z | 51 00 slot 3 |
| 31 | 2026-10-02T15:53:51.235820Z | 51 00 slot 1 |
| 49 | 2026-10-02T15:57:04.152080Z | 51 00 slot 3 |
| 51 | 2026-10-02T15:57:04.571796Z | 51 00 slot 1 |


## V2 owned-writer inventory and bounded clean window — 2026-10-03

见 [Profile Drift Source Attribution V2](PROFILE_DRIFT_SOURCE_ATTRIBUTION_V2.md)。本轮未发现或停止项目writer；官方select5后20次BasicInfo在189.999秒连续窗口均5、10秒cadence，整份idle capture只有12 00，无51 00/setter/Apply。不能据此归因AuraOwnershipExperiment或声称source已隔离。只读handle snapshot发现LightingService/asus_framework的MI_01 candidate，handle不是sender；枚举partial。未启动Procmon/ETW、未停ASUS服务、未继续getters或power test。上轮W0.1污染未恢复、full hardware state仍UNKNOWN。phase=STOP_SOURCE_UNKNOWN；此clean窗口仅满足有界稳定观察门槛，等待owner review。
