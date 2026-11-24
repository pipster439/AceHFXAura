# Raw Input Delivery Audit

日期2026-10-09。纯源码、历史诊断与离线测试；原生窗口/注册/桌面 **NOT RUN**。分类限定被审计命题，DISPROVEN不意味着已证明真实投递。

## 关键判断

不存在已确认能解释最终六轮正常 timeout、持续安全心跳且 `WM_INPUT=0` 的软件根因。报告ID/长度/目标过滤在旧 WM_INPUT计数之后；这些解析错误即使存在，也不会造成该旧计数为0。旧计数不是完整WndProc总数，已修正；安全条件异常应导致提前停止，与正常完成记录不一致，但不能由静态推理补出未记录消息。

## 完整链路分类

| 项目 / 假设 | 分类 | 证据、处置与边界 |
|---|---|---|
| 研究工具误建 HWND_MESSAGE 而不能成为前台 | DISPROVEN | 观察器 CreateWindowEx 的 parent=nullptr，WS_OVERLAPPED可见顶层窗口；正式Monitor才使用HWND_MESSAGE。没有改为后台窗口 |
| Window对象/thread/USERDATA过早销毁 | DISPROVEN（源码路径） / UNKNOWN（新原生验收） | App活到loop结束；WM_NCCREATE绑定App；Destroy后清空hwnd；新增IsWindow及GetWindowThreadProcessId核验。输出validity，不持久化实际handle/thread数字 |
| Raw Input flags或cbSize错误 | DISPROVEN（当前源码） | 单项page/usage、sizeof RAWINPUTDEVICE、DEVNOTIFY、指定自己hwnd；无INPUTSINK/NOLEGACY/PAGEONLY。REMOVE使用nullptr |
| 同进程多窗口替换注册 | SUSPECTED通用风险，当前正常路径DISPROVEN | 单一进程/窗口/usage，mutex阻止第二个研究工具；启动拒绝已有注册，active timer新读回注册。嵌入生产进程会有冲突，禁止集成 |
| 跨进程注册直接替换RT monitor | DISPROVEN（API契约） | 只对同进程有“一个class一个window”限制。真实双进程共存依然UNKNOWN/NOT RUN，不启动daemon验证 |
| 注册NULL sizing和strict flags回显导致提前退出 | CONFIRMED BUG（历史、已修复） | 两次0-trial诊断失败，不计入12次A。旧最终版已有直接单项读回、接受0/2000；本轮空表也改用实buffer，减少NULL语义依赖。flags0是本机历史观察，不能证明DEVNOTIFY实际送达 |
| GetMessage把-1误作有效消息 | DISPROVEN | `>0`循环正确，hwnd=null/filters0涵盖线程所有窗口与线程消息；Translate/Dispatch均调用，无额外PeekMessage吞输入 |
| loop -1或异常WM_QUIT没有正确退出原因 | CONFIRMED BUG | 旧版只设置fatal或cleanup Cancel，run_stop可为none。新增message_loop_end和错误码，unexpected exit停止并汇总清理；native投递验收仍NOT RUN |
| WM_INPUT总计数在过滤之后 | CONFIRMED BUG（诊断） | 旧计数在active/safety/deadline后；新增进程total和active-trial计数在第一处理步骤，分别统计inactive/guard/deadline丢弃 |
| WM_INPUT未走DefWindowProc清理 | DISPROVEN（正常路径）；异常路径已加强 | 正常旧版调用DefWindowProc；本轮明确调用后return0，catch路径同样清理。不会伪造HRAWINPUT/PostMessage |
| GetRawInputData返回与buffer界限 | 旧正常路径DISPROVEN；新离线PASS | RID_HEADER sizeof、RID_INPUT NULL ret0/size、copy ret字节数均校验。对齐uint64 buffer≤65536，拷贝header type/device/size二次比对，64位乘法检测count/length，所有路径RAII擦除 |
| RAWINPUT尾部padding导致合法batch拒绝 | SUSPECTED（真实输入） / OFFLINE已证旧边界 | 旧要求payload可用长度完全等于report_size×count；合成带padding会失败。新按dwSizeHid×dwCount取有效报告，剩余只计padding。历史WM_INPUT=0不可能由此解释 |
| ID0前缀/65与64假设错误 | UNKNOWN | 缓存65不是实际RAWHID长度，未知format仍停止并计length/ID；不尝试偏移猜测。若收到target batch再决定fixture，不能用更换parser解释旧零消息 |
| 设备过滤错漏正确目标 | SUSPECTED至真机正控 | VID/PID、精确MI/Col、TLC、cached caps、唯一hDevice必须一致。header非目标不fetchpayload。历史两scope各唯一匹配，未获得真实header证明来源 |
| WTS状态或前台条件恒失败 | DISPROVEN（历史完成窗口），新原生UNKNOWN | 旧最终6轮持续安全ticks且正常timeout，非恒失败；新工具仍默认本地active/unlocked/WinSta0/Default，WTS锁定/断开立即停止。OS事件与锁屏race不能由mock证明 |
| WM_TIMER饥饿可无期限处理输入 | DISPROVEN（连续active WM_INPUT路径）；调度延迟UNKNOWN | 每个active输入也校验deadline、报告预算；timer不是实时保证。缓存查询阻塞/系统调度仍可能推迟收尾，输出实际时长 |
| timer之间时钟倒退没有报告则漏检 | CONFIRMED BUG（离线边界） | 旧仅比较started/previous-report时间；新watermark包含每次tick。GetTickCount64正常为单调，非历史因果证据 |
| callback exception最终stop可仍none | CONFIRMED BUG（诊断） | catch先标记Exception/清raw/注销；loop收尾输出分类。外层未知异常不再虚报capture_started=false |
| 拔插/重连与generation复用 | UNKNOWN（真实） / OFFLINE PASS | removal或timer目标消失结束，generation前进，目标清零，无自动重连；新process新随机session。注册移除的idle间隙可能漏拔插，handle复用不能靠cached身份完全排除；不提升为验证过的连接身份 |
| 73/74无事件是固件需模式使能或功能限定 | SUSPECTED | 固件静态条件支持调查入口，字段语义/活动配置未知；本轮没有调用使能命令 |

