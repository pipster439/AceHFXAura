# alpha.7 — resetType 1 官方 USB 捕获验证

审计日期：2026-10-01（Asia/Shanghai）；抓包 UTC：2026-09-30。本轮基线 HEAD：`01786b1bdc368d6b9fc625cfbcf2eda8c8ac79f4`。

## 1. 结论与评审门槛

**resetType 1 official USB sequence: PHYSICAL CAPTURE PASS**

上述 PASS 限于 **Armoury Crate 在真实 Ace HFX 上发送的 type 1 USB sequence**：已捕获实际 `51 52 01 00 0A 00`、全零 reserved bytes、完整相同 echo、后续 `50 55` 及相同 echo。它与此前官方 frontend / SDK / HAL 的静态布局相符。

**没有放行生产写入。** 未新增 builder、runtime API、NativeHid allowlist；未改 Profile planner、reconnect/quarantine、已验证 Deadzone resetType 4、固件或任何产品源码。没有 packet replay、自定义 HID WriteFile、`51 2C` 或 quarantine acknowledge。请先评审这份证据，再决定后续 typed production implementation。

实机版本由用户查看 Armoury Crate 确认为 **1.00.59**；捕获 device descriptor 的 `bcdDevice=0x0159` 与之对应。此前反汇编的是 **1.00.58**，不能将此捕获说成物理验证了 1.00.58 handler，也不证明两版所有内部字段和副作用完全一致。

## 2. 原始文件与设备身份

| 项目 | 实际结果 |
| --- | --- |
| 完整原始 capture | `F:\USBPcap\reset_type1.pcap`，未替换成过滤版 |
| SHA-256 | `8ab5e8e6bafeae679693d608072c6ff5852be6c8c3e84e60144fdcc3c8d7b153` |
| 文件大小 | 20,863,343 bytes |
| 容器 | classic pcap 2.4，little-endian，LINKTYPE_USBPCAP=249，snaplen=65535 |
| 原始记录 | 384,006；全部 record header/payload 完整；snapshot 截断 0 |
| 捕获区间（UTC） | `2026-09-30T15:59:17.284985Z` → `2026-09-30T16:03:27.520213Z`，250.235228 s |
| 控制器 / USB 地址 | `\\.\USBPcap4` / bus 4 address 7；按捕获 descriptor 识别，不靠候选 reset 字节筛选 |
| 设备 | VID `0B05` / PID `1B7E`，ROG FALCHION ACE HFX |
| 目标接口 | interface 1（MI_01），HID class 3；OUT `0x0D` / IN `0x85` |
| 端点 | max packet 64 bytes；OUT interval 4，IN interval 1（descriptor 原值） |
| HID descriptor | 声明 report descriptor 长度 34 bytes；report descriptor **正文没有捕获** |
| 目标设备记录 / 命令接口记录 | 766 / 24；有 payload 的 MI_01 记录 12 |
| USBD status | 目标设备捕获记录均为 0；echo 不是 firmware configuration readback |

`--inject-descriptors` 生成的 frame 19–24 是描述符注入记录（IRP ID 0）。包括 frame 23 的 SET_CONFIGURATION：它是采集工具注入的元数据，**不是 Reset 前的真实 Profile/layer select**。设备/config descriptor 用于识别与 framing。没有把它计入官方实际六次 OUT。

pcap 不携带本次 kernel-drop 统计；“record 完整”不代表对整个控制器宣称无丢包。完整三组匹配 echo/gate 与全部目标 OUT 清单是本次 sequence 判断依据。原始文件包含该 root hub 其它设备流量；派生分析只展示目标键盘。

## 3. USBPcapCMD 状态诊断与停止

最初两次检查间隔 3 秒：当时 USBPcapCMD 实例为 **0**，目标文件不存在。不能据此反推用户更早看到的 PID 状态。USBPcap driver 是 Running，设备 PnP 正常，无需重启；没有无效实例需要结束。

