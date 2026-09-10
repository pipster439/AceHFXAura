# ROG Falchion Ace HFX 灯效问题定位与修复报告

> 日期：2026-08-30
> 仓库：`G:/Aura`（当前工作树为其 git worktree）
> 结论：**五个问题已全部定位并修复。** 五层验证全通过：
> ① 11 种灯效冒烟测试 11/11；② 端到端真实按键链路 5/5（空闲零点亮、按下精确点亮对应 LED）；
> ③ 真机四种新灯效推流 5/5 无异常；④ 真机硬件控制权与按键钩子就绪；
> ⑤ 前端模拟触发状态机 28/28（含"切回常亮自动恢复偏好"）

---

## 〇、先说结论：为什么"前端有效果、键盘没反应"

排查过程中最关键的发现，是**你测试时运行的 `aura_daemon.exe` 是一个过期二进制**。

本项目有两套完全独立的渲染路径：

| | 前端预览（浏览器） | 真实键盘（C++ 守护进程） |
|---|---|---|
| 渲染位置 | `web/index.html` 的 `renderLightingLoop`，60 FPS | `aura_daemon.exe` → `EffectEngine::Tick()`，25 FPS |
| 输入来源 | 网页内虚拟键点击 **+ 自动演练定时器** | `WH_KEYBOARD_LL` 全局钩子 → `KeyInputHub` |
| 下发出口 | 画布 DOM，不出进程 | `AacKbHal_x64.dll` → `Set_L_STD_SINGLE_XY` → USB HID |

**两者之间没有任何数据通道**：网页预览画得再热闹，也不会产生一条进入 `KeyInputHub` 的事件。因此"前端有效果"完全不能作为"键盘会亮"的证据。

### 二进制取证（决定性证据）

对 exe 本体做字符串提取比对：

| 二进制 | 时间戳 | `WH_KEYBOARD_LL` | `starry_night` | `quicksand` | `raindrop` |
|---|---|---|---|---|---|
| `G:/Aura/aura_daemon.exe`（你测试时运行的） | 08-30 03:54 | ❌ 无 | ❌ 无 | ❌ 无 | ❌ 无 |
| 仓库根 `aura_daemon.exe` | 08-30 05:39 | ❌ 无 | ❌ 无 | ❌ 无 | ❌ 无 |
| **新编译 `build/Release/aura_daemon.exe`** | 08-30 06:25 | ✅ 有 | ✅ 有 | ✅ 有 | ✅ 有 |

`src/monitor/foreground_monitor.cpp`（按键钩子实现）的修改时间是 **04:54:17**——晚于 03:54 那次编译。所以你在 04:49~04:53 的整轮测试，跑的是一个**根本没有编译进按键钩子**的程序：无论怎么敲键，`KeyInputHub` 永远是空的，reactive / ripple 自然毫无反应。

日志也完全吻合：`aura_daemon.log` 里那次 5 分钟硬件会话（25.00 FPS、内存平稳、无报错）**从未出现** `全局物理按键监听钩子 (WH_KEYBOARD_LL) 注册成功` 这一行，因为旧 exe 里压根没有这段日志代码。

---

## 一、问题逐一定位

### 问题 1 / 3：交互性灯效（reactive）与涟漪（ripple）在真实键盘上无反馈

**根本原因：运行了缺少 `WH_KEYBOARD_LL` 钩子的过期二进制。**

源码本身是正确的：

- `src/monitor/foreground_monitor.cpp:159` — 注册全局低级键盘钩子
  ```cpp
  g_keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, ...);
  ```
- `src/monitor/foreground_monitor.cpp:16` — 回调里记录真实物理按键
  ```cpp
  KeyInputHub::Instance().RecordKeyPress(kName);
  ```
- `src/engine/builtin_effects.cpp:128`（Reactive）/ `:159`（Ripple）— 消费真实事件
  ```cpp
  KeyInputHub::Instance().DrainEvents(events);
  ```