注册契约、REMOVE条件与进程范围依据 [RegisterRawInputDevices](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerrawinputdevices)，前台与flags依据 [RAWINPUTDEVICE](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawinputdevice)。空表及容量不足规则依据 [GetRegisteredRawInputDevices](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getregisteredrawinputdevices)。窗口种类依据 [Window Features](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features)。

## 消息、读取与生命周期契约

[GetMessageW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getmessagew) 返回>0处理、0结束、-1错误；新helper仅分类返回值，离线调用它不等于测试了OS queue。[WM_INPUT](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-input) 的 lParam必须是真正HRAWINPUT；对前台输入须DefWindowProc cleanup，处理后return0。新测试不构造OS handle或发送WM_INPUT。

[GetRawInputData](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getrawinputdata) 的NULL成功返回0、copy成功返回拷贝字节数、错误UINT(-1)；header size与DWORD对齐必须正确。新InputSource仅隔离三个调用，InputDelivery在完整copy后用memcpy读取固定ABI字段。内部mock可分别返回错误/不足buffer/changed header，native_adapter的真实OS返回尚未覆盖。[RAWHID](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawhid) 按dwSizeHid×dwCount界定报告，不把RAWINPUT尾部padding解析成数据。

[WM_INPUT_DEVICE_CHANGE](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-input-device-change) 需DEVNOTIFY并携带device handle；新指标只是收到的arrival/removal数量，0通知不证明设备一直连接。[WTSRegisterSessionNotification](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsregistersessionnotification) 成功后正常销毁注销；[WTSINFOEX_LEVEL1_W](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/ns-wtsapi32-wtsinfoex_level1_w) 的当前Windows锁状态用作guard。锁定/桌面切换仍存在检测race，不承诺内核级零窗口；发现不安全立即终止。

## 与 HardwareRtGateMonitor 对照

| 项目 | 生产Monitor（只读审计、未改） | 独立研究工具 |
|---|---|---|
| Window/thread | 独立thread + HWND_MESSAGE，PeekMessage建立queue，Stop PostThreadMessage WM_QUIT + join | 可见顶层窗口，wmain thread拥有并泵消息；Owner手动Start |
| 注册 | FFC0/1，INPUTSINK\|DEVNOTIFY，后台设备Gate通知 | 一次一个vendor usage，只有DEVNOTIFY，前台限定；snapshot持续读回 |
| payload取得 | 先取得完整payload再target filter | header-only type/hDevice与session判断通过才copy目标payload |
| RT parser | ParseHardwareRtGateReport纯函数 | 直接复用同一header纯函数；不实例化Monitor，不改Gate |
| framing | count×size≤剩余buffer，取有效bytes | 本轮与其对齐接受padding；仍严格指定ID/length，不扫描payload |
| 生命周期 | daemon持续监听 | 每轮10秒，5..30配置边界，三轮或异常退出，idle60秒；不自动重连 |

未因生产Monitor使用INPUTSINK而扩大研究工具到后台捕获。新增原生诊断需明天一次有限正控验收，**不得将以上代码对照标记PHYSICAL PASS**。

## 隐私与残余限制

只处理指定TLC的目标payload；同usage其它设备可能投递header，payload不读。长期只输出booleans、API error、来源类别、fixed bins、计数及aggregate时间统计。实际路径、handle、thread身份不写盘；raw buffer≤65536和previous-report≤65短时内存，stop/异常擦除，无全文日志、网络或AI API。资源注销失败记录并退出，以进程终止作为最后边界，不声称OS注销成功。

Control未编号report的prefix、真实WM_INPUT_DEVICE_CHANGE、registration flag回显、窗口与WTS行为、RT共存和idle重插仍BLOCKED_PHYSICAL。正控失败若只有零消息，必须保留不可辨识的USB/HIDClass/Raw Input三层缺口。
