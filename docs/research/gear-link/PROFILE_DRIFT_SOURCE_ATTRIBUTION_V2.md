# PROFILE_DRIFT_SOURCE_ATTRIBUTION_V2

日期：2026-10-03（北京时间，UTC+08）。UTC时间线保留原格式。

## Decision

**DRIFT NOT REPRODUCED WITH OWNED WRITERS ABSENT**。独立窗口20次exact official BasicInfo采样覆盖189.999秒；实际bank与UI均5，间隔9.985–10.014秒。整份观察capture只有12 00，51 00 / setter / Apply为0。没有磁轴/lighting mutation、getter replay、power test或W状态恢复。

**SOURCE UNKNOWN / PROCESS ATTRIBUTION NOT CONFIRMED**。启动与终点均未发现AuraOwnershipExperiment、Aura/aura_daemon或owned hardware smoke writer；实际停止数0。本次不能形成“停止某writer后drift消失”的A/B因果链，也不能将AuraOwnershipExperiment强相关。按owner“只有source隔离才DRIFT_ISOLATED”的规则，最终phase=STOP_SOURCE_UNKNOWN，不伪装source已经隔离。

这段稳定窗口满足owner要求的bounded clean-bank观察条件；如果owner接受它作为后续实验门槛，未来可以在即时pre/post BasicInfo与live drift guard下另建slot5测试基线。当前不存在无限期安全保证，且本轮没有自动继续。现有污染W0.1硬件参数/受影响bank仍UNKNOWN。

## Owned writer inventory / shutdown

以Win32_Process只读快照检查process name、executable path、start time、owned_by_project及可能写HFX原因；提升后的清单保存在candidate_processes.json。未保存commandline/PID。文件路径是owner明确要求的本地归因材料，不上传第三方。

| Candidate process name | Count | Classification |
|---|---:|---|
| ASUSGPUFanService.exe | 1 | ASUS background candidate |
| ArmouryCrate.Service.exe | 1 | ASUS background candidate |
| ArmouryCrate.UserSessionHelper.exe | 1 | ASUS background candidate |
| ArmouryHtmlDebugServer.exe | 1 | ASUS background candidate |
| ArmourySocketServer.exe | 1 | ASUS background candidate |
| ArmourySwAgent.exe | 1 | ASUS background candidate |
| AsusCertService.exe | 1 | ASUS background candidate |
| AsusFanControlService.exe | 1 | ASUS background candidate |
| AsusUpdateCheck.exe | 1 | ASUS background candidate |
| GearLink_KBAgent.exe | 1 | Gear Link native candidate |
| GearLink_KBProcess.exe | 1 | Gear Link native candidate |
| GearLink_KBService.exe | 1 | Gear Link native candidate |
| GearLink_MacroServer.exe | 1 | Gear Link native candidate |
| GearLink_PowerNotification.exe | 1 | Gear Link native candidate |
| GearLink_UtilityCompanion.exe | 1 | Gear Link native candidate |
| LightingService.exe | 1 | ASUS background candidate |
| asus_framework.exe | 9 | ASUS background candidate |
| chrome.exe | 11 | Browser candidate |
| msedge.exe | 15 | Browser candidate |
| msedgewebview2.exe | 6 | Browser candidate |

当前项目writer：AuraOwnershipExperiment=0；Aura/aura_daemon=0；AuraWorker/AceHFXService=0；可识别project hardware smoke=0。没有调用kill或shutdown端点，stopped_owned_writers.json的stopped=[]。先期非提升清单中的pwsh是审计自身，不是writer；终点搜索也排除当前inventory process，避免commandline匹配自己的搜索文字。ASUS/Windows服务和Gear Link保持运行。

没有进行AuraOwnershipExperiment受控启动：它不在现场运行，不能称主要被隔离候选；重启会额外引入hardware ownership mutation，当前并无支持该条件的before/after证据。没有修改其代码。

## Authority and capture separation

初始页面已连接HFX/UI5，但official BasicInfo为1。没有reload或HID chooser；直接通过官方配置文件菜单明确选择5，UI5/BasicInfo5成立。选择操作独立capture，不混入idle窗口。

授权selector frame7，UTC 2026-10-02T16:17:01.351661Z，vendor64字节 `51 00 00 00 05 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00`。选择窗口含一笔51 31已知初始化companion（官方polling setting），不是agent编辑磁轴/灯光，也不被并入readonly idle窗口。没有其它bank selector。