安装工具 `F:\USBPcap\USBPcapCMD.exe` 为 **1.5.4.0**，SHA-256 `8b13173e9453fda9914d707c732880de44081e20e1113b3a2c827abd7a28b15d`。实际 `--help` 确认 `-d`、`-A`、`--inject-descriptors`、`-o` 均支持；默认 snaplen 65535、buffer 1 MiB，buffer 范围 4096–134217728。extcap device tree 确认 USBPcap4→Port 8 hub→Port 2 / address 7 Ace HFX。

PE subsystem 为 Windows GUI（2），因此 shell 立即返回不证明 capture 已停止。官方 1.5.4.0 源码支持非管理员提权 worker；不是所有运行都必定 detach。直接参数模式会 attach parent console 并处理 stdin handle；退出逻辑等待小写 q 的 ConsoleInput。交互模式另开 console 并连接 CONIN$。这些与本次 q 无效相容，但**未检查运行中的 stdin handle，不能确认 q 失效的具体底层原因**。来源：[官方 cmd.c（1.5.4.0）](https://github.com/desowin/usbpcap/blob/1.5.4.0/USBPcapCMD/cmd.c)。

本次受控启动使用管理员捕获控制台，只启动一个 USBPcapCMD（PID 17452、parent 25588），参数保持 `-d \\.\USBPcap4 -A --inject-descriptors -o F:\USBPcap\reset_type1.pcap`。启动前确认 Aura/aura_daemon 退出、无竞争捕获进程；目标文件原本不存在，无需备份。helper 对已有文件使用时间戳 Move-Item 备份；官方输出本身使用 CREATE_NEW，不覆盖已有文件。

确认文件 **944,292 → 1,064,421 bytes / 3 秒** 持续增长后，才提示用户做官方操作。用户报告已完成 Global 1.0 / M 4.0 / 官方单键触发点弹窗 Reset；小写 q 无反应。随后用户在同一 console 按 Ctrl+C，返回 PowerShell 提示符。此后 USBPcapCMD 为 0，pcap 关闭可读、大小/hash 稳定且尾部完整。

这是 **Ctrl+C 停止成功且文件完整**，不是声称 q 停止已经修好；helper 因中断未产出 capture_stopped.json，也没有伪造 exit code。未强杀其它进程、未删文件、未重启或安装工具。捕获控制台在提示符上可以由用户关闭。下次可优先使用工具自身交互模式独立 console，并先确认目标接口；本轮不为验证停止方式再启动捕获。

## 4. 完整官方写入顺序与 timing

全窗口的非注入目标 OUT **恰好 6 次**：`51 50` ×1、`51 4F` ×1、`51 52` ×1、`50 55` ×3。以下为全部 12 个带 payload 的 OUT/IN；每个 payload 长度均为 **64**，status 均为 0。时间是 UTC 2026-09-30（本地 +08:00 已跨到 2026-10-01）。prefix 只供表格阅读；完整每包 hex 见证据目录 `payload_sequence.txt`。

| 原始 frame | UTC 时间 | 方向 | 前 8 bytes |
| --- | --- | --- | --- |
| 86429 | 16:00:28.673471 | OUT 0x0D | `51 50 00 00 0a 00 00 00` |
| 86430 | 16:00:28.674360 | IN 0x85 | `51 50 00 00 0a 00 00 00` |
| 86435 | 16:00:28.678788 | OUT 0x0D | `50 55 00 00 00 00 00 00` |
| 86477 | 16:00:28.783359 | IN 0x85 | `50 55 00 00 00 00 00 00` |
| 99207 | 16:00:35.177530 | OUT 0x0D | `51 4f 00 00 34 00 28 00` |
| 99210 | 16:00:35.178782 | IN 0x85 | `51 4f 00 00 34 00 28 00` |
| 99213 | 16:00:35.182826 | OUT 0x0D | `50 55 00 00 00 00 00 00` |
| 100394 | 16:00:35.401913 | IN 0x85 | `50 55 00 00 00 00 00 00` |
| 116136 | 16:00:41.466832 | OUT 0x0D | `51 52 01 00 0a 00 00 00` |
| 116137 | 16:00:41.467454 | IN 0x85 | `51 52 01 00 0a 00 00 00` |
| 116140 | 16:00:41.472703 | OUT 0x0D | `50 55 00 00 00 00 00 00` |
| 116182 | 16:00:41.577456 | IN 0x85 | `50 55 00 00 00 00 00 00` |

序列对应：

1. `51 50 ... 0A`：global/common Actuation 1.0 mm → echo → Apply → echo。
2. `51 4F ... 34 00 28`：M wire key ID `0x0034`，raw 40，即 4.0 mm → echo → Apply → echo。现有 audited mapping 为 logical `0x0405`→wire `0x0034`。
3. `51 52 01 00 0A 00`：type 1、common raw 10、layer 0 → echo → Apply → echo。

官方 UI 入口与此前静态审计一致：“单键触发点设置：编辑”→弹窗“重置”，不是“取消全选”或 overall reset。用户没有再次点击 Reset。M stage 到 reset stage 为 6.289302 s。

所有空 payload 的 OUT completion / IN submission 也保留在 `capture_analysis.json` 和 `control_sequence.csv` 的 **24 条**完整 MI_01 记录里；原始文件并未过滤。已有 pending IN 的完成时间不必晚于本窗口里的下一笔 IN submission，不能据此伪造 API 调用顺序。

| 操作 | Stage→echo ms | Stage→Apply OUT ms | Apply OUT→echo ms | Stage→最终 echo ms |
| --- | ---: | ---: | ---: | ---: |
| Global Actuation 1.0 mm | 0.889 | 5.317 | 104.571 | 109.888 |
| M Actuation 4.0 mm | 1.252 | 5.296 | 219.087 | 224.383 |
| Actuation resetType 1 | 0.622 | 5.871 | 104.753 | 110.624 |

以上是 **USB 捕获的主机 URB 时间戳间隔**，不是固件内部耗时、HID API 总耗时或安全 settle 的最小要求。不据此缩短 Aura timing，也未运行 Aura apply。

## 5. resetType 1 的 exact packet 与 framing

### 实际捕获：64-byte USB payload

frame 116136（OUT）与 frame 116137（IN）逐 byte 相同：

```text
51 52 01 00 0a 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

### 与静态 host transport 对齐：65-byte Windows buffer

下列是 **`00` dummy ReportID + 64-byte 实测 USB payload 的归一化重建**。此前 HAL transport 静态代码构造 65-byte WriteFile buffer；USBPcap 本次直接看到的是上面的 64 bytes，未捕获 WriteFile 的用户态 buffer，未取得 HID report descriptor 正文。不能称下列 65 bytes 全部直接被 USBPcap 捕获。

```text
00 51 52 01 00 0a 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00
```

| USB offset（直接捕获） | Windows host offset（静态对齐） | 值 | 含义 / 证据 |
| --- | --- | --- | --- |
| 无 | 0 | `00` | dummy ReportID，来自此前 HAL/既有 transport 证据；不在本次 USB payload 中 |
| 0 | 1 | `51` | vendor family |
| 1 | 2 | `52` | reset subcommand |
| 2 | 3 | `01` | resetType low byte：1 |
| 3 | 4 | `00` | resetType high byte：0 |
| 4 | 5 | `0A` | common Actuation raw=10；与官方 Global 1.0 mm 操作一致 |
| 5 | 6 | `00` | layer 0；与 frontend/HAL static contract 一致 |
| 6–63，每个 byte | 7–64，每个 byte | 全部 `00`（58 bytes） | reserved/zero initialization，与 HAL 一致；没有添加 guessed 参数 |

完整 Apply payload 为 `50 55` + **62 个 `00`**。frame 116140→116182 两包逐 byte 相同。reset stage 后 **没有额外 `51 50`**，官方 type 1 本身携带 common raw。

## 6. 静态链、物理行为及剩余证据边界

此前审计：[M605_RESET_TYPE1_AUDIT.md](M605_RESET_TYPE1_AUDIT.md)。官方 `7038/index.js`→bundle 的 resetType literal 1→SDK selector `0xCE` / FunctionID `0x2B`→HAL `FUN_180022100` 的 packet 布局，现已获得对应官方实发 USB payload 与 Apply sequence。前端/bundle、SDK、归档 HAL、1.00.58 固件和中文资源共 **7 项 hash 重新计算均与此前审计一致**，见证据 `static_input_recheck.json`。

没有 attach 官方进程、录制跨进程 SDK→HAL 调用栈或确认本次加载 HAL module 的具体实例。归档 HAL hash 匹配静态输入，不能据此称已实测官方加载的模块 hash。协议布局闭环与模块调用栈验证是不同证据。

| 判断 | 当前证据等级 |
| --- | --- |
| 官方 resetType、raw、layer、reserved、echo、Apply gate | **PHYSICAL CAPTURE PASS，1.00.59，本次真实 USB** |
| M=4.0→官方 Reset 后随 global1.0；之后 global1.5 时 M 跟随1.5 | **用户此前已报告真机行为通过**；本次捕获只含1.0/4.0/Reset，不含1.5那次测试 |
| 清所选 layer 全部 Actuation override bit15、恢复 common inheritance | **1.00.58 静态确认** + 上述 M 的用户物理行为证据；不是1.00.59固件逐字段 readback |
| type1 不进入 RT/DZ/DKS/SpeedTap/remap 配置重置分支 | **1.00.58 静态支持**；本次无相应额外命令，未做这些配置的物理前后保留测试 |
| 跨 layer / 全部板载 Profile slots / power-cycle persistence | **未验证**；官方仅发layer0，没有Aura GUID或bank参数 |
| 65-byte用户态 WriteFile buffer / HID report descriptor正文 | **此前静态与transport归一化证据**；本次USB只直接捕获64-byte payload |

本次全窗口 **没有观察到**额外 `51 53/54` RT、`51 58/59` Deadzone、DKS、SpeedTap、remap 或固件 Profile/layer select 写入，也没有 resetType0/4。这个结论只覆盖本次记录；单一 reset command 内部是否保留这些状态仍不能靠“没有额外包”证明。反馈/灯光缓存与通用待处理标志的静态副作用仍保留此前报告的限制。

## 7. 验证、归档与下一步

- 离线 decoder 按官方 [USBPcap 1.5.4.0 packed header](https://github.com/desowin/usbpcap/blob/1.5.4.0/USBPcapDriver/include/USBPcap.h) 校验 headerLen、dataLength、transfer、endpoint、info、control stage；先按 descriptor 识别目标，枚举全部目标 OUT，再解读reset。没有用静态候选“匹配出答案”。
- 文件尾部、record lengths、timestamps、snapshot truncation、目标status、完整stage/echo/gate以及派生pcap原始record逐byte一致性均验证。
- **16 项 parser / evidence unit tests PASS**：大小端与时间分辨率、损坏/截断拒绝、USB字段与descriptor、全部目标OUT inventory、exact reset/echo、三组sequence、派生记录不改写。
- 337 个开始时已有的非ignored工作树文件hash保持不变；本轮只增加此文档与隔离的capture/分析/报告材料。已有alpha.7未提交代码完整保留。
- 本轮未运行 CTest/WinUI/Aura.Tests/frontend 全套回归：产品源码没有修改，也没有将以前结果冒充本次执行结果。`git diff --check` 和仅文档patch校验另行归档。

证据目录：`G:\Aura\audit_artifacts\alpha7_reset_type1_usb`。提供完整原始pcap（仍保留在F盘）、30条目标descriptor/MI_01记录的派生pcap、12条完整hex、24条时序CSV、解析JSON、timing、16项测试日志、工具help/device tree及source provenance。evidence ZIP 同时保存原始pcap；其中原始文件 hash 与F盘原件一致。

**下一步等待用户审核，不能自动进入生产实现。** 如授权后做 typed `ResetAllPerKeyActuationOverrides`，仍需限定已经确认的 layer0、可信common raw、现有stage/Apply/quarantine安全框架，并评估1.00.59与1.00.58静态版本差异。随后需要独立验证 RT/DZ/DKS等配置保留、多个键、Profile A→B inherited、partial failure 和现有安全regression。其它 layer、板载bank与断电行为另列实机验收。
