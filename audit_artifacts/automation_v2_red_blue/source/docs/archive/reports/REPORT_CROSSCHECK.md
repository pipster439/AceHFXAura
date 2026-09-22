> Historical snapshot. Superseded by [current documentation](../../README.md). Claims and TODOs below describe their original period, not current implementation or verified acceptance.

# 外部 Review 报告核查结论

- **被核查文件**：`C:\Users\ROG\Downloads\repository_review_report.md`（下称"该报告"）
- **核查基准**：`G:\Aura` 工作树实测（Git 状态查询、源文件阅读、用户本机 Steam 目录实际文件）
- **我方对照报告**：`G:\Aura\AURA_CODE_REVIEW_REPORT.md`
- **核查时间**：2026-09-10

---

## 一句话结论

该报告共提出 **15 项主张**，其中 **11 项成立**、**3 项部分成立或过度推断**、**1 项不成立（需更正）**。

**价值**：其中 **4 项为本次核查新确认的有效发现，且我此前的报告未覆盖**（GSI/前端/测试源码未被 Git 跟踪、用户本机 GSI 配置缺 `bomb`、前端保存隐式覆写 `default_profile`、重连风暴×内存泄漏的场景重估）。该报告在**工程纳管（Git 状态）与真实环境一致性（本机配置文件）**&#x4E24;个维度上，比我的报告更敏锐。

**局限**：该报告完全未触及我报告中 **唯一的"严重"级缺陷**（`GsiState::Evaluate` 的 `!=` 死锁）以及 **硬件缓冲区无边界写**、**Web 接口路径穿越/CSRF**、**`brightness` 契约断裂**等问题；且在文档时效性判断上存在一处明确错误。

---

## 一、逐条核查明细

