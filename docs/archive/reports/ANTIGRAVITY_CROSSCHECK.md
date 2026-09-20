> Historical snapshot. Superseded by [current documentation](../../README.md). Claims and TODOs below describe their original period, not current implementation or verified acceptance.

# Antigravity 交叉验证记录（Gemini 3.8 Flash High）

- **交叉验证对象**：本地 Google Antigravity（`agy` CLI v1.2.0），模型 `gemini-3.8-flash-high`
- **调用方式**：`mcp_stdio_client.py --tool antigravity_plan_task`（只读 `plan` 模式，`--workspace G:/Aura`）
- **被验证对象**：`AURA_CODE_REVIEW_REPORT.md`（我方）+ `repository_review_report.md`（外部）
- **执行时间**：2026-09-10 19:14–19:25

---

## 一、执行记录

| 轮次 | 工具 / 模型 | 耗时 | Token | 会话 ID | 结果 |
| :---: | :--- | ---: | ---: | :--- | :--- |
| 1 | `plan_task` / `gemini-3.8-flash-high`（effort=high） | 233.6 s（墙钟 246 s） | 322,975 | `80b0d3ff-3ce9-470d-aff2-192975d6e50d` | ✅ 完整 |
| 2 | 同上（6 项主张，含需读 `httplib.h` 7800 行） | 304 s | — | — | ❌ **超时，0 输出** |
| 3 | 同上（4 项，限定每条 ≤100 字） | 66.5 s | 107,602 | `263fe600-938e-42c9-bc39-5f43b226ed2c` | ✅ 完整 |

> **操作教训**：`plan_task` 的硬上限约 300 s（`executor.py` `timeout=300.0`；`agy --print-timeout` 默认 `5m0s`）。超时后**只返回 `Execution timed out`，已消耗的 token 全部作废、无部分结果可挽救**。结论：单轮 ≤5 项主张，且把"证据在超大 vendored 头文件里"的条目单独拆轮。已回写至 `~/.workbuddy/skills/antigravity-session-read/SKILL.md`。

---

## 二、第一轮：7 项主张的裁定与我的独立复核

**原则**：Antigravity 的每条裁定都当作"待裁定主张"处理，逐条回到源码复核后才采纳。

| # | 主张 | Antigravity 裁定 | 我方独立复核 | 最终 |
| :---: | :--- | :---: | :---: | :---: |
| 1 | `GsiState::Evaluate` 的 `!=` 分支重入死锁 | 成立 | 一致：`gsi_adapter.h:101` 为 `std::mutex` 非 `recursive_mutex`；`gsi_adapter.cpp:470-471` 进入即持锁；`:552-553` 递归自调用 | ✅ 维持 |
| 2 | 推流缓冲与驱动偏移写入无边界 | 成立 | 一致且**其推导经我复算无误**（见下） | ✅ 维持（后果精确化） |
| 3 | `main.cpp` 两个变量数据竞争 | **部分成立** | **复核确认其正确**：`last_proc_name` 无竞争 | ⚠️ **我方需更正** |
| 4 | `brightness`/`speed_index` 引擎零读取 | 成立 | 一致 | ✅ 维持 |
| 5 | 残影：60 个未写入 LED 残留上一帧颜色 | **不成立** | **复核确认其正确**：我方论证错误 | ❌ **我方需撤回** |
| 6 | `CurrentEffect` 在 `period_ms=1` 时除零 | 成立 | 一致（`EXCEPTION_INT_DIVIDE_BY_ZERO` / `0xC0000094`） | ✅ 维持 |
| 7 | `/web/(.*)` 路径穿越 | 成立 | 其证据经核对全部准确（见下） | ✅ 维持 |

### 主张 2 的推导复算（其数字正确）

`BuildPaddedHardwareTable` 的规则是"`size % 15 == 14` 时先插一个 `0xFF`"。逐点复算：触发点出现在 `size = 14 / 29 / 44 / 59`，对应已推入物理键数 `14 / 28 / 42 / 56`；第 57 键触发第 4 次填充后 `size = 61`，此后 `61…72` 无 14 模 15 的值，最终 **68 键 + 4 填充 = 72**，恰好等于 `HARDWARE_STREAM_BUFFER_SIZE / 3`，**零余量**。

69 键时：`size = 72`，`72 % 15 = 12 ≠ 14` → 直接压入得 73 → `PushFrame` 写入 `stream_buffer_[216..218]`，越界 3 字节。

