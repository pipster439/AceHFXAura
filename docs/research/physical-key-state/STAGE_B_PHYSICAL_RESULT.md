# Physical Key State — Stage B 真机结果

日期：2026-10-09（Asia/Shanghai）。项目 `pipster439/AceHFXAura`；版本 `0.1.0-alpha.8`；HEAD `c52398bef2738f71b927594424736a42da5184ab`。

## 结论

**INCONCLUSIVE**。Owner 已在 notifications 与 control 两条 vendor Collection 上，各完成两组三次实体 A 按下、保持约2秒、松开，共 **12 次**，名义监听窗口累计 **120 秒**。全部正常超时结束，目标报告为0，`0x73/0x74` 为0。第二组带注册/过滤/运行诊断：两个 Collection 各三轮均为 **WM_INPUT=0**，不是已收到报告被过滤或解析丢弃。

这否定了本次受限试验中“普通 A 自然产生可由该观察器收到的候选事件”的成功预期，**不证明固件没有数字物理事件**。没有产生不修改配置的 vendor 报告输送正控，也没有读取 USB 总线数据；Windows 的注册成功、前台运行及目标缓存匹配不能证明整个设备→USB→HIDClass→Raw Input 链路有活动输送。路由、使能条件和当前固件/Profile仍未知。

本轮没有物理按键协议层 **PHYSICAL PASS**。不能进入 PhysicalKeyStateProvider 真实协议实现或正式 Reactive/Ripple 迁移；Copilot与真实Win/Shift的独立性仍未验证。正式 Hook、输入管线、硬件 Gate、配置和版本均未改。

## 继承与环境检查

继承 `PHYSICAL_KEY_STATE_STAGE_B_EVIDENCE.zip`，SHA-256 `5abbc00eeeece8110a5a939de6c5a6f308f3534003f83c0f343c41e8d1be3ee0`；CRC、内部文件哈希、上轮7个任务文件与既有exe哈希全部匹配。未重复开发观察器、未执行 HAL、未重做完整静态逆向，也未重开 Hall ADC 研究。上轮 [STAGE_B_REPORT.md](STAGE_B_REPORT.md) 的静态推断及未知项保持原等级。

只读预检显示本地 active/unlocked console 与 Default input desktop可用，VID `0x0B05` / PID `0x1B7E` 的两个指定 TLC各唯一匹配。工具按 VID/PID、Usage、精确MI/Col路径、缓存Input长度/ID和 `hDevice` 绑定来源；设备路径、序列号和handle不输出。每个进程使用随机session与连接generation，不混合两条通道。

源码检查确认 RT Gate monitor 在 daemon进程内注册 FFC0/1；研究工具独立进程，注册和注销仅操作自己的窗口与class。Microsoft规定一个进程每个Raw Input class只有一个接收窗口，因此本次不会替换 daemon同进程注册。本机检查未发现 `aura_daemon` 运行；没有启动正式UI或daemon，也没有进行“两个进程同时接收真实RT通知”的共存验收。**静态注册隔离已确认，实体RT共存 NOT RUN**；不将前者伪装为后者。

## HID 来源及 Report ID 对照

| 通道 | 当前缓存身份 | 缓存Input契约 | 本轮实际Raw报告/ID/长度 | Endpoint来源 |
|---|---|---|---|---|
| notifications | MI_02 Col03；FFC0/1 | ID3，21 bytes | 无报告；实际ID/长度 **UNKNOWN** | 上轮历史注入配置元数据：IN8C/max21；本轮未实测endpoint |
| control | MI_01；FF00/1 | ID0，Windows缓存65 bytes | 无报告；实际ID/长度及0前缀 **UNKNOWN** | 上轮历史注入配置元数据：IN85/max64；本轮未实测endpoint |
| 标准键盘/NKRO | MI_00、MI_03；01/06 | 继承标准Keyboard缓存能力 | **NOT RUN**；未注册或读取逻辑按键 | 继续参见 [HID_TOPOLOGY.md](HID_TOPOLOGY.md) |

上轮HAL `ReadEvent_interface3` 是内部名称，不能当成真实MI编号；`0x73/0x74` 的来源仍 UNKNOWN。RT通知 `03 76` 的历史来源是 MI_02/8C，但本轮没有触发或读取RT开关报告，不用它证明73/74路由。预期report ID/长度来自缓存，不能当成本轮捕获到的字段。

## 实验方法与 Owner 证据

Owner明确授权只读被动观察。每次窗口只显示 A 的指定动作；Owner用鼠标点击 Start，看到TEST ACTIVE后在目标键盘物理按A、保持约2秒、松开，每轮10秒自动结束，三轮后窗口退出。保持时间是操作要求/Owner确认，未测量开关行程或真实输入延迟。Agent没有发送虚拟键，也没有自动点击Start或读取其它应用内容。

