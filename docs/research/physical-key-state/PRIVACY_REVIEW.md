# 隐私与只读边界审查

本轮研究工具只处理描述符能力元数据，**没有输入捕获功能**。没有输入文本、密码、聊天内容、完整按键历史或报告 payload 进入日志、磁盘、网络或 LLM。拓扑元数据和静态二进制代码不属于用户键入数据。

## 本轮数据生命周期

| 数据 | 处理方式 | 持久化 / 输出 |
|---|---|---|
| Raw Input 设备清单 | 只枚举，不注册；取得名称以筛选 VID/PID/MI | 仅目标 MI/Col 后缀，零事件；其它设备名称不导出 |
| SetupAPI HID 接口路径 | 内存里精确匹配 VID0B05/PID1B7E 与 MI 后缀 | 不保存设备路径、序列号、GUID 路径、hDevice/句柄 |
| 目标 preparsed data | 缓存读取，内存 HidP_GetCaps/GetButtonCaps/GetValueCaps | 仅 public 能力字段；不保存不透明缓存 blob |
| Keyboard/Mouse 输入 | 未注册、未读取 | 无 |
| HID Input/Feature/Output reports | 没有 ReadFile、GetInputReport、GetFeature、WriteFile、SetFeature | 无 |
| 既有捕获 | 仅解析已导出的 device/config descriptor 元数据 JSON | 不打开历史 pcap，不重建键历史 |
| HAL/固件 | 文件只读、既有静态片段、只读 Ghidra/本地反汇编 | 有界静态代码片段、文件哈希；不执行 HAL/DLL，不包含二进制 |

默认工具只使用 `GetRawInputDeviceList` 和 `GetRawInputDeviceInfoW` 缓存查询，不打开 HID 句柄。`--all-hid-cached` 是显式元数据模式：只对已匹配路径开 `desiredAccess=0`、`FILE_SHARE_READ|FILE_SHARE_WRITE`、`OPEN_EXISTING` 句柄；只调用 `HidD_GetPreparsedData` 取得 HIDClass 已解析的 TLC 信息，随后释放。它不是向设备发出的 HID report query。设备拒绝时记录 unavailable，不提权、不改变共享模式、不尝试输入读取。

使用 `finally` 清理 preparsed data、元数据句柄和 SetupAPI info set；枚举数量/缓冲区/capability 数量有上限；失败保持 Unknown。工具进程退出后无线程、消息窗口、注册项、状态缓存或按键 held 集合。Python 内存不承诺逐字节安全擦除；本轮不取得输入 payload，只有短时路径和描述符元数据在进程内存中。

本次成功打开7个零访问目标元数据句柄，Col04 打开失败并停止该通道。没有独占端点、安装驱动、修改 queue、调用 HAL、发送查询/固件指令、修改 Profile/磁轴/灯效/注册表或启动持续监听。普通 OS/其它正在运行的程序流量未被本轮总线审计，不能声称全机没有其它 HID 写入。

## 将来 Stage B 的最低隐私约束（未实现，未运行）

1. 用户恢复 Stage B 后再运行明确启动、可随时停止的独立研究进程。只在专用测试窗口和无敏感输入的环境中启用短时会话，预选测试键/测试用例；不常驻、不随 daemon 启动。
2. 只注册已证实 vendor TLC 的 usage pair，校验 VID/PID、MI、Col、能力与准确 hDevice。Raw Input 注册按 usage pair 而非设备路径，因此系统可能送达同类其它设备；先检查 header 身份，不解析非目标 payload，不保存它。多目标设备或身份变化则停测、清空，不能合并。
3. 优先前台测试窗口，无 `RIDEV_INPUTSINK` 的后台键盘捕获；焦点丢失、窗口隐藏、会话锁定、超时、停止或异常立即 unregister、释放窗口/线程并清空状态。未证明能可靠感知这些条件的观察器不得放行。
4. 不建立通用 Keyboard logger。若需要标准 Keyboard 对照，仅在显式短时测试会话内处理选定键，禁止文本转换、ToUnicode、完整键历史、文件/clipboard/网络输出。能用 case 判定/计数表达就不输出键序列。
5. 原始 vendor 报文默认不持久化；必要时仅有界、短时 RAM 分析，完成用例即丢弃。测试摘要只输出 Case ID、通过/失败/未知、计数、聚合延迟和有限结构；若确需研究原始 fixture，另行说明与明确选定，不从敏感场景采集。
6. 不向 LLM Provider/遥测/云端上传任何输入数据。缺少证据时本地人工对照与匿名判定摘要优先；本次没有输入数据上传。
7. 不修改其它进程注册；同进程 vendor class 注册会覆盖 RT GateMonitor，因此未来只用独立进程或经过单一注册所有者设计，禁止临时往 daemon 插入监听器。
8. 不调用 Feature Get/Set、Output、查询、主动使能、HAL 初始化、queue 修改或端点独占。若被动条件不够，则停测，报告 UNKNOWN。

## 当前风险与验收

8 项离线测试覆盖路径脱敏与近似目标拒绝、公共能力 ABI 和边界；不是完整安全证明。API 审查和源码确认只有固定元数据方法，没有 report capture/query/write 或网络方法。数据包只含报告、结构、哈希、测试/审查日志及必要静态小证据，不含完整 capture、用户配置、设备路径或 ASUS DLL/固件。

未来输入捕获的隐私、锁屏、焦点取消和资源生命周期尚未实现/验收，不能以本轮元数据工具的 PASS 替代。依据：[Windows preparsed data API](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidsdi/nf-hidsdi-hidd_getpreparseddata)、[Raw Input 注册约束](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerrawinputdevices)。
