# Physical Key State — Overnight Offline Report

日期：2026-10-09，Asia/Shanghai。基线 `0.1.0-alpha.8`，HEAD `c52398bef2738f71b927594424736a42da5184ab`。本轮是无人值守离线审计，已完成授权范围；真实投递与 RT 正控 **NOT RUN / BLOCKED_PHYSICAL**。

## 结论

**INCONCLUSIVE**。没有发现足以解释此前正常完成的最终六轮 `WM_INPUT=0` 的确定软件缺陷。发现并修复了计数位置、异常/消息循环退出原因、时钟检查和 RAWINPUT padding 兼容性等研究工具问题；它们不能被追溯认定为历史零输入的原因。

Release 构建通过，**OFFLINE TEST 49/49 PASS**。测试覆盖实际观察器共用的读取结果校验、来源过滤、framing、RT76 解析、资源所有权与停止逻辑；数据来自 mock 和内存 fixture，未调用输入注册、窗口创建或真实 GetRawInputData。它们不证明原生 `WM_INPUT` 投递成功。

新增可靠 **STATIC** 证据：HAL 的 `+0x1070` 包装对象由传输工厂创建；工厂存在 FF00/FFC0 两种模式；reader 的虚表 `+0x30` 对应 `FUN_180014bc0`，它以 `param_3+1` 读取、去掉首字节后再回调。M605 调用传入 `0x40`，所以日志中的“interface3/64”不能直接解释成 MI_03 或真实 report 长度。尚未闭合“当前 M605 对象选择哪种模式 → USB endpoint”的链路。73/74 触发依赖 per-key 配置、阶段及额外条件，不能确认为全键 Down/Up。

明天最小重复正控为 **6 次实体 RT 拨动：3 轮，每轮拨到另一状态后拨回原状态**。每轮手动 Start、10 秒、有两个至少约2秒的稳定状态。两次拨动即可检查一次 ON/OFF，但不满足三次独立重复。今晚没有执行这些动作、启动任何观察器模式或自动等待 Owner。

## 继承证据

三个 ZIP 的外部 SHA-256、CRC、内部文件哈希均核验 PASS，记录在本轮 `inheritance.json`。已阅读完整归档报告、研究日志、拓扑、源码与脱敏物理统计。先前证据保持原等级：

| 证据 | 等级 / 范围 |
|---|---|
| 12 次 Owner 确认实体 A，两个 vendor TLC 各6次，全部报告0 | 历史 PHYSICAL FAIL，仅“指定窗口中候选自然出现”目标失败 |
| 最终两组各3轮：WM_INPUT、目标/其它 Header、payload 均0；注册、心跳、注销、目标复核通过 | 历史实测软件诊断；旧计数在 active/safety/deadline 判断后，并非 WndProc 无条件总数 |
| MI_02 Col03 / FFC0/1 / ID3 / 21 bytes；MI_01 / FF00/1 / 缓存65 bytes | 历史缓存身份，今晚未刷新硬件 |
| RT `03 76 00 00 state` + 16个零字节 | 历史物理通知契约；今晚为重建 fixture，不是新捕获 |
| 73/74 callbacks 与固件调用 | STATIC，物理身份、真实 routing、当前配置使能 UNKNOWN |
| Copilot、真实 Win/Shift 独立性、68键、DKS/重映射、当前正控 | NOT RUN / UNKNOWN |

旧包 SHA：初始 `bb4bd64b712258b022af83546cd79cf335edbee4d5ebf98e389edbeda1ac00bd`；Stage B `5abbc00eeeece8110a5a939de6c5a6f308f3534003f83c0f343c41e8d1be3ee0`；Physical `8b56d261e2b21caf82251d4b88060f6c73adf3b7f940f25438d1cfbec190b696`。未把零输入升级为“固件完全不支持”。

## 主要变更

仅修改 `tools/research/physical-key-stage-b/` 独立项目：