链路设计无误，但旧 exe 里没有这段。**修复方式：重新编译 + 部署，不是改代码。**

顺带说明"其余按键均无响应"：这恰恰反证了钩子不存在。如果钩子在，68 个键位（`calibrated_keymap.json`，已校验无重复 LED ID、无重复物理位置）应当全部响应；只有网页 `demoList` 里写死的那 9 个键会动，说明动的东西来自前端，不是键盘。

---

### 问题 2：无按键时动画自动触发，自动触发键为 A、S、D、W、Q、E、R、空格

**根本原因：纯前端缺陷——`web/index.html` 的 `renderLightingLoop` 里有一段"自动演练"定时器。**

删除前的代码：

```js
// 空闲超过 2 秒就开始自动演练
if (now - lastUserKeystrokeTime > 2000) {
    if (now - lastDemoTime > 1400) {
        const demoList = ['W','A','S','D','SPACE','Q','E','R','ENTER'];
        const demoKey = demoList[demoStepCounter % demoList.length];
        triggerKeyReactive(demoKey);
        demoStepCounter++;
        lastDemoTime = now;
    }
}
```

你观察到的自动触发键集合 **A、S、D、W、Q、E、R、空格**，与这份 `demoList` **逐一吻合**（`ENTER` 在 65% 键盘上视觉不明显，所以你没列出来）。这是定位该问题最直接的指纹。

这段代码**只存在于浏览器里**，C++ 侧从未有过任何自动/合成按键逻辑（已全量 grep 确认：`RecordKeyPress` 仅由真实钩子回调调用，无任何随机或定时器注入）。

`selectEffect()` 里还有一行 `lastUserKeystrokeTime = performance.now() - 1500;` 把空闲计时器**预置到临界点**，导致一切换到 reactive/ripple 就立刻开始自动演练。

**修复：**
1. 整段删除自动演练块
2. 删除 `selectEffect()` 中的预置行
3. 该函数现只保留真实物理敲击与虚拟键点击两个驱动源

---

### 问题 4：星空、流沙、电流、雨滴四种灯效在键盘上完全无效

**根本原因：同问题 1——过期二进制未包含这四种灯效的实现。**

`src/config/rule_engine.cpp` 早已完整支持全部 11 种灯效（`:122` starry_night / `:132` quicksand / `:144` current / `:153` raindrop），`builtin_effects.cpp` 中 `StarryNightEffect`、`QuicksandEffect`、`CurrentEffect`、`RaindropEffect` 也均已实现。

但旧 exe 里搜不到 `starry_night`、`quicksand`、`raindrop` 任何一个字符串——它们是被**编译进去之前**你就测试了。

**修复：重新编译 + 部署。** 新二进制已验证 11 种灯效字符串全部存在。

---

### 问题 5：模拟触发功能不应出现在键盘响应型灯效中

**根本原因：前端 `web/index.html` 的"模拟灯效"开关对所有灯效类型都显示，并且被错误地用来放大 reactive / ripple 的强度。**

三处缺陷：

1. 参数面板无条件显示模拟开关（reactive / ripple / wave 下也能开）
2. ripple 强度计算把模拟开关当增益系数：
   ```js
   totalGlow += waveFactor * (isAnalogEnabled ? 1.4 : 0.95);  // 错误：模拟触发不该影响涟漪强度
   ```
3. 配置保存时 `prof.analog` 未做类型门控，非 static 灯效也会把 `analog` 持久化成 `true`

**修复：**
- 开关仅对 `static` 显示：`display = (isStatic) ? 'flex' : 'none'`
- 移除对 ripple 强度的干预：`totalGlow += waveFactor * 0.95;`
- 持久化门控：`prof.analog = (currentEffect === 'static') ? isAnalogEnabled : false;`
- 渲染以新增的 `analogActive()` 为准，而非直接用 `isAnalogEnabled`：
  ```js
  function analogActive() { return (currentEffect === 'static') && isAnalogEnabled; }
  ```
