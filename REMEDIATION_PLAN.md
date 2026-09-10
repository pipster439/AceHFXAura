# Aura 缺陷处理方案（待批准）

- **依据**：`AURA_CODE_REVIEW_REPORT.md`（80 项）+ `REPORT_CROSSCHECK.md` + `ANTIGRAVITY_CROSSCHECK.md`
- **状态**：用户已批准（2026-09-10 20:12）。**批次 A（R1/R2/R3）已实施并验证通过**；批次 B–E 待启动。R19（键盘插拔测量）按用户要求**放到最后做**，由用户手动插拔键盘。
- **原则**：按"阻断性 → 功能/安全 → 结构/体验"分批，每批独立可构建、可回滚、可验证；凡涉及硬件或用户目录的操作单独请示。

---

## 0. 总体顺序与依赖

```
批次 A（P0 阻断性，必须先做）
  R1 Evaluate 死锁  ─┬─ R2 依赖 R1 的落锁重构（同一文件，合并提交）
  R2 并发单点化      ┘
  R3 推流缓冲边界

批次 B（P1 安全与健壮性，A 完成后）
  R4 请求体上限 ─┐
  R5 WorkerLoop 忙等 ─┤ 互相独立
  R6 路径穿越   ─┤
  R7 写接口鉴权 ─┤
  R8 非 ASCII 路径 ─┤
  R9 COM 生命周期 ─┤
  R10 日志轮转  ─┘
  R11 日志级别可配置

批次 C（P1 功能闭环，需用户决策）
  R12 config.json 补 coding + 引用校验
  R14 前端 handleSave 语义
  R15 brightness/speed_index 契约   ← 决策点 D1
  R13 测试体系修复

批次 D（P2 结构与治理，需用户决策）
  R16 参数下界校验统一（含 CurrentEffect 除零）
  R17 .gitignore + 源码纳管          ← 决策点 D2（涉 git rm --cached）
  R18 用户本机 CS2 cfg 补 bomb        ← 决策点 D3（涉写用户目录）
  R20 构建加固 /W4 + sanitizer
  R21 Python 工具链抽共享模块
  R22 文档合并与勘误

批次 E（需真机/需测量，单独立项）
  R19 内存泄漏：先测量，再定方案      ← 决策点 D4
```

**不做的事（明确边界）**：不重构 GSI 适配器的线程模型（已合规）；不改动 `httplib` 与 `json.hpp` vendored 源码（用其公开 API 解决）；不为兼容旧 `config.json` 保留静默降级路径（改为显式报错）；不引入新第三方依赖。

---

## 批次 A —— P0 阻断性

### R1 `GsiState::Evaluate` 的 `!=` 重入死锁

- **问题定位**：`src/gsi/gsi_adapter.cpp:552-553`（递归自调用）+ `:470-471`（进入即持锁）+ `include/gsi/gsi_adapter.h:101`（`std::mutex` 不可重入）
- **具体改法**（抽锁内实现，最小改动、零行为变化）：

  ```cpp
  // gsi_adapter.h —— 新增私有实现，声明"调用前必须已持有 mutex_"
  bool EvaluateLocked(const std::string& field, const std::string& op,
                      const nlohmann::json& target_val) const;   // 前置：mutex_ 已锁

  // gsi_adapter.cpp
  bool GsiState::Evaluate(field, op, target_val) const {
      std::lock_guard<std::mutex> lock(mutex_);
      return EvaluateLocked(field, op, target_val);
  }
  bool GsiState::EvaluateLocked(field, op, target_val) const {
      // 原 470-562 行全部逻辑迁入，一行不改；
      // 其中 "!=" 分支改为： return !EvaluateLocked(field, "==", target_val);
  }
  ```

  同时消除 `#18` 的持锁全量刷新开销：新增 `mutable uint64_t last_event_sync_ms_{0};`，`EvaluateLocked` 中刷新前判断 `if (now_ms != last_event_sync_ms_) { SyncEventFieldsToFlatState(now_ms); last_event_sync_ms_ = now_ms; }`。同一帧内多次 `Evaluate` 只刷新一次。
- **验收标准**：① 新增用例 `Evaluate("x", "!=", v)` 在 2 s 内返回（用 `std::async` + `wait_for` 守护）；② 现有 `test_gsi_rules` 全部行为不变；③ 同帧内 3 次 `Evaluate` 仅触发 1 次 `SyncEventFieldsToFlatState`（可用计数断言）。
- **回归风险**：低。**⚠ 本项经交叉验证增加一处改进**：不要保留 `const_cast<GsiState*>(this)`（`gsi_adapter.cpp:475/569` 现共 2 处），而是把 `flat_state_`（`gsi_adapter.h:102`）与新增的 `last_event_sync_ms_` 一并声明为 `mutable`，并把 `SyncEventFieldsToFlatState`（`gsi_adapter.cpp:376`）改为 `const` 成员函数——由此**彻底移除 `const_cast`**，同时不改变 `Evaluate`/`ToJson` 的 `const` 签名（`RuleEngine::MatchProfile(const GsiState*)` 的调用链不受影响）。

### R2 并发收敛：profile 决策单点化

- **问题定位**：`current_active_profile_name` 竞争（`src/main.cpp:185/192` 监控线程 vs `:221/222/247/253` 主循环）；`current_process_name_` 竞争（`src/monitor/foreground_monitor.cpp:118/153` 写 vs `:214-216` 读）
- **具体改法**（改为"回调只通知、主循环独占决策"，同时去掉重复的匹配计算）：
  1. `main.cpp` 回调体收缩为最小动作 —— 仅 `gsi_adapter.GetState().SetForegroundProcess(proc_name)`（该函数已由 `mutex_` 保护），**不再**在回调内调用 `MatchProfile` / `SetActiveProfile` / `SetSuppressed`，**不再**持有 `last_proc_name` / `current_active_profile_name`。
  2. `current_active_profile_name` 与 `last_proc_name` 从此**仅由主循环访问** → 竞争消失，无需加锁（保留为普通 `std::string`）。
  3. `main.cpp` 主循环成为**唯一**决策点。**⚠ 本条经交叉验证修正**：`SetSuppressed` **不能**挂在"方案名变化"的分支上——当两个前台进程映射到**同一个**方案名（例如都落到 `desktop`，或将来的两个游戏共用 `cs2_gamer`）而 `suppress_web_ui` 标记不同时，方案名不变 → 抑制状态永远不会被更新。正确做法是在主循环维护 `last_proc_seen_`（初值哨兵 `"__UNSET__"`），**只要前台进程名变化就调用** `web_supervisor.SetSuppressed(rule_engine.ShouldSuppressWebUi(cur_proc))`——`SetSuppressed` 内部是原子交换且仅在真正变化时才 `notify`，重复调用无副作用。配置热重载导致的标记变化由既有分支（`main.cpp:250-251`，已含 `SetSuppressed`）覆盖。**两条路径合起来才完备**，缺一不可。
  4. `ForegroundMonitor::current_process_name_` 加 `mutable std::mutex name_mutex_;`：`GetCurrentProcessName()` 与写入处（`WinEventProc` / `MonitorThreadProc`）各自加锁；删除头文件中从未使用的 `cached_proc_name_`（`include/monitor/foreground_monitor.h:45`）。