|  编号  | 该报告主张                                                                                    |             判定             | 证据                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| :--: | :--------------------------------------------------------------------------------------- | :------------------------: | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| §1-a | Phase 1 已落地：`CLSID_ClaymoreHal` 实例化、`CreateLedDevice` 虚表调用、USB 64 字节边界隔离、68 键标定、11 种内建灯效 |            ✅ 成立            | `aura_adapter.cpp:84-177`（CLSID/VTable[5]/VTable[19]）、`:52-61`（边界隔离注释）、`calibrated_keymap.json` 实测 **68 键 / 68 个唯一 led_id**、`builtin_effects.h` 恰 **11 个 Effect 子类**                                                                                                                                                                                                                                                                                          |
| §1-b | Phase 2 已落地：独立进程、Job Object 兜底、命名事件平滑通知、`SO_REUSEADDR`、React 单文件打包                       |         ⚠️ 成立但归因需修正        | `web_supervisor.cpp:11-18`（Job Object）、`:116-118,183-185`（命名事件）、`vite.config.js`（`viteSingleFile`）。**`SO_REUSEADDR` 并非本仓库代码所设**，而是 `httplib` 默认行为（`httplib.h:2061-2068`）；`gsi_adapter.cpp:721-722` 的注释写"严格设置地址复用"，实际调用的是 `set_keep_alive_max_count(100)`（注释与 API 不符）                                                                                                                                                                                          |
| §1-c | Phase 3 代码已写但**处于 Git 未跟踪状态**                                                            |     ✅ **成立（我此前的报告遗漏）**     | `git status --porcelain` 实测：`?? include/gsi/`、`?? src/gsi/`、`?? tests/`、`?? frontend/`、`?? test_cs2_gsi.py`。`git ls-files include/gsi src/gsi tests frontend/src test_cs2_gsi.py` 返回 **0 项**                                                                                                                                                                                                                                                                  |
| §1-d | 硬件标识 VID `0x0B05` / PID `0x1B7E` / 型号 `7038`                                             |          ✅ 成立（非杜撰）         | 仓库内 `AURA_HARDWARE_VERIFICATION_REPORT.md`、`calibrated_keymap.json`、`calibrate_keys.py`、`set_rgb.py` 等多处记载                                                                                                                                                                                                                                                                                                                                                    |
| §2.1 | GSI/前端/测试源码未纳入版本控制，且 `CMakeLists.txt` 已引用它们                                              |            ✅ 成立            | 同上；`CMakeLists.txt` 为已跟踪文件（`git status` 显示 ` M`），其 `DAEMON_SOURCES` 含 `src/gsi/gsi_adapter.cpp`、`test_gsi_rules` 目标含 `tests/test_gsi_rules.cpp` → **全新 clone 无法构建**（该报告未点出"无法构建"这一最直接后果）                                                                                                                                                                                                                                                                      |
| §2.2 | 完全缺少 `.gitignore`，存在污染风险                                                                 |            ✅ 成立            | 实测根目录**无** `.gitignore`；`git ls-files` 194 项中含 `build/` 下 107 项、6 个 `.exe`、3 个 `.log`、3 个 `.pyc`                                                                                                                                                                                                                                                                                                                                                              |
| §2.3 | 用户本机 `gamestate_integration_aura.cfg` 缺少 `"bomb" "1"`，导致 C4 事件无法送达                       |     ✅ **成立（我此前的报告遗漏）**     | 实测 `D:\SteamLibrary\...\game\csgo\cfg\gamestate_integration_aura.cfg`（750B，2026-08-30 11:35）**确实无 `bomb` 节点**；而代码模板 `web_server.cpp:293` 含 `"bomb" "1"`。差异仅此一项，其余 16 个数据块一致                                                                                                                                                                                                                                                                                   |
| §2.4 | `config.json` 规则引用的 `coding` profile 缺失                                                  |            ✅ 成立            | 与我的报告 #25 一致。`config.json:219,223` 引用 `coding`；`profiles` 中实际仅有 bomb_pulse / cs2_gamer / cyberpunk / danger_red / desktop / office / rainbow_wave / reactive / ripple                                                                                                                                                                                                                                                                                         |
| §2.5 | 根目录堆积 30+ 逆向标定脚本                                                                         |            ✅ 成立            | 实测根目录 **34 个 `.py`**，其中 18 个含 COM 虚表直调逻辑                                                                                                                                                                                                                                                                                                                                                                                                                      |
| §2.6 | `README.md` 与 `AGENT.md` **仍停留在 Phase 2**，对 GSI 架构、事件推导、API 路由"未做任何文档更新"                 |       ❌ **不成立（需更正）**       | `README.md:239` 设有专章「CS2 GSI 深度联动与遥测管道 (Phase 3)」，并已包含：线程解耦说明（`:244`）、生命周期/超时清理（`:246`）、字段分类与观战限制（`:248`）、`gsi_bindings` 事件脉冲说明（`:271`）、前台隔离红线（`:307-310`）、GSI 综合测试证据（`:313-345`）、API 路由（`:122, 234, 330-332`）。**README 已完整更新**；仅 `AGENT.md:463`（"下一阶段是 CS2 GSI 适配器"）确实滞后                                                                                                                                                                                     |
| §3.1 | `ForegroundMonitor` 跨线程无锁读写 `current_process_name_`（UB）                                  |            ✅ 成立            | 与我的报告 #47 一致。写：`foreground_monitor.cpp:118, 153`；读：`main.cpp:217`；`include/monitor/foreground_monitor.h:45` 的 `cached_proc_name_` 全工程**零使用**                                                                                                                                                                                                                                                                                                                  |
| §3.2 | 双机切换断连时 `CheckReconnect` 每 1.5s 重试，叠加已知泄漏 → 1 小时泄漏约 900MB~1.1GB，必致 OOM 崩溃                | ⚠️ **部分成立（场景成立，量级为未实测外推）** | 场景与机制成立：`aura_adapter.cpp:279-295`（固定 1.5s 退避，无指数退避）、`main.cpp:233-237`（断开时每帧调 `CheckReconnect`）；算术自洽（3600/1.5=2400 次 × 0.38–0.49MB ≈ 912–1176MB）。**但**：`AGENT.md §13` 实测的 0.38–0.49MB/次来自 `--test-init 100` 的**成功**路径（100/100 创建到设备），而断连场景走的是 `ConnectHardwareInternal()` 中 `dev_count == 0` 的**提前返回分支**（`aura_adapter.cpp:138-143`，该分支**会**调用 `ReleaseHardwareInternal()`）。失败路径是否同样每次泄漏 ≈0.44MB **从未被测量**，故"1 小时必然泄漏 1GB""必定耗尽内存而崩溃"属**未经验证的推断**，不应作为既成事实陈述 |
| §3.3 | `std::wstring(cmdline.begin(), cmdline.end())` 逐字节加宽破坏中文路径，`CreateProcessW` 报 `0x2`      |            ✅ 成立            | 与我的报告 #41 一致，`web_supervisor.cpp:122`                                                                                                                                                                                                                                                                                                                                                                                                                         |
| §3.4 | 前端 `handleSave` 无条件把当前编辑方案写入 `default_profile`，与"设为默认"功能冲突                               |     ✅ **成立（我此前的报告遗漏）**     | `frontend/src/App.jsx:206` `default_profile: currentProfileName`；另有专用动作 `handleSetDefaultProfile`（`App.jsx:325-327`）及 `defaultProfileName` 展示（`:414, 515`）。在 CS2/办公方案上点"保存"确实会把桌面默认灯效替换掉                                                                                                                                                                                                                                                                      |
| §3.5 | `test_gsi_rules.cpp` 在 Release 下 `assert` 被移除，产生假阳性                                      |         ✅ 成立（需补前提）         | 与我的报告 #62 一致。`CMakeLists.txt` 对测试目标配置 `/O2`；`NDEBUG` 由 MSVC Release 配置默认定义（本仓库 `build/` 即为 Release）。该报告引用的终端输出文本与 `test_gsi_rules.cpp:30` 的 `std::cout` 格式吻合，但我方无法复现其运行记录，**建议标注为推理一致而非实测引用**                                                                                                                                                                                                                                                                 |
| §3.6 | `LoadHtmlContent()` 依赖相对路径，非预期 CWD 下会降级到应急页面                                             |      ⚠️ 基本成立，3 处细节不精确      | 成立部分：`web_server.cpp:221-239` 确实只在相对路径探测（Task Scheduler / 快捷方式启动时 CWD 常为 `C:\Windows\System32`）。不精确之处：① 候选路径实际有 **3 个**（该报告漏了 `../../web/index.html`，`web_server.cpp:226`）；② 兜底页 `EMBEDDED_FALLBACK_HTML`（`:12-31`）是**带样式的 HTML**，非"纯文本应急 HTML"；③ 影响被高估——仅前端页面降级，daemon 灯效与 GSI 不受影响。另：该报告建议的修复范式（`GetModuleFileNameW`）在仓库中**已有现成实现**可复用（`WebUiSupervisor::FindExecutablePath()`，`web_supervisor.cpp:64-86`）                                                  |

