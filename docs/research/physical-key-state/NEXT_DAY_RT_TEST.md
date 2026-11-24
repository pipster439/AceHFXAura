# Next Day RT Positive Control — Owner操作流程

本报告日期2026-10-09；计划在Owner明日恢复本地交互会话后执行。**本轮NOT RUN / BLOCKED_PHYSICAL**。本文提供操作流程，不是执行日志；今晚没有启动观察器、注册自检、桌面测试或RT拨动。

## 最小目标与操作数量

仅验证已知RT通知的被动投递：VID0B05/PID1B7E，MI_02 Col03，FFC0/1，ID03，21bytes，`03 76 00 00 state`后16个零字节，state0=OFF/1=ON。历史endpoint为8C，但Raw Input不返回USB endpoint，不能把本次TLC匹配当成总线端点实测。

**6次实体RT拨动，3个独立轮次，每轮拨到另一位置后拨回原位置。** 每轮涵盖ON与OFF，返回初始状态，最终恢复原位置。额外3次Start鼠标点击不算RT操作。只有2次拨动可做一次ON/OFF烟测，但不满足三次重复。无正控报告时不追加A/Copilot等输入，不改变flags/Collection来扩大实验。

## 明天执行前

1. Owner明确进入专用测试会话，关闭敏感输入上下文；使用本地active/unlocked console，保持观察器前台。不要用远程锁屏或自动化代替Owner。
2. 记录物理RT开关当前位置，观察键盘上的ON/OFF标识；只保留状态/完成检查，不保存文本或设备序列号。
3. 使用本轮build的exe，并核对证据包`manifests/validation.json`中的SHA。旧exe没有本轮acquisition/RT诊断；不要混用。
4. 不启动Aura UI/daemon或ASUS控制软件以制造事件，不停止它们，不修改软件或键盘设置。若原已运行，记录是否存在即可；出现设备共存风险立即停止。这三轮不宣称完成双进程RT共存专项验收。
5. 可由Owner明天运行以下预检；只读缓存，不注册输入：

```powershell
Set-Location G:\Aura
$observer = '.\audit_artifacts\physical-key-overnight-20261009\build\Release\stage_b_observer.exe'
& $observer --preflight --collection notifications
```

需要`local_unlocked_console=true`、`matching_collections=1`。失败留NOT RUN，停止；不提权、接管端点或绕过guard。注册audit不必额外执行，实际trial内已有注册与持续snapshot核验。

## 可复制启动与每轮动作

```powershell
# 仅Owner在明天专用本地会话内执行；不是本轮自动执行命令。
& $observer --collection notifications --case RTGATE --timeout 10
```

直接前台运行即可；如用Start-Process，显式`-WindowStyle Normal`确保交互窗口可见。`--case RTGATE`对control拒绝；不用control冒充已知RT route。

每轮执行同样步骤：

1. **START信号：** 鼠标点击Start trial，确认窗口显示`TEST ACTIVE`和剩余时间。未显示或提前停止时不拨动，该轮NOT RUN/INCONCLUSIVE。
2. 开始后约1秒，物理拨动RT到另一位置；保持至少约2秒，查看窗口RT state显示是否与实体位置一致。
3. 再物理拨回本轮初始位置；保持至少约2秒，查看状态。不要按键、输入文本或切应用。
4. **END信号：** 10秒到期显示Trial ended；第三轮自动关闭。确认实体原位置已恢复。若Owner没完成两次动作，该轮不能记物理FAIL。
5. 再手动Start下一轮，累计三轮；只保留程序脱敏summary与Owner检查结果。不要粘贴/上传原始report，程序也不提供raw export。

Owner记录模板（可保留空表直到实测）：

| Trial | 初始位置 | 两次拨动完成 | ON/OFF显示均正确 | final位置=初始 | stop/cleanup | 判定 |
|---|---|---|---|---|---|---|
| 1 | 待填 | 待填 | 待填 | 待填 | 待填 | NOT RUN |
| 2 | 待填 | 待填 | 待填 | 待填 | 待填 | NOT RUN |
| 3 | 待填 | 待填 | 待填 | 待填 | 待填 | NOT RUN |