- **收益**：消除两处 UB；去掉主线程与钩子线程的重复匹配（当前同一进程名会被匹配两次）；钩子回调变为"零业务逻辑"，符合 WinEvent 回调快速返回的最佳实践。
- **验收标准**：① 主循环内以 1 ms 间隔连续 1000 次切前台应用，`aura_daemon` 无崩溃、日志中方案切换记录与前台进程一一对应且**不出现重复切换**；② 前台切换后灯效响应延迟 ≤ 40 ms（一个推流周期）；③ 编译期无 `-W4` 相关新增告警。
- **回归风险**：**中**。行为差异是"方案决策从钩子线程同步改为主循环下一个 40 ms 帧内完成"——延迟从 ~0 ms 变为 ≤40 ms。当前主循环本就每帧重新匹配，因此实际观感无变化。**该风险点是本次改造最需要在实施前确认的一处。**

### R3 推流缓冲与驱动表边界保护

- **问题定位**：`src/aura/aura_adapter.cpp:222-230`（写 `stream_buffer_[i*3+2]`，无断言）、`:159-163`（写 `pDev_+0x6C/0x74`，无校验）；`include/aura/aura_types.h:28-29`（`HARDWARE_STREAM_KEYS=72`，与 68 键恰好等长）
- **具体改法**：
  1. `aura_types.h` 把缓冲区容量与 USB 隔离规则解耦：
     ```cpp
     constexpr size_t MAX_HARDWARE_STREAM_KEYS = 144;   // 128 键最坏情况需 128+16 个隔离槽
     constexpr size_t HARDWARE_STREAM_BUFFER_SIZE = MAX_HARDWARE_STREAM_KEYS * RGB_CHANNELS;
     ```
     （`HARDWARE_STREAM_KEYS=72` 的**数值语义**仍体现在 `BuildPaddedHardwareTable` 的 `%15==14` 规则里，不受影响——缓冲区大小只是本地存储，实际长度由设备对象的 `+0x6C` 字段决定。）
     **144 的依据（已按 `%15==14` 规则逐点数值验证）**：68 键 → 表长 72（+4 隔离槽）；69 键 → 73（越界 3 字节）；127 键 → 136；**128 键（`TOTAL_LEDS` 上界）→ 137（+9 隔离槽，411 字节）** ≤ 144，余量 7 段。原常量旁应注释该推导，避免后人误改。
  2. `BuildPaddedHardwareTable()` 末尾增加运行期校验：若 `padded_hardware_table_.size() > MAX_HARDWARE_STREAM_KEYS`，`LOG_ERROR` 并返回失败（改为返回 `bool`），`Initialize()` 据此返回 `false`，**拒绝以可能越界的表去驱动硬件**。
  3. `PushFrame()` 入口增加 `assert` + 运行期早退：`if (padded_hardware_table_.size() * RGB_CHANNELS > sizeof(stream_buffer_)) return false;`
  4. 驱动内部表写入（`0x6C/0x74`）增加同一上界校验，并在注释中明确"底层容量未知，此为上界保护而非契约"。
- **验收标准**：① 用 68 键键位表跑 30 秒推流，日志与灯效与改动前完全一致；② 构造 69/128 键的临时键位表，`--dry-run` 下必须报错退出而非越界；③ `MSVC /analyze` 或 `/W4` 无缓冲区告警。
- **回归风险**：低。缓冲区扩大不影响发送长度；失败路径由"静默越界"变为"显式报错"。
- **说明**：本项**不解决** `AGENT.md §12.7` 指出的"栈伪造 vector"手法（`aura_adapter.cpp:116-125`）——那需要重新设计设备枚举方式，属独立课题，见 R23。

---

## 批次 B —— P1 安全与健壮性

### R4 HTTP 请求体上限

- **定位**：`src/gsi/gsi_adapter.cpp:685-737`、`src/web/web_server.cpp`（两个 server 均未设置）
- **改法**（**⚠ 上限经交叉验证收紧**）：在两处 `SetupRoutes()` 之前调用 `svr_.set_payload_max_length(256 * 1024);`（256 KB）。依据：仓库测试脚本构造的 payload 仅约 2 KB；真实对局含 10 人 `allplayers` 全量字段约 30–50 KB → 256 KB 仍有约 5 倍余量，同时把内存 DoS 的攻击面收敛一个数量级。超限时 httplib 本身即返回 `413 Payload Too Large`（已核实 `httplib.h:4475-4476`）。另外注册 `set_error_handler`，把 413 加工为规范 JSON 错误体，便于前端提示。请求头大小无需另设——httplib 已有 `CPPHTTPLIB_HEADER_MAX_LENGTH` 硬编码 8 KB（`httplib.h:85-86`）。
- **验收**：① 正常 GSI payload 仍 200；② `curl` 发送 4 MB body 时返回 4xx（httplib 在读取阶段即以超限拒绝）、进程内存无异常增长。
- **风险**：低。若未来需要更大 payload 只需调常量。

### R5 `WebUiSupervisor::WorkerLoop` 忙等

