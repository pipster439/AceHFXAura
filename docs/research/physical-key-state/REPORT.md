# M605 物理按键状态研究

日期：2026-10-08（Asia/Shanghai）。基线：`0.1.0-alpha.8`，HEAD `c52398bef2738f71b927594424736a42da5184ab`。

## 结论

**INCONCLUSIVE**：目前没有足够证据证明可被动读取覆盖全部目标键、可靠区分物理身份的数字状态报告。确认存在设备专属的标准 Keyboard/NKRO 输入表面及 vendor 通知通道；这不等于确认存在可替换 Hook 的 Physical Key Matrix。

`MI_03` 当前缓存能力确认为标准 Keyboard TLC：Usage Page `0x01` / Usage `0x06`，Input Length 20，Report ID 0，输入按钮范围是 Keyboard Usage Page `0x07` 的 `0x00..0x97`、Data/Variable/Absolute。这是数字位图的强证据，却没有声明磁轴 Wire ID 或独立物理身份。修饰键 `0xE0..0xE7` 在 `MI_00` 声明，不能承诺只读 MI_03 即覆盖 Win/Shift/Ctrl/Alt。

发现值得保留的 vendor 候选：HAL `FUN_18001fc00` 的 `0x73/0x74` 分支以 16 位值反查 `DAT_1801caef0`，并调用 `+0xC8/+0xD0` 回调。固件 1.00.58 扫描函数中存在条件性 `0x73`（参数 1..4）、`0x74` 调用。这是后续数字事件研究入口，**不是**完整按下/释放协议证明；条件、覆盖范围、实时路由、丢失恢复及 1.00.59 的一致性仍未知。内部4字节队列 builder `0x180044C0` 的第二个标识字节还存在模式相关赋值分支，不能直接把内部记录推广成统一16位身份的 USB framing。没有据此编写报文解析器。

Copilot 串亮从物理输入层解决**有条件希望，尚未验证**：只有在 Copilot 实体键、真实 Win/Shift 同时按下、释放顺序及重映射情况下仍可分别识别来源，才能取消时间窗口猜测。标准逻辑输出包含多个固件合成键时，改用 Raw Input 仍不能恢复已经丢失的来源关系。

## 本次变更与范围

- 新增本目录研究、拓扑、可行性、隐私、验收矩阵及 Provider 设计文档。
- 新增独立缓存元数据枚举器与 8 项离线 ABI/目标筛选测试；不是按键捕获器或 PhysicalKeyStateProvider 实现。
- 保存脱敏拓扑快照：接口、能力、Report ID、历史配置描述符及源文件哈希；无报告 payload、设备路径或序列号。
- 未修改任何已有生产源码、CMake、正式 Reactive/Ripple、Hook、KeyInputHub、Native HID、配置或校准数据。任务开始已有 CI/存储/UX 等改动，排除在本次 patch 外。
- 未执行 Computer Use、实体按键测试、HAL 初始化、输入监听注册、USBPcap 新捕获、HID 报文读写、Feature Get/Set 或固件查询。未发布、合并、提交或推送版本。

## 研究过程与证据

| 层次 | 本次工作 | 能证明什么 | 不能证明什么 |
|---|---|---|---|
| 当前源码 | 阅读指定 9 份文档/代码/数据及 Wire 映射 | 生产 Hook、RT Gate 与 Native HID 的真实边界 | 实体按键行为 |
| 当前 Windows 缓存 | Raw Input 清单、HIDClass preparsed data、HidP caps | 5 个接口对应 8 个已枚举 TLC；7 个可取得完整能力 | 活动 Profile、当前固件、实际输入或时序 |
| 既有 USB 描述符 | 解析 resetType1 调查的注入配置元数据 | Interrupt IN/OUT 地址、最大包长、原始 bInterval、描述符声明长度 | 当前端点速率、报告正文、键事件 |
| HAL 静态 | 重读已归档 `FUN_18001fc00`；只读 Ghidra 反编译 `FUN_180020b80`，本地 PE 字节反汇编核对关键表/回调 | vendor 分发、Wire 表反查、`0x70` 热键/配置事件处理 | 已开启的物理报告服务 |
| 固件静态 | 既有 1.00.58 扫描反编译，加只读本地 Thumb 反汇编 `0x1800439A..0x18004504` | 条件性数字通知、4 字节内部队列格式和分支 | USB report 布局、当前设备覆盖与可靠性 |
| 软件离线 | 8 项测试、描述符 framing 校验、API 审查 | 元数据处理边界、脱敏及结构正确性 | 硬件协议或灯效行为 |

