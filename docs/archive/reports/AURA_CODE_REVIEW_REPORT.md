> Historical snapshot. Superseded by [current documentation](../../README.md). Claims and TODOs below describe their original period, not current implementation or verified acceptance.

# Aura 代码仓库深度审查报告

- **审查对象**：`G:\Aura`（ROG Falchion Ace HFX 硬件级单键 RGB 灯效控制系统）
- **审查时间**：2026-09-10
- **审查范围**：`src/`、`include/`（排除 `third_party` 第三方库）、`tests/`、`frontend/src/`、`web/`、根目录 Python 工具链、`CMakeLists.txt`、`config.json`、工程文档
- **问题总数**：75 项（编号 #1–#64，另有 11 项以「—」列出的补充项）— **第一轮普查口径**
- **严重度分布**：严重 1 · 高 15 · 中 26 · 低 33
- **经第三轮交叉验证后的合并口径**：**80 项**（严重 1 · 高 15 · 中 28 · 低 36）——撤回 1 项、收窄 1 项、新增 6 项，详见 **§八**

---

## 一、简明摘要

**核心结论**：架构纪律（主线程独占 COM/HAL、GSI 网络线程与硬件解耦、配置热重载用 `shared_ptr` 防悬垂、Job Object 兜底子进程）设计水平明显高于一般个人项目，且已知风险（内存泄漏、伪造 vector）均有文档留痕。但存在 **1 个会导致整进程死锁的潜伏缺陷、2 处跨线程数据竞争、1 组功能性配置缺失导致"方案静默失效"，以及若干无边界硬件内存访问**。这些问题共同的特点是：**在特定条件下才会暴露，且当前测试无法捕获**。

### 必须优先处理的 4 项

| 优先级 | 问题 | 位置 | 后果 |
| :---: | :--- | :--- | :--- |
| **P0** | `GsiState::Evaluate` 的 `!=` 分支递归自调用，`std::mutex` 重入 | `src/gsi/gsi_adapter.cpp:552-553` | 一旦配置使用 `!=` 运算符，主循环每帧调用该函数 → **整个 daemon 死锁**（无崩溃、无日志，表现为键盘灯效完全冻结） |
| **P0** | `last_proc_name` / `current_active_profile_name` 被监控线程与主循环并发读写，均为非原子 `std::string` | `src/main.cpp:178-193` vs `217-227` | 堆内存撕裂 → **随机崩溃**，且时间点不可复现 |
| **P0** | `ForegroundMonitor::current_process_name_` 跨线程无同步读写 | `src/monitor/foreground_monitor.cpp:118,153` / `:214-216` | 同上；头文件已预留 `cached_proc_name_` 原子缓存却从未使用，说明原设计意图未落地 |
| **P0** | `AuraAdapter::PushFrame` 向 `stream_buffer_[216]` 按 `padded_hardware_table_.size()` 写入，无任何边界校验；同一模式亦见于 `:159-163` 按条目数写 COM 对象内部固定偏移 `0x6C/0x74` | `src/aura/aura_adapter.cpp:222-230`、`:159-163` | 当键位表唯一 `led_id` > 68 个时 **越界写**（当前恰为 68，处于容量临界点）；超出底层内部表容量即破坏 COM 对象内存 |

### 高优先级（功能性静默失效 + 安全）

| 优先级 | 问题 | 位置 | 后果 |
| :---: | :--- | :--- | :--- |
| P1 | **`config.json` 引用了不存在的 profile `coding`**（`code.exe` / `devenv.exe` 规则） | `config.json:219,223`（`profiles` 中无该键，`config.example.json` 中存在） | 两条规则**静默回退**到 `desktop`，"编码工作方案"从未真正生效，且无任何告警日志 |
| P1 | **`brightness` / `speed_index` 字段前端写入、引擎完全忽略**（引擎侧 0 处引用） | `frontend/src/App.jsx:170` / `KeyboardVisualizer.jsx:300-302` vs `src/config/rule_engine.cpp` | 亮度滑块只改变**网页预览**，不改变真实键盘 → 能力与 UI 承诺不符 |
| P1 | **`tests/test_gsi_rules.cpp` 与 `config.json` 不一致，测试必然失败** | `tests/test_gsi_rules.cpp:29`（断言 `code.exe→"coding"`）、`:74`（断言 `chrome.exe→"cyberpunk"`，配置无此规则） | 测试基线与发布配置脱节；且该测试用 `assert`，**Release 构建（`/O2` + MSVC 默认 `NDEBUG`）下断言被编译移除 → 测试静默"通过"、零校验力** |
| P1 | `WinEventHook` 注册失败时 `Stop()` 早退不 join，`std::thread` 析构触发 `std::terminate` | `foreground_monitor.cpp:142-146` + `:200-201` | 监控钩子注册失败场景下，**退出时必然崩溃** |
| P1 | `/web/(.*)` 静态路由未规范化路径，存在**路径穿越** | `src/web/web_server.cpp:51-68` | `GET /web/../config.json` 可读取工作目录及上层任意文件（本地信息泄露；仅 127.0.0.1 可达） |
| P1 | 所有写接口（`POST /api/config`、`/api/gsi/install-cfg`）**无认证、无 Origin 校验** | `web_server.cpp:94-206` | 浏览器内任意网页可向 `127.0.0.1:19898` 发起跨站简单请求改写 daemon 配置（CSRF / 本地 DNS-rebinding） |
| P1 | `CreateProcessW` 命令行用逐字节加宽 `std::wstring(begin,end)` 构造 | `src/supervisor/web_supervisor.cpp:122` | `--config` 路径含**非 ASCII 字符**（如中文目录）时命令行损坏 → 网页服务**永远启动失败** |
| P1 | `AGENT.md §6` 明确要求"日志文件大小/轮转控制"，`Logger` 无任何轮转/上限 | `include/utils/logger.h:28-61` | 常驻进程日志无限 append（`aura_daemon.log` 已 255KB 且跨运行累积）——**未闭环需求** |
| P1 | `CreateLedDevice` 初始化/释放循环线性内存泄漏（0.38–0.49 MB/次，100 次后 `PrivateBytes` 3.07MB→44.30MB） | `aura_adapter.cpp:80-178,297-322` | 设备热插拔重连（双 PC 切换）时累积泄漏；`AGENT.md §13.4` 待办未销项，§13.5 决定"刻意接受" |
| P1 | 照搬 `AGENT.md §12.7` 明文建议"正式实现不应照搬"的**栈伪造 `std::vector<void*>`** 手法 | `aura_adapter.cpp:116-125` + `aura_types.h:34-38` | 底层若触发扩容即崩溃；当前仅有事后边界校验（`vec.first/last/end`），无预防 |
| P1 | 存档的键位表冲突未修复：`set_per_key.py` 中同一 LED ID 分配给多个物理键 | `set_per_key.py:48-87`（I=66 与 ENTER、K=67 与 INS、O=74 与 PGUP/MINUS、L=75 与 DEL、P=82 与 BACKSPACE/EQUAL） | 默认走该校验表会**点错灯**；`AGENT.md §12.3` 已记录但未闭环 |

### 建议修复顺序

