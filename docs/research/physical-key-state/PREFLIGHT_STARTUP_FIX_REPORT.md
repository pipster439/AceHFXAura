# Stage B 独立观察器启动预检修复

日期：2026-10-09。仓库基线：`c52398bef2738f71b927594424736a42da5184ab`，`0.1.0-alpha.8`。

## 结论

**CONFIRMED BUG，启动元数据验证通过。物理测试 NOT RUN。**

共同阻塞条件为 `no_existing_registration()` 对 `GetRegisteredRawInputDevices` 的成功返回值解释错误。空注册表读取时，实际返回 `0`，传入的容量参数 `1` 保持为 `1`，错误码为 `0`。旧判断要求 `returned == 0 && count == 0`，因此把空表误判为存在注册。成功返回值才是写入的条目数；容量参数不能被当成保证更新的成功路径条目数。

旧 `--preflight` 只检查桌面和目标 Collection，提前返回；另外两个模式继续检查进程注册表，因此表现为预检成功但启动失败。失败发生于创建互斥量、窗口及 WTS 通知注册之前，和前台焦点、用户未点击 Start、窗口未显示没有因果关系。

该判断在上一轮离线审计中引入；本次验证针对该版本的启动回归。它不能追溯解释更早已经运行完毕的 12 次 A 键操作为何没有 WM_INPUT。修复也不能证明 RT 或 0x73/0x74 的真实投递、语义或物理身份。

## 所有 preflight_blocked 分支

任务开始时，`observer_win32.cpp` 只有一个输出 `preflight_blocked` 的位置：旧第 472–475 行的复合分支。

| 条件 | 旧模式差别 | 实测及分类 |
|---|---|---|
| `!desktop_safe()` | 三模式都检查，但旧预检不输出具体原因 | 本机未触发；本轮本地解锁会话通过 |
| `target.matches != 1` | 三模式都检查 | notifications/control 各匹配 1；未触发 |
| `!no_existing_registration()` | 旧预检跳过，审计和交互模式检查 | **CONFIRMED BUG**：返回 0、容量 1，被旧判断拒绝 |

修复后仍只有一个 `preflight_blocked` 输出位置；三模式调用同一 `startup_check()`，依据最先失败的阶段输出诊断。启动阶段包括 `session`、`desktop`、`target_collection`、`registration_table`。初始化阶段另外报告 `single_instance`、`window_class`、`window_create`、`controls_create`、`session_notifications`、`safety_timer`。非法参数报告 `blocked_stage=cli`，退出 2，且捕获尚未开始。

## 窗口、状态机与安全门槛审计

| 检查 | 结论与修复范围 |
|---|---|
| 窗口创建 | 实际是 parent=nullptr 的可见顶层窗口，不是 HWND_MESSAGE。原共同阻塞先于 CreateWindowExW；本轮两通道审计已创建自己的窗口并验证注册 HWND 绑定 |
| 前台焦点 | 在显式 Start、每次活动输入及活动定时检查时核验；没有在创建窗口之前错误要求焦点。相关 App 与消息路由代码未改动 |
| WTS 会话 | 保留当前进程属于活动本地控制台、WTSActive、协议=0、SessionFlags=UNLOCK、WinSta0、线程和输入桌面=Default 的要求。仅拆分失败原因/API 错误，并额外拒绝无效空缓冲；没有降低锁屏限制 |
| 显式 Start | WM_COMMAND 的 Start 按钮调用 App::start；唯一 observation.arm 仍在安全与注册核验全部通过之后。没有自动 Start |
| CLI 分支 | 预检只读缓存与会话/注册元数据；注册审计不启动消息循环、不 arm、不读取报文；普通模式之后才创建控件、注册 WTS、安装安全计时器、显示窗口、进入消息循环 |
| 注册前置条件 | 所有 no_existing_registration 使用点共用修复，包括启动、每轮 Start 和结束后的空表验证；已存在条目、多个条目导致的不足缓冲、API 错误仍拒绝 |
| 注册 flags 与共存 | NativeSubscription 完全未改：请求仅 DEVNOTIFY，撤销 REMOVE/NULL HWND。没有 INPUTSINK、NOLEGACY、键盘 Usage 注册，也没有更改生产 RT Gate。进程注册表不是其他进程注册表 |
| 停止与清理 | 原超时、锁屏、焦点丢失、设备断开、异常、WTS/窗口释放逻辑保留。两通道本次审计均撤销成功，撤销后返回 0、容量仍 1 |

空注册表的成功返回 `0` 现在被正确接受；`UINT(-1)` 仍携带当次 API 错误并阻塞，返回实际条目数大于零也仍阻塞。这是修正 API 契约判断，不是删除安全检查。

## 结构化诊断

预检输出包含 `blocked_stage`、`reason`、`api`、`win32_error`，以及会话布尔值、匹配数量、`registration_returned`、`registration_capacity_out`、`registration_win32_error`。窗口创建前明确输出 `window_created=false`、`capture_started=false`、`raw_registration=false`。

正常启动/只读预检的前台检查标为 `deferred_until_explicit_start`；注册审计标为 `not_applicable_metadata_only`。注册审计报告 added/exact_snapshot/removed、撤销后的返回值与容量、注册和撤销错误。原错误记录字段保持兼容，新增诊断字段便于定位。

逻辑不匹配不伪造 Windows 错误码。会话锁定、远程协议、非默认桌面、无匹配或多匹配有各自原因。设备枚举内部某些缓存能力/路径核验失败仍表现为无匹配，尚未细分每个 HID 元数据子调用；本次问题不在该路径。

## 主要改动