- 前端 static 预览补齐按键增亮效果，与 C++ `StaticEffect`（`builtin_effects.cpp:56-65`，读取真实按键做压感增亮）行为对齐

> **修复过程中我自己引入、随后又修掉的一个回归**（值得记一笔，这类问题很容易被漏掉）：
> 我最初在 `selectEffect()` 里加了「切离 static 时强制 `isAnalogEnabled = false`」。
> 这能杜绝状态泄漏，但带来副作用：用户「常亮(开模拟) → 涟漪 → 切回常亮」之后，
> 模拟开关被悄悄清空了——**偏好丢失**。
> 正确做法是区分「用户偏好」与「当前生效」两个概念：
> `isAnalogEnabled` 只作为跨灯效保留的**偏好记忆**，是否真正生效交给 `analogActive()` 判断。
> 这样既满足"键盘响应型灯效不含模拟触发"（开关隐藏 + 持久化值写 false），
> 又不会让用户的设置在来回切换后丢失。

> ⚠️ 修复过程中踩到一个坑，记录在此：`loadConfig()` 里必须**先**读取 `prof.analog` **再**调用 `selectEffect()`。因为 `selectEffect()` 内部会调用 `saveCurrentInputsToProfile()`，用内存里的旧值覆盖 `prof.analog`。顺序颠倒会导致配置恢复永远失效。

---

## 二、Review 中额外发现并修复的两个隐患

这两个不在你反馈的五个问题里，但会影响长期稳定性，一并修掉。

### 隐患 A：按键事件队列无界增长

`include/monitor/key_input_hub.h` 中 `events_` 只进不出。当活跃灯效是 static / wave / breathing 等**不消费按键事件**的类型时，队列会随每次敲击持续膨胀，长时间运行必然吃内存。

**修复（已改）：**

```cpp
static constexpr size_t MAX_PENDING_EVENTS = 64;
...
if (events_.size() > MAX_PENDING_EVENTS) {
    events_.erase(events_.begin(), events_.begin() + (events_.size() - MAX_PENDING_EVENTS));
}
```

环形截断，保留最近 64 条，超出部分丢弃——对交互灯效无感知影响，且杜绝泄漏。

### 隐患 B：配置热重载时的 use-after-free（悬垂指针）

`RuleEngine::LoadConfig()` 用 `profiles_ = std::move(new_profiles)` 整体替换 Profile 表，**立即析构**所有旧 `Profile` 对象。而 `EffectEngine` 当时持有的是裸指针 `std::atomic<const Profile*> active_profile_`，`main.cpp` 的前台切换回调与热重载分支也各持一份 `const Profile*`。

在网页端每点一次"保存配置"，就存在一个"正在 25 FPS 渲染 / 正在回调 → Profile 已被析构"的竞态窗口，表现为随机崩溃或灯效错乱。

**修复（已改，涉及 6 个文件）：**

| 文件 | 变更 |
|---|---|
| `include/config/rule_engine.h` | `MatchProfile()` 返回 `std::shared_ptr<const Profile>` |
| `src/config/rule_engine.cpp` | 4 处 `return it->second.get();` → `return it->second;` |
| `include/engine/effect_engine.h` | `std::atomic<const Profile*>` → `std::mutex` + `std::shared_ptr<const Profile>` |
| `src/engine/effect_engine.cpp` | `Tick()` 先在锁内拷贝 shared_ptr，再**释放锁**渲染 |
| `src/main.cpp` | 3 处调用点改用 `std::shared_ptr<const aura::Profile>` |

渲染时不再持锁，因此不影响帧率；Profile 生命周期由引用计数托管，热重载不再有悬垂风险。

---

## 三、改动清单