---

## 二、该报告成立且我此前遗漏的 4 项（已确认，应并入结论）

| 新增项    | 事实                                                                                       | 影响                                                                                                                                                                   |
| :----- | :--------------------------------------------------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **N1** | `include/gsi/`、`src/gsi/`、`tests/`、`frontend/`、`test_cs2_gsi.py` **全部未被 Git 跟踪**         | ① Phase 3（GSI+事件引擎）与全部测试**不在版本控制内**，分支切换/清理即丢失；② `CMakeLists.txt`（已跟踪）引用了这些文件 → **全新 clone 直接构建失败**；③ 我此前"194 个跟踪文件"的统计口径隐含了这一盲区，应更正                                 |
| **N2** | 用户本机 `D:\SteamLibrary\...\cfg\gamestate_integration_aura.cfg` **缺 `"bomb" "1"`**（代码模板已含） | `bomb.state` / `round.bomb` 永不到达 → `bomb_pulse` 方案与 `event.bomb_*` 全部失效。这是**唯一一处"代码正确、环境不同步"**&#x7684;实机配置缺陷，且与 `README.md:333` 宣称的"CFG 模板已集成 bomb 节点"形成对照，用户会误以为已生效 |
| **N3** | `frontend/src/App.jsx:206` 的 `handleSave` 无条件覆写 `default_profile`                        | 用户微调 `cs2_gamer`/`office` 后点保存 → 桌面默认灯效被静默替换；且违背 `AGENT.md §4.2` 中"默认策略 = 桌面"的产品约定                                                                                   |
| **N4** | 断连重连风暴（1.5s × 无限次）与已知泄漏的组合风险                                                             | 我原报告将固定 1.5s 退避列为"低"（#13）。该报告指出它与 `§13` 泄漏的**叠加效应**具有方向性价值，应上调优先级——但**必须先实测失败路径的每次泄漏量**再定性（见 §3.2 判定）                                                                |