- **定位**：`src/supervisor/web_supervisor.cpp:240-247`
- **改法**（**⚠ 原设计经交叉验证被驳回，以下为修正版**）：不能把谓词简化成"只等 `stop_requested_`"——那样虽消除了自旋，但**会引入最长 1.5 s 的前台切换迟滞**：`wait_for(lock, dur, pred)` 等价于 `wait_until(lock, now()+dur, pred)`，截止时刻只计算一次；`SetSuppressed` 的 `notify_one()` 会唤醒线程，但谓词（仅 `stop_requested_`）仍为假 → 线程回到等待并耗完**剩余**超时，之后才执行动作。
  正确做法：谓词改为"**与上一轮观察到的状态快照相比发生了变化**"，既保留即时响应、又消除恒真自旋：
  ```cpp
  const bool s_prev = target_suppressed_.load(std::memory_order_acquire);
  const bool r_prev = is_running_.load(std::memory_order_acquire);
  std::unique_lock<std::mutex> lock(cv_mutex_);
  cv_.wait_for(lock, std::chrono::milliseconds(1500), [&]() {
      if (stop_requested_.load(std::memory_order_acquire)) return true;
      return target_suppressed_.load(std::memory_order_acquire) != s_prev
          || is_running_.load(std::memory_order_acquire) != r_prev;
  });
  ```
  自旋为何消失：`exe` 缺失场景下 `s=false, r=false` 且**状态不变** → 谓词恒假 → 完整等待 1500 ms（退避期 30 s 内仅约 20 次迭代，而非满速空转）。
  响应为何不受损：`SetSuppressed` 改变 `target_suppressed_` → 与快照不等 → 谓词立即为真 → 立即唤醒并执行动作（迟滞仅剩主循环的调度开销）。`StopChildProcess()` 把 `is_running_` 置 false 后虽不发 `notify`，但下一轮循环顶部会重新取快照，不会造成重复动作或饿死。
- **验收**：① 临时移除 `aura_web_ui.exe` 启动 daemon，观察工作线程 CPU 占用在 30 秒退避期内应接近 0（当前为满一核）；② 前台切到带 `suppress_web_ui` 的进程时，网页服务在 ≤1.5 s 内被停止、切回后在 ≤1.5 s 内被拉起。
- **风险**：低-中。需确认 `SetSuppressed` 的 `notify_one` 在"状态未变化"时不会误唤醒（现有实现用 `old_val != suppressed` 做了变化判定，保持不动即可）。

### R6 `/web/(.*)` 路径穿越

- **定位**：`src/web/web_server.cpp:51-68`
- **改法**（**⚠ 原设计经交叉验证被驳回，以下为修正版**）：原设计用 `weakly_canonical` + `std::filesystem::relative` 做归属校验，**已弃用**——它会为每个静态资源请求带来真实磁盘 I/O（符号链接解析/`status` 调用），且 `path::native()` 的字符类型随平台变化（Windows 为 `wchar_t`、POSIX 为 `char`），破坏可移植性。
  改用**纯词法检查**（零磁盘 I/O、无异常抛出风险）：在解码后的 `subpath` 上按 `/` 与 `\` 双分隔符切分组件，拒绝任何满足以下之一的请求：
  1. 含 `..` 组件；
  2. 是绝对路径 / 含盘符（`:`）/ 以 UNC 前缀（`\\`）开头 —— **这是原设计遗漏的第二条绕过路径**：`std::filesystem::path("web") / "C:/Windows/win.ini"` 会因右侧为 rooted path 而**整体替换**左侧，从而读取任意绝对路径文件；
  3. 含控制字符（含 `%00` 解码出的内嵌 NUL）。
  其余保留原判定（`exists` && 非目录）。
  依赖前提（已验证）：`httplib` 在路由前已完成百分号解码（`httplib.h:6483-6485`），因此 `%2e%2e`、`%2f`、`%5c` 等变体在处理器内已还原为字面字符，可被上述词法检查覆盖。
- **验收**：① `GET /web/..%2fconfig.json`、`/web/%2e%2e/config.json`、`/web/..%5cconfig.json`、`/web/C:/Windows/win.ini`、`/web//server/share/x` 一律 404；② `GET /web/index.html` 仍 200；③ 处理器不抛异常（日志中不出现 500）。
- **风险**：低。纯字符串判断，无平台差异。
- **补充说明**：若将来需要支持 `web/` 下的子目录资源，本词法检查天然兼容（只拒绝上跳与绝对路径）。

### R7 写接口鉴权与 `install-cfg` 目录白名单

- **定位**：`src/web/web_server.cpp:94-206`
- **改法**（**⚠ 原三层设计经交叉验证修正为四层**，纵深防御，不引入额外依赖）：
  1. **校验 `Host` 头**（**新增，防 DNS 重绑定**）：`Host` 的 host 部分必须 ∈ {`127.0.0.1`, `localhost`, `[::1]`}，端口可省略。这是抗重绑定的第一道也是最早的一道闸门——重绑定攻击下浏览器认为同源、可能不发预检，但 `Host` 一定带的是攻击者域名。
  2. **强制 `Content-Type: application/json`**（否则 415）：挡住 form/fetch 型简单请求。
  3. **`Origin` / `Referer` 校验**：带 `Origin` 时其 host 必须属于同一白名单；**显式拒绝字面量 `"null"`**（沙箱 iframe / `data:` / `file:` 场景会发出 `Origin: null`，**不可**当作"缺省"放过）；缺 `Origin` 但带 `Referer` 时校验其 host；两者皆空才放行（本地 curl/脚本）。
  4. **`install-cfg` 的 `target_dir` 收紧**：不再接受任意目录。允许的取值 = `DetectCs2CfgPaths()` 的结果 ∪ 目录内已存在 `gamestate_integration_*.cfg` 的目录（后者用于兼容 `DetectCs2CfgPaths()` 硬编码扫描覆盖不到的自定义 Steam 库——**这一点是交叉验证指出的误杀风险**）。写入文件名固定不变，并回传实际写入路径。
- **验收**：① 从本地页面正常保存配置仍成功；② `Origin: http://evil.test` → 403；③ `Origin: null` → 403；④ `Host: evil.test` → 400/403；⑤ 传 `target_dir: C:\Windows` → 400 且不产生文件；⑥ 自定义 Steam 库（含既有 `gamestate_integration_*.cfg`）仍可安装。
- **风险**：中。若用户通过非 `127.0.0.1/localhost/[::1]` 访问（当前架构不支持局域网，`AGENT.md §5.2` 明确未开放），会被上述白名单拒绝。**该约束需写入 README。**

### R8 `WebUiSupervisor` 非 ASCII 路径

