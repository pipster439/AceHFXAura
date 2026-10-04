# HISTORICAL CHECKPOINT / SUPERSEDED

This alpha.7 investigation records its original device state and acceptance boundary. Later physical acceptance and package acceptance supersede its release status; historical failures and observations are retained unchanged. Current truth: [HardwareSlot closure](../../../release/HARDWARE_SLOT_RELEASE_CLOSURE.md) and [package candidate](../../../release/PACKAGE_CANDIDATE_AUDIT.md). Raw captures/private evidence remain local and are not committed.

# Profile Drift Sender Attribution — A/B Checkpoint

日期：2026-10-03。所有时间线使用 UTC；北京时间为 UTC+08。

## 决策

**UNAUTHORIZED_HOST_SELECTOR_CONFIRMED / OWNER_MANUAL_SWITCH=EXCLUDED / SENDER=UNKNOWN。**

Owner 已明确排除上一轮 slot1→3→6 期间任何手动操作。真实 host OUT selector 不是 UI stale，也不是 Aura activation；已修正 `FRESH_AP_CONNECTED_RETEST_RESULT.md`。

本轮 A、B 均没有重现 selector，**没有达到发送者确认或可重复 ON/OFF 强关联的完成标准**。phase=`STOP_SOURCE_UNKNOWN`。功能验收继续暂停；没有运行 bare selector、slot5 activation、getter、ManualHold、reconnect、bank authoring 或修改生产实现。

## 本轮 A/B 结果

| Window | 观察时间 | 起点 | 实际证据 | 判定 |
|---|---:|---|---|---|
| A：Gear Link connected | 212.117s（明确起点到末次显式 BasicInfo） | 官方选择 slot1，BasicInfo1 | 16 个 completed BasicInfo 全部1；整份capture仅一个授权 selector1 | DRIFT_WITH_GEAR_LINK_CONNECTED=NOT REPRODUCED |
| B：Gear Link webpage closed | 185.259s | 关闭前 BasicInfo1 | 关闭后已保留 selector=0；ASUS后台保持 | DRIFT_WITH_GEAR_LINK_CLOSED=NOT REPRODUCED |

A 起点 `2026-10-03T05:06:42.819Z`，末次显式 query `05:10:14.936Z`；capture 内最后 completed BasicInfo1 为 `05:10:27.412482Z`。授权 selector1=`05:06:26.108966Z`（retained frame3）。capture 连续至 worker退出 `05:11:09.7919236Z`。

B 官方 BasicInfo1=`05:19:23.712764Z`；最后网页自然查询 completed IN1=`05:19:27.422023Z`。网页关闭=`05:19:36.473Z`；对照结束=`05:22:41.731674Z`，capture worker退出=`05:22:41.9420878Z`。关闭后不再有网页发送通道，**没有新增手写HID sender用于B末端采样**。所以B只能报告“未捕获selector”，不能声称整段每次BasicInfo均1或末端actual1。

A 中为准备进程工具启动/尝试操作 Procmon 窗口，Gear Link一直连接，但桌面foreground可能变化，没有连续foreground记录。它是connected-window观察，**不是完全同foreground的严密复现实验**。没有 Owner 操作要求，没有网页导航/设置编辑。B关闭前确认网页focus为AXWebArea，没有待提交numeric input。

B 是 A 阴性后的附加阴性对照，不是用户条件“A重现后才做B”所预期的阳性配对；两次阴性不能证明Gear Link是必要条件，更不能证明问题解决。

## USB / privacy audit

| OUT | A整份（包括官方选择准备） | B整份（包括关闭前query） |
|---|---:|---:|
| 51 00 | 1，授权slot1 | 0 |
| 12 00 | 16 | 4 |
| 25 00 / 25 01 | 各4，正常官方selection refresh | 各0 |
| 51 31 | 4，官方selection polling companion | 0 |
| 50 55 | 0 | 0 |
| 已保留AP/DZ/RT/DKS/firmware lighting content setters/reset | 0 | 0 |

A idle起点后只有BasicInfo，没有selector或上述companion/content写入。A中官方伴随51 31属于已授权正常Profile selection，不是新增bank authoring。

两份均由已有隐私collector保存，outcome=complete。serial、普通按键输入、其它设备和未审查vendor内容在内存丢弃。**C0 81不在保留范围，不能声明该类别为0或RGB streaming OFF。**

- A SHA256：`2b34fccdca60c5583e8cde30c0a9652d92a237bc6ffff54e60ee4eff092a4cc5`。
- B SHA256：`6366e84e8e3a16e9d340dbbc2e37c99a78e33393cf9bc891c4bb0d9c287b9f65`。
- Evidence：`audit_artifacts/profile-drift-ab-20261003-130558/`。
- Capture：`window-a/usb/drift_connected.pcap`、`window-b/usb/drift_closed.pcap`。
- 解码：各window的`parsed/final-summary.json`。

## 进程级归因尝试

### Procmon

限定本机路径未找到既有可用Procmon，因此从微软官方Sysinternals下载。Microsoft Authenticode=Valid，文件hash及来源在`state/procmon-source.json`；已有EULA接受状态在启动前存在，本轮未修改安全设置或驱动签名策略。

工具以 `/NoConnect` 启动，画面明确显示capture disabled。Computer Use菜单/键盘输入未能可靠打开过滤设置，停止重试。**没有启动未过滤的全机PML，没有取得Procmon HFX WriteFile trace。**这是配置/输入操作限制，不能称“Procmon无法记录HID WriteFile”。只对自己启动且PID/路径验证匹配的实例执行官方`/Terminate`；清理记录`state/procmon-cleanup.json`。

