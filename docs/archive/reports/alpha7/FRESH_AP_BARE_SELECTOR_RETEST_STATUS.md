# HISTORICAL CHECKPOINT / SUPERSEDED

This alpha.7 investigation records its original device state and acceptance boundary. Later physical acceptance and package acceptance supersede its release status; historical failures and observations are retained unchanged. Current truth: [HardwareSlot closure](../../../release/HARDWARE_SLOT_RELEASE_CLOSURE.md) and [package candidate](../../../release/PACKAGE_CANDIDATE_AUDIT.md). Raw captures/private evidence remain local and are not committed.

# Fresh AP Bare Selector Retest — checkpoint

## 2026-10-03 12:46–12:48 北京时间

Owner 返回并允许继续。已准备新独立脱敏被动 capture，打开 Gear Link 官方页面，点击官方“连接”。没有编辑设置、选择槽位或执行 Aura activation。

检查浏览器原生 HID chooser 时，Computer Use 自动策略终止操作：无法充分确定当前浏览器 URL，以执行安全策略。遵照该结果停止所有 UI 操作。**没有确认 chooser 已出现或 HID 已授权，不能将此轮视为已连接设备。**

被动 capture 已正常停止并封存；没有启动 daemon、getter replay 或物理验收。未修改生产代码、板载内容或 ASUS 服务。

## 新捕获的异常

从开始连接准备前到停止，连续脱敏 capture 捕获 6 个未经本任务授权的 `51 00` OUT：

| 北京时间 | requested slot |
|---|---:|
| 12:46:53.243898 | 3 |
| 12:47:39.824670 | 6 |
| 12:47:45.744643 | 3 |
| 12:47:58.927778 | 6 |
| 12:48:07.386984 | 3 |
| 12:48:12.754077 | 6 |

本任务没有选择这些槽位，也没有启动 Aura daemon 或调用 Profile activation。仅凭 USB 无法确认发送进程；**sender UNKNOWN**。未建立 slot1 physical PRE，当前 bank 状态不适合继续物理对照。不能归因给 Edge、Gear Link、ASUS 服务或 Aura。

其它 reviewed OUT：`12 00`×13、`12 12`×5、`12 03`×4、`27 00`×4。目标 magnetic/firmware-lighting setters 与 `50 55` 在保留范围内均为 0；`27 00` 单独记录，不当成 Aura 激活请求。只证明已保留协议范围，不证明未保留流量不存在。

Capture SHA256：`abe501f1778e66d19343c402c69e560f1caee08e1b0d831c8a68ded01cd5dba2`；worker 于 UTC `2026-10-03T04:48:24.0495383Z` 退出，privacy outcome=complete。离线统计：`parsed/preparation-usb.json`。

证据：`artifacts/fresh-ap-bare-selector-retest-20261003-124647/`。状态：`audit_session_state.json`。capture 的 privacy manifest 记录实际保留范围与 SHA256；不声称 C0 81 为零。

上一轮 `INCONCLUSIVE_AUTHORITY_LOST_BEFORE_PHYSICAL_CHECK` 结论保持。本轮尚未建立 slot1 shallow PRE，也未执行 bare selector5，故不能输出 AP PASS/FAIL。

后续需要先处理本轮重复的未经授权 selector，并恢复浏览器控制/正常 HID 连接；不能只重新打开同一个 URL 后忽略这段干扰。得到可信短窗口后，才恢复已授权流程：official slot1 physical PRE → Gear Link session isolation → 当前 Aura 单次 selector5 → 紧邻的 Owner A 深触发确认。当前停止，无新增物理测试。

无 commit/push/package/release。