1. **阻断性正确性**：`Evaluate` 死锁 → 三处数据竞争 → 硬件缓冲区边界断言
2. **功能闭环**：补齐 `coding` profile 与 `brightness` 实现（或从 UI/配置中移除）→ 修复测试与配置的一致性 → 测试断言改为显式失败（`assert` → 计数 + 非零退出码）
3. **安全与健壮性**：路径穿越与写接口鉴权 → 非 ASCII 命令行 → 日志轮转
4. **工程治理**：补 `.gitignore`、停止跟踪构建产物、Python 脚本抽取共享 HAL 模块、合并重复文档

---

## 二、未完成事项清单

| # | 类型 | 位置 | 内容 |
| :---: | :--- | :--- | :--- |
| U1 | 占位代码 | `hw_effect_test.py:94` | `dev_count = "device_count"  # 占位` —— 死变量，设备数提取退化为字符串匹配 |
| U2 | 未闭环需求 | `include/utils/logger.h` | `AGENT.md §6` 要求日志轮转，实现缺失 |
| U3 | 未闭环待办 | `aura_adapter.cpp:80-178` | `AGENT.md §13.4`：修复 `pDev`/`pHal` 释放阶段缺失的 `Release()`、重跑 `--test-init 100` 验收 |
| U4 | 未闭环冲突 | `set_per_key.py:44-87` | `AGENT.md §12.3` 记录的 5 组 LED ID 冲突仍在代码中 |
| U5 | 未实现能力 | `rule_engine.cpp` 全部 | `brightness`、`speed_index` 配置字段无任何读取逻辑（前端已提供 UI） |
| U6 | 未实现能力 | `AGENT.md §5.2` | 局域网/手机访问（含访问口令）、手动总开关——按设计"暂不做"，但若开放则当前无任何鉴权框架 |
| U7 | 未集成构建 | `frontend/` + `CMakeLists.txt` | 前端为独立 Vite 工程，CMake 无前端构建目标；`web/index.html` 为手工提交的构建产物，易与 `frontend/src` 漂移 |
| U8 | 未接入测试框架 | `CMakeLists.txt`（无 `enable_testing`/`add_test`） | `tests/` 未接入 CTest，无 CI 一键执行入口 |
| U9 | 调试残留 | `tests/test_diag_hook.cpp`、根目录 18 个 `test_*.py` | 一次性诊断程序与探索脚本长期驻留仓库 |
| U10 | 文档勘误未改 | `README.md`（`AGENT.md §14.2` 已指出） | 声称"平滑关闭耗时 1 毫秒"，与日志实测 14ms 不一致 |
| U11 | 死代码 | `main.cpp:160-164` | `if (!gsi_adapter.Start(19897))` 分支不可达（`Start` 恒返回 `true`，`listen` 在子线程内失败），端口占用告警形同虚设 |
| U12 | 死配置 | `config.json`（`desktop` profile） | `"speed_index": 2` 无消费者 |

---

## 三、逐模块问题明细

### M1 守护进程主控 — `src/main.cpp`

| # | 严重度 | 位置 | 缺陷 | 影响范围 |
| :---: | :---: | :--- | :--- | :--- |
| 1 | **高**（范围已收窄，见 §八） | `169` 定义 / `180` 捕获 / `185, 192`（监控线程）vs `221-222, 247, 253`（主循环） | **`current_active_profile_name`** 为普通 `std::string`，被监控线程回调与主循环同时读写 → 数据竞争。~~`last_proc_name` 同样竞争~~ → **经交叉验证更正：`last_proc_name` 不构成竞争**（仅在 `178` 定义、`180` 引用捕获、`181-182` 在回调内读写；主循环 214–298 行对其零引用，访问全部发生在监控线程内部） | 数据竞争 → 堆撕裂 / 随机崩溃 / 灯效方案错乱；无法稳定复现 |
| 2 | 中 | `74-77` | `std::stoi` / `std::stod` 解析 `--test-init`、`--test-stability` 无异常保护 | 传入非法参数（如 `--test-init abc`）→ 未捕获 `std::invalid_argument` → `std::terminate` 直接崩溃 |
| 3 | 中 | `56-57` | 硬编码 `C:\Program Files\ASUS\Aac_Keyboard`；`SetDllDirectoryW` 修改全局 DLL 搜索路径；`hHalPreload` 返回值未检查、失败无日志、句柄不释放 | 非 C 盘安装 / 未安装华硕组件时静默退化为"找不到设备"，排障困难；DLL 搜索路径被全局篡改属安全隐患 |
| 4 | 中 | `60, 343-346` | `CoInitialize(NULL)` 返回值未检查；`AuraAdapter::com_initialized_` 恒为 `false`（全工程无赋值点） | `CoUninitialize()` 永不执行 → COM 未反初始化；`RPC_E_CHANGED_MODE` 等失败被忽略 |
| 5 | 中 | `243-256` + `rule_engine.cpp:220-232` | 配置**存在但解析失败**时 `last_write_time_` 未更新 → `CheckAndReload` 每秒重试一次并重复打印错误 | 日志刷屏、持续无效 I/O；且用户无法区分"语法错误"与"文件缺失" |
| 6 | 低 | `214-298` | 主循环每帧调用 `RuleEngine::MatchProfile`（加锁 + 遍历 GSI 绑定，内层每次 `Evaluate` 再加锁） | 25FPS 下可接受，但持锁粒度为双层嵌套，锁竞争面偏大 |
| 7 | 低 | `70-86` | 未知命令行参数被静默忽略，无校验提示 | 参数拼写错误（如 `--dryrun`）时用户误以为已生效 |

### M2 硬件适配层 — `src/aura/aura_adapter.cpp` / `include/aura/*`

| # | 严重度 | 位置 | 缺陷 | 影响范围 |
| :---: | :---: | :--- | :--- | :--- |
| 8 | **高（潜在）** | `222-230` | `stream_buffer_` 固定 216 字节（72 槽），循环上界为 `padded_hardware_table_.size()`，**无容量断言** | 唯一 `led_id` > 68 时越界写（当前 `calibrated_keymap.json` 恰为 68 个唯一 ID → 表长 72，正好占满，零余量）。自定义/换键盘键位表即触发栈对象内存破坏 |
| 9 | **高（潜在）** | `159-163` | 以 `padded_hardware_table_.size()` 为长度写入 COM 对象内部固定偏移 `+0x6C`（长度字段）与 `+0x74`（表体） | 底层内部表容量未知且不可校验；条目数超出即破坏 COM 对象内存。属逆向实现固有风险，但当前无任何防御 |
| 10 | **高（已知）** | `116-125` | 栈伪造 MSVC `std::vector<void*>` ABI（`FakeVector` + `dev_storage[64]`）传参，`AGENT.md §12.7` 明示"正式实现不应照搬" | 底层触发扩容 → 写栈越界崩溃；现有 `vec.first/last/end` 校验只能事后发现问题，无法阻止 |
| 11 | **高（已知）** | `80-178` / `297-322` | `CreateLedDevice` 循环线性内存泄漏（实测 0.38–0.49 MB/次，无收敛迹象） | 每次设备重连泄漏；双 PC 频繁切换场景累积。`AGENT.md §13.4` 待办未销项 |
| 12 | 中 | `196, 259` | `ForceReset` 在未连接时直接返回 `false`；异常退出兜底的复位可能被跳过 | `main.cpp:153-155` 调用后不检查返回值 → 上次崩溃残留的"脏光效"未被清除 |
| 13 | 中 | `279-295` | 重连退避固定 1.5s，无指数退避、无尝试上限 | 键盘长期不在位时每 1.5s 打一条日志并完整走一遍失败路径 |
| 14 | 低 | `100, 269` | `LOG_*("0x" + std::to_string(hr))`：`HRESULT` 按十进制输出且负值显示异常，前缀 `0x` 具误导性 | 排障时错误码无法直接与文档/MSDN 对照 |
| 15 | 低 | `352-399` | `RunInitStressTest` 注释声称"内存安全压测"但无内存泄漏断言，仅输出曲线供人工判读 | 压测结论依赖人工看日志，无法自动判定通过/失败 |
| 16 | 低 | `36-45` | 回退时硬编码读取工作目录下 `calibrated_keymap.json` | 依赖 cwd，从其他目录启动时静默走空表 |