第二capture名称按owner要求为drift_after_owned_writer_shutdown.pcap，但**没有实际shutdown**；名称不代表因果证据。开始后的准备/预观察片段也保留，前段跨tool调用造成采样jitter，未用来满足10秒cadence验收。最后重新完成如下连续窗口：

| Sample | SDK acknowledgement UTC | Matching retained IN frame | active_slot |
|---|---|---:|---:|
| 1 | 2026-10-02T16:25:34.215Z | 162 | 5 |
| 2 | 2026-10-02T16:25:44.203Z | 170 | 5 |
| 3 | 2026-10-02T16:25:54.206Z | 178 | 5 |
| 4 | 2026-10-02T16:26:04.215Z | 180 | 5 |
| 5 | 2026-10-02T16:26:14.206Z | 188 | 5 |
| 6 | 2026-10-02T16:26:24.217Z | 196 | 5 |
| 7 | 2026-10-02T16:26:34.215Z | 204 | 5 |
| 8 | 2026-10-02T16:26:44.204Z | 210 | 5 |
| 9 | 2026-10-02T16:26:54.218Z | 218 | 5 |
| 10 | 2026-10-02T16:27:04.210Z | 226 | 5 |
| 11 | 2026-10-02T16:27:14.219Z | 234 | 5 |
| 12 | 2026-10-02T16:27:24.204Z | 242 | 5 |
| 13 | 2026-10-02T16:27:34.217Z | 250 | 5 |
| 14 | 2026-10-02T16:27:44.217Z | 258 | 5 |
| 15 | 2026-10-02T16:27:54.204Z | 262 | 5 |
| 16 | 2026-10-02T16:28:04.215Z | 270 | 5 |
| 17 | 2026-10-02T16:28:14.208Z | 278 | 5 |
| 18 | 2026-10-02T16:28:24.218Z | 286 | 5 |
| 19 | 2026-10-02T16:28:34.210Z | 288 | 5 |
| 20 | 2026-10-02T16:28:44.214Z | 296 | 5 |

每次只调用官方currentDevice.getDeviceInfo(0)，使用当前官方builder/send queue，没有raw HID sender。请求payload `12 00`+62零，reportId0/vendor64；IN decoder只有BasicInfo已确认offset10 active slot。source/mapping、capture parser沿用已审计工具，不调用DKS/RT/AP/DZ参数getter。

全部观察capture中12 00 OUT=150、completed BasicInfo IN=150，IN全部active5。SDK显式20次和官方页面/SDK其它BasicInfo活动分开统计，不把每笔OUT说成人工调用。20个SDK返回与最近真实completed IN完整64bytes相同，时间差均小于10ms；详见state/window-a-validation.json。

UTC 2026-10-02T16:25:34.215Z 至 2026-10-02T16:28:44.214Z；北京时间分别为2026-10-03T00:25:34.215000+08:00、2026-10-03T00:28:44.214000+08:00。未reload、切页面、改setting或切Profile。UI末端截图stable-slot5-final.jpg。UI仍显示旧host W0.1；它不是实际硬件参数确认。

## Selector timeline / process attribution

本轮idle窗口51 00=0，故没有新的5→3→1时刻可与WriteFile匹配。之前异常仍未解释，corrected Window D历史PASS不被改写；不宣称reload correlated或Gear Link sender confirmed。

Procmon/Handle未在PATH、已检查local tool directories找到。未下载/安装工具、接受EULA或修改服务/registry。没有新的Procmon WriteFile trace或ETW写事件。既有Kernel-File realtime observer先前不能有效覆盖HID WriteFile，本轮未拿handle名字充当ETW/process sender证据。tool-availability.json区分“未取得”与“确定不支持”；本轮不宣称Procmon本身不支持HID。

因A窗口未发生drift，没有执行conditional escalation或B（关闭Gear Link180s）、C（其它候选隔离）窗口，也没有强行启动新的parallel writer制造对照。之前历史B的背景selector不能归入本轮A。

## Read-only handle ownership snapshot

使用本地native只读name probe：SetupDi/QueryDosDevice查当前HFX接口别名；NtQuerySystemInformation获取handle metadata；OpenProcess仅DUP_HANDLE，DuplicateHandle后NtQueryObject读取name；只关闭observer的duplicate，不关闭remote handle，不CreateFile目标HID，不ReadFile/WriteFile/DeviceIoControl。NUL handle仅用于发现File object type。