- `tools/research/physical-key-stage-b/observer_win32.cpp`：修正空表读取；三模式统一启动门槛；细分会话/桌面、CLI、初始化和注册审计诊断。
- `tools/research/physical-key-stage-b/startup_checks.h`：新增无 OS 调用的启动判断与结果类型，用于离线故障注入。
- `tools/research/physical-key-stage-b/offline_tests.cpp`：新增 9 个回归测试。
- `tools/research/physical-key-stage-b/README.md`：更新构建目录、58 个测试及启动诊断说明。
- 本报告及 `evidence/PREFLIGHT_STARTUP_VALIDATION.json`：长期保留脱敏验证结果。

没有修改生产 Aura、Reactive/Ripple、WH_KEYBOARD_LL、正式 RT Gate、磁轴/Profile/固件/灯效配置。任务开始的 651 个非忽略仓库文件中，仅上述 3 个既有工具文件改变，另外 648 个内容不变；新增 3 个文件。既有未提交改动不进入本次 patch。

## 验证结果

| 验证 | 实际结果 | 证据类型 |
|---|---|---|
| 独立空表 P/Invoke 读取，无注册/窗口/输入 | returned=0, capacity_out=1, error=0，旧判断 false | NATIVE METADATA PASS |
| 原版本 notifications 预检 / 注册审计 | 退出 0 / 退出 2、preflight_blocked | NATIVE METADATA：回归复现 |
| Release x64 构建 | VS 2026、MSVC 19.51、SDK 10.0.26100，/W4 /WX 通过 | BUILD PASS |
| CTest | 1/1 通过；58 项离线场景，49 既有 + 9 新增 | OFFLINE TEST PASS |
| 修复后 notifications 预检 / 注册审计 | 均退出 0；added/exact_snapshot/removed=true | NATIVE METADATA PASS |
| 修复后 control 预检 / 注册审计 | 均退出 0；added/exact_snapshot/removed=true | NATIVE METADATA PASS |
| 非兼容 CLI 模式、control+RTGATE、timeout=4 | 各退出 2、blocked_stage=cli、capture_started=false | CLI PASS |
| App/路由/目标绑定/NativeSubscription | 与任务起点对应代码片段完全相同；源码及 PE 直接调用/导入检查无禁止 API | STATIC PASS，非一般化安全证明 |
| RT 实体操作、真实 WM_INPUT、锁屏/拔插/前台运行验收 | 本轮没有执行 | NOT RUN |

9 个回归场景覆盖空表返回0/容量1、空表容量0、已有注册、缓冲不足、其他API失败及错误码、统一启动就绪/阻塞、锁定/远程/未知会话结果不得绕过、桌面API错误优先、缺失/多Collection诊断。会话故障注入测试消费合成结果，不证明 Windows 实际 WTS 通知或锁屏交互。

所有本次注册审计均无消息循环、无 arm、`payloads_read=0`、`capture_started=false`。没有模拟输入，没有原始报文、文本或按键历史持久化/上传。

## 修复后的复现与验收命令

从 `G:\Aura` 执行；新产物不覆盖旧观察器。

```powershell
cmake -S tools/research/physical-key-stage-b -B audit_artifacts/physical-key-preflight-20261009/build -G "Visual Studio 18 2026" -A x64
cmake --build audit_artifacts/physical-key-preflight-20261009/build --config Release
ctest --test-dir audit_artifacts/physical-key-preflight-20261009/build -C Release -V
$observer = 'G:\Aura\audit_artifacts\physical-key-preflight-20261009\build\Release\stage_b_observer.exe'
& $observer --preflight --collection notifications
& $observer --registration-audit --collection notifications
& $observer --preflight --collection control
& $observer --registration-audit --collection control
```

预计就绪阶段为 `blocked_stage=none, reason=ready`，退出0；空表返回0、容量1是正常结果。审计应 added/exact_snapshot/removed=true、payloads_read=0。若非就绪，依据具体阶段停止，不绕过锁屏或转为后台捕获。

**以下只供 Owner 在专用安全本地测试会话人工运行，本轮没有执行：**

```powershell
& $observer --collection notifications --case RTGATE --timeout 10
```

窗口出现本身不开始监听；每轮仍须鼠标点击 Start，保持观察器前台，再按既有 `NEXT_DAY_RT_TEST.md` 进行实体 RT 开关操作。本次只建议恢复该正向对照，不宣称普通交互模式或物理 RT 已验收。

## 剩余问题与产物

真实 WM_INPUT 投递、RT ON/OFF、设备变更、锁屏与焦点丢失运行时验收仍待实测。0x73/0x74 的事件来源、字段与物理身份继续 UNKNOWN/INCONCLUSIVE；不能进入生产 PhysicalKeyStateProvider 迁移。

日志和结果在 `audit_artifacts/physical-key-preflight-20261009/`；独立 patch、证据包和外部 SHA-256 位于其 `output/`。证据包从独立 staging 目录构建，仅包含本报告、脱敏日志、验证 JSON、manifest、哈希和小型复现脚本；不含程序二进制、旧包、完整源码、baseline 快照或按键数据。patch 以本次起点为基础，隔离验证应用后的六个文件哈希。

## 官方 API 依据

微软文档定义成功返回值为写入缓冲的 RAWINPUTDEVICE 数量；缓冲不足/NULL 时返回 -1，并报告所需容量及错误。本机成功空表不改容量的行为已通过独立读取复现。[GetRegisteredRawInputDevices](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getregisteredrawinputdevices)

注册按进程管理，REMOVE 需 NULL 目标窗口；因此没有将其他进程 RT Gate 的注册混入本工具的空表检查。[RegisterRawInputDevices](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerrawinputdevices)
