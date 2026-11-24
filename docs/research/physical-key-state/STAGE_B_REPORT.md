# Physical Key State — Stage B

日期：2026-10-08（Asia/Shanghai）。仓库 `pipster439/AceHFXAura`；版本 `0.1.0-alpha.8`；HEAD `c52398bef2738f71b927594424736a42da5184ab`。

## 结论与完成范围

**INCONCLUSIVE**。已完成证据继承、独立 Windows Raw Input 观察器、21 项离线模拟测试和无捕获的本机只读预检。实体按键验收 **NOT RUN**；没有任何本轮 **PHYSICAL PASS**。这不是“按键测试没有收到 73/74”的负面结果：本轮没有启动监听，也没有执行按键试验。

当前预检显示本地已解锁 console desktop 可用，两个指定 vendor Collection 各匹配一个目标。尚未收到 Owner 明确进入专用实体测试会话的答复，因此停在预检。桌面可用不等于测试授权；未用远程、逻辑注入、Aura Hook 或自动操作替代物理验收。

Copilot 串亮从物理层解决仍有条件希望，尚无新证据证明：必须分别识别 Copilot 与真实 Win/Shift，证明独立释放、组合及重映射后来源关系。即便 vendor 标记出现，也不能只凭普通 A/W/S/D 或报文时间接近就确认物理矩阵。

## 继承证据及可信等级

上一轮证据包 `audit_artifacts/physical-key-state-20261008/output/PHYSICAL_KEY_STATE_EVIDENCE.zip` 的 SHA-256：`bb4bd64b712258b022af83546cd79cf335edbee4d5ebf98e389edbeda1ac00bd`。24 项文件，ZIP CRC 与内部 `hashes/files.sha256` 全部校验通过。完整报告、拓扑、可行性、隐私/验收/Provider 文档及静态片段、固件 builder、测试/失败日志已继承；本轮只补查 reader 与 transport 路由，不重启完整逆向或 Hall ADC 研究。

| 结论 | 证据等级 | 当前判定 |
|---|---|---|
| MI_01 FF00/1，缓存 Input 65、ID 0；MI_02 Col03 FFC0/1，Input 21、ID 3 | 继承缓存；本轮只读预检匹配 caps/路径 | 缓存能力已验证；实际 RAW framing 未验收 |
| MI_03 为标准 Keyboard usage 位图，Input 20；不是已确认 vendor physical matrix | 继承缓存 STATIC/metadata | 标准 Keyboard 能力已验证；物理语义 UNKNOWN |
| RT Gate `03 76 00 00 state` 来自 MI_02/EP8C | 继承历史硬件证据 | 已验证的 RT 通知；不是本轮 PHYSICAL PASS，也不是 73/74 路由证明 |
| HAL `FUN_18001fc00` 分派 73/74、查 `DAT_1801caef0`，回调 +C8/+D0，转交第三个数据字节 | 继承静态反编译/本地字节核对 | STATIC 已验证实现；按下/释放、DKS、全键覆盖 UNKNOWN |
| FW 1.00.58 有条件发送 73 参数1..4、74 参数0/2；内部4字节队列 builder 高身份字节受模式影响 | 继承 FW58 有界反汇编与扫描函数 | STATIC 已验证代码路径；正常运行是否开启、真实 USB 格式 UNKNOWN |
| 无正常持续 Hall ADC 流 | 继承历史限定负面结果 | 沿用该限定结论；不扩大为“没有数字事件” |
| 上轮8项 metadata 测试 PASS | 继承 OFFLINE TEST | 元数据处理证据，不能证明活动按键协议 |

当前固件版本及 Profile UNKNOWN；未查询。FW58 静态分析不能无条件推广到当前设备。HAL SHA-256 `52d575bf942b7551b3f120c446bf0d853e36f9225c6b9a17407a80e0b1829f04`；FW58 SHA-256 `8fe68a13d7a0cd3bf8b4fd0dbe0575c0b7e67c1d4373e8d63970cf248684e668`。源文件哈希与上一轮报告之间的联系保留于交付 manifest；没有执行 HAL 或固件。

## 73/74 路由与字段：尚未确认

窄范围静态检查显示 `FUN_18001f070` 日志写 `[AacM605Function][ReadEvent_interface3]`，经对象 `*(param_1+8)+0x1070` 的虚调用 +0x30，传入 `0x40` 与 `FUN_18001fc00`。`FUN_18001ef70` 创建该 reader 线程。同一 callback 也被 M606 reader 使用。

这证明 HAL 内部 reader/callback 关系，不证明 USB MI 编号。`0x40` 是调用参数，不足以判定实际 report 长度或端点。既有 `FUN_180012060` transport factory 分别按 mode 0/1 筛选 FF00/FFC0；这一步与 M605 +0x1070 对象赋值的完整闭环尚未建立。新增 caller `FUN_1800126b0` 反编译存在参数恢复不足，不能拿未恢复的调用参数选定 mode。Ghidra 映像信息核对不等于字节映像哈希证明；这些新增反编译仅作为 STATIC 路由线索。