## 三、该报告存在的 3 处问题（需更正）

1. **§2.6 事实错误**：称 README 未更新 Phase 3 / GSI / API。实测 README 已完整覆盖（专章 + API 路由 + 测试证据）。**正确表述应为**："`AGENT.md` 仍停留在 Phase 2 末尾（`:463`），README 已更新"。
2. **§3.2 量级过度推断**：把**成功路径**实测的 0.38–0.49MB/次直接套用到**失败提前返回路径**，并给出"必定耗尽系统内存而崩溃"的确定性结论。应改为条件式表述，并补做实测（见下方建议验证）。
3. **§3.3 修复方案的技术选择不准确**：报告要求用 `MultiByteToWideChar(CP_UTF8, ...)`。但 `exe_path` 来自 `std::filesystem::path::string()`（Windows 下返回 **ACP/本地码页**编码，非 UTF-8），`config_path_` 来自 `main(int, char* argv[])`（同样为 ACP）。**改用 CP_UTF8 会引入新的乱码**。正确方向是：优先全程走宽字符（`path::wstring()` + `CreateProcessW`），退而求其次用 `CP_ACP`。

## 四、该报告完全未覆盖的重要问题（我方报告中的高优先级项）

|    我方编号   |   严重度  | 问题                                                                                                                                                    |
| :-------: | :----: | :---------------------------------------------------------------------------------------------------------------------------------------------------- |
|    #17    | **严重** | `GsiState::Evaluate` 的 `op == "!="` 递归自调用 → `std::mutex` 重入 → **daemon 完全冻结**（`gsi_adapter.cpp:552-553`）                                              |
|  #8 / #9  |    高   | `stream_buffer_[216]` 与 COM 对象 `0x6C/0x74` 按表长写入，**无边界校验**（`aura_adapter.cpp:222-230`、`:159-163`）；当前键位表恰好占满 72 槽、零余量                                  |
|     #1    |    高   | `main.cpp:178-193` vs `217-227` 的 `last_proc_name` / `current_active_profile_name` 数据竞争（该报告的 P0 修复只覆盖了 `ForegroundMonitor`，**修复不完整**，同样的 UB 仍会从主控侧爆发） |
|    #26    |    高   | `brightness` / `speed_index` 引擎侧零引用 —— 亮度滑块不影响真实键盘                                                                                                    |
| #32 / #33 |    高   | `/web/(.*)` 路径穿越；`POST /api/config`、`/api/gsi/install-cfg` 无鉴权、无 Origin 校验                                                                            |
|    #64    |    中   | `logger.h` 无日志轮转（违背 `AGENT.md §6` 明确要求），常驻进程日志无限增长                                                                                                    |
|    #55    |    中   | `CurrentEffect` 在 `period_ms: 1` 时 `% 0` 除零 UB                                                                                                        |
|    #54    |    中   | 响应类灯效不 `Clear()` → 未映射的 60 个 LED 残留上一帧颜色（残影）                                                                                                          |
| #10 / #11 |  高（已知） | 栈伪造 `std::vector` 手法被照搬；`CreateLedDevice` 线性泄漏未销项（`AGENT.md §12.7 / §13.4`）                                                                           |