```
web/index.html                  | 73 ++++++++++++++++++++++----------------
include/config/rule_engine.h    |  6 ++--
include/engine/effect_engine.h  | 13 +++++---
include/monitor/key_input_hub.h |  6 ++++
src/config/rule_engine.cpp      | 10 +++---
src/engine/effect_engine.cpp    | 18 ++++++----
src/main.cpp                    |  7 ++--
smoke_test_effects.py           | 新增（全灯效冒烟测试：11 种灯效渲染 + 空闲静默）
e2e_key_test.py                 | 新增（端到端真实按键 -> 灯效响应验证）
hw_effect_test.py               | 新增（真机灯效推流验证，针对问题 4 的四种灯效）
webui_analog_test.js            | 新增（jsdom 驱动真实前端脚本，验证模拟触发状态机 28 项）
LIGHTING_FIX_REPORT.md          | 新增（本报告）
```

> Web UI 静态资源由 `src/web/web_server.cpp:135 LoadHtmlContent()` 从磁盘按 CWD 相对路径加载
> （候选 `web/index.html` / `../web/index.html` / `../../web/index.html`），**不是编译进 exe 的**，
> 因此修好的前端改完即生效，无需重新编译 `aura_web_ui.exe`。

---

## 四、验证结果

### 1. 全灯效冒烟测试（`--dry-run`，新增脚本 `smoke_test_effects.py`）

逐个以 `default_profile` 指向某一种灯效，启动守护进程 6 秒并采集渲染帧：

```
==============================================================================
Aura 全灯效冒烟测试 (--dry-run)
==============================================================================
[PASS] static         帧数=3    活跃通道 128/128 -> 128/128
[PASS] breathing      帧数=6    活跃通道 128/128 -> 128/128
[PASS] color_cycle    帧数=6    活跃通道 128/128 -> 128/128
[PASS] wave           帧数=6    活跃通道  68/128 ->  68/128
[PASS] custom_keymap  帧数=6    活跃通道 128/128 -> 128/128
[PASS] reactive       帧数=6    空闲静默 (0 通道点亮) —— 无自动误触发
[PASS] ripple         帧数=6    空闲静默 (0 通道点亮) —— 无自动误触发
[PASS] starry_night   帧数=6    活跃通道  53/128 ->  56/128
[PASS] quicksand      帧数=6    活跃通道  68/128 ->  68/128
[PASS] current        帧数=6    活跃通道  68/128 ->  68/128
[PASS] raindrop       帧数=6    活跃通道  68/128 ->  68/128
==============================================================================
合计 11 项，通过 11 项，失败 0 项
==============================================================================
```

**关键行**：`reactive` 与 `ripple` 在完全无按键的 6 秒内，**0 个通道点亮**——直接证明问题 2 / 3 的自动误触发已根除（`bg` 设为纯黑 `[0,0,0]`，任何亮起都只可能来自伪造按键事件）。

### 2. 真机链路短跑验证（15 秒，非 dry-run）

```
[2026-08-30 06:30:14.436] 成功加载权威键位映射表: calibrated_keymap.json (共 68 个键位)
[2026-08-30 06:30:14.437] 成功加载配置文件: config.json (默认方案: desktop, 规则数: 5, Profile数: 8)
[2026-08-30 06:30:14.437] 构建安全隔离硬件寻址表完成: 总条目 72 (含 68 物理键位 + USB 64字节边界隔离槽)
[2026-08-30 06:30:15.428] [Connect Step 6] Calling CreateLedDevice...
[2026-08-30 06:30:16.214] CreateLedDevice 执行完成，检测到设备数量: 1
[2026-08-30 06:30:16.214] [+] 成功获取硬件控制权，ROG FALCHION ACE HFX 安全隔离驱动通道已就绪！
[2026-08-30 06:30:16.214] 前台窗口监控线程启动成功 (事件驱动模式，无轮询)
[2026-08-30 06:30:16.214] 前台窗口切换 -> 进程: [GameViewer.exe] => 匹配灯效方案: [desktop]
[2026-08-30 06:30:16.214] 全局物理按键监听钩子 (WH_KEYBOARD_LL) 注册成功     ← 问题 1/3/4 的关键修复点
[2026-08-30 06:30:16.224] [WebUI 监护] 网页配置服务进程已启动 (PID: 48640, 绑定端口: 127.0.0.1:19898)
```