| 候选来源 | 已确认缓存契约 | 历史 endpoint 元数据 | 73/74 来源 |
|---|---|---|---|
| MI_01，FF00/1 | Input 65，ID0（Windows 无显式ID缓冲前缀假设） | IN85，max64 | UNKNOWN |
| MI_02 Col03，FFC0/1 | Input 21，ID3 | 共用 IN8C，max21 | UNKNOWN；76 的历史来源不能替代验证 |
| MI_03，01/06 Keyboard | Input20，ID0；标准 usage 位图 | IN8E，max19 | 未证实；本工具不注册 Keyboard、不取逻辑事件 |

上述端点来自上轮已审计的历史 USBPcap **注入配置元数据**，不是本轮总线读取。Windows Raw Input 只能确认收到报告的 `hDevice`/Collection；USB endpoint 的新物理确认仍缺失。完整八个 TLC 继续见 [HID_TOPOLOGY.md](HID_TOPOLOGY.md)，本轮未更改拓扑。

本工具只做严格结构探针：假设 byte0=缓存 ID、byte1=73/74、byte2..3=配置 Wire ID 的小端值、byte4=不透明参数。这是 **STATIC 派生假设**，不是已验证的 USB 字段表。MI_01 的 `RAWHID.dwSizeHid` 是否包含0前缀仍待验收；不匹配时停止，绝不自动滑动偏移、扫描任意 payload 或主动查询。不存在可交付为真实 PhysicalKeyStateProvider 的解析协议。

## 主要新增内容与隐私边界

- 独立工具目录 [physical-key-stage-b](../../../tools/research/physical-key-stage-b/README.md)：CMake、Win32 adapter、共用结构观察 core、模拟 fixture 测试。构建目录仅在 `audit_artifacts`；未接入仓库根 CMake 或 daemon。
- 每个进程只选一个 vendor TLC；VID/PID、Usage、精确 MI/Col 路径、缓存长度/ID 和唯一性均需匹配。内存精确绑定 `hDevice`；输出仅 alias、随机 run session、连接 generation。断连结束，不自动重连；下一次需要新预检与手动启动。
- 每组3次试验逐次点击 Start，5..30秒超时，闲置60秒退出。无后台 INPUTSINK、NOLEGACY、PAGEONLY、标准 Keyboard 注册或 Hook；同进程已注册则拒绝。独立进程规避 HardwareRtGateMonitor 同进程注册覆盖；跨进程实体共存仍 NOT RUN。
- 先取 RID_HEADER，目标类型/handle 确认后才取 RID_INPUT；其他设备的 payload 不获取。每个 batch 上限65536 bytes，每组200000 reports。只保留当前 scratch 与至多65 bytes上一报告，结束显式清零。无完整输入历史、文本、原始输出或默认文件持久化。
- 输出仅 marker、参数分桶、与所选配置字段假设的匹配数量、重复/有限顺序计数、主机接收间隔统计及停止原因。字段假设不匹配时只计 other，不打印键值。没有每键事件历史、时间配对、自动物理 PASS 或逻辑输入比较器；任何 `physical_identity/press_release` 固定 UNKNOWN。
- 桌面必须是本地 active/unlocked console、WinSta0/Default、Default input desktop，窗口处于前台。WTS锁屏/会话变化、失焦、断线、异常、错误 framing、预算或超时停止；输入回调也先检查状态，超时后不读 payload。清理 Raw Input、WTS、timer、window；注销失败记录并退出，进程退出作为最终释放边界。
- 无 HID handle、Output/Feature/Get Input 查询、控制命令、配置写入、HAL执行、驱动、网络或 AI API。现有 Aura Hook 和正式 Reactive/Ripple 保持原状，也未读取它们作为 vendor 证据。

本轮预检没有输入数据可泄露。未来窗口仅在明确专用测试会话内运行，Owner不得在其它应用输入敏感内容；不得上传 raw input 或向 LLM 发送实时报告。工具只输出脱敏摘要，保留摘要也需避免夹带终端/应用内容。工具无法保证 OS/第三方崩溃转储、休眠或调试器不复制进程内存；未提供 raw 导出。原始内存清除只覆盖程序自己拥有的缓冲。

## 验证结果

| 验证层 | 结果 | 限定范围 |
|---|---|---|
| STATIC | 证据包 SHA/CRC/内部哈希 PASS，窄范围 reader/transport 审查完成 | 路由仍 UNKNOWN；不是运行 HAL |
| OFFLINE TEST | **21/21 PASS**；Release MSVC `/W4 /WX /utf-8` 构建 PASS | 同一 core 的合成 fixtures、过滤、重复、乱序时钟、预算、断连/重置、fake注册与异常清理 |
| 本机只读预检 | 两个 scope 各1匹配；local_unlocked_console=true | `capture_started=false`、`raw_registration=false`；无实体按键或设备报告 |
| 无效参数 | timeout0 被拒绝，未启动 | 不是实体安全故障注入 |
| PHYSICAL PASS | **无；全部 NOT RUN** | native Raw Input delivery/锁屏通知/失焦/断线及与 daemon 共存未真机验收 |
| 生产回归 | 未重跑 | 无生产修改；用起始634个 tracked/非忽略untracked文件哈希核验保留 |