---

## 五、合并后的修复优先级建议

| 阶段           | 内容                                                                                                                                                                                          | 依据                                    |
| :----------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | :------------------------------------ |
| **P0 阻断性**   | ① `Evaluate` 的 `!=` 死锁；② 三处跨线程数据竞争（`main.cpp` **两处** + `ForegroundMonitor` 一处）；③ 硬件缓冲区/COM 偏移写入边界断言                                                                                         | 我方 #17、#1、#47、#8/#9                   |
| **P0 运行稳定性** | ④ 重连改指数退避 + 上限；**先实测**断连路径的每次泄漏量（用 `--test-init` 在键盘拔除状态下重跑 100 次即可判定）                                                                                                                      | 该报告 §3.2（需补实测）+ 我方 #13                |
| **P1 工程纳管**  | ⑤ 新增 `.gitignore`；⑥ `git add` 纳管 `include/gsi`、`src/gsi`、`tests`、`frontend`、`test_cs2_gsi.py`（否则新 clone 不可构建）；⑦ `config.json` 补齐 `coding` profile；⑧ 同步用户本机 cfg 补 `"bomb" "1"`（或重跑 WebUI 安装） | 该报告 §2.1/§2.2/§2.4/§2.3               |
| **P1 安全与契约** | ⑨ 路径穿越 + 写接口鉴权；⑩ `brightness` 落地或从 UI/配置移除；⑪ 非 ASCII 路径（**用 `CP_ACP` 或全程宽字符，勿用 `CP_UTF8`**）；⑫ 日志轮转                                                                                          | 我方 #32/#33/#26/#41/#64（修复方案按 §三.3 更正） |
| **P2 体验与结构** | ⑬ 前端 `handleSave` 与 `setAsDefault` 解耦；⑭ 测试断言改为显式失败 + 接入 CTest；⑮ 根目录脚本归档至 `tools/calibration/`；⑯ 更新 `AGENT.md` 至 Phase 3（README 无需改动）                                                        | 该报告 §3.4/§3.5/§2.5/§2.6（更正后）          |



---

## 六、核查方法论说明

- Git 状态类主张：以 `git status --porcelain`、`git ls-files <paths>`、`git ls-files --error-unmatch` 实测为准。
- 配置一致性主张：以本机真实文件（`D:\SteamLibrary\...\gamestate_integration_aura.cfg`）与代码模板逐节点比对为准。
- 并发/内存类主张：以代码路径与 `AGENT.md` 实测记录交叉核对；凡涉及运行期量级外推者，一律标注"需实测验证"，不采信为既成结论。
- 本核查未执行构建、未在真机硬件上运行，也未复现该报告引用的终端输出。

---

## 七、跨代理交叉质证记录（WorkBuddy ↔ Antigravity，经 MCP）

### 七.1 会话 ID 的可续接性（关键前提）

用户指定的会话 UUID `f7494a39-5673-4ee4-afc1-28cf5983f454` 经实测为 **IDE 轨迹**：

- 存放于 `C:\Users\ROG\.gemini\antigravity\conversations\f7494a39-…​.db`（3.0MB，2026-09-10 18:06）
- `antigravity_list_conversations` 可列出它（标注为 `(IDE)`），其 prompt 预览为"深度review当前仓库…"——即 `repository_review_report.md` 的产出会话
- 但 `agy CLI` 只能续接 `~/.gemini/antigravity-cli/` 下的 CLI 轨迹，对 IDE 轨迹返回 `trajectory not found`