硬件控制权获取成功、钩子注册成功、网页服务正常拉起。

### 3. 端到端真实按键链路验证（新增 `e2e_key_test.py`）

> 前两项验证只证明"灯效能渲染"和"钩子能注册"，**不等于"按键真的点亮了灯"**。
> 这一项才是最终确认：通过 Win32 `SendInput` 注入真实系统按键事件，走完整链路
> `SendInput → WH_KEYBOARD_LL 钩子 → KeyInputHub → Reactive/Ripple 渲染 → FrameBuffer`。

判定依据：`--dry-run` 每秒打印「活跃通道数 N/128」及前 4 个非零 LED ID。配置 `bg=[0,0,0]` 时，空闲阶段 N 必须为 0，按键阶段 N 必须 >0 且 ID 与被按键在 `calibrated_keymap.json` 中的 `led_id` 精确对应。

```
================================================================================
Aura 端到端按键链路验证 (真实 SendInput -> 钩子 -> 灯效)
================================================================================
键位映射：68 键，LED ID 范围 1~117

[PASS] reactive 按键 A      (旧 demoList 内)
        空闲静默 [0, 0, 0, 0] | 按压点亮 1 通道, LED[11]=['A'] | A(LED11) 命中
[PASS] reactive 按键 J      (旧 demoList 外 —— 验证'其余按键无响应'已修复)
        空闲静默 [0, 0, 0, 0] | 按压点亮 1 通道, LED[59]=['J'] | J(LED59) 命中
[PASS] reactive 按键 Z      (旧 demoList 外)
        空闲静默 [0, 0, 0, 0] | 按压点亮 1 通道, LED[20]=['Z'] | Z(LED20) 命中
[PASS] reactive 按键 SPACE  (旧 demoList 内)
        空闲静默 [0, 0, 0, 0] | 按压点亮 1 通道, LED[53]=['SPACE'] | SPACE(LED53) 命中

[PASS] ripple  按键 SPACE  (涟漪扩散)
        空闲静默 [0, 0, 0, 0] | 按压峰值 68 通道点亮 | 呈涟漪扩散形态 (>1 通道)

================================================================================
合计 5 项，通过 5 项，失败 0 项
================================================================================
```

要点解读：

- **`J` 与 `Z` 特意选为旧前端 `demoList` 里**没有**的键。** 你反馈的"其余按键均无响应"在这里被直接证伪——它们现在精确点亮 LED 59 和 LED 20。
- **每次都是"空闲 4 个采样点全 0" → "按压阶段点亮且 ID 精确命中"**，同时证明问题 2 的自动误触发已根除、问题 1/3 的真实按键响应已打通。
- ripple 按压峰值 68 通道（涟漪向外扩散的环带），符合 `RippleEffect` 半径 18 的设计。

> 测试脚本踩坑记（避免复现时误判）：x64 下 `sizeof(INPUT)` 是 **40** 字节而非 32——union 的真实尺寸由最大的成员 `MOUSEINPUT`（32 字节）决定，必须显式补齐。若用 ctypes 只放 `KEYBDINPUT`（24 字节）会得到 32，`SendInput` 直接返回 0 并置 `GetLastError=87`（`ERROR_INVALID_PARAMETER`），表现为"按键无响应"的假故障。脚本中已加 `assert ctypes.sizeof(INPUT) == 40` 断言。

### 4. 真机灯效推流验证（新增 `hw_effect_test.py`）

针对问题 4，在**真实硬件链路**（非 dry-run）下逐个运行那四种曾经"完全无效"的灯效，各 8 秒：