依次运行：原观察器notifications三轮、control三轮；两条均无报告后，补充独立工具诊断并复测同样的两组三轮。三次未完成尝试均保留：首次Hidden启动使窗口隐藏、两次新增注册核验错误安全停止。它们均为0 trial，不计入12次或120秒。Owner逐组明确确认实体操作完成；长期证据只保存问题标签、完成反馈与脱敏摘要。

诊断版本在每轮开始后读回当前进程注册项，核对一个目标page/usage、前台flags和自己的窗口；每个安全timer tick核对本地会话、前台和缓存目标身份；每轮结束核验目标仍匹配、注册已移除。WM_INPUT/header目标或其它来源/payload batch分别计数。所有报告仍受header来源过滤、限定framing、内存擦除、预算及停止条件约束；没有更广Collection、后台输入或主动查询。

## 脱敏统计

| Collection / 版本 | Owner确认重复 | 名义窗口 | WM_INPUT | 目标报告 | 73 / 74 | 结束 |
|---|---:|---:|---:|---:|---:|---|
| notifications / 原观察器 | 3 | 30秒 | 未提供该诊断 | 0 | 0 / 0 | 3次timeout，cleanup成功 |
| control / 原观察器 | 3 | 30秒 | 未提供该诊断 | 0 | 0 / 0 | 同上 |
| notifications / 最终诊断版 | 3 | 30秒 | 0 | 0 | 0 / 0 | 同上；注册/注销/目标复核均true |
| control / 最终诊断版 | 3 | 30秒 | 0 | 0 | 0 / 0 | 同上；注册/注销/目标复核均true |

最终诊断notifications每轮安全ticks为93、92、92；窗口结束时长10078、10047、10016ms。control为92、93、93 ticks；结束时长10000、10078、10094ms。时长包含超时检测调度及收尾缓存复核，**不是精准采集时长、开关响应延迟或固件频率**。名义timeout始终10秒，输入handler也检查deadline。每轮目标header、其它来源header及payload batch均0，没有可用于Report ID、字段或按键映射解析的样本。

全部完成进程均输出 `raw_memory_clear=true`、`cleanup_ok=true`；最终六轮额外 `registration_verified/registration_removed_verified/end_target_verified=true`。所有研究进程已停止；没有持续监听或残留输入状态。本轮只测试A一种实体身份，物理操作范围 **1/68**，成功确认物理ID范围 **0/68**。不能以12次重复替代68键覆盖。

机器可读统计与逐组归类见 [STAGE_B_PHYSICAL_STATS.json](evidence/STAGE_B_PHYSICAL_STATS.json)，逐轮JSONL和Owner确认在独立证据包中。无原始payload或完整按键序列。

## 每项判定

| 测试项目 | 判定 | 准确范围 |
|---|---|---|
| A / notifications被动候选自然出现 | **PHYSICAL FAIL** | Owner确认6次；未获得任何报告。仅此通道、动作、当前配置和窗口范围的目标未达成 |
| A / control被动候选自然出现 | **PHYSICAL FAIL** | 同上；不是全固件不存在协议的结论 |
| A物理ID、Press/Release、重复/释放语义 | **INCONCLUSIVE** | 没有有效报告，无法分析；不做时间配对 |
| Win / 左右Shift / Copilot | **NOT RUN** | Phase2没有有效候选，扩展门槛未满足 |
| Copilot+Win/Shift、多键、W/S/D、快速连按/长按 | **NOT RUN** | 同上；A的约2秒保持不等于长按专项验收 |
| DKS/SpeedTap/重映射、Fn/Ctrl/Alt、68键覆盖 | **NOT RUN** | 未修改现有配置，也未推断这些场景 |
| 锁屏、设备插拔、重连、异常故障注入、RT共存 | **NOT RUN** | 正常timeout与注销实测成功不能代替这些故障/共存项目 |
| 可用于Reactive/Ripple的物理输入Provider | **INCONCLUSIVE / GATE CLOSED** | 无身份、按下/释放、完整覆盖或恢复证据 |

这里PHYSICAL FAIL指已完成指定物理操作却未达到“被动收到候选”的测试目标；不是NOT VIABLE。Windows注册元数据核验、构建和离线测试采用自己的软件证据等级，不记为物理按键协议PHYSICAL PASS。

## 观察器修复与验证

保留原exe及原证据，诊断版构建在独立 `audit_artifacts/physical-key-physical-20261009/build/`。仅修改研究目录四个文件：`observer_win32.cpp`、`observation_core.h`、`offline_tests.cpp`、`README.md`。没有修改根CMake、生产源码或旧研究报告。