- **定位**：`src/supervisor/web_supervisor.cpp:122`
- **改法**：**全程宽字符**，绕开编码推断：`FindExecutablePath()` 改返回 `std::wstring`（用 `candidate.wstring()` 而非 `.string()`），`config_path_` 改存 `std::wstring`（`WebUiSupervisor::StartSupervisor` 接收 `std::filesystem::path`），命令行用 `std::wstring` 拼接后直接交给 `CreateProcessW`。
- **明确不采用** `MultiByteToWideChar(CP_UTF8, ...)`：`path::string()` 与 `main(char* argv[])` 在 Windows 下均为**本地码页(ACP)** 编码，按 UTF-8 解码会引入新的乱码（此点已在 `ANTIGRAVITY_CROSSCHECK.md §三` 更正外部报告的错误建议）。
- **验收**：① 在含中文的目录（如 `D:\我的工具\Aura`）下放置 cfg 与 exe，网页服务能被正常拉起；② 纯 ASCII 路径行为不变；③ 含**非 CP936 字符**（生僻字/日韩文/emoji）的目录同样可用。
- **风险**：低-中。**⚠ R8b 已从"可选"提升为"本次必做"**（交叉验证指出）：若只改父进程、子进程仍用窄 `main(char* argv[])`，CRT 在 `argv` 初始化阶段就用 ANSI 码页转换命令行，**不在 ACP 内的字符会被有损替换为 `?`**，`std::ifstream` 必然打不开 → 只解决了 CP936 范围内的路径，未闭环。故本次一并做：
  - `src/web/main_web.cpp` 改为 `wmain(int argc, wchar_t* argv[])`，用 `CommandLineToArgvW` 解析（或直接用 `std::filesystem::path`）；
  - `WebServer` 的 `config_path_` 改为 `std::filesystem::path`，内部文件操作全面切宽：`std::ifstream` 走 `path` 重载（MSVC 支持），`MoveFileExA` → **`MoveFileExW`**（`web_server.cpp:265`；否则非 ASCII 配置路径下原子替换会静默失败并退化为非原子截断写，放大 #36 的风险）；
  - `DetectCs2CfgPaths()` 返回 `std::vector<std::filesystem::path>`。

### R9 COM 生命周期平衡

- **定位**：`include/aura/aura_adapter.h:57` + `:343-345`（`com_initialized_` 恒 false，`CoUninitialize()` 不可达）；`src/main.cpp:60`（`CoInitialize` 未配对，且 `:84/:93/:133` 三条提前返回路径直接跳过清理）
- **改法**：
  1. 删除 `AuraAdapter::com_initialized_` 字段与 `Shutdown()` 中的 `CoUninitialize()` 分支（该类的契约就是"COM 由调用方初始化"）。
  2. `main.cpp` 用 RAII 守卫管理 COM 生命周期：
     ```cpp
     struct ComScope {
         HRESULT hr;
         explicit ComScope() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
         ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
         bool ok() const { return SUCCEEDED(hr); }
     } com;   // 置于 main 顶部，四条 return 路径自动平衡
     ```
     并检查 `hr`（`RPC_E_CHANGED_MODE` 时给出明确日志）。**⚠ 交叉验证补充**：`ComScope` 需显式 `= delete` 拷贝与移动构造（防止守卫被复制导致 `CoUninitialize` 被调用多次）；另外在 `AuraAdapter::Initialize()` 入口加一条断言/日志，显式声明"调用方已完成 COM 初始化"这一前置契约（该类不再自行初始化 COM）。
- **验收**：① 四条退出路径（`--help`、重复实例、键位表加载失败、正常退出）均无 COM 相关的资源残留；② `--test-init` 压测后 `PrivateBytes` 曲线不受影响。
- **风险**：低。注意不得让 `ComScope` 覆盖到仍在使用 COM 的析构顺序（声明在 `main` 最前、后于其他局部对象析构，次序正确）。

### R10 日志轮转

- **定位**：`include/utils/logger.h:28-61`（`AGENT.md §6` 明确要求但未实现）
- **改法**：`Logger` 增加 `max_bytes_`（默认 8 MB）与 `rotate_keep_`（默认 3）；每次写文件前检查 `tellp()`，超限则 `close` → 将 `aura_daemon.log` 重命名为 `.1`（已有则顺延 `.2`、`.3`，最旧删除）→ 重新 `open`。全部在既有 `mutex_` 内完成。
- **验收**：① 临时把阈值调为 64 KB，持续运行至产生多次轮转，`aura_daemon.log` 不超过阈值且 `.1/.2` 存在；② 轮转期间日志不丢行（按尾部若干行核对）。
- **风险**：低。重命名失败时降级为"继续写不轮转"并记一条告警，避免因日志问题影响主流程。

### R11 日志级别可配置（消除死代码）

- **定位**：`include/utils/logger.h:63-69`，`SetLogLevel` 与 `LOG_DEBUG` 全仓 0 调用
- **改法**：`main.cpp` 新增 `--log-level <debug|info|warn|error>` 参数，在**启动其他线程之前**调用 `Logger::Instance().SetLogLevel(...)`。这样既让 Debug 级可达、又保证 `current_level_` 在并发开始前就固定（使 `ShouldLog` 的无锁读不成问题）。同时在 `logger.h` 注释中写明"必须在启动工作线程前调用"。
- **验收**：① `--log-level debug` 时能看到新增的调试输出；② 不带该参数时输出与当前一致。
- **风险**：极低。

---

## 批次 C —— P1 功能闭环

### R12 `config.json` 补 `coding` + 配置引用校验

- **定位**：`config.json:219/223` 引用不存在的 `coding`（`config.example.json` 中有定义）
- **改法**：
  1. 把 `config.example.json` 中的 `coding`（`type: static`、`color:[10,30,50]`、`keys: ESC/ENTER/TAB`）补入 `config.json` 的 `profiles`。
  2. `RuleEngine::LoadConfig` 增加**引用完整性校验**：任何 `rules[].profile` 或 `gsi_bindings[].profile` 在 `profiles` 中不存在时，`LOG_ERROR` 明确指出"哪个规则引用了未定义方案"，并使 `LoadConfig` 返回 `false`（当前是静默回退 `desktop`）。
  3. 未知 `type` 值同样报错（当前 `base_effect` 为 `nullptr` → 静默全黑）。
- **验收**：① 修正后 `code.exe` 前台切实匹配到 `coding`，日志有对应记录；② 故意把某个 `profile` 名改错，daemon 启动即报明确错误、不再静默降级；③ 热重载解析失败时保留旧配置并只报一次错（配合 R12b 见下）。
- **风险**：中。第 2 条把"静默降级"改为"显式失败"，若用户既有 `config.json` 本身有笔误，daemon 会拒绝启动——这是**期望行为**，但需在 README 与错误信息中给出修复指引。
- **R12b（配套）**：`CheckAndReload` 在解析失败时把 `last_write_time_` 同步为当前文件时间，避免每秒重试刷屏；仅在 mtime 再次变化时才重试。

### R14 前端 `handleSave` 语义