### M3 GSI 适配器 — `src/gsi/gsi_adapter.cpp` / `include/gsi/gsi_adapter.h`

| # | 严重度 | 位置 | 缺陷 | 影响范围 |
| :---: | :---: | :--- | :--- | :--- |
| 17 | **严重** | `552-553` | `op == "!="` 分支实现为 `return !Evaluate(field, "==", target_val);` —— 外层已持 `std::lock_guard<std::mutex>`，递归进入后再次加锁 | `std::mutex` 不可重入 → **死锁 / 未定义行为**。主循环每帧经 `RuleEngine::MatchProfile` 调用 `Evaluate`，一旦配置出现 `!=` → daemon 完全冻结（无崩溃、无日志）。当前 `config.json` 未使用 `!=`，属潜伏缺陷 |
| 18 | 中 | `470-476, 564-569` | `Evaluate` / `ToJson` 为刷新事件脉冲用 `const_cast<GsiState*>(this)` 调用 `SyncEventFieldsToFlatState`，在**持锁状态下**对 `flat_state_` 做全量写入（十余个 `operator[]`） | 破坏 `const` 语义；热路径（主循环每帧、前端高频轮询）持锁时间被拉长，锁竞争加剧 |
| 19 | 中 | `564-625` | `ToJson` 序列化**整个** `flat_state_`（含 `allplayers.*` 全量字段）+ 50 条事件历史 | 单次响应可达数十 KB；前端轮询 `/api/gsi/current`（经 19898 代理）时放大 CPU 与网络开销 |
| 20 | 中 | `62-162` | `UpdateFromPayload` 对 payload 每个顶层分类先全量 `erase` 再重建（O(全部字段)），GSI 心跳 `buffer/throttle=0.1s` 时高频执行 | 网络线程长时间持锁；与主循环 `MatchProfile` 争锁 |
| 21 | 低 | `627-632, 606-609` | `IsActive` / `ToJson` 使用 `now_ms - last_update_ms_` 无下溢守卫（`system_clock`） | 用户回拨系统时间时差值异常；`IsActive` 误判离线 |
| 22 | 低 | `462-468` | `IsEventActive` 与 `Evaluate` 对同一事件采用两套独立的时间判断代码路径 | 逻辑重复，维护时易不同步 |
| 23 | 低 | `713-737` | `Start()` 在 `listen` 尚未执行时就置 `is_running_ = true` 并返回 `true` | 调用方无法同步感知端口占用（详见 U11） |
| 24 | 低 | `164-165` | `DetectGameEvents` 形参 `payload`、`now_ms` 均 `(void)` 弃用 | 接口残留，暗示原设计有两参数依赖 |

### M4 规则引擎 — `src/config/rule_engine.cpp` / `include/config/rule_engine.h`

| # | 严重度 | 位置 | 缺陷 | 影响范围 |
| :---: | :---: | :--- | :--- | :--- |
| 25 | **高** | `config.json:219, 223` | 规则引用 profile `coding`，但 `config.json` 的 `profiles` 中**不存在** `coding`（`config.example.json` 中存在） | `code.exe` / `devenv.exe` 规则静默回退 `desktop`；"编码模式"从未生效；无告警日志 |
| 26 | **高** | `rule_engine.cpp`（全文） | `brightness`、`speed_index` 字段无任何解析逻辑（引擎侧 0 处引用），前端却提供亮度滑块并写入配置 | 用户调节亮度只影响网页预览，真实键盘无变化 → 功能承诺不兑现 |
| 27 | 中 | `277-285` | profile 未找到时静默回退到 `default_profile`，再退化为"任意第一个 profile"，无任何日志 | 配置拼写错误（如 `offcie`）被完全掩盖 |
| 28 | 中 | `81-179` | 未知 `type` 值时 `base_effect` 保持 `nullptr` → `Profile::Render` 静默输出全黑 | `"type": "breathig"` 之类笔误表现为"键盘全黑"，无错误提示 |
| 29 | 中 | `209-212` | 在 `mutex_` 作用域**之外**读取 `rules_.size()` / `gsi_bindings_.size()` / `profiles_.size()` 打印 | 与并发 `MatchProfile` 读同一容器 → 数据竞争（虽仅影响日志，但属 UB） |
| 30 | 低 | `220-232` | 热重载使用 `FILETIME` 精确比较，且失败时不更新 `last_write_time_` | 解析失败时每秒重试（见 #5） |
| 31 | 低 | `234-271` | 同一次匹配中对 `lower_proc` 与各规则做 `==`、去 `.exe` 后缀两轮比较，逻辑重复 | 可维护性；新增匹配维度（如路径、窗口类）时易漏改 |

### M5 网页配置服务 — `src/web/web_server.cpp` / `src/web/main_web.cpp`

| # | 严重度 | 位置 | 缺陷 | 影响范围 |
| :---: | :---: | :--- | :--- | :--- |
| 32 | **高** | `51-68` | `std::filesystem::path("web") / subpath`，`subpath` 取自正则 `(.*)`，未做规范化 / 白名单校验 | **路径穿越**（CWE-22）：`GET /web/../config.json`、`/web/../../…` 可读取工作目录及上层任意相对路径文件。HTTPS 层无鉴权，仅靠 127.0.0.1 绑定限制暴露面 |
| 33 | **高** | `94-206` | `POST /api/config`、`POST /api/gsi/install-cfg` 无认证、无 `Origin`/`Referer` 校验、无 CSRF token | 浏览器内任意网页可向 `127.0.0.1:19898` 发起跨站简单请求改写配置；配合 DNS-rebinding 可进一步扩大 |
| 34 | 中 | `163-206` | `install-cfg` 接受请求体中任意 `target_dir` 并写入固定文件名 | 受限的任意目录写（文件名固定为 `gamestate_integration_aura.cfg`，危害有限）；建议限定在检测到的 CS2 cfg 白名单内 |
| 35 | 中 | `221-239` | `LoadHtmlContent()` 每次 `GET /` 都重新读取 401KB 的 `web/index.html`，无缓存 / ETag / mtime 比对 | 每次刷新页面都触发整文件磁盘读；候选路径含 `../`、`../../`，部署位置敏感 |
| 36 | 中 | `251-273` | 原子写失败后回退为**非原子截断写**（`ofstream trunc`） | 回退路径下若写入中断 → 配置文件损坏，daemon 热重载读到半截 JSON |
| 37 | 中 | `241-273` | `file_mutex_` 仅保护本进程内读写，daemon 侧读取 `config.json` 无任何锁 | 与 #36 叠加形成"web 写 / daemon 读"竞态窗口 |
| 38 | 低 | `98-102` | 仅校验顶层存在 `profiles` 与 `rules` 两个键，不校验内层结构 | 可通过 API 存入结构合法但语义错误的配置（如 `period_ms: 1`，见 #46） |
| 39 | 低 | `12-31` | 内置兜底 HTML 硬编码端口 19898 文案；`/api/gsi/current` 代理硬编码 127.0.0.1:19897 | 端口改为可配置后文案与代理失配 |
| 40 | 低 | `307-366` | CS2 路径探测硬编码 9 个盘符前缀 + 单条 Steam `libraryfolders.vdf` 固定路径 | 自定义库路径 / 多盘 Steam 库遗漏 |