修复/补充：明确scope/case窗口标题、轮次倒计时、进程注册读回/注销验证、消息/过滤计数、安全心跳与窗口时长、每tick目标缓存复核、非捕获 `--registration-audit`。后者无message pump、不arm Observation、不读取payload，注册元数据后立即移除；不是实体测试。

新增诊断最初有两处工具错误：NULL注册查询的 `ERROR_INSUFFICIENT_BUFFER` 被当成失败；随后又要求snapshot必须回显DEVNOTIFY。安全停止记录表明两次均未arm、0 trial、清理成功。当前主机实测成功请求flags8192(DEVNOTIFY)后，读回flags0，page/usage/window一致。最终仅接受snapshot flags0或8192，拒绝INPUTSINK、NOLEGACY、PAGEONLY及其它flag；实际注册请求仍恒为DEVNOTIFY，没有为了PASS开启后台监听。通知送达本身未做插拔物理验证，不能由snapshot回显推断。

**OFFLINE TEST：25/25 PASS**（原21项合成core测试+4项注册snapshot回归，拒绝错误返回、额外注册、混合Collection、窗口不匹配和不允许的flags）。Release MSVC `/W4 /WX /utf-8` 构建通过。最终六个Owner确认trial原生注册、心跳、目标复核、正常timeout及注销均实测通过；输入接收/报告解析分支因WM_INPUT=0未被真机覆盖。没有硬件capture fixture，也没有实现真实按键状态机。

源码与PE imports审查限于只读缓存、Windows注册及UI生命周期：无Hook、HID handle、Report查询/Output/Feature写入、vendor HAL执行、网络或AI API。641个起始文件中，仅上述4个研究工具文件按授权改变，另外637个内容不变；新增报告和统计单独列入本轮patch。生产全套矩阵未重跑，因为生产代码未改；原21和最终25的软件测试不冒充硬件协议通过。

## 未解决事项与下一阶段

核心缺口仍是有效vendor数据及输送正控：可能有固件使能条件、特定功能限定、不同实际route/framing，或Raw Input可见性限制；本轮没有证据区分它们。没有当前固件/Profile读回，没有USB endpoint新验证，没有独立Copilot身份，没有初始全态或丢报恢复证明。不得把缓存配置Wire ID和假设字段当成已验证物理映射。

**不建议现在进入PhysicalKeyStateProvider实现。** 若继续，先审查已有HAL/固件的窄范围路由/使能链，并设计无需查询、配置改变或驱动接管的vendor输送正控；没有合规正控就停止该被动路径。不能为获得报告自行执行HAL、切RT/DKS/Profile、发使能命令、打开独占HID或安装捕获驱动。标准Raw Input按来源过滤仍可能只有固件逻辑输出，不能自动解决Copilot合成Win+Shift+F23。

本轮研究完成后已停止；未迁移正式功能、未移除WH_KEYBOARD_LL、未改硬件Gate、未发布/提交/推送或合并新版本。

## 复现与交付

构建与Owner启动步骤见 [工具README](../../../tools/research/physical-key-stage-b/README.md)。只读预检和注册元数据自检先于有限实测；每个scope单独进程、手动开始、三次重复，停止原因与未完成尝试均保留。只保留脱敏摘要和Owner完成检查，不保存raw、不上传实时输入。

- 本报告：`docs/research/physical-key-state/STAGE_B_PHYSICAL_RESULT.md`。
- 脱敏机器统计：`docs/research/physical-key-state/evidence/STAGE_B_PHYSICAL_STATS.json`。
- 本轮研究日志/manifest：`audit_artifacts/physical-key-physical-20261009/`。
- 独立交付staging：该目录的 `delivery-staging/`；输出 `output/PHYSICAL_KEY_STATE_STAGE_B_PHYSICAL_EVIDENCE.zip`、`physical-key-state-stage-b-physical.patch` 和外部SHA-256。
- Patch以本轮起始工作树（已含上轮Stage B交付）为基础，仅含4个工具修改与本轮新增文档/统计；不混入上轮未提交文件或已有CI/存储/UX改动。ZIP不包含旧ZIP、二进制、源码树、构建/依赖/缓存或原始输入。内容/CRC/内部hash/敏感信息检查见外部delivery inspection。

API依据：Microsoft [RegisterRawInputDevices](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerrawinputdevices)的进程注册隔离和REMOVE约束；[GetRegisteredRawInputDevices](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getregisteredrawinputdevices)的NULL/不足缓冲返回约定。snapshot flags0属于**本机实测**，不声称是文档保证的全部Windows行为。