因此实际交流在 **CLI 轨迹 `aacb4fd5-ad34-4870-a9d5-b8aa6f8f1193`** 上进行（该轨迹承载了本仓库全部交叉质证上下文，brain/db 均存在）。这与既有结论一致：`antigravity_list_conversations` 会同时列出 IDE 与 CLI 两类会话，**列表中出现过的 UUID 未必可续接**。

### 七.2 第 1–2 轮要点与对方立场

| 我方质证 | 对方（Antigravity）立场 |
| :--- | :--- |
| 更正一：README 已更新 Phase 3，仅 `AGENT.md:463` 滞后 | ✅ 确认为事实错误，接受更正，承认"将工程文档与内部跟踪日志混淆" |
| 更正二：断连泄漏量级属未验证外推 | ✅ 完全证实，承认"将成功路径数据外推到枚举失败路径"，并给出判别实验（拔除键盘后重跑 `--test-init 100`，看 `PrivateBytes` 是否仍线性增长） |
| 更正三：应用 `CP_ACP` 而非 `CP_UTF8` | ✅ 完全同意，并补充"最优解为原生宽字符管道" |
| 我方 #17 死锁 / #1·#47 竞态 / #8·#9 缓冲越界 | ✅ 均认定为原报告的**严重审查遗漏**，非有意排除 |
| SO_REUSEADDR 归因、§3.6 细节偏差 | ✅ 接受 |

对方同时提出 3 项排序建议与 2 项"新发现"，我方复核如下（记录于本文件的依据即在此）：

| 对方主张 | 我方判定 |
| :--- | :--- |
| 排 1：`.gitignore` + 纳管未跟踪代码 提升为 **P0-Pre** | ✅ 采纳（理由成立：含死锁的 `src/gsi/gsi_adapter.cpp` 本身处于未跟踪状态，先建立版本基线再改代码） |
| 排 2：非 ASCII 命令行路径 提升为 **P0** | ⚠️ 有条件采纳（当前用户路径为纯 ASCII，属潜伏；部署到中文用户名机器则为阻断） |
| 排 3：将 #48（`ForegroundMonitor` → `std::terminate`）补入 P0 | ✅ 采纳（该项确在我方报告中，但未进入 §五 路线图，属我方编排遗漏） |
| 新发现 1：`ForegroundMonitor` 钩子失败 → `std::terminate` | ✅ 成立但**非新发现**（等同我方 #48） |
| 新发现 2：`WriteConfigFile` 缺原子替换，可能截断清空配置 | ❌ **幻觉，驳回**（详见 §七.3） |

### 七.3 驳回详情：`WriteConfigFile` "缺原子替换"为看漏分支的误读

对方称 `web_server.cpp:259-270` "直接使用 `std::ios::trunc` 打开生产配置文件……可能永久损毁变为空文件"，并列为两份报告均漏检的 P1 新发现。

逐行核对源码（`web_server.cpp:251-273`）后确认与事实不符：

1. 先写临时文件 `config_path_ + ".tmp"`（`:254-260`）；
2. 关闭临时文件后调用 `MoveFileExA(tmp_path, config_path_, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` 完成原子替换（`:263`）——**这才是主路径**；
3. 仅当 `MoveFileExA` 返回失败（`:263` 的 `if` 为假）时，才在 `:265-267` 回退为 `std::ios::trunc` 直接写。

即：原子替换是本仓库已实现的主路径，真实缺陷只在**兜底分支非原子**（我方 #36 已记录）。对方在**第 3 轮明确撤回**该项，承认"仅注意到了 `:267-270` 的 `trunc` 写入，未核实其处于 `MoveFileExA` 失败的 else 兜底分支内"。