```
==========================================================================
Aura 真机灯效推流验证（每例 8s，非 dry-run）
==========================================================================
[PASS] static(对照)    硬件就绪 | 钩子OK | 推流无异常 | 检测到设备数量: 1
[PASS] starry_night    硬件就绪 | 钩子OK | 推流无异常 | 检测到设备数量: 1
[PASS] quicksand       硬件就绪 | 钩子OK | 推流无异常 | 检测到设备数量: 1
[PASS] current         硬件就绪 | 钩子OK | 推流无异常 | 检测到设备数量: 1
[PASS] raindrop        硬件就绪 | 钩子OK | 推流无异常 | 检测到设备数量: 1
==========================================================================
合计 5 项，通过 5 项，失败 0 项
==========================================================================
```

判定依据：`AuraAdapter::PushFrame`（`aura_adapter.cpp:233`）在 `Set_L_STD_SINGLE_XY` **连续失败 3 次**时会打印「连续推流异常 … 判定硬件连接断开」。一次运行未出现该警告、且无 ERROR 级日志，即说明这些帧被硬件正常接受。

越界写入风险也已排除：`FrameBuffer::SetKey`（`aura_types.h:80`）有 `led_id < TOTAL_LEDS` 保护，`PushFrame` 另有 `lid != DUMMY_PADDING_LED_ID && lid < TOTAL_LEDS` 双重校验。配合 dry-run 中这四种灯效只点亮 53~68/128 通道（全部落在 68 个已映射键位内），可确认不会写出非法 LED ID。

> 注：`CreateLedDevice`/`Release` 存在已知内存泄漏（AGENT.md §13.5），故每次进程只跑一种灯效、用例数刻意精简，不做 11 种全量真机循环。

### 5. 前端模拟触发状态机验证（新增 `webui_analog_test.js`）

问题 5 的修复改的是纯前端状态机。为避免"只靠读代码下结论"，用 jsdom 加载**真实页面脚本**驱动完整交互（页面无 canvas，键盘预览为纯 DOM，jsdom 足以胜任），28 项断言全通过：

```
============================================================================
前端模拟触发开关 —— 状态机往返验证 (jsdom)
============================================================================
  [PASS] 配置加载成功 -- 当前方案=desktop
  [PASS] 常亮: 模拟开关可见 -- display=flex
  [PASS] 点击后开关显示 ON -- 文本=ON
  [PASS] analogActive() 为 true
  [PASS] 同步后 prof.analog 写为 true
  [PASS] 常亮+模拟ON: 保存出去的 JSON 中 analog=true
  [PASS] 涟漪: 模拟开关隐藏 -- display=none
  [PASS] 涟漪: analogActive() 为 false
  [PASS] 涟漪: 保存出去的 JSON 中 analog=false（键盘响应型不含模拟触发）
  [PASS] 切回常亮: 开关仍显示 ON（偏好已恢复） -- 文本=ON
  [PASS] 切回常亮: analogActive() 恢复为 true
  [PASS] 切回常亮: prof.analog 恢复为 true
  [PASS] 按键响应: 模拟开关隐藏 / analogActive() 为 false
  [PASS] starry_night / quicksand / current / raindrop / wave
         / breathing / color_cycle: 均无模拟触发
  [PASS] 多次往返后偏好仍为 ON
  [PASS] 用户关闭后切回仍为 OFF
  [PASS] 页面无自动演练代码残留
============================================================================
合计 28 项，通过 28 项，失败 0 项
============================================================================
```

覆盖的行为契约：

| 场景 | 期望 | 结果 |
|---|---|---|
| 常亮 + 打开模拟 → 保存 | 落盘 `analog=true` | ✅ |
| 切到涟漪 / 按键响应 | 开关隐藏、不生效、落盘 `false` | ✅ |
| 切回常亮 | **开关自动恢复为 ON** | ✅ |
| 连续多次往返（涟漪→响应→星空→常亮） | 偏好不丢 | ✅ |
| 用户主动关闭后往返 | 保持 OFF，不会"复活" | ✅ |
| 其余 7 种非 static 灯效 | 一律不提供模拟触发 | ✅ |