- **定位**：`frontend/src/App.jsx:206`
- **改法**：`handleSave` 中 `default_profile` 改为沿用现值（`default_profile: config.default_profile`），保存动作只负责写回当前方案的参数；"设为默认"仍由既有 `handleSetDefaultProfile`（`:325-327`，含独立提示语）承担。
- **验收**：① 编辑 `cs2_gamer` 并保存后，`config.json` 的 `default_profile` 不变；② 点击"设为默认"后 `default_profile` 正确变更；③ 保存后灯效立即生效（热重载）。
- **风险**：低。需重新构建 `web/index.html`（`npm run build`，输出目录为 `../web`）。

### R15 `brightness` / `speed_index` 契约  ← **决策点 D1**

- **现状**：前端 `App.jsx:170` 写入 `brightness`、`KeyboardVisualizer.jsx:300-302` 仅作用于 Canvas 预览；C++ 侧 `rule_engine.cpp` 对两者零读取；`Profile`（`include/engine/effect.h:22-26`）无亮度字段。
- **方案 A（实现，推荐）**：`Profile` 增加 `uint8_t brightness = 255;`，`rule_engine` 解析 `brightness`（0.0–1.0 浮点 × 255）；`Profile::Render` 在 base_effect 与 key_overrides 全部写入**之后**做一次统一缩放（`out_frame` 逐通道 `v * brightness / 255`）。`speed_index` 映射为 `period_ms` 的倍率（如 `period_ms * speed_factor / 255`）或在实现前先把该字段从 UI 移除。
- **方案 B（移除）**：从 `frontend` 移除亮度滑块与 `brightness`/`speed_index` 的写入，`config.json` 中删除对应字段。
- **取舍**：方案 A 让 UI 承诺兑现，但会给每帧增加一次 384 字节的缩放遍历（可忽略），且需定义"亮度对 already-dark 灯效的影响"；方案 B 成本更低但不满足 `AGENT.md §5.1` 的可调目标。
- **需你拍板**：选 A 还是 B？（A 的话，`speed_index` 是实现还是移除也要一并定。）

### R13 测试体系修复

- **定位**：`tests/test_gsi_rules.cpp:29/74` 断言依赖不存在的映射；全文件用 `assert`（MSVC Release + `/O2` 下被编译移除）；`CMakeLists.txt` 无 CTest
- **改法**：
  1. 引入 `tests/test_util.h` 提供 `CHECK(cond, msg)` 宏：失败时打印文件/行/表达式并 `++failures`，`main` 末尾 `return failures == 0 ? 0 : 1`；全部 `assert` 替换为 `CHECK`。
  2. 测试基线**与发布配置解耦**：把测试用配置移入 `tests/fixtures/test_config.json`（含 `coding`、`chrome.exe` 等测试所需映射），测试加载该 fixture 而非仓库根的 `config.json`。
  3. `CMakeLists.txt` 增加 `enable_testing()` + `add_test(NAME gsi_rules COMMAND test_gsi_rules)`（工作目录设为 `tests/fixtures`），并把 `test_diag_hook` 标记为不注册的手动诊断工具。
  4. **补充用例**（覆盖本轮发现的缺陷）：`Evaluate` 的 `!=` 返回性（R1 回归护栏）、`period_ms=1` 不崩溃（R16）、键位表 69 键时 `BuildPaddedHardwareTable` 返回失败（R3）、`/web/..%2f` 返回 404（R6，需起服务）。
- **验收**：① `ctest` 一键执行且**故意破坏一个断言时返回非零**（证明校验力真实存在）；② Debug 与 Release 两种配置下结果一致。
- **风险**：低。这是后续所有修复的"安全网"，故排在批次 C 但**建议先于 R12/R14/R15 实施**。

---

## 批次 D —— P2 结构与治理

### R16 参数下界校验统一

- **定位**：`builtin_effects.cpp:250`（`period_ms_/2` 为 0 时除零）；`include/engine/builtin_effects.h:28-146` 各效果的 `period_ms > 0 ? ... : 默认` 只挡 0
- **改法**：抽出 `static constexpr uint64_t ClampPeriod(uint64_t v, uint64_t def, uint64_t min = 33)`，所有效果的构造函数统一走它（下界取 33 ms ≈ 30 FPS，低于此无物理意义）。`CurrentEffect` 内部另加 `const uint64_t half = period_ms_ / 2 ? period_ms_ / 2 : 1;` 的双保险。
- **⚠ 交叉验证补充（报错哲学统一）**：构造函数钳制只作为**防崩溃兜底**，不能让它掩盖配置错误。按 R12 的"显式报错"原则，在 `RuleEngine::LoadConfig`（`rule_engine.cpp:92` 附近解析 `period_ms` 处）对 `period_ms < 33` 或非正整数输出 `LOG_WARN`，明确指出"哪个 profile 的 period_ms 非法、已被钳制为 X"。这样既不因一个笔误拒绝启动，又能让用户看见问题。
- **验收**：① 配置 `period_ms` 为 `0/1/2/33` 时 daemon 均不崩溃且产生合理灯效；② 新增用例覆盖。
- **风险**：极低。

### R17 `.gitignore` 与源码纳管  ← **决策点 D2**

- **定位**：无 `.gitignore`；`git ls-files` 194 项含 `build/` 下 107 项构建产物、6 个 `.exe`、3 个 `.log`、3 个 `.pyc`；且 `include/gsi/`、`src/gsi/`、`tests/`、`frontend/`、`test_cs2_gsi.py` **全部未被跟踪**（全新 clone 无法构建）
- **改法**：
  1. 新增 `.gitignore`：`build/`、`*.exe`、`*.obj`、`*.tlog`、`*.lastbuildstate`、`*.recipe`、`*.pdb`、`__pycache__/`、`*.pyc`、`*.log`、`aura_daemon.log*`、`.daemon_running`、`node_modules/`、`frontend/dist/`、`*.bak`、`hw_test_config.json`、`smoke_test_config.json`。
  2. `git rm -r --cached build/ __pycache__ *.exe *.log` （**只解除跟踪，不删磁盘文件**）。
  3. `git add` 纳管 `include/gsi/`、`src/gsi/`、`tests/`、`frontend/`（排除 `node_modules`）、`test_cs2_gsi.py`、`e2e_key_test.py` 等。
  4. 提交前用 `git status --porcelain` 确认：不再出现 `??` 的核心源码、不再出现 ` M build/...`。