官方工具文档：[Microsoft Process Monitor](https://learn.microsoft.com/en-us/sysinternals/downloads/procmon)。

### ETW

复用已安装TraceEvent，开启`Microsoft-Windows-Kernel-File`实时observer，先以只读SetupAPI/QueryDosDevice取得6个当前HFX kernel aliases。audit-only副本为匹配输出增加PID/thread字段；没有raw ETL、没有其它文件内容落盘、没有新HIDopen/read/write。

观察器B窗口期间运行，最终：seen=1,288,046，matched=0，decode_failures=0。**没有 target HFX path + Write event + process/PID 的联合证据**；没有同期selector可供关联。零匹配不能证明没有writer，也不能证明该provider的HID覆盖完整。

读取本机HIDCLASS provider manifest仅看到rundown/device information，未将其冒充WriteFile attribution。没有注入/patch任何候选进程。

## 句柄候选（不是发送者）

提升后只读NtQuerySystemInformation/NtQueryObject快照，复制已有handle用于name检查，不关闭远程handle、不打开目标HID。保存PID、executable路径与服务名是Owner明确请求的本机归因材料，未上传。

| Process | PID | Connected snapshot | Closed snapshot | 分类 |
|---|---:|---|---|---|
| LightingService.exe | 7124 | MI01、MI02 Col03 | MI01、MI02 Col03 | CANDIDATE |
| GearLink_KBProcess.exe | 14176 | MI02 Col03 | MI02 Col03 | CANDIDATE；没捕获持有MI01，短暂open仍不能排除 |
| Edge native device process | 22816 | MI01、MI02 Col01/02/03 | 无已匹配HFXhandle | CANDIDATE，网页关闭后句柄释放与隔离一致 |

路径/服务完整记录：`state/hid_handle_owners-before-close.json`、`state/hid_handle_owners-after-close.json`；覆盖有个别bounded timeout，不能作为全进程排除证明。LightingService正式路径确认于第二份提升快照。未发现Aura daemon、AuraOwnershipExperiment、ArmouryCrate.exe前端或项目smoke writer；实际停止项目writer=0。ASUS后台服务/任务未停止、未修改。

## Native companion 静态线索

既有官方decompile显示：`Scenario_Profile`接收`SetDeviceProfile`，转换Profile1–5/DefaultProfile到slot，然后调用`SetProfile_Type_1` mode4；后者构建65-byte host report中的 `51 00 00 00 <slot>`，进入已有WriteHID/WriteFile。存在可在网页之外切bank的实现路径。

这是**STATIC CAPABILITY，不是本次sender attribution**。该路径有CheckACHtml和localhost9013连接条件；本轮对应registry探针未发现HFX注册，9013未见listener。它不能证明此路径当前活跃，更不能把GearLink_KBProcess定为sender。现有KBApi日志只读筛选也未取得可对应本次写时刻的有效switch事件；没保存原始日志或设备serial。

## 历史selector统计

`selector_drift_timeline.json`校验39份所选canonical隐私capture SHA256，汇总47笔selector（**包括授权动作和未分类动作**）。不是47次drift；frame是retained编号。未知connect时间、foreground、服务状态保留UNKNOWN，不凭文件名补历史事实。

明确的3→6历史对照：

| 第一笔UTC | Pair间隔 | 情境 |
|---|---:|---|
| 2026-10-02T21:26:08.355198Z | 0.702687s | Gear Link前端已隔离，Aura当时运行但无对应activation；Owner排除手动 |
| 2026-10-03T04:57:18.967657Z | 1.385270s | Gear Linkconnected，Aura daemon OFF；Owner排除手动 |

后者距授权slot1选择为**89.007249s**；距最后completedBasicInfo1为51.560082s。“约50秒稳定”不能替代“connect/selection之后50秒”的计时。

此前还有已记录的closed-window3→1，UTC `2026-10-02T14:04:10.165610Z → .613112Z`，间隔0.447502s；当时Edge重开/新标签同邻近，属于browser lifecycle/foreground混杂，不能作sender证明。但它与曾经GearLink前端隔离后的3→6证据一起说明：当前不能假定网页是所有drift的必要条件。

3→1与3→6序列多次出现，但connect/selection到drift延迟差异大，中间有未覆盖窗口和未知操作。**固定周期 / 固定timer = NOT ESTABLISHED**。之前valid corrected Window D无selector的结果不变；不写reload correlated或GearLink sender confirmed。

## 保留的阻塞与下一步

没有足够证据安全决定停止LightingService/asus_framework，也没有阳性B case要求立即升级服务隔离。没有逐个kill试错。当前sender尚未归因/隔离，不能恢复freshAP功能验收。

下一步应先取得可用的严格HFX过滤Procmon WriteFile观察，或已证明能显示targetpath/write/process的observer；在真实selector重现时与USB对应。如果工具只能给handle/零target ETW，则继续标候选，不能宣布sender confirmed。后续若确有可安全、可逆的非核心companion隔离方案，再单变量180s比较；当前两份阴性不足以支持其因果判定。

## 本轮文件 / 校验

- 更新`FRESH_AP_CONNECTED_RETEST_RESULT.md`，加入Owner明确排除手动与正确分类。
- 新增本报告；audit-only工具、snapshot、timeline与session state在ignored evidence目录。
- `scoped.patch`只覆盖上述两份报告，与当前回合前文档作diff；不纳入累计未提交产品修改。
- 离线capture parser、SHA256、summary/session边界校验；不运行产品CI冒充归因测试。
- 无production修改、commit、push、package、release；捕获worker/ETW已停止，Gear Link网页保持关闭。