每个process probe最多2s，总budget90s；timeout仅结束本工具自己的probe child，不停止候选程序。共尝试287个process，38个timeout，11个不可访问；枚举不完整，缺席不能排除进程。输出只留HFX匹配owner和相关candidate coverage，未保存其它handle名/文件内容/PID/serial。

| Process | HFX handle interface (instance redacted) | Category |
|---|---|---|
| dwm.exe | `HID#VID_0B05&PID_1B7E&MI_04#[instance-redacted]` | Other Windows handle owner |
| svchost.exe | `HID#VID_0B05&PID_1B7E&MI_02&COL01#[instance-redacted]` | Other Windows handle owner |
| LightingService.exe | `HID#VID_0B05&PID_1B7E&MI_01#[instance-redacted]` | ASUS service candidate |
| LightingService.exe | `HID#VID_0B05&PID_1B7E&MI_02&COL03#[instance-redacted]` | ASUS service candidate |
| asus_framework.exe | `HID#VID_0B05&PID_1B7E&MI_01#[instance-redacted]` | ASUS service candidate |
| asus_framework.exe | `HID#VID_0B05&PID_1B7E&MI_02&COL03#[instance-redacted]` | ASUS service candidate |
| GearLink_KBProcess.exe | `HID#VID_0B05&PID_1B7E&MI_02&COL03#[instance-redacted]` | Gear Link native candidate |

LightingService/asus_framework有MI_01 handle，是后续归因候选；GearLink_KBProcess只有已观察状态接口handle。本次未发现browser匹配不代表它无WebHID权利，因为枚举partial且native bridge/driver间接持有路径存在。**Handle owner != 51 00 sender**。没有停止任何这些进程或ASUS服务。

## W0.1 and getters remain unchanged conclusions

不尝试修复上轮被污染写入：hostcache意图slot5/W0.1；实际numeric OUT前已有bank1；activebank/editbank区别未确认，所以可能影响其它bank，当前full hardware state UNKNOWN。官方选5只是bank selection，不证明或重写W/AP/DZ/DKS/lighting参数。

DKS25 02、AP25 05/04、DZ25 0A/09此前各4 OUT/0 matching IN保持；SDK771/3/3/source-only全部不可readback。RT25 06/A6未执行且HFX分支未解决。本轮getter replay0、persistence0、magnetic/lighting editor mutation0。没有Aura production51 00 writer或Profile Engine修改。

## Capture paths / evidence

| Acquisition-original privacy-filtered pcap | SHA-256 |
|---|---|
| `G:\Aura\audit_artifacts\profile-drift-v2\usb\authorized_slot5_selection.pcap` | `8e0bad113989336900afcba59ad57c7f8d83ffb11523a570afba0955b27407c0` |
| `G:\Aura\audit_artifacts\profile-drift-v2\usb\drift_after_owned_writer_shutdown.pcap` | `44c5f61affb575c0ab6819d3d29f2ccb2f1d6b880416c537cffa5378285f8740` |

pcap是privacy-filtered original retained acquisition，不是完整bus dump；丢弃serial/typing/其它设备/unreviewed类别。absent命令结论只适用reviewedfamily。保留完整64byte request/response、frame、UTC、endpoint、direction、length、echo、delta timing、CSV、payloadsequence。candidate_processes.json包含owner请求的路径/time；deviceinstance与PID仍脱敏。私有probe-input含内部PID只供运行，不进入zip。

## Validation and cleanup

Fresh53 research fixture/parser tests PASS，见logs/research-tests.log（0.076s）；实际观察window byte/time/authority assertions全部PASS。git diff --check与scoped.patch apply-check结果见validation-summary.json/logs。没有full productCI需求或physical/hardware settingsPASS声明。本轮只新增/追加research docs，artifacthelpers不修改production。

finish完成capture flush/manifest/hash后USBPcap已停止。随后shutdown command以非原子方式写入，worker读取遇到file-sharing race，terminalstate=failed；没有伪造exited。cleanup.json确认owned worker/live guard/USBPcapremaining0，capture仍complete。这是observer shutdown记录错误，不影响之前完整窗口；本轮未修改旧worker实现。

scoped.patch以本轮保存的research-document基线为准，不包含其它dirty产品代码。无commit/push/package/release。STOP_SOURCE_UNKNOWN，等待owner review；不自动恢复硬件测试。