> 方法论意义：这是本轮跨代理质证中**唯一被证实为幻觉**的主张。对 LLM 产出的"新发现"必须落到具体分支上下文核对，仅凭片段行号不足以采信。

### 七.4 第 3 轮：语义缺口的最终裁决

对方在上下文修正后被要求裁决两件事，结论如下：

| 议题 | 对方最终裁决 | 我方裁定 |
| :--- | :--- | :--- |
| "字段缺失 → `!=` 成立"是否独立缺陷 | **不构成代码缺陷**：字段缺失在 `gsi_adapter.cpp:490-492` 即 `return false`，控制流物理上到不了 `!=` 分支；该项属提问阶段的假设性推论，不计入任何报告 | ✅ 一致。该错误前提系我方提问时引入（未先核对早退位置），对方第 2 轮答复中已自我修正，但其结论段的"独立缺陷"定性前后矛盾，本轮已澄清 |
| `!=` 重写应取何语义 | **`type_matched && !is_equal`**（类型不匹配 → `false`），与 `<`/`<=`/`>`/`>=` 的防御风格一致 | ✅ 一致。其第 2 轮给出的 `type_matched ? !is_equal : true` 存在残留缺口（字段存在但类型不匹配时恒真、规则劫持），本轮已改正 |

### 七.5 阻断级最终清单（双方合并结论）

对方被要求"仅列阻断级、不得凑数"，给出 6 项并确认 `REPORT_CROSSCHECK.md §五` 可作为最终实施基线。合并后按实施依赖排序：

| 序 | 项 | 位置 | 归属 |
| :---: | :--- | :--- | :--- |
| 0 | 版本基线：新增 `.gitignore` + 纳管未跟踪源码（否则无法构建，且后续修复易丢失） | 仓库根 | 外部报告 N1 + 我方 #65 |
| 1 | 死锁：`Evaluate` 的 `!=` 递归自调用 | `gsi_adapter.cpp:552-553` | 我方 #17 |
| 2 | 竞态：三处非原子跨线程读写 | `main.cpp:178-193` vs `:217-227`；`foreground_monitor.cpp:118,153` vs `:214-216` | 我方 #1、#47 |
| 3 | 退出崩溃：钩子失败时 `Stop()` 早退致 `std::terminate` | `foreground_monitor.cpp:142-146`、`:200-212` | 我方 #48 |
| 4 | 内存破坏：缓冲/COM 偏移写入无边界断言 | `aura_adapter.cpp:222-230`、`:159-163` | 我方 #8、#9 |
| 5 | 功能阻断：非 ASCII 路径逐字节加宽 | `web_supervisor.cpp:122` | 我方 #41 |

**双方均未将其列入阻断级、但我方坚持应单列的项**：#32/#33（`/web/` 路径穿越与写接口无鉴权）。对方的"阻断级"定义限于崩溃/死锁/越界/无法启动/无法编译，安全性缺陷未纳入；考虑到服务虽仅绑定 `127.0.0.1`，但可通过浏览器跨站请求触发，建议作为独立安全项单独排期。

### 七.6 本轮交流的技术元数据

| 轮次 | 工具 | 会话 | effort | 耗时 | tokens |
| :---: | :--- | :--- | :--- | ---: | ---: |
| 1 | `antigravity_plan_task`（workspace=G:\Aura） | 新建 | high | 101.89s | 172,693 |
| 2 | `antigravity_ask` | `aacb4fd5-…` | medium | — | 510,179 |
| 3 | `antigravity_ask` | `aacb4fd5-…` | medium | 690.51s | 526,436 |

> 操作注意：`antigravity_ask` 内部有约 300s 的硬上限且不暴露超时参数，`effort=high` 曾因此失败；降为 `medium` 可成功（尽管服务端自报耗时高于该上限）。宿主未注入 antigravity MCP 工具，需以 `mcp_stdio_client.py` 走标准 MCP stdio 协议直连。