- **需你拍板**：是否执行？该操作会**改变版本库内容**（解除跟踪 + 新增纳管），但不删除任何磁盘文件。当前工作树有 40+ 条 `build/` 变更，操作后 git 状态会显著变干净。
- **验收**：① `git ls-files | grep -c "\.exe$"` 为 0；② 在临时目录 `git clone` 后直接 `cmake` + 构建可成功（证明未跟踪源码已全部纳管）。

### R18 用户本机 CS2 配置补 `bomb`  ← **决策点 D3**

- **定位**：`D:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\cfg\gamestate_integration_aura.cfg` 缺 `"bomb" "1"`（代码模板 `src/web/web_server.cpp:293` 已有）
- **改法**：**优先由你在 WebUI 中点一次"安装 GSI 配置"**（复用已有功能，零风险）；若你希望我直接改，我会先备份原文件为 `.bak` 再补一行 `"bomb" "1"`。
- **需你拍板**：走 WebUI 自助，还是授权我直接改写该文件？（涉及你的 Steam 安装目录）

### R20 构建加固

- **改法**：`/W3` → `/W4`（对 `third_party` 头文件用 `/external:W0` 抑制）；`enable_testing()` 见 R13；新增 `AURA_ENABLE_ASAN` 选项（仅 MSVC Debug 下启用 `/fsanitize=address`）；对 `aura_daemon` 增加 `/permissive-` 试运行。`:47` 的 `CMAKE_BUILD_TYPE` 未设置时给出显式默认值，避免"Debug 构建悄悄变成无优化"或"Release 悄悄定义 NDEBUG 导致 assert 消失"（R13 已从代码侧根治，此处为配置侧兜底）。
- **验收**：`/W4` 下清理所有新增告警（预期会有若干，逐个修复或显式抑制并注明理由）。
- **风险**：中。`/W4` 可能暴露较多既有告警，需要预留处理时间；建议**单独一个提交**，便于回滚。

### R21 Python 工具链治理

- **定位**：18 个根目录脚本各自复制 20–31 处 ctypes/COM 虚表直调逻辑；`set_per_key.py:48-87` 的 `HARDWARE_KEY_MAP` 仍含 `AGENT.md §12.3` 记录的 5 组 LED ID 冲突（I=66 与 ENTER、K=67 与 INS、O=74 与 PGUP/MINUS、L=75 与 DEL、P=82 与 BACKSPACE/EQUAL）
- **改法**（分两步，避免一次性大重构）：
  1. 新建 `tools/py/aura_hal.py`，封装 CLSID/VTable 索引/`0x6C`/`0x74` 偏移与 `CreateLedDevice`/`Set_L_STD_SINGLE_XY`/`Release` 的安全包装；先迁移 `set_per_key.py`、`gui_calibrator.py`、`e2e_key_test.py` 三个最常用脚本，其余脚本加头部注释指向该模块。
  2. `set_per_key.py` 的冲突项：以 `calibrated_keymap.json` 为权威源覆盖冲突条目；无法从权威源确定者**显式标注"仅推导、未实测"**并拒绝用于逐键写灯（改为报错而非静默点错灯）。
- **验收**：① 迁移后的三个脚本行为与迁移前一致（同参对照日志）；② `set_per_key.py` 在冲突键上给出明确报错而非错误点灯。
- **风险**：中。属重构，需逐个脚本对照验证。

### R22 文档合并与勘误

- **改法**：① README 的"平滑关闭耗时 1 毫秒"更正为与日志一致的 14 ms（`AGENT.md §14.2` 已给出结论）；② `AGENT.md` 的 Phase 状态推进到 Phase 3（README 已到位，仅 AGENT.md `:463` 滞后）；③ 把 5 份并行报告（`LIGHTING_FIX_REPORT.md`、`AURA_HARDWARE_VERIFICATION_REPORT.md`、`WORKING_SOLUTION_REPORT111.md`、`request-working-solution-report-prompt.md`、`README_PER_KEY.md`）归并为单一事实来源 + `docs/archive/` 存档；④ 把本方案与三份审查报告一并纳入 `docs/`。
- **验收**：新读者仅凭 README + AGENT.md 即可判断当前完成度与技术边界。
- **风险**：极低。

---

## 批次 E —— 需实测才能定方案

### R19 `CreateLedDevice` 内存泄漏  ← **决策点 D4**

- **已知**：`AGENT.md §13` 实测 0.38–0.49 MB/次，取自 `--test-init 100` 的**成功**路径；用户已决定"刻意接受"（§13.5）。
- **未知**：断连场景走 `aura_adapter.cpp:138-143` 的 `dev_count == 0` 提前返回分支，其每次泄漏量**从未被测量**。外部报告据此推出"1 小时泄漏约 1GB → 必然 OOM"，属未经证实的外推。
- **第一步（判别实验，只需一次）**：
  1. 拔掉键盘（或拨码切至另一台 PC），使 `dev_count == 0`；
  2. 运行 `aura_daemon.exe --test-init 100`；
  3. 观察 `PrivateBytes` 曲线是否仍线性增长。
  - **仍增长** → 外推成立：`CheckReconnect` 必须改为指数退避（1.5 s → 5 s → 15 s → 60 s 封顶）+ 失败次数上限，并把重连升为 P0；
  - **走平** → 外推不成立：维持固定退避，仅补一条注释说明"失败路径不泄漏"的实测依据。
- **第二步（若需根治）**：设计一个小变体压测，区分"泄漏在 HAL 实例（`CoCreateInstance`）"还是"泄漏在设备对象（`CreateLedDevice`）"——若在 HAL 实例，则把 `pHal_` 在重连间**复用**（只在设备层重建）即可大幅降低泄漏速率。
- **需你拍板**：是否安排这次实验？需要你配合拔插/切换键盘。
- **风险**：实验本身不修改任何代码，无风险。

### R23（新增，独立课题）栈伪造 `std::vector` 的替代方案

- `AGENT.md §12.7` 明确要求"正式实现不应照搬"该手法，但现行代码仍在用（`aura_adapter.cpp:116-125`）。R3 只能给它加上界保护，不能消除假设本身。
- **建议**：作为独立课题立项，用"固定输出缓冲区 + 显式长度"或"先用小规模探测确认底层真实行为边界"的方式替换 `FakeVector`。**本方案不纳入本次实施范围**，仅登记待办。

---

## 待你拍板的决策点汇总