### M6 网页服务监护 — `src/supervisor/web_supervisor.cpp`

| # | 严重度 | 位置 | 缺陷 | 影响范围 |
| :---: | :---: | :--- | :--- | :--- |
| 41 | **高** | `122` | `std::wstring wcmdline(cmdline.begin(), cmdline.end())` —— 逐字节加宽，非 Latin-1 字符被破坏 | `--config` 路径含中文/emoji 等非 ASCII 字符时命令行损坏 → `CreateProcessW` 失败 → 网页服务**永久无法启动**，且退避 3 次后静默 30s |
| 42 | 中 | `230-238` | 子进程异常退出的探针分支只递增 `consecutive_failures_`，**不进入退避** | 子进程崩溃循环时每 1.5s 重启一次，退避机制（仅 `StartChildProcess` 失败路径生效）被绕过 |
| 43 | 中 | `174-219, 230-233` | `pi_`（`PROCESS_INFORMATION`）由工作线程写、`Shutdown()` 主线程读，无同步保护 | 跨线程可见性未定义；极端时序下句柄泄漏或重复释放 |
| 44 | 低 | `163-165` | `AssignProcessToJobObject` 返回值未检查 | 若子进程已在其他 Job 内（Windows 7 无嵌套 Job）则静默失败，孤儿兜底失效 |
| 45 | 低 | `39-41` | `Shutdown()` 以 `stop_requested_.exchange` 早退去重，但 `~WebUiSupervisor` 又调用一次 `Shutdown()` 后 `CloseHandle(hJob_)` | 逻辑正确但依赖调用次数；`StopChildProcess` 被调用三次（WorkerLoop 末尾 / Shutdown / 析构路径） |
| 46 | 低 | `104-111, 154-158` | 两处退避逻辑完全重复（找不到 exe / CreateProcess 失败） | 可维护性 |

### M7 前台监控与按键采集 — `src/monitor/*`

| # | 严重度 | 位置 | 缺陷 | 影响范围 |
| :---: | :---: | :--- | :--- | :--- |
| 47 | **高** | `foreground_monitor.cpp:118, 153` 写 / `:214-216` 读 | `current_process_name_`（`std::string`）由监控线程写、主线程 `GetCurrentProcessName()` 读，无任何同步 | 数据竞争 → 撕裂读 / 崩溃。头文件 `:45` 已声明 `cached_proc_name_`（原子缓存）但**全工程无使用点**，原设计意图未落地 |
| 48 | **高** | `:142-146` + `:200-201` | `SetWinEventHook` 失败时线程置 `running_=false` 并返回；`Stop()` 首行 `if (!running_) return;` 早退，**不 join** | `thread_` 保持 joinable → `ForegroundMonitor` 析构时 `std::thread::~thread` → **`std::terminate`** |
| 49 | 中 | `:196, 205` | `thread_id_` 为非原子 `DWORD`；`Start()` 后立即 `Stop()` 时可能仍为 0 | `PostThreadMessage(0, WM_QUIT, …)` 将消息投递到当前桌面**所有线程**，产生意外副作用 |
| 50 | 中 | `key_input_hub.h:10-21` | 全局 `WH_KEYBOARD_LL` 钩子记录**所有**按键（含密码/隐私输入），且在回调内做 `std::mutex` 加锁 + `std::vector` 追加 | 隐私合规风险（需在文档/隐私说明中披露）；低层钩子回调内做堆分配与加锁违反"快速返回"最佳实践，可能引入输入延迟 |
| 51 | 低 | `key_input_hub.h:32-34` | `events_.erase(begin, begin + (size - MAX))` 为 O(n) 搬移 | 已有 64 条上限保护，影响可忽略 |
| 52 | 低 | `:159-172` | `SetWindowsHookExW` 第二次尝试传 `NULL` 作为 `hMod` 的兜底分支未区分失败原因 | 失败仅告警；按键响应类灯效（reactive/ripple/analog）静默失效 |
| 53 | 低 | `:125-146` | hook 失败路径未清理已创建的消息队列与 `hook_handle_`，也未向上层回报 | `main.cpp` 仍认为监控在运行 |

### M8 效果引擎与内置灯效 — `src/engine/*` / `include/engine/*`

| # | 严重度 | 位置 | 缺陷 | 影响范围 |
| :---: | :---: | :--- | :--- | :--- |
| ~~54~~ | ~~中~~ | — | **已撤回（经交叉验证）**：原判"响应类灯效残留上一帧颜色造成串色"**不成立**。`PushFrame`（`aura_adapter.cpp:222-230`）只读取 `padded_hardware_table_` 内的 68 个标定 `led_id`，其余 60 个槽位**从不被读取、不可能发往键盘**；且 `RippleEffect` 确实在 `builtin_effects.cpp:174` 调用 `Fill`（原判误归为"未 Fill"），`ReactiveEffect` 在 `else` 分支写 `base_color_`，其余灯效对每个 keymap 键无条件 `SetKey`。原判只审查了"写入端"而忽略了"发送端过滤"。降级为潜在项（仅当键位表被裁剪到少于实际 LED 数时才可能显现） | — |
| 55 | 中 | `builtin_effects.cpp:250` | `CurrentEffect` 使用 `period_ms_ / 2` 作模数；构造函数仅保证 `period_ms_ >= 1` | 配置 `"period_ms": 1` → `elapsed_ms % 0` **除零 UB**（输入驱动的崩溃） |
| 56 | 中 | `builtin_effects.cpp:114-115` | `WaveEffect` 归一化硬编码 `/ 15.0`、`/ 4.0`（假设 16 列 × 5 行） | 与实际键位表 `physical_col/row` 范围不绑定；换键盘 / 改键位表后波形错位 |
| 57 | 低 | `builtin_effects.cpp:215, 272` | `StarryNight` / `Raindrop` 用 `(col*k + row*m) % N` 作"随机"种子，图案完全确定 | 与"随机闪烁/随机激荡"命名不符（同一按键每次开机相位相同） |
| 58 | 低 | `effect_engine.cpp:18-28` | `GetElapsedMs()` 为引擎全局时间，切换 profile 不复位相位 | 灯效切换瞬间相位跳变，无淡入/过渡，观感突兀 |
| 59 | 低 | `effect_engine.h:26` | `GetActiveProfileCopy()` 每帧加锁拷贝 `shared_ptr` | 每帧一次 `mutex` 加解锁；可改为原子 `shared_ptr` 或 seqlock 优化 |
| 60 | 低 | `builtin_effects.cpp:74-78, 138-142` | 衰减表 `analog_decays_` / `key_decays_` 条目只递减不清除，长期保留 0 值条目 | 条目数受键名集合限制（≈40），无泄漏，但属可清理的冗余状态 |
| 61 | 低 | `include/engine/builtin_effects.h:161-163` | `CustomKeymapEffect` 忽略 `elapsed_ms` 与 `keymap`，仅 `Fill` 背景色 | 逐键颜色完全依赖 `Profile::key_overrides`，无动画能力——第二期"可视化逐键编辑器"（`AGENT.md §5.1`）尚缺动画帧支持 |