Raw Input 的 RIDI_PREPARSEDDATA 对当前 Keyboard/Mouse 项不可用。本次没有绕过此限制读取输入，另以 SetupAPI 枚举现有 HIDClass TLC，仅用 `CreateFileW(desiredAccess=0, share=READ|WRITE)` 取得缓存 preparsed data。共成功打开并释放 7 个目标元数据句柄；没有 `ReadFile`、报告查询或监听。`MI_02 Col04` 元数据句柄不可用，停止于 UNKNOWN；Raw Input 类型 Mouse 是可用证据。详见[隐私审查](PRIVACY_REVIEW.md)。

既有注入配置描述符声明 5 个接口、141 字节；不是一次新 USB 事务。历史 `bcdDevice=0x0159` 与既有用户确认的固件 1.00.59 对应；**没有查询当前固件版本**。1.00.58 静态固件哈希为 `8fe68a13d7a0cd3bf8b4fd0dbe0575c0b7e67c1d4373e8d63970cf248684e668`；本地 HAL 哈希为 `52d575bf942b7551b3f120c446bf0d853e36f9225c6b9a17407a80e0b1829f04`。Ghidra 项目路径/架构检查不等于内存映像哈希验证，因此关键新增 HAL 观察另由该本地文件字节反汇编支撑。

## 历史冲突与负面结果

1. `HALL_MAGNETIC_SWITCH_PROTOCOL.md` §21 已否定正常运行时连续 Hall ADC 流。本次接受此历史范围，不重启 ADC 研究，不把这一负面结果扩大为“没有数字 vendor 通知”。
2. 旧 `MAGNETIC_SWITCH_PROTOCOL.md`、HAL 报告中“Interface 3 / 64 字节 / 0x73 ADC 深度”未经验证。当前 MI_03 的标准 usage/位图与长度不支持这套描述；已有固件 `0x73` 调用的参数 1..4 更不能直接解释为通用 ADC 深度。保留原报告，明确在本目录纠偏。
3. 旧拓扑表声称 8 个 Collection，却只列 7 行，漏了 `MI_02 Col04`。其 `Col03` 仅作为无线遥测的描述也不完整：已有 RT Gate 证据确认有 `03 76` 通知。
4. `calibrated_keymap.json` 的历史 `lighting_interface` 文本写 Interface 3；当前 Native HID 匹配 MI_01。键位/LED 校准不是 USB 接口或输入语义的证明，本次不修改运行期数据。
5. `0x70` vendor bitset 不能直接视为全键矩阵：新增 HAL 只读反编译显示消费者遍历前 5 字节，通过 `DAT_18014c600/14c6c0` 产生热键/配置事件，含 default/other Profile 分支。其存在不足以证明普通键持续状态；实际调用 HAL 还可能改注册表，因此本次未加载或执行 HAL。
6. 固件 `0x1800439A` 有表查找及键码转换分支，包括修饰键范围；这提醒“位图”与“输入身份”必须分开验收。它不能证明报告一定处于重映射前。

## 验证结果与限制