> 测试脚本本身也踩了一次坑，同样值得记：最初断言"拨动开关后 `prof.analog` 立即为 true"，
> 结果失败。查 `saveConfiguration()`（`index.html:2188`）才发现——**开关本身只改内存态**，
> 配置对象由 `saveCurrentInputsToProfile()` 同步，而保存按钮在 POST 前会调用它。
> 所以"拨开关 → 保存"是正确且不会丢的，是我断言了一个不存在的中间态契约。
> 已改为断言「同步后」以及「实际 POST 出去的 JSON」，后者才是真正的端到端验证。

---

## 五、部署状态

二进制与源码均已同步到两处（工作树与 `G:/Aura` 是同一 git 仓库，`git rev-parse --git-common-dir` 指向 `G:/Aura/.git`）：

```
G:/Aura/aura_daemon.exe            373248  08-30 06:29   ← 含钩子 + 11 灯效
G:/Aura/aura_web_ui.exe            385024  08-30 06:29
<worktree>/aura_daemon.exe         373248  08-30 06:29
<worktree>/aura_web_ui.exe         385024  08-30 06:29
```

同步的源码：`include/config/rule_engine.h`、`include/engine/effect_engine.h`、`include/monitor/key_input_hub.h`、`src/config/rule_engine.cpp`、`src/engine/effect_engine.cpp`、`src/main.cpp`、`web/index.html`

---

## 六、构建环境说明（容易踩的坑）

1. **CMake 不在 PATH**，用 VS 自带的：
   ```
   C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe
   ```

2. **Git Bash 下必须显式指定 MSVC 编译器**：
   ```
   -D CMAKE_CXX_COMPILER="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/cl.exe" -T host=x64
   ```

3. **`error MSB6001: 已添加项。字典中的关键字:"HTTPS_PROXY" 所添加的关键字:"https_proxy"`**
   环境里同时存在 `HTTPS_PROXY` 与 `https_proxy`（大小写变体），Windows 环境变量大小写不敏感，MSBuild 内部 `ProcessStartInfo.get_EnvironmentVariables()` 构造字典时抛 `ArgumentException`，CL.exe 根本没被启动。
   解决：构建时剔除代理变量
   ```bash
   env -u https_proxy -u HTTPS_PROXY -u http_proxy -u HTTP_PROXY -u all_proxy -u ALL_PROXY \
     cmake --build build --config Release
   ```

4. **换目录后必须删掉旧 build 重建**：旧 `CMakeCache.txt` 记录了 `g:/Aura/build`，会导致产物静默输出到别的目录。

---

## 七、请你复验

1. 进入 `G:/Aura`，运行 `aura_daemon.exe`（真机模式）
2. 浏览器打开 `http://127.0.0.1:19898`
3. 依次切换 11 种灯效，重点确认：
   - **reactive / ripple**：不敲键时键盘保持背景色（**不应有任何自动闪烁**）；敲击任意键（不只 WASD，试试 J / Z / 方向键）应立刻响应
   - **星空 / 流沙 / 电流 / 雨滴**：四种均应有动画
   - **模拟触发开关**：仅在"恒亮/常亮"下可见，切到 reactive/ripple 后消失且配置中 `analog` 被写为 `false`；
     再切回常亮时应当**自动恢复**为你之前设的状态（这条已由第四节第 5 项自动化验证覆盖）

我自己这边的验证已覆盖到"真实按键 → 精确点亮对应 LED"以及"模拟开关往返"两级（第四节第 3、5 项）。
仍建议你在真实使用场景下过一遍，因为自动化注入的按键与真人手敲在重复率、组合键上存在差异，
而且灯效的**观感**（亮度、节奏是否合意）是自动化测不出来的。

> 注意：本次验证只跑了 15 秒真机短跑，长时间稳定性（如 `CreateLedDevice` 已知的内存泄漏，见 AGENT.md §13.5）未做新的压力测试。