### M9 构建、测试与仓库治理

| # | 严重度 | 位置 | 缺陷 | 影响范围 |
| :---: | :---: | :--- | :--- | :--- |
| 62 | **高** | `tests/test_gsi_rules.cpp:29, 74` + CMake `/O2` | 断言依赖 `config.json` 中**不存在**的 `coding`、`chrome.exe` 映射 → 必然失败；且使用 `assert`，MSVC Release（`/O2`，默认 `NDEBUG`）下**断言被全部编译移除** | 测试既**跑不通**又**无校验力**；`tests/` 未接入 CTest（U8）→ 实际上没有任何自动化质量门 |
| 63 | 中 | 仓库根 | **无 `.gitignore`**；`git ls-files` 共 194 个文件，其中 `build/` 下 107 个（47 个 `.tlog`、12 个 `.obj`、7 个 `.lastbuildstate`）、6 个 `.exe`、3 个 `.pyc`、3 个 `.log` 被跟踪 | 二进制污染版本库、diff 噪声、可能泄露本地构建路径与调试信息；`git status` 长期处于大面积修改状态（本次审查开始时即有 40+ 条 `build/` 变更） |
| 64 | 中 | 根目录 | 提交了 `web/index.html.bak`（93KB 陈旧构建产物）、`aura_daemon.log`（255KB）、`test_init_100.log`（42KB）、`dump_fn5.txt`（58KB） | 仓库体积与噪声；日志含运行环境信息 |
| — | 低 | `CMakeLists.txt` | 未设置 `/W4`、未开启 ASan/UBSan 选项、未定义 `NDEBUG` 策略、`test_com` 目标无测试语义 | 编译期静态检查能力不足；本次审查发现的除零、缓冲区边界、符号比较类问题本可被工具提前拦下 |
| — | 低 | `test_com.cpp` | 与 `aura_adapter.cpp` 重复实现 HAL 探测逻辑（CLSID/虚表/0x6C/0x74） | 两处需同步维护 |

### M10 Python 校准与测试工具链（根目录 34 个 `.py`，其中 18 个含 HAL 调用）

| # | 严重度 | 位置 | 缺陷 | 影响范围 |
| :---: | :---: | :--- | :--- | :--- |
| — | **高（可维护性）** | 18 个脚本（`calibrate_keys.py`、`gui_calibrator.py`、`set_per_key.py`、`test_method19_*.py` 等，每脚本 20–31 处 `ctypes`/`CoCreateInstance`） | 各自复制实现同一套 COM 虚表直调逻辑（CLSID、VTable 索引、`+0x6C/+0x74` 偏移），**无共享模块** | 任何一处逆向发现（vtable 索引变更、边界修正）需同步改 18 处；极易与 C++ 实现漂移；是本次审查中风险最集中的"技术债区" |
| — | **高（正确性）** | `set_per_key.py:48-87` | `HARDWARE_KEY_MAP` 含 5 组 LED ID 冲突（I=66 与 ENTER、K=67 与 INS、O=74 与 PGUP/MINUS、L=75 与 DEL、P=82 与 BACKSPACE/EQUAL） | 默认走该校验表逐键设色会点错物理键；`AGENT.md §12.3` 已证伪"100% 实测标定"的文件头声明但代码未修复 |
| — | 低 | `hw_effect_test.py:94` | 占位变量 `dev_count` 未使用 | 设备数校验逻辑形同虚设（见 U1） |
| — | 低 | `hw_effect_test.py:24` vs `smoke_test_effects.py:13` | 可执行文件路径不一致（根目录 `.exe` vs `build/Release/.exe`） | 构建目录变更后其中一个脚本静默失败 |
| — | 低 | `hw_effect_test.py:50-61` 等 | 非 dry-run 脚本直接向真实硬件推流，仅以日志文本判定通过；`hw_effect_test.py` 头部注释已提示需靠"限制用例数"规避内存泄漏 | 验证手段依赖日志字符串匹配，脆弱 |
| — | 低 | `e2e_key_test.py:30-42`（正则解析）等 | 多个脚本以正则匹配日志文本判定结果 | 一旦日志格式微调，全部测试失效 |

### M11 文档一致性与单一事实来源

| # | 严重度 | 位置 | 缺陷 | 影响范围 |
| :---: | :---: | :--- | :--- | :--- |
| — | 中 | 根目录 | 5 份内容重叠的并行报告：`README.md`、`AGENT.md`、`LIGHTING_FIX_REPORT.md`、`AURA_HARDWARE_VERIFICATION_REPORT.md`、`WORKING_SOLUTION_REPORT111.md`、`request-working-solution-report-prompt.md`、`README_PER_KEY.md` | 结论互相覆盖，新人无法判断哪份是当前事实来源；`AGENT.md` 自身即记录了"此前报告结论不成立，需要修正" |
| — | 低 | `README.md` | "平滑关闭耗时: 1 毫秒"与日志实测 14ms 冲突（`AGENT.md §14.2` 已给出勘误结论） | 文档可信度 |
| — | 低 | `AGENT.md §14.2` | 明确记录"快速切换压力测试只做了 3 次，样本量偏少" | 端口释放竞争场景验证强度不足 |

---

## 四、测试覆盖评估

| 维度 | 现状 | 缺口 |
| :--- | :--- | :--- |
| 单元测试框架 | 无（2 个手写 `main()` + `assert`） | 未接入 CTest / GoogleTest；Release 构建下 `assert` 被移除（#62） |
| `GsiState::Evaluate` | 间接覆盖 `==`（布尔/数值/字符串） | **未覆盖 `!=`**（正是 #17 死锁路径）、`<`/`<=`/`>`/`>=` 的边界、`contains`、别名查找、缺失字段、类型不匹配 |
| `RuleEngine` | 覆盖进程匹配 + GSI 绑定优先级 + "非 CS2 时绑定不生效"红线 | 断言依赖不存在的 profile（#62）；未覆盖：无 `default_profile`、profile 名拼写错误、热重载失败重试、去 `.exe` 后缀分支 |
| `AuraAdapter` | 无自动化测试（依赖真实硬件） | 缓冲区边界（#8）、`padded_hardware_table_` 构造逻辑（本可纯函数化单测）、重连退避 |
| 内置灯效 | `smoke_test_effects.py`（dry-run 日志正则） | 未覆盖解析类回归（残影 #54）、除零（#55）、相位/方向正确性 |
| `WebServer` | 无 | **路径穿越（#32）**、跨站写（#33）、非原子回退（#36）、代理超时降级 |
| `WebUiSupervisor` | 无 | 非 ASCII 命令（#41）、崩溃循环退避（#42）、Job Object 兜底 |
| 并发 / 竞态 | 无 | #1、#47 属典型 TSan/压力测试可捕获项，当前完全无覆盖 |
| 配置健壮性 | 无 | 非法 `type`、`period_ms: 0/1`、超范围颜色、缺字段、超大配置 |

