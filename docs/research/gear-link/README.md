# Gear Link official reference audit

审计日期：2026-10-02。设备：ROG Falchion Ace HFX，VID 0B05 / PID 1B7E，固件 1.00.59。设备网页模块 1.00.28，ServiceWorker 缓存版本 1.02.17。

这是官方行为参考研究，不是 Aura 生产实现、硬件 allowlist 或发布验收。

- [总报告](GEAR_LINK_FULL_AUDIT_REPORT.md)
- [用户行为规范](GEAR_LINK_OFFICIAL_BEHAVIOR_SPEC.md)
- [协议参考](GEAR_LINK_PROTOCOL_REFERENCE.md)
- [Profile 审计](GEAR_LINK_PROFILE_AUDIT.md)
- [磁轴审计](GEAR_LINK_MAGNETIC_AUDIT.md)
- [灯光审计](GEAR_LINK_LIGHTING_AUDIT.md)
- [Aura 差距与实施顺序](AURA_VS_GEAR_LINK_GAP_MATRIX.md)
- [Computer Use 执行日志](COMPUTER_USE_AUDIT_LOG.md)
- [Typed readback 审计](TYPED_READBACK_AUDIT.md)
- [Readback 可行性](READBACK_FEASIBILITY.md)
- [单槽持久化与阻塞状态](HARDWARE_BANK_PERSISTENCE_AUDIT.md)

新阶段证据：`audit_artifacts/computer-use-audit/`，包含slot ledger、可恢复session state、before/after screenshots、privacy-filtered captures及每case派生时间线。该阶段单槽实验因未计划bank切换而暂停，不是Physical PASS。

## 证据与复现

完整本地研究目录：`audit_artifacts/gear-link-full-audit/`，由现有 ignore 规则排除。

| 文件 | 用途 |
|---|---|
| `raw/resource-manifest.json` | 匿名下载的公开资源 URL、SHA-256、长度 |
| `js/`、`js/beautified/` | 原件与仅 AST 美化的派生文件 |
| `notes/source-anchors.json` | 原件 hash → 美化源码行 → 功能职责 |
| `schemas/profile-defaults.json` | 从公开 AST 提取六个默认 host Profile；不读取用户配置 |
| `schemas/official-profile-contract.json` | 字段含义与 authority |
| `schemas/capability-matrix.*` | 产品能力；公共 manifest 与页面实际调用分开 |
| `schemas/official-key-namespaces.json` | 共享键符号/逻辑命名空间/wire 映射，不是物理扫描矩阵 |
| `schemas/aura-key-namespace-comparison.csv` | 与 Aura 已审计 68 个磁轴键的逐项比较 |
| `schemas/edit-lifecycle-matrix.csv` | 编辑、Apply、作用范围、持久化边界 |
| `schemas/readback-matrix.csv` | 查询 API、实际页面读取、host cache 的区别 |
| `notes/user-observations.json` | 本次用户观察，不补造逐功能物理测量 |
| `usb/*.pcap`、`usb/*.privacy.json` | 采集时即过滤隐私的原始留存记录与移除统计 |
| `timelines/` | 全部留存报文、UTC 时间、echo、Apply 分组与切换窗口 |

```powershell
python -m unittest discover -s tests -p test_gear_link_reference.py -v
python tools/research/gear_link_reference.py `
  audit_artifacts/gear-link-full-audit/usb/gear_link_test_profiles_20261002.pcap `
  audit_artifacts/gear-link-full-audit/timelines
```

分析器只读 pcap；不连接设备/服务，不加载官方 JS/DLL，不发送 HID。它拒绝无隐私 provenance 或 SHA-256 不一致的输入。采集工具是 USBPcap 的被动管道接收端，不是 HID transport。

## 证据等级

**STATIC CONFIRMED**：当前公开 JS 的可达职责/字段；不是固件实机证明。**OFFICIAL USB CAPTURE PASS**：官方正常操作的精确报文。**USER PHYSICAL BEHAVIOR PASS**：仅用户明确报告的行为。**USER UI OBSERVATION**：页面状态/入口。**INFERRED**：源与 capture 的关联推断。**NOT VERIFIED**：没有足够证据。

旧的固件 1.00.58/HAL 审计只作为带版本边界的交叉参考。源码 minified 名称只用于定位；不作为 Aura 应公开的契约。

## Safe recovery checkpoint

- [External Writer Recovery](EXTERNAL_WRITER_RECOVERY.md)：slot5 authority、非人工3/1切槽、停止状态。
- [Readback Result Matrix](READBACK_RESULT_MATRIX.md)：exact official source契约；replay尚未执行；未宣称readback不可用。
- 本次报告/patch/脱敏证据位于 `audit_artifacts/computer-use-audit/recovery-*`，此前交付保持原样。


- [Continuous Profile Drift Navigation Audit](CONTINUOUS_PROFILE_DRIFT_AUDIT.md)：连续capture、focused-field hard exit、startup selectors与post-T1稳定导航分开判定。