| 编号 | 决策点 | 我的建议 |
| :---: | :--- | :--- |
| **D1** | `brightness`/`speed_index`：在引擎实现，还是从 UI/配置移除？ | 实现 `brightness`；`speed_index` 若一并实现则映射为 `period_ms` 倍率，否则先从 UI 移除 |
| **D2** | 是否执行 `.gitignore` + `git rm --cached` + 纳管未跟踪源码？（会改动版本库，不删磁盘文件） | 建议执行，且**单独一个提交**便于回滚 |
| **D3** | 本机 CS2 `gamestate_integration_aura.cfg` 补 `"bomb" "1"`：你走 WebUI 自助，还是授权我直接改（先备份）？ | 建议你走 WebUI 点一次"安装 GSI 配置" |
| **D4** | 是否做内存泄漏判别实验（需你拔插/切换键盘）？ | 建议做，一次实验即可终结"1 小时 OOM"这个悬而未决的外推 |
| **D5** | 实施节奏：是否按批次 A→B→C→D 各出一个提交、每批构建+验收后再进下一批？ | 建议如此，且**批次 A 单独一次提交**（阻断性修复） |
| **D6** | 是否允许我在本机用 MSVC 构建以做验收（`build/` 已有 MSVC 环境）？ | 需要，否则多数修复无法验证 |

---

## 附录：实施方式交叉验证（Antigravity / Gemini 3.8 Flash High）

- **方法**：把每项拟实施方案（含关键代码片段）送 Antigravity 只读 `plan` 模式做**对抗性复核**，要求它只挑错；其每条裁定再回到源码独立复核后才采纳（"对方的输出是待裁定主张，不是结论"）。
- **执行**：2 轮，各 5 项方案，分别耗时 146.9 s / 144.3 s（均 < 300 s 上限）。

### A. 逐项裁定与处置

| 方案 | Antigravity 裁定 | 我的独立复核 | 最终处置 |
| :---: | :--- | :--- | :--- |
| 1 `Evaluate` 重构 + 刷新节流 | 部分通过：无其他重入路径，节流无行为差异；建议改为 `mutable` + `const` 成员函数以移除 `const_cast` | 确认可行（`mutex_` 已是 `mutable`；`flat_state_` 标记 `mutable` 后 `const` 成员函数可写） | ✅ **采纳并增强**（R1 已更新） |
| 2 并发收敛 | **驳回**：`SetSuppressed` 若只随方案名变化调用，当不同进程映射到同名方案时会漏调用 | **确认其正确** —— 我独立核对了 `config.json`：`code.exe` 与 `devenv.exe` **确实都映射到 `coding`**，一旦将来给其中一个加 `suppress_web_ui: true` 即触发该缺陷（`AGENT.md §4.2` 的设计正是给游戏加该标记） | ⚠️ **修正后采纳**（R2 已更新：改为进程名变化即调用） |
| 3 缓冲边界 | 通过：扩缓冲不改变 HID 对齐；128 键表长 137 ≤ 144 | 独立数值验证一致（68→72、69→73、127→136、**128→137**） | ✅ **采纳**（R3 已补推导注释） |
| 4 非 ASCII 路径 | 部分通过：**子进程必须同步改 `wmain`**，否则非 ACP 字符在窄 `argv` 初始化阶段被有损替换为 `?` | 确认（CRT 窄 `argv` 走 ANSI 码页，非 ACP 字符不可逆丢失） | ⚠️ **R8b 升级为必做**（R8 已更新，含 `MoveFileExW`） |
| 5 参数下界 | 通过，但要求统一报错哲学：构造钳制 + `LoadConfig` 告警 | 采纳（与 R12 的"显式报错"原则一致；33 ms 远低于现有最小值 600 ms，无冲突） | ✅ **采纳**（R16 已更新） |
| 6 路径穿越 | **驳回**：`weakly_canonical` 每请求产生磁盘 I/O、平台可移植性差 | **结论成立、但理由有一处高估**：它称"会抛 `filesystem_error` 导致进程崩溃"，实测 httplib 已在 `routing()` 外层包了 `try/catch`（`httplib.h:7236-7255`）→ 会转成 **HTTP 500 而非崩溃**。I/O 与可移植性问题成立 | ⚠️ **改为纯词法检查**（R6 已重写，并补上**绝对路径/盘符/UNC 绕过**这条被双方同时遗漏的路径） |
| 7 CSRF 三层 | 部分可行：第 1 层挡不住 DNS 重绑定；第 2 层漏验 `Host`；`Origin: null` 易被绕过；`install-cfg` 白名单会误杀自定义 Steam 库 | 全部确认（重绑定场景下浏览器可能不发预检；`Origin: "null"` 必须显式拒绝而非按"缺省"放过） | ⚠️ **修正为四层**（R7 已更新，含 `Host` 校验与白名单放宽规则） |
| 8 请求体上限 | 通过但偏宽：10 人 `allplayers` 全量仅 30–50 KB；建议收紧至 256 KB；并指出超限返回 413、请求头已有 8 KB 硬上限 | 全部确认（`httplib.h:4475-4476` 返回 `PayloadTooLarge_413`；`httplib.h:85-86` 的 `CPPHTTPLIB_HEADER_MAX_LENGTH=8192`） | ✅ **采纳收紧版**（R4 已更新为 256 KB + `set_error_handler`） |
| 9 `WorkerLoop` 谓词 | **坚决驳回**：会导致前台切换最长 1.5 s 迟滞；建议改为"快照比对"谓词 | **结论成立、但机制描述有误**（见下 B.2）——原设计的真实后果是只能等满剩余超时，**最长约 1.5 s 迟滞**，属真回归 | ⚠️ **采纳其替代方案**（R5 已重写为快照比对） |
| 10 COM RAII | 通过：栈局部变量逆序析构保证 `ComScope` 晚于 `AuraAdapter` 析构；`S_FALSE` 同样需配对 `CoUninitialize` | 确认（标准保证逆序析构；MSDN 明确 `S_FALSE` 计入引用计数） | ✅ **采纳 + `= delete`**（R9 已更新） |

### B. 对 Antigravity 两处理由的更正（结论对、机制错）

1. **方案 6**：其称 `weakly_canonical`/`relative` 抛异常会"导致进程崩溃"。实测不成立——`httplib.h:7236-7255` 把 `routing()`（含所有 handler）包在 `try/catch (std::exception&)` 中，未注册 `exception_handler_` 时置 `500` 并写入 `EXCEPTION_WHAT` 头。**故"崩溃"应更正为"返回 500"**；驳回理由由"崩溃"降级为"可避免的磁盘 I/O + 平台可移植性"。
2. **方案 9**：其称"谓词为假时 `notify_one()` 会被视为虚假唤醒并继续等待，导致通知被吞掉"。**该机制描述不符合 C++ 标准**：`wait_for(lock, dur, pred)` 等价于 `wait_until(lock, now()+dur, pred)`，后者形如 `while (!pred()) { if (wait_until(lock, abs_time) == timeout) return pred(); }` —— **谓词在每次被唤醒后都会重新求值，通知不会被吞掉**。真实后果是：截止时刻在调用时只算一次，谓词若不含"状态已变化"，线程醒来发现谓词仍为假便继续等待至**原定截止时刻**，因此动作被推迟最长约 1.5 s。**结论（属回归、应驳回）不变，但把机理写准**，否则后续维护者会按错误心智模型改坏这段代码。