**结论**：测试覆盖严重不足，且现有测试**既不能通过、也不具备校验能力**。在修复 #62 之前，该仓库实际上没有可用的自动化质量门。

---

## 五、影响范围矩阵（按模块 × 严重度）

| 模块 | 严重 | 高 | 中 | 低 | 小计 |
| :--- | :---: | :---: | :---: | :---: | :---: |
| M1 守护进程主控 | 0 | 1 | 4 | 2 | 7 |
| M2 硬件适配层 | 0 | 4 | 2 | 3 | 9 |
| M3 GSI 适配器 | 1 | 0 | 3 | 4 | 8 |
| M4 规则引擎 | 0 | 2 | 3 | 2 | 7 |
| M5 网页配置服务 | 0 | 2 | 4 | 3 | 9 |
| M6 网页服务监护 | 0 | 1 | 2 | 3 | 6 |
| M7 前台监控/按键 | 0 | 2 | 2 | 3 | 7 |
| M8 效果引擎/灯效 | 0 | 0 | 3 | 5 | 8 |
| M9 构建/测试/仓库 | 0 | 1 | 2 | 2 | 5 |
| M10 Python 工具链 | 0 | 2 | 0 | 4 | 6 |
| M11 文档一致性 | 0 | 0 | 1 | 2 | 3 |
| **合计** | **1** | **15** | **26** | **33** | **75** |

> 注：M2 的 4 项"高"中有两项（#10 栈伪造 vector、#11 内存泄漏）在 `AGENT.md` 中已被明确记录为**已知且刻意接受**的风险；M10 的 2 项"高"为可维护性/正确性风险。计为"严重"的唯一项是 M3 的 `Evaluate` 死锁（#17）。

---

## 六、建议的验收标准（修复后）

1. **死锁**：新增 `Evaluate` 的 `!=` 用例，断言在 2s 内返回（可用 `std::async` + `wait_for` 守护测试）并通过。
2. **竞态**：三处跨线程共享状态改为原子或加锁后，在 `-fsanitize=thread`（或 Windows 下 Application Verifier + 高频前后台切换压测）下无告警。
3. **边界**：`BuildPaddedHardwareTable` 增加 `static_assert`/运行期断言 `table.size() * RGB_CHANNELS <= HARDWARE_STREAM_BUFFER_SIZE`，并对超限键位表给出明确失败返回。
4. **功能闭环**：`config.json` 的每条 `rules[].profile` 与每个 `gsi_bindings[].profile` 都能在 `profiles` 中解析到；解析失败时 `LoadConfig` 返回 `false` 并输出 error 级日志（而非静默回退）。
5. **前端契约**：`brightness`/`speed_index` 要么在引擎中实现，要么从 `frontend` 与 `config.json` 中移除。
6. **测试**：`assert` 全部替换为显式的失败计数 + 非零退出码；`CTest` 可一键执行 `test_gsi_rules`；测试基线配置与发布 `config.json` 解耦（使用 `tests/` 下的专用 fixture）。
7. **仓库**：新增 `.gitignore`（`build/`、`*.exe`、`*.obj`、`*.tlog`、`__pycache__/`、`*.pyc`、`*.log`、`node_modules/`），并将已跟踪的构建产物执行 `git rm --cached`。
8. **安全**：`/web/` 路由改为白名单/规范化校验；写接口至少校验 `Origin` 为同源并考虑加本地口令（`AGENT.md §4.4` 已预告此要求）。
9. **健壮性**：`Logger` 增加大小上限与轮转（`AGENT.md §6`）；`CurrentEffect` 对 `period_ms_ < 2` 兜底；响应类灯效统一 `Clear()` 后再 `SetKey`。
10. **可维护性**：Python 侧抽取共享 `aura_hal.py` 模块，消除 18 份重复实现；`set_per_key.py` 的冲突 LED ID 依据权威键位表校正或明确标注为"仅推导、未实测"。

---

## 七、第二轮交叉核查补充（外部报告交叉验证后新增）

> 本节为对另一份外部 Review 报告（`repository_review_report.md`）逐条核查后**新确认**的发现，前六节的 75 项统计未包含本节。完整核查过程与逐条判定见 `G:\Aura\REPORT_CROSSCHECK.md`。

| # | 严重度 | 类型 | 位置 / 事实 | 说明与影响 |
| :---: | :---: | :--- | :--- | :--- |
| 65 | **高** | 工程纳管 | `include/gsi/`、`src/gsi/`、`tests/`、`frontend/`、`test_cs2_gsi.py` **全部未被 Git 跟踪**（`git ls-files` 对这些路径返回 0 项） | ① Phase 3（GSI 适配器 + 事件引擎）与全部测试**不在版本控制内**；② `CMakeLists.txt`（已跟踪）引用了这些文件 → **全新 clone 直接构建失败**。第 #63 项的统计口径（194 个跟踪文件）低估了该风险：问题不只是"多跟踪了构建产物"，而是"**漏跟踪了核心源码**" |
| 66 | **高** | 环境一致性 | 用户本机 `D:\SteamLibrary\...\game\csgo\cfg\gamestate_integration_aura.cfg`（750B，2026-08-30 11:35）**缺 `"bomb" "1"`**，而代码模板 `web_server.cpp:293` 已包含 | `bomb.state` / `round.bomb` / `event.bomb_*` 永不到达 → `config.json` 的 `bomb_pulse` 绑定在实机对局中**永不激活**。`README.md:333` 宣称"CFG 模板已集成 bomb 节点"，用户会误判为已生效。修正只需经 WebUI 重装 cfg 或手工补一行 |
| 67 | **中** | 前端契约 | `frontend/src/App.jsx:206`：`handleSave` 无条件写入 `default_profile: currentProfileName` | 与专用动作 `handleSetDefaultProfile`（`App.jsx:325-327`）职责重叠。用户在 `cs2_gamer` / `office` 上微调后点"保存"→ **桌面默认灯效被静默替换**，违背 `AGENT.md §4.2`（默认策略 = 桌面）。应改为 `default_profile: config.default_profile` |
| 68 | **中** | 风险重估 | `AuraAdapter::CheckReconnect` 断连时固定 1.5s 无限重试（`aura_adapter.cpp:279-295`）× `CreateLedDevice` 已知泄漏 | 原 #13 将固定退避列为"低"。叠加已知泄漏后风险应上调：每小时约 2400 次重试。**但泄漏量级尚未证实**——`AGENT.md §13` 实测的 0.38–0.49MB/次取自 `--test-init 100` 的**成功**路径，而断连走的是 `dev_count == 0` 提前返回分支（`aura_adapter.cpp:138-143`，该分支会调用 `ReleaseHardwareInternal()`）。**验收前提**：在键盘拔除状态下重跑 `--test-init 100`，确认失败路径是否同样线性增长，再决定退避策略与优先级 |

### 对第 #41 项修复方案的更正

`web_supervisor.cpp:122` 的逐字节加宽缺陷（#41）应修正为**全程宽字符**或 `MultiByteToWideChar(CP_ACP, ...)`。**不可采用 `CP_UTF8`**：`exe_path` 源自 `std::filesystem::path::string()`（Windows 下返回本地码页 ACP 编码，非 UTF-8），`config_path_` 源自 `main(int, char* argv[])`（同为 ACP），用 `CP_UTF8` 解码会引入新的乱码。

### 对第 #13 项的补充观察