**我进一步做了对象布局换算**以确认越界落点：`aura_adapter.h` 成员序为 `dry_run_(0) / state_(4) / com_initialized_(8) / hHalMod_(16) / pFactory_(24) / pHal_(32) / pDev_(40) / fn_set_single_(48) / keymap_(56) / padded_hardware_table_(64，MSVC 24 字节) / stream_buffer_(88，216 字节，止于 303) / last_reconnect_attempt_(304，8 字节对齐)`。故越界 3 字节**精确落在 `last_reconnect_attempt_` 的前 3 字节**。其"踩踏 `last_reconnect_attempt_`"的说法成立，但后果应精确表述为**重连时间戳被污染（可能引发异常的重连节奏），而非立即崩溃**。

### 主张 7 的证据核对（全部准确）

| 其引用 | 实测 | 结论 |
| :--- | :--- | :---: |
| `httplib.h:6483-6485` 对 `req.path` 做 `decode_url` | `req.path = detail::decode_url(std::string(lhs_data, lhs_size), false);` | ✓ |
| `httplib.h:6168-6171` 正则路由用 `std::regex_match` | `return std::regex_match(request.path, request.matches, regex_);` | ✓ |
| `is_valid_path` 仅用于内置 mount point | 全文件仅在 `:6741` 被引用（静态文件服务路径） | ✓ |

**补充确认**：因 `decode_url` 在路由前生效，`GET /web/%2e%2e/config.json` 可绕过浏览器端的路径归一化后仍被解码为 `../config.json`，攻击实际可行。

---

## 三、本次交叉验证的核心收益：对**我方报告**的 2 处更正

### 更正 1 — `#1` 的竞争范围必须收窄（原判过宽）

| 变量 | 访问点 | 线程 | 是否竞争 |
| :--- | :--- | :--- | :---: |
| `last_proc_name` | `main.cpp:178`（定义）、`:180`（引用捕获）、`:181`（读）、`:182`（写） | 全部在回调内 = 监控线程 | **否** |
| `current_active_profile_name` | `:185` 写 / `:192` 读（监控线程）；`:221` 读 / `:222` 写 / `:247` 写 / `:253` 读（主循环） | 两个线程 | **是** |

复核手段：`grep -n "last_proc_name" src/main.cpp` 仅返回 178/180/181/182；对主循环体（214–298 行）单独 grep 无任何命中。**我原先把它一起列为竞争变量属于过度归并。**

### 更正 2 — `#54`（响应类灯效残影）**撤回**

Antigravity 的反驳成立，且我复核后确认。**我的论证错误在于只看了"写入端"，没看"发送端"**：

1. `PushFrame`（`aura_adapter.cpp:222-230`）遍历的是 `padded_hardware_table_`，**只读 `frame.buffer[]` 中那 68 个已标定 led_id 对应槽位**。其余 60 个槽位的值**从不被读取**，因此不可能进入 `stream_buffer_` 发往键盘。
2. 且每个灯效每帧都写满这 68 个键：
   - `RippleEffect::Render` **确实**在 `builtin_effects.cpp:174` 调用 `out_frame.Fill(base_color_...)`（我原报告把它归入"未 Fill"，属事实错误）；
   - `ReactiveEffect` 在 `else` 分支（`:149-151`）显式写 `base_color_`；
   - `Current` / `Raindrop` / `StarryNight` / `Wave` / `Quicksand` 对每个 keymap 键**无条件** `SetKey`；
   - `CustomKeymap` / `Breathing` / `ColorCycle` / `Static`(非 analog) 用 `Fill`（写满 128 槽）。
3. 剩余理论缺口（keymap 覆盖数少于 `padded_hardware_table_`）不存在：两者由**同一个** keymap 对象构建。

**结论**：该缺陷在当前代码中不存在，降级为"潜在（仅在键位表被裁剪到少于实际 LED 数时才可能显现）"，不计入有效缺陷。

---

## 四、第三轮 + 我方独立复核：双方确认的 6 项**新增**缺陷

以下 6 项均**不在此前任何一份报告中**（第一轮 Antigravity 的 3 项"额外发现"反而全部与我方既有结论重合，见 §五）。