## 计数与PASS标准

每轮注册/window/thread核验true，持续registration_checks/safe_ticks>0，end_target_verified/removal/cleanup=true，stop=trial_timeout，active WM_INPUT>0、target_headers>0、payload_batches>0。有效RT应包含ON和OFF各至少1，invalidRT/length/ID/read_failures=0；理想没有额外初始快照时是2个报告、rt_changes=1、rt_repeats=0。首次未知→状态只建立状态，不计change。

Report ID histogram中3与length batch bin21需出现；一个WM_INPUT可以携带多report，所以messages、batches、reports不要求相等。`rt_current`在结束为STALE是正确清理，`rt_last_valid`只表示本轮最后有效switch状态，需与Owner恢复后的原位置一致；不能再当新鲜Gate状态。活动窗口的实时显示由纯RT解析更新，不是基于switch操作时间猜测。

若出现重复，保留rt_repeats/duplicates/counts，不静默丢弃；若少一种状态或final不匹配，标INCONCLUSIVE或该目标PHYSICAL FAIL并说明。只有聚合统计，没有每个物理动作序列与firmware sequence，不能精确定位漏了哪一次拨动。若计数因初始snapshot或重复而复杂，先保留UNKNOWN，不用时间相关性补充事件。

**PHYSICAL PASS**仅限Owner确认完成且上述来源、解析、ON/OFF与完整性条件满足的RT投递正控。它不等于物理键状态PASS，也不能证明73/74与RT共用route。三轮不足、失焦、锁定、超时前异常或Owner未完成动作，分别标NOT RUN/INCONCLUSIVE；不为了PASS自动重复或扩大权限。

## 正控失败定位

| 观察 | 可以定位 | 尚不能得出的结论 / 下一步 |
|---|---|---|
| startup/registration失败、Window/threadfalse、loop error或无active窗口 | 监听前置条件失败；保留stage/error/stop | NOT RUN，先离线修工具；不称固件无事件 |
| total>0而active-trial0或guard/deadline过滤 | 消息到达，观察窗口条件未覆盖 | INCONCLUSIVE；恢复专用会话后另行Owner测试，不放宽guard |
| total>0，other_headers>0，target0 | 有同usage消息但来源不是当前绑定目标 | 检查脱敏TLC metadata/身份，禁止读取非目标payload |
| target header>0但read_failures或framing errors | Windows已送目标handle消息；读取/结构契约故障 | 修正有证据支持的adapter/framing后离线回归；不猜偏移、不主动query |
| target报告>0但RT0或rt_invalid>0 | 目标TLC有数据；预期事件/格式未得到有效证据 | 不搜索所有offset，记录ID/长度及错误；73/74route仍未知 |
| 注册、前台、心跳正常但所有WM_INPUT=0 | 没有目标消息到达本窗口的证据 | 不能区分USB不发、HIDClass不可见、Raw Input未投递；停止，标INCONCLUSIVE |
| Owner动作完成，ON/OFF缺失/重复/顺序无法判断 | 此正控目标未完整通过 | 保留覆盖与计数；不凭时间编造配对/缺失位置 |
| 有效RT正控PASS，后续另行A仍0 | 该TLC投递链在RT条件下工作 | 排除部分通用listener失败，仍不能证明73/74应由同TLC发送或全键不存在 |

若要进一步区分“listener failure”与“firmware没有发”，必须有同会话、同目标TLC的独立被动输送证据或既有可信总线证据。当前工具和静态资料不能提供这一最终分界。不要自动启动其它捕获器、安装USBPcap/driver、发Feature/GetInput/enable命令，或运行HAL初始化。若已有生产Monitor恰在运行，可Owner仅观察既有Gate状态变化作为辅助；涉及按键或配置写入的控制程序不可当安全正控，且没有第二读者也不能宣称固件不存在事件。

本轮停止于文档交付；明天先闭合RT正控，再审查是否继续73/74物理反例。Provider实现与正式迁移仍等待Owner审查。