模拟报告在源码中构造，**不是设备 capture fixture**；tests 不把73/74解码为 Press/Release。本机预检的 `HidP_*` 处理缓存元数据，不通过 Output/Feature 查询。源码 API 清单与必要二进制 import 核查见交付安全 manifest。

## 实体矩阵与覆盖率

| 顺序/组 | 每个候选 Collection 最少重复 | 本轮结果 |
|---|---:|---|
| A按下、保持、松开 | 3 | NOT RUN |
| W / S / D分别 | 每键3 | NOT RUN |
| 左Win、左右Shift分别 | 每键3 | NOT RUN |
| Copilot独立 | 3 | NOT RUN |
| Copilot+真实左Shift / 左Win分别 | 每组合3 | NOT RUN |
| A/W/S/D多键、分别松开 | 3 | NOT RUN |
| 快速连按、持续保持、插拔/重连/清除、全部68键 | 另行明确动作与安全验收 | NOT RUN |
| Ctrl/Alt、Fn、已配置DKS/SpeedTap/重映射 | 基础结果通过后分阶段设计 | NOT RUN |

实体重复次数 **0**，物理身份确认覆盖 **0/68**，已执行按键捕获时间 **0秒**。这些不是缺失率或未观察事件的实测。右Ctrl/Copilot配置表同一物理槽位0x40，不能虚构两个独立实体键。配置映射只辅助假设探针；不证明 remap 前身份。

真实验收须记录 Owner完成动作与否、所选源、完整/中止状态，三次重复保留。Win/Copilot可能触发 OS UI 失焦；工具会中止，此时不能用不完整 trial 推断缺少释放或固件不支持。即使出现 marker，当前摘要也不足以证明组合中的逐键顺序/物理唯一性；需要 Owner审查后的有限语义验证，不能为了 PASS 开启更广输入记录。

## 风险、阻塞与下一步

主要阻塞是缺少明确进入会话的 Owner确认及实际报告证据。报告形状、字段、是否需使能、Press/Release vs DKS参数/其它功能、覆盖率、Copilot独立性、丢失恢复均 UNKNOWN。新工具 native行为只有编译与非捕获预检，WTS/Raw Input生命周期及共存未实测，不应宣布成熟可靠。

值得继续一次严格限定的 Owner操作 A测试：先 notifications，再单独 control，各三次。若事件存在，保留脱敏摘要并审核来源、framing和其它候选解释后再扩大到 Copilot/修饰键；三次普通键成功仍不构成 VIABLE。若有效完整试验无标记，只报告 scope、动作、时长及覆盖范围；不同偏移、使能条件或不向 Raw Input 暴露仍是具体未知，不推断全固件不支持。不发送使能或查询命令，也不升级独占或底层接管。

若失焦/安全检查/设备共存阻止观察，停止对应试验。全键物理身份、可靠释放、重连/初始未知状态和去重语义未闭环前，Provider 与正式迁移 Gate保持关闭；不移除 `WH_KEYBOARD_LL`、不改硬件 Gate、不发布 alpha.9、不合并。

## 复现与产物

构建、仅预检、Owner逐次启动的方法见工具 [README](../../../tools/research/physical-key-stage-b/README.md)。Stage B独立文件及报告由单独 patch交付；排除上轮11个新增文件以及任务前CI/存储/UX修改。

- 研究日志 staging：`audit_artifacts/physical-key-stage-b-20261008/staging/`；仅将必要产物复制到独立 `delivery-staging/` 打包。
- 交付目录：`audit_artifacts/physical-key-stage-b-20261008/output/`。
- `PHYSICAL_KEY_STATE_STAGE_B_EVIDENCE.zip`：本报告、工具README、脱敏预检/测试日志、继承校验、窄范围静态线索、安全/文件保留/内容清单和哈希；无原始按键、旧ZIP、完整源码或二进制。
- `physical-key-state-stage-b.patch`：仅本轮独立工具和文档的新增差异；外部 `.sha256`。本地编译exe仅留在 artifact/build，不放证据包，不自动运行监听。
- 脱敏机器可读验收状态：[STAGE_B_VALIDATION.json](evidence/STAGE_B_VALIDATION.json)。无捕获报告。

## API依据

Microsoft [RegisterRawInputDevices](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerrawinputdevices)规定同一进程每个设备class只有一个接收窗口、REMOVE需要null hwnd；本工具独立进程且不替换 daemon注册。[GetRawInputData](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getrawinputdata)区分RID_HEADER与RID_INPUT；本工具先按来源过滤。[WTSRegisterSessionNotification](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsregistersessionnotification)与注销及 [OpenInputDesktop](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-openinputdesktop)用于会话/桌面检查；未执行桌面切换或远程绕过。