| # | 严重度 | 位置 | 缺陷 | 来源 |
| :---: | :---: | :--- | :--- | :--- |
| N1 | 中 | `httplib.h:97-98`；`gsi_adapter.cpp:687-723`；`web_server.cpp` 全量 | **HTTP 端点无请求体上限**：`CPPHTTPLIB_PAYLOAD_MAX_LENGTH` 默认 `std::numeric_limits<size_t>::max()`，且 `set_payload_max_length` **全仓未被调用**（grep 确认）。任何本地进程可 POST 超大 body 触发 `bad_alloc` / OOM。GSI(19897) 与 Web(19898) 两个 server 均受影响 | 双方独立确认 |
| N2 | 中 | `web_supervisor.cpp:240-247` | **WorkerLoop 忙等自旋烧 CPU**：谓词 `(s && r) \|\| (!s && !r)` 在 `!suppressed && !running` 状态下恒为 `true` → `cv_.wait_for` 立即返回 → 循环空转。触发条件：`aura_web_ui.exe` 不存在（如全新 clone 未构建）。前 3 次失败快速空转，随后进入 30 秒退避期，**退避期内持续满速空转占满一核** | 双方独立确认 |
| N3 | 低 | `keymap.cpp:155-172`；`effect.h:36-42` | **复合键展开静默误解析且无告警**：`"WASE"`（应为 `WASD`）因逐字符全部合法而被解析为 W/A/S/E 四键，无任何提示；`Profile::Render` 对 `ResolveKeys` 返回 false 的 key_spec **静默跳过、不打日志**。配置笔误表现为"少亮几个键"，难以自查 | 双方独立确认 |
| N4 | 中 | `aura_adapter.cpp:13, 343-345`；`main.cpp:60` | **COM 初始化/反初始化不平衡**：`com_initialized_` 初值 false 且**全仓无任何赋 true 的点** → `Shutdown()` 中的 `CoUninitialize()` 是不可达死代码；而 `main.cpp:60` 的 `CoInitialize(NULL)` 从不配对反初始化 | 我方发现，Antigravity 第二轮未完成，由我方独立确认 |
| N5 | 低 | `logger.h:63-69` | **日志级别机制整体为死代码**：`SetLogLevel` 与 `LOG_DEBUG` 在全仓**均 0 处调用** → `current_level_` 构造后恒为 `Info`，Debug 日志永不可达。**同时这也使"`ShouldLog` 无锁读 `current_level_`"不构成实际竞争**（无写入者），故对该项主张裁定为不成立 | 我方发现并裁定 |
| N6 | 低 | `CMakeLists.txt` 全文 | 无 `enable_testing` / `add_test`（未接入 CTest）、未设 `/W4`、未配置任何 sanitizer，四个 target 一律 `/utf-8 /W3 /O2` | 双方独立确认 |

---

## 五、Antigravity 第一轮 3 项"额外发现"与我方既有结论的对照

| 其"额外发现" | 我方对应编号 | 判定 |
| :--- | :---: | :--- |
| `current_process_name_` 跨线程无锁读写（`foreground_monitor.cpp:118,153` / `main.cpp:217`） | #47 | 与我方一致，**属独立发现而非新增**；其补充的"超 SSO 15 字节阈值时发生堆重分配、易致 UAF"是对机制的合理细化 |
| `web_supervisor.cpp:119-122` 宽字符强转破坏非 ASCII 路径 | #41 | 与我方一致 |
| `POST /api/config` 与 `/api/gsi/install-cfg` 零鉴权、无 Origin 校验（CSRF） | #33 | 与我方一致 |

**结论**：Antigravity 未发现任何我方未覆盖的**高危**缺陷；其 3 项"额外发现"全部与我方既有结论重合——这本身是一次有价值的**独立复现**（两套审查互相印证），但新增项全部落在中/低严重度。

---

## 六、未决问题（两轮推理均无法判定，需实测）

**断连路径的 `CreateLedDevice` 泄漏量级**（外部报告 §3.2 的 1 小时 900MB~1.1GB → OOM 外推）。

- 已知：`AGENT.md §13` 实测 0.38–0.49 MB/次，取自 `--test-init 100` 的**成功**路径（100/100 均创建到设备）。
- 未知：断连时走的是 `aura_adapter.cpp:138-143` 的 `dev_count == 0` **提前返回分支**（该分支**会**调用 `ReleaseHardwareInternal()`），其每次泄漏量从未被测量。
- **判别实验（一次即可定论）**：在键盘拔除（或拨码切至另一台 PC）状态下运行 `aura_daemon.exe --test-init 100`，观察 `PrivateBytes` 是否仍呈线性增长。
  - 若仍增长 → 该外推成立，重连退避应升为 P0；
  - 若走平 → 该外推不成立，"1 小时 OOM"结论应撤回。
- Antigravity 与我都只能推理，**无法代替该实验**。

---

## 七、净收益汇总

| 项目 | 变化 |
| :--- | :--- |
| 我方报告缺陷总数 | 75 → **80** |
| 撤回 | 1 项（#54 残影 —— 我方论证错误） |
| 范围收窄 | 1 项（#1 仅 `current_active_profile_name` 存在竞争） |
| 新增 | 6 项（N1–N6，全部中/低） |
| 严重度重算 | 严重 1 · 高 15 · 中 28 · 低 36 |

**共同盲区提示**：外部报告与 Antigravity 两轮都**没有**主动指出 `Evaluate` 的 `!=` 死锁（本轮由我在提示中带出后才确认）。该缺陷隐蔽（需同时注意到"外层已持锁"与"这是递归自调用"），建议在同类 C++ 审查中把它作为固定检查项。