- `input_delivery.h` 新增可注入的 Header → size → copy → 校验 → core 链路；使用真实 Win32 sizeof/offsetof，非目标来源或旧 generation 不读取 payload，临时对齐缓冲正常与异常退出均擦除。
- `observation_core.h` 补充 RT 状态/统计、ID与长度分布、framing错误类别、timer时钟 watermark、消息循环退出分类和断开 generation 更新。复用已有纯 RT parser，未链接正式 monitor。
- `observer_win32.cpp` 采用该链路；计数前移，补充窗口/线程有效性、注册持续核验、设备变更、API阶段错误、loop/异常与初始化诊断；前台、显式 Start、超时、锁定/断开停止边界保持。
- `delivery_tests.cpp` 新增24项离线测试；保留原25项。独立 CMake 增加测试源码，README更新复现和诊断解释。
- 新增本轮五份报告及 `evidence/OVERNIGHT_VALIDATION.json`。没有实现 73/74 Press/Release 或 PhysicalKeyStateProvider。

正式 Reactive/Ripple、WH_KEYBOARD_LL、HardwareRtGateMonitor、安全 Gate、Profile、RT/DKS配置及发布版本无改动。本轮不提交、不推送、不合并、不发布。起始既有 CI/存储/UX及历史研究改动由任务前哈希保护，patch只以本轮开始工作树为基础。

## 验证及实际结果

| 项目 | 本轮结果 |
|---|---|
| 三个旧证据包完整性 | PASS，外部/内部 hash 与 CRC |
| 独立 Release `/W4 /WX /utf-8` 构建 | PASS，MSVC19.51 / VS18 / x64 / SDK10.0.26100 |
| 离线 core、mock acquisition、故障注入 | 49/49 PASS；CTest 1/1 PASS |
| 静态 FW/HAL hash与关键字节、HAL虚表核验 | PASS；不执行 HAL，不改变 Ghidra DB |
| 观察器运行、注册自检、预检、WM_INPUT/设备变更/桌面验收 | **NOT RUN**，本轮禁止 |
| RT、A、Copilot/Win/Shift真机 | **NOT RUN / BLOCKED_PHYSICAL** |
| 全量生产回归 | 未重跑；没有生产变更，文件哈希核验代替生产验收声明 |

初次测试构建因 fixture 的 `std::fill` 将 int 转 uint8_t，触发 `/WX C4244`，改为显式 uint8_t 后通过；无硬件动作。新旧离线 PASS 都不能替代 Physical PASS。

首次隔离patch应用通过，但逐文件hash比对发现新增validation JSON使用Windows默认CRLF而patch为LF；生成器改为显式LF，使用全新隔离目录重新核验。初次失败收据保留，未改动已有工作树或旧交付包。

## 残余问题与下一阶段

RT正控 PASS 可确认指定 MI_02 Col03 在该会话的真实投递/解析链路，不能证明73/74也来自它。若 WM_INPUT仍0且注册、窗口、心跳均正常，单一观察器不能区分“USB没发、HIDClass不可见、Raw Input没投递”；应停在 INCONCLUSIVE。详见 [失败定位表](NEXT_DAY_RT_TEST.md)，不得主动查询、使能模式、接管endpoint或安装捕获驱动。

FW分析版本1.00.58与历史实体1.00.59不同；当前 Profile/固件未查询。内部队列 consumer、73/74 report路由、门控字段的全体写入者未闭合。Copilot Wire配置槽位不是物理事件映射；Fn模式还会改变内部标识高字节。无需 Hook 的物理解决路径仍有条件希望，无可用协议证据，**不进入 Provider 实现或正式迁移**。

值得继续一次有限 RT 正控；成功后再决定是否值得单键被动复测。正控失败就停止硬件路径、交回报告，不能靠更多 A 重复或扩大权限消除未知。

## 产物

- [Raw Input 审计](RAW_INPUT_DELIVERY_AUDIT.md)、[Vendor静态分析](VENDOR_EVENT_73_74_ANALYSIS.md)、[离线结果](OFFLINE_TEST_RESULTS.md)、[明日RT流程](NEXT_DAY_RT_TEST.md)。
- 本轮实验目录：`audit_artifacts/physical-key-overnight-20261009/`；`build/Release/stage_b_observer.exe` 仅构建，未运行；不要混用上一轮 exe。
- 独立 `delivery-staging/` → `output/PHYSICAL_KEY_STATE_OVERNIGHT_OFFLINE_EVIDENCE.zip`；单独 `physical-key-state-overnight-offline.patch` 和外部 `.sha256`。包只含本轮报告、脱敏日志、manifest、hash及小型静态证据，不含二进制、源码树、旧包或原始输入。

API审计依据微软官方 [RegisterRawInputDevices](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerrawinputdevices) 与 [GetRawInputData](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getrawinputdata)，完整契约对照在专项报告。
