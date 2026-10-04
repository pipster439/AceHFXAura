# Aura profile baseline and restore analysis — M2.10

**没有建立完整、当前、可冻结且可回放的 pre-write baseline，也没有有界失败回滚契约。**
ServiceMediator 写后端最终拒绝；只读发现保留。
证据：`audit_artifacts/phase3-m2.10/profile-restore-sequence.json`、`baseline-source-analysis.json`。
这是静态结论；本次无 setter/ownership/engine/RGB/服务控制调用。

## 精确跟随 StartEngine cmd0

LightingService `0x2DC6C0` 的 cmd0 分支：

1. 停止所有 engine/threads，经manager、0x2B7940，并清理内部脚本集合。
2. `0x1F8D50` LoadLastScript 从安装目录 `script\LastScript.xml` 固定路径读取当前文件。
3. 文件读入成功则经 manager SetScript `0x2D0D30` 安装该脚本；不能把读取成功当作脚本语义有效或灯光恢复成功。
4. 读取失败则记录 `load lastscript fail`，跳过脚本重装。
5. **两种情况下均**经 `0x2CFAC0` 将 global `DAT_008B588C` 设成0。

指令证据补充 Ghidra x86 对 by-value std::wstring/隐式this恢复不完整的部分；关键调用和push0
保存在本地 `raw/service-critical-instructions.json`，从真实函数入口解码，不从任意字节开始。
cmd1 的内部 final flag和cmd0不同；不把 public rhs与 internal do-not-save flag混为同一个参数。

该代码能说明“停止并重装当前持久script/转模式0”，没有证明这是 ASUS正常UI退出专用协议，
更没有“立即先前profile/owner”的保留承诺。缺失文件路径也不回滚已停止的状态。

## LastScript 来源和可覆盖性

manager SetScript `0x2D0D30` 先 stop executors，再解析/设置脚本；当 internal final do-not-save flag
为0时调用 `0x1F9D10`。后者组装 **script\LastScript.xml**，
`CreateFileW(GENERIC_WRITE, share-read, CREATE_ALWAYS)`→`WriteFile`→关闭。
它的失败日志文字虽写 SaveLedMatrixLastScript，**实际路径literal是 LastScript.xml**，不能按日志
标签改认成 LedMatrix_LastScript.xml。

这是一份可替换的“最近持久脚本”，不是带实验ID、owner或版本的 predecessor slot。
经SetScript发送的新脚本可覆盖它；cmd0重新读取的是读取时刻的内容。cmd1是否在具体 public rhs
组合下保留该文件，不能从方法名推导；即便保留，也没有完整profile/ownership状态的capture或restore保证。
`0x293140` 另有固定 LastScript加载后进入manager SetScript的路径，说明该文件被其它运行状态使用，
不是 AceHFXAura 可独占的事务备份。

当前只读配置的 LastScript header为 AURA_3.0，含 performance、version、effectProvider、viewport、
effectList。viewport出现 type/model/csv/location/usage等另一套选择字段，不能与SetProfile Group/led
keys或QueryAllDevice index互换。没有证明它包括所有活动设备、第三方owner、provider资源、动画phase、
矩阵/global flags或外部并发操作。文件hash不变只能证明该区间字节不变。

## 内部 RestoreAll 的真实边界

`0x2C92D0` RestoreAll 具有当前 LastProfile加载、exclusivemode、token条件与当前profile取回路径；
其分支可调用 `0x2CBE90` SetAllDeviceProfile→Group/independent应用。funcid10/16等内部路径可到达它。
这属于服务自身“重装当前持久/期望配置”的机制，**并非已识别的 temporary caller transaction**。
没有公开 previous-state snapshot传入，没有客户身份配对、predecessor version或有界失败保证。
恢复路径还读写系统配置/模式；本次仅静态观察，没有执行或加载它可能使用的传感器DLL。

| 路径 | 分类 | 与临时rollback的区别 |
|---|---|---|
| 另选profile/effect | REPLACEMENT | 修改当前对象后重新构造/应用请求 |
| AuraCreator ApplyLastMode0x076D30 | REPLACEMENT | 重新进入AuraApply；完整 previous snapshot 未见 |
| cmd0重装LastScript | REPLACEMENT | 固定当前文件、全局停止；缺失仍转模式0 |
| internal RestoreAll | REPLACEMENT | 当前persisted profile/exclusive/token条件，不是实验predecessor |
| scenario StopLightingService逻辑request | UNKNOWN | selector5→XMLfuncid14→SetProfile；没有rollback/restore contract |
| 实际UI退出、DLL卸载、普通temporary结束 | UNKNOWN | 未建立与同一功能write配对的 mandatory cleanup |
| 精确RESTORE_PREVIOUS / RELEASE_ONLY | UNKNOWN | 没有可供S1采用的完整路径 |

不能因为命名 RestoreAll、ApplyLastMode、LastProfile 或 Stop 就把路径提升为恢复/释放已验证。

## Baseline 候选逐项判断

| 来源 | 完整/current | immutable | replayable / scope |
|---|---|---|---|
| public GetProfile | E_NOTIMPL，无法capture | 不适用 | 无；本次不再次调用 |
| plugin helper/global LED/effect table | 当前“期望”状态；完整live事实UNKNOWN | 否，应用前就改写，共享 | 无public capture/replay契约；多设备/独立亮度处理 |
| LastProfile.xml | 持久配置；是否完全反映现在的engine/owner UNKNOWN | 否，apply/report→SaveLastProfile同一文件 | 可解析不等于安全回放；owner/phase/并发缺失 |
| LastScript.xml | 最近脚本及provider/viewport；不是完整平台状态 | 否，CREATE_ALWAYS覆盖 | 外部assets/owner/mode缺失，不能保证全设备 |
| QueryAllDeviceStatus / DevLastStatConfig / registry | 状态/同步/flags元数据 | 否，无atomic multi-source capture | 不是可重放的完整引擎snapshot |
| in-memory engine | 完整性/current UNKNOWN | UNKNOWN | 未发现public snapshot/restore；未读取进程内存 |

本次只读重新复制七份固定配置，hash均与M2.9一致；未重新activate/queryCOM，也未借助这些文件
构造write payload。所有变更记录/缺失值与null保留在机器证据中，不以默认true制造成功。

## 错误与恢复政策

ASUS wrapper保存HRESULT、释放BSTR/COM，等待者记录错误或timeout并关闭thread handle/解锁。
此清理没有补偿已改profile/global/engine状态，也不证明thread已结束。SaveLastProfile失败日志和
LastScript写失败日志不是有界rollback；WriteFile调用/返回处理没有提供事务恢复成功证明。
未恢复到同一功能的完整失败cleanup；不声称整个ASUS产品从无其它cleanup。

S1现已拒绝，不建立候选。如果将来基于实质新契约另行立项，任何地址/acquire/write/restore/release/
post-check失败仍必须 RECOVERY_REQUIRED；禁止第二次write、alternateAPI、AuraSdk/HID fallback、
vendor service restart或reboot。普通medium owner若无可证明的bounded restore，timer/finally或UI退出
后继续运行都不能弥补契约缺失。read-only20秒强制退出不成为write worker cleanup。

这就是最后一轮有界静态决策；不请求再做泛化逆向，也不以“不写颜色、只调用控制API”绕过禁令。