### C. 验证净收益

| 项目 | 结果 |
| :--- | :--- |
| 送审方案 | 10 项 |
| 直接通过 | 5 项（1、3、5、8、10） |
| 被驳回并修正 | 4 项（2、6、7、9）——其中 2 项（2、9）若按原设计实施**会引入真实缺陷/回归** |
| 升级范围 | 1 项（4 → R8b 必做） |
| 我方对其理由的更正 | 2 处（6、9） |
| 我方在修正中新补的遗漏 | 1 处（6 的**绝对路径/盘符/UNC** 绕过路径，双方原先都未提及） |

**结论**：10 项实施方案经双向验证后全部收敛，其中 4 项被实质性修正，2 项避免了会被引入的新缺陷。**方案已无已知问题，等待你批准后开始实施。**

---

## 实施记录

### 批次 A —— 已完成并验证（2026-09-10 20:14–20:26）

**改动文件（9 个）**

| 文件 | 对应项 | 变更规模 |
| :--- | :---: | ---: |
| `include/gsi/gsi_adapter.h` | R1 | 16 行 |
| `src/gsi/gsi_adapter.cpp` | R1 | 23 行 |
| `src/main.cpp` | R2 | 36 行 |
| `include/monitor/foreground_monitor.h` | R2 | 6 行 |
| `src/monitor/foreground_monitor.cpp` | R2 | 11 行 |
| `include/aura/aura_types.h` | R3 | 39 行 |
| `include/aura/aura_adapter.h` | R3 | 4 行 |
| `src/aura/aura_adapter.cpp` | R3 | 53 行 |
| `tests/test_gsi_rules.cpp` | R1 护栏 | 新增测试 6 + 失败计数器 |

**改动前快照（回滚手段）**：`G:\Aura-batchA-backup-20260910-201411\`（8 个源文件，已 `cmp` 校验与工作树一致）。
> 之所以需要文件快照：`include/gsi/`、`src/gsi/`、`tests/` 均为**未被 Git 跟踪**的文件，无法用 `git checkout` 回滚。

**构建与验证证据**

| 项 | 证据 |
| :--- | :--- |
| 改动前基线 | `cmake --build`（VS18 2026 / MSVC 19.51）**0 error / 0 warning**；`test_gsi_rules` 退出码 0 但打印 `code.exe -> desktop`（已复现"assert 被 NDEBUG 消除 → 假阳性"） |
| 改动后构建 | **0 error / 0 warning**（与基线一致） |
| R1 修复有效性 | `test_gsi_rules` 退出码 `0x00000000`，24 PASS / 0 FAIL |
| R1 护栏有拦截力 | 受控实验：临时还原 `return !Evaluate(...)` → 进程 **665 ms 内以 `0xC0000409` fail-fast 崩溃**（stdout 截断 320 字节）；恢复修复后立即通过 |
| R1 无回归 | 测试 1–5 输出与基线逐行一致（`cs2_gamer` / `danger_red` / `bomb_pulse` / `TeamRoundVictory` / "8 个完整游戏事件历史"） |
| R2 运行期 | `--dry-run` 12 s：新增日志 `前台进程变更 -> [桌面/未知]` → `[msedge.exe]` 正常触发；WebUI 子进程拉起/平滑停止（14 ms）；GSI 启停正常；**退出码 0**、无 ERROR |
| R3 行为不变 | 真实 68 键：表长 **72**（与改动前一致）、FATAL 0 行 |
| R3 修掉真实越界 | 128 个唯一 `led_id`（表长 **137**、411 字节 > 原缓冲 216 字节）：FATAL 0 行、退出码 0 —— 改动前会越界 **192 字节** |
| R3 编译期证明双向有效 | `static_assert(padded_len(TOTAL_LEDS) <= MAX)`：容量 144 编译通过；**改回原值 72 → `error C2338` 编译失败**（证明原容量确实不足，且该缺陷本可被编译器拦下） |

**顺手修掉的额外问题**：`ForegroundMonitor::current_process_name_` 的竞争（原 #47）与 `cached_proc_name_` 死字段删除，已随 R2 一并完成。R1 同时移除了两处 `const_cast`（原 #18 的一半）。

**验证局限（须如实告知）**：
1. 未做真机硬件验证——批 A 全程 `--dry-run`，`PushFrame` 的真实设备路径未跑（R3 的改动只在容量与校验，不影响帧内容）。
2. 未做线程竞态的工具级检测（MSVC 无 TSan；未跑 Application Verifier）。R2 的竞争消除是**代码级论证**（回调不再触碰任何共享变量 + 访问点加锁）＋ 运行期无异常，而非工具证明。
3. 单元测试仍以"24 项 PASS"计量，但**旧有断言在 Release 下依旧被 `NDEBUG` 消除**——这属 R13 的范围，尚未修。

### 批次 A 之后的下一步

1. **等待用户就 D1 / D2 / D3 拍板**（批次 C、D 需要）：
   - D1 `brightness`/`speed_index`：实现还是移除；
   - D2 是否执行 `.gitignore` + `git rm --cached` + 纳管未跟踪源码（**建议优先做，可让后续改动获得可回滚能力**）；
   - D3 本机 CS2 cfg 补 `"bomb" "1"`：走 WebUI 自助还是授权直改。
2. **批次 B 可立即启动**（不依赖 D1–D3）：R4 请求体上限、R5 监护循环忙等、R6 路径穿越、R7 写接口鉴权、R8 非 ASCII 路径（含 `wmain`）、R9 COM 生命周期、R10 日志轮转、R11 日志级别。
3. R19（内存泄漏测量）**按用户要求排到最后**，由用户手动插拔键盘配合。

## 实施前请确认

1. **决策点 D1–D3** 的取舍（D4/D5/D6 已由"可以"覆盖：D4 排最后且用户手动插拔、D5 分批提交、D6 允许 MSVC 构建验证）；
2. 批次 B 是否现在开始。