`gsi_adapter.cpp:721-722` 注释写"严格设置地址复用，避免重启时 TIME_WAIT 冲突"，但实际调用的是 `set_keep_alive_max_count(100)`（HTTP keep-alive 计数，与地址复用无关）。`SO_REUSEADDR` 实际由 `httplib` 默认设置（`httplib.h:2061-2068`）。属**注释与 API 不符**，易误导后续维护者。

---

## 八、第三轮交叉验证（Antigravity / Gemini 3.8 Flash High）新增项与更正

> 本节为用本地 Antigravity（`agy` v1.2.0，模型 `gemini-3.8-flash-high`，只读 `plan` 模式）对前七节做独立复核后的结果。完整执行记录、逐条裁定与复核证据见 `G:\Aura\ANTIGRAVITY_CROSSCHECK.md`。
> **注意**：§一 的"75 项"与 §五 的矩阵是**第一轮普查口径**；本节给出更正与增量后的合并口径。

### 8.1 第一轮 7 项主张的裁定结果

| # | 主张 | 裁定 | 处理 |
| :---: | :--- | :---: | :--- |
| 17 | `Evaluate` 的 `!=` 重入死锁 | 成立 | 维持（唯一"严重"项，两套外部审查均未主动发现） |
| 8 / 9 | 推流缓冲 + COM 偏移无边界写 | 成立 | 维持；其"68 键 → 表长 72、零余量，69 键 → 越界 3 字节"的推导经我复算无误，越界落点经对象布局换算确认落在 `last_reconnect_attempt_` 前 3 字节（后果为重连时间戳被污染，非立即崩溃） |
| 1 | `main.cpp` 变量数据竞争 | **部分成立** | **已收窄**（见 §8.2） |
| 31 | `brightness`/`speed_index` 零读取 | 成立 | 维持 |
| 54 | 灯效串色残影 | **不成立** | **已撤回**（见 §8.2） |
| 55 | `period_ms=1` 除零 | 成立 | 维持 |
| 32 | `/web/(.*)` 路径穿越 | 成立 | 维持；其引用的 `httplib.h:6483-6485`（`decode_url`）、`:6168-6171`（`RegexMatcher` 用 `std::regex_match`）、`:6741`（`is_valid_path` 仅用于 mount point）三处证据经我逐条核对**全部准确**；并补充确认 `%2e%2e` 可绕过浏览器端归一化 |

### 8.2 对前七节的两处更正

1. **#1 竞争范围收窄**：`last_proc_name` **不构成**数据竞争——`grep -n "last_proc_name" src/main.cpp` 仅命中 `178/180/181/182`，主循环体（214–298 行）对其零引用，访问全部局限于监控线程内部的回调。仅 `current_active_profile_name` 真实竞争。
2. **#54 撤回**：原判错误，理由见该行备注。根因是**只审查了写入端、忽略了发送端的白名单过滤**。

### 8.3 新增缺陷（6 项，均不在此前任何报告中）

| # | 严重度 | 位置 | 缺陷 |
| :---: | :---: | :--- | :--- |
| 69 | 中 | `httplib.h:97-98`；`gsi_adapter.cpp:687-723` | **HTTP 端点无请求体上限**：`CPPHTTPLIB_PAYLOAD_MAX_LENGTH` 默认 `std::numeric_limits<size_t>::max()`，`set_payload_max_length` 全仓未被调用 → 本地超大 POST 可致 `bad_alloc` / OOM。GSI(19897) 与 Web(19898) 均受影响 |
| 70 | 中 | `web_supervisor.cpp:240-247` | **`WorkerLoop` 忙等自旋烧 CPU**：谓词 `(s && r) \|\| (!s && !r)` 在 `!suppressed && !running` 下恒为 `true`，`cv_.wait_for` 立即返回；触发于 `aura_web_ui.exe` 缺失时，30 秒退避期内持续满速空转占满一核 |
| 71 | 低 | `keymap.cpp:155-172`；`effect.h:36-42` | **复合键展开静默误解析**：`"WASE"` 因逐字符均合法被解析为 W/A/S/E，无提示；`Profile::Render` 对解析失败的 key_spec 静默跳过、不打日志 |
| 72 | 中 | `aura_adapter.cpp:13, 343-345`；`main.cpp:60` | **COM 初始化/反初始化不平衡**：#4 的精确化——`com_initialized_` 全仓无任一赋 `true` 点，故 `CoUninitialize()` 为不可达死代码，而 `CoInitialize(NULL)` 从不配对 |
| 73 | 低 | `logger.h:63-69` | **日志级别机制整体为死代码**：`SetLogLevel` 与 `LOG_DEBUG` 全仓均 0 处调用 → `current_level_` 构造后恒为 `Info`，Debug 级永不可达。**同时该事实使"`ShouldLog` 无锁读"不构成实际竞争**（无写入者），故相关主张裁定为不成立 |
| 74 | 低 | `CMakeLists.txt` 全文 | 无 `enable_testing`/`add_test`、未设 `/W4`、无 sanitizer（与 §M9 补充项一致） |

### 8.4 合并口径

| 项目 | 数值 |
| :--- | :--- |
| 第一轮普查 | 75（严重 1 · 高 15 · 中 26 · 低 33） |
| 撤回 | −1（#54，中） |
| 新增 | +6（#69–#74：中 3、低 3） |
| **合并后总计** | **80（严重 1 · 高 15 · 中 28 · 低 36）** |

### 8.5 未决问题（需实测，两轮推理都无法判定）

**断连路径的 `CreateLedDevice` 泄漏量级**（外部报告"1 小时泄漏约 1GB → 必然 OOM"的外推）。`AGENT.md §13` 实测的 0.38–0.49 MB/次取自 `--test-init 100` 的**成功**路径；断连走的是 `aura_adapter.cpp:138-143` 的 `dev_count == 0` 提前返回分支（该分支会调用 `ReleaseHardwareInternal()`），其每次泄漏量**从未被测量**。

**一次实验即可定论**：键盘拔除状态下运行 `aura_daemon.exe --test-init 100`，看 `PrivateBytes` 是否仍线性增长。仍增长 → 该外推成立、重连退避升为 P0；走平 → 该外推应撤回。

---

## 九、批 A 实施记录带来的两处实测修正