- 离线自动测试：**8/8 PASS**；验证目标路径精确筛选/脱敏、HIDP 能力 ABI、范围与非范围 union、非法长度与类型。仅验证元数据工具。
- 两种缓存枚举：完成；8 个目标 TLC 被 SetupAPI 枚举，7 个能力成功；Col04 能力受限，保留 UNKNOWN。
- 既有配置描述符：长度及 5 个接口/端点数量校验 PASS；report descriptor **正文未在该捕获记录中提供**。
- 静态工具 API：仅缓存元数据、枚举、共享零访问句柄及释放 API；无输入注册、输入读取、HID query/write、HAL 执行或网络 API。
- 初版 patch 校验发现新拓扑 JSON 的 CRLF 被严格空白检查拒绝；生成器改用 LF，最终隔离 `git apply --check --whitespace=error` 通过。初次失败日志一并保留。
- 固件两个地址的 Ghidra 反编译因没有已定义函数而失败；未创建函数、重分析或修改数据库，改用已存本地二进制的有界反汇编。失败日志保留在证据包。
- 生产测试矩阵未重跑：生产文件未改；本次以 615 个起始 tracked 文件的内容哈希验证不变。不能把以前 CTest/UI/物理 PASS 当成本次结果。
- Stage B 输入捕获及全部 Stage C 物理验收：**NOT RUN**；Computer Use 阶段 B 未恢复。Stage D 仅提交[接口与状态机设计](PROVIDER_DESIGN.md)，无真实报文 parser 或硬件 fixture。

## 残余风险与未解决事项

主要缺口是 vendor 数字事件是否在当前配置中被动存在，以及是否覆盖 Copilot、Fn、修饰键、DKS、SpeedTap 与重映射，具备完整释放/初始快照/丢包恢复。缓存长度与 Usage 不能回答这些问题。事件队列或开关通知可能需使能，**本任务不得尝试使能或发送任何命令**；若只有主动使能才可观察，停止当前被动方案。

标准 Raw Input 可以按 hDevice 区分来源，但 Keyboard 类提供 RAWKEYBOARD make/break，不能假设得到 RAWHID 的原始 MI_03 位图，也不能凭同一设备的逻辑宏序列反推出独立物理键。没有把当前负面证据提升为全接口 NOT VIABLE。

后续只有在用户恢复独立 Stage B 测试窗口后，按[验收矩阵](PHYSICAL_ACCEPTANCE_MATRIX.md)短时被动观察已确认 TLC；先验证 vendor 候选是否自然出现，再验证物理身份与生命周期。缺报文、无释放、需主动命令或无法安全观察时停止。当前[集成门槛](INTEGRATION_PROPOSAL.md)保持关闭。

## 重要证据与交付位置

- 持久化[拓扑快照](evidence/TOPOLOGY_SNAPSHOT.json)、[HID_TOPOLOGY](HID_TOPOLOGY.md)、[可行性](PHYSICAL_KEY_FEASIBILITY.md)、[隐私](PRIVACY_REVIEW.md)。
- 工具：`tools/research/physical_key_state_metadata.py`、`tools/research/physical_key_state_hid_inventory.py`；测试：`tests/test_physical_key_state_metadata.py`。
- 证据 staging：`audit_artifacts/physical-key-state-20261008/staging/`，含源哈希、两份缓存元数据、配置描述符分析、静态片段、反汇编及本次测试日志。
- 交付：`audit_artifacts/physical-key-state-20261008/output/PHYSICAL_KEY_STATE_EVIDENCE.zip`、`physical-key-state.patch`、外部 `.sha256`。从独立 staging 打包，不含旧包、pcap、二进制、源码树、依赖或凭据。

## API 来源

Windows 元数据与输入语义采用 Microsoft 文档：[GetRawInputDeviceInfoW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getrawinputdeviceinfow)、[HidD_GetPreparsedData](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidsdi/nf-hidsdi-hidd_getpreparseddata)、[RAWKEYBOARD](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawkeyboard)、[RAWHID](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawhid)。同一进程每个 Raw Input class 仅允许一个接收窗口，后一次注册会替换前一次，依据 [RegisterRawInputDevices](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerrawinputdevices)；本次没有注册。