> 批 A（R1/R2/R3）已实施并用 MSVC 19.51 Release 实测验证。实施过程中的受控实验推翻了本报告两处**机理描述**（结论与严重度不变）。证据见环境目录 `G:\Aura-build-verify\`（仓库外）与 `G:\REMEDIATION_PLAN.md` 的批 A 记录。

### 9.1 更正 #17：`Evaluate` 的 `!=` 后果是「**硬崩溃**」而非「冻结」

- **原判断**：后果为"daemon 完全冻结，无崩溃、无日志"。
- **实测（受控实验）**：把修复临时还原为 `return !Evaluate(...)` 重新编译后运行，进程**并未挂起**，而是在 **665 ms 内异常终止**，权威退出码为 **`0xC0000409`**（`STATUS_STACK_BUFFER_OVERRUN`，即 MSVC 的 fail-fast/`abort()` 路径），stdout 被截断（仅 320 字节）、stderr 为空。
- **真实机理**：MSVC STL 的 `mutex::lock()` 会检测"同线程重复加锁"并返回错误 → `_Throw_C_error` 抛出 `std::system_error` → 测试/daemon 中无人捕获 → `std::terminate()` → `abort()` → fail-fast。
- **修正后的表述**：在 MSVC 19.51 上表现为**立即崩溃并留下异常的退出码**（而非静默冻结）；在其他 STL 实现上则很可能真的死锁。两种表现**都是 P0**，但排障特征完全不同——崩溃是"daemon 消失 + 留下 `.daemon_running`（下次启动会走异常退出兜底）"，冻结是"进程还在、灯效卡住"。
- **附带收获**：该实验同时证明了新增的回归护栏有真实拦截力——恢复修复后同一用例立刻通过（退出码 `0x00000000`，24 项 PASS / 0 FAIL）。

### 9.2 更正 #8/#9：溢出**可达**，但新增的 144 运行期上界**不可达**——已改为编译期证明

- **溢出可达性（原结论成立）**：`BuildPaddedHardwareTable` 只收 `led_id ∈ [0, TOTAL_LEDS)` 且去重，故表长上界为 `PaddedTableLength(128) = 137`。原 216 字节（72 槽）缓冲在 **69 个唯一 `led_id` 时即越界 3 字节**，最坏（128 个唯一 id）越界 **192 字节** —— 这是**真实可达**的缺陷，R3 通过把容量扩到 432 字节解决。
- **但运行期 144 上界永远不会触发**：因最大表长 137 ≤ 144，该检查在键位表路径上不可达（仅作纵深防御）。**真正有约束力的是编译期证明**，故 R3 最终采用：
  - 新增 `constexpr PaddedTableLength(N)`（与插入规则严格一致）；
  - `static_assert(PaddedTableLength(TOTAL_LEDS) <= MAX_HARDWARE_STREAM_KEYS)`；
  - `static_assert(PaddedTableLength(68) == 72)`（锁死 USB 64 字节隔离假设）。
- **该静态断言已双向验证**：容量取 144 编译通过；把容量改回原值 72 则**直接编译失败**（`error C2338: 容量不足：隔离规则下的最坏表长已超出上界`）——即"当年这个缺陷本可被编译器拦下"。
- **实测行为**：真实 68 键 → 表长 72（行为与改动前完全一致）；最坏 128 个唯一 id → 表长 137，72/137 两种情形均 `FATAL` 行数 0、退出码 0。

### 9.3 新确认的一处契约（`!=` 与缺失字段）

实测：`Evaluate("no.such.field", "!=", 1)` 返回 **false**（而非 true）。原因是字段查找失败会**早于**运算符分派直接 `return false`。语义上是良性的（`!=` 绑定不会因字段缺失而误命中），已作为受测契约写入 `tests/test_gsi_rules.cpp` 的测试 6。

---

## 十、D2 版本库治理与一个新发现的严重问题：**提交历史与工作树相位不一致**

> 本节记录 R17（`.gitignore` + 源码纳管）的实施与验证结果。验收标准是"全新 clone 后可直接构建"——正是这条标准暴露了一个比原 #63/#65 更严重的问题。

### 10.1 新发现（严重）：HEAD 是 Phase 2，工作树是 Phase 3 —— 提交历史上**无法构建**

- **事实**：原报告只指出"`include/gsi`、`src/gsi`、`tests`、`frontend` 未被跟踪"。实测进一步确认：**已被跟踪的 `CMakeLists.txt` 也是 Phase 2 版本** —— 缺少 `add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)`、不含 `src/gsi/gsi_adapter.cpp`、缺少 `ws2_32`/`crypt32` 链接项，两个测试目标根本不存在；`include/config/rule_engine.h` 的 GSI 感知 `MatchProfile(process, gsi_state)` 重载在 HEAD 中也**不存在**（HEAD 出现 0 次，工作树 1 次）。
- **后果**：即使把未跟踪源码全部纳入跟踪，**全新 clone 仍然构建失败**。实测错误演化：**102 个错误**（全部落在 Windows SDK `ws2def.h`：`sockaddr` 重定义 —— 根因是缺 `WIN32_LEAN_AND_MEAN` 时 `windows.h` 先拉入旧 `winsock.h`、随后 `winsock2.h` 冲突）→ 提交 CMakeLists 后降为 **20 个**（`error C2660: MatchProfile 函数不接受 2 个参数`）→ 补齐 Phase 3 源码后 **0 个**。
- **性质**：这不是"漏跟踪文件"的细枝末节，而是**仓库在 HEAD 上自相矛盾**（源码与其构建定义相位不一致），任何新人 clone 都无法复现该项目。
- **处置**：提交 4 个 commit 后，全新 clone 已可复现构建（见 10.2）。

### 10.2 处置与验证（已全部实测）

新增 4 个提交（未改写历史，未使用 `--force`）：

| commit | 内容 |
| :--- | :--- |
| `ce232a3` | `fix(daemon)`：批次 A —— R1/R2/R3（9 文件，+1363/−40） |
| `468dfcd` | `chore(repo)`：补 `.gitignore`，解除 **115 项**产物跟踪，纳管 27 项源码与文档 |
| `b00e4e5` | `build`：提交 Phase 3 构建定义（HEAD 仍是 Phase 2） |
| `7dac58a` | `build`：补齐 Phase 3 源码（rule_engine、web_server、test_com） |

**验收证据（全新 clone，路径 `G:\Aura-clone-verify3`）**

| 检查项 | 结果 |
| :--- | :--- |
| clone 文件数 | 109（不含 `node_modules` / `build/` / `__pycache__`） |
| 关键源码齐备 | `src/gsi/gsi_adapter.cpp`、`include/gsi/gsi_adapter.h`、`tests/`、`frontend/src/`、`test_cs2_gsi.py`、`include/third_party/json.hpp` **全部 ✓** |
| 未误带产物 | `build/`、`node_modules`、`frontend/node_modules`、`__pycache__` **均不存在 ✓** |
| `git ls-files` 中 `*.exe` / `*.pyc` / `build/` | **均为 0 ✓** |
| **全新 clone `cmake` 配置** | **exit 0** |
| **全新 clone 构建** | **exit 0，0 error / 0 warning**，产出全部 5 个可执行文件（`aura_daemon` / `aura_web_ui` / `test_com` / `test_diag_hook` / `test_gsi_rules`） |
| clone 内 `test_gsi_rules` | 退出码 `0x00000000`，**24 PASS / 0 FAIL** |
| 主工作树 `git status` 余项 | 由 40+ 条 `build/` 噪声降至 **4 项**（且均为用户既有未提交内容，非本次改动） |

### 10.3 有意保留、未纳入提交的内容

`README.md`、`config.json`、`config.example.json`、`web/index.html` 仍留在工作树**未提交**。理由：它们不影响构建，且 `config.json` 属用户运行期状态、`web/index.html` 属前端构建产物，是否冻结应由用户决定。**注意**：这意味着 HEAD 上的 `config.json` 仍是不含 `gsi_bindings` 的旧版，`web/index.html` 也是旧版 UI；若希望 clone 出的运行时行为与当前一致，需要另行提交这三项 + README。

---

*报告依据对 `G:\Aura` 工作树的静态阅读与交叉核对生成（初审时 HEAD = `55555de`；批次 A 与版本库治理后 HEAD = `7dac58a`）。所有位置标注对应审查时的文件行号。涉及硬件行为的判断（如 COM 内部偏移容量、底层扩容行为）基于代码与 `AGENT.md` 记录，未经真机验证，标注为"潜在"。*
