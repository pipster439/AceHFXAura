> HISTORICAL / PRE-RETIREMENT: this document records an earlier design. Current product behavior is documented in docs/architecture/AUTOMATION_V2.md. Legacy Automation execution and coexistence are no longer supported.

# AceHFXAura: Automation / Studio / Effect / GSI 职责边界审计与架构基线 (Architecture Review & Baseline)

- **版本**: v1.1.0 (Owner 审阅修订版)
- **日期**: 2026-09-20
- **目标硬件**: ASUS ROG Falchion Ace HFX (68-Key, 8000Hz, Dual Magnetic Switch)
- **状态**: 待 Owner 批准 (Pending Owner Approval - Revised)

---

## 目录 (Table of Contents)
1. [审计背景与核心产品原则 (Core Principles)](#1-审计背景与核心产品原则)
2. [四位一体最终职责划分 (Target Architecture & Responsibility Matrix)](#2-四位一体最终职责划分)
3. [核心判定规则与心智模型 (Decision Rules & Mental Model)](#3-核心判定规则与心智模型)
4. [十项架构深度审计与事实核查 (10 Grounded Code Audits)](#4-十项架构深度审计与事实核查)
5. [经典全场景端到端范例 (CS2 End-to-End Examples)](#5-经典全场景端到端范例)
   - 范例 A: Kill One-shot Overlay (击杀瞬间全键盘白光并平滑淡出)
   - 范例 B: HealthGradient Continuous Base Effect (实时动态血量颜色渲染)
6. [当前代码差距分析 (Current Technical Gaps)](#6-当前代码差距分析)
7. [向后兼容与 ABI 保护承诺 (Backward Compatibility & ABI Evolution Caveats)](#7-向后兼容与-abi-保护承诺)
8. [分阶段渐进式迁移方案 (Migration Stages)](#8-分阶段渐进式迁移方案)
9. [Phase 4 范围回退与严格锁定 (Phase 4 Strict Scope)](#9-phase-4-范围回退与严格锁定)
10. [Automation v2 详细设计与 Daemon 最小扩展点 (Automation v2 Proposal)](#10-automation-v2-详细设计与-daemon-最小扩展点)
11. [配置并发与多端写入限制审计 (Config Concurrency Caveat)](#11-配置并发与多端写入限制审计)

---

## 1. 审计背景与核心产品原则

在继续推进 AceHFXAura **Phase 4** 之前，我们基于代码库实况（C++ 守护进程、Plugin ABI、OverlayManager、RuleEngine、LightingControlService、Web Server 19898、Blockly 前端与 WinUI 页面）完成了一次系统性的职责边界审计与架构收敛。

### 核心产品原则
> **产品中最终只允许一个地方负责：“什么时候触发什么 (WHEN TO RUN & WHAT TO DO)”。**  
> **这个地方就是：`Automation`。**  
> **严禁让用户在 Studio 和 Automation 两处重复配置同一个判断条件！**

- **严禁出现**：在 Studio 里拖拽积木写 `if (kill)`，同时在 Automation 规则里又配置 `event.kill -> Trigger kill`。
- **严禁出现**：在 Studio 里拖拽积木写 `if (health < 20) switch_profile(...)`，同时在 Automation 里配置 `player.state.health < 20 -> Activate Profile`。
- **职责收敛**：一个业务条件在整个系统中**只允许配置一次**，并且只能配置在 Automation 中。

---

## 2. 四位一体最终职责划分

```text
┌──────────────────────────────────────────────────────────────────────────────────┐
│                                 产品职责四分工                                  │
├───────────────────────┬──────────────────────────┬───────────────────────────────┤
│ 模块 (Module)         │ 负责 (Responsibility)    │ 不负责 (Out of Scope)         │
├───────────────────────┼──────────────────────────┼───────────────────────────────┤
│ 1. Lighting           │ “普通用户想让灯怎么亮”   │ - 应用匹配 (cs2.exe / code.exe)│
│    (基础灯效设置)     │ - 内置静态/呼吸/Wave等   │ - GSI 业务条件判断            │
│                       │ - Reactive / Ripple 动画 │ - 事件触发逻辑                │
│                       │ - 速度/色彩/亮度参数调整 │                               │
│                       │ - 键盘 Base Lighting     │                               │
├───────────────────────┼──────────────────────────┼───────────────────────────────┤
│ 2. Studio             │ “造一个 Effect 本身长啥样│ - 前台窗口匹配 (cs2.exe)       │
│    (光效工坊/编辑器)   │ (HOW TO RENDER)”         │ - 业务事件发生与否 (event.kill)│
│                       │ - One-shot 动画时序/衰减 │ - 阈值决策 (health < 20)      │
│                       │ - Continuous 连续数学插值│ - 哪个应用切哪个 Profile       │
│                       │ - Blockly / C++ 源码生成 │ - AND / OR / NOT 业务条件树   │
│                       │ - 接收连续 GSI 作为绘制值│ - 决定何时运行 (WHEN TO RUN)   │
├───────────────────────┼──────────────────────────┼───────────────────────────────┤
│ 3. Automation         │ 唯一的 Trigger / Routing │ - Effect 本身的颜色数学计算    │
│    (自动化与编排中枢) │ / Condition Engine       │ - 帧缓冲区的逐点着色细节      │
│                       │ - 前台应用焦点监控       │ - 驱动层的 HID/HAL 发送       │
│                       │ - GSI 状态 / 事件检测    │                               │
│                       │ - AND / OR / NOT 条件树  │                               │
│                       │ - DO: Activate Profile   │                               │
│                       │ - DO: Trigger Effect     │                               │
│                       │ - Composition: Overlay / │                               │
│                       │   Replace / Blend Mode   │                               │
├───────────────────────┼──────────────────────────┼───────────────────────────────┤
│ 4. Game Integration   │ GSI 接入与遥测数据源状态 │ - 创建自动化规则              │
│    (游戏接入与遥测)   │ - GSI 连接状态 (IsActive)│ - 决定激活哪个 Profile        │
│                       │ - 实时字段 Inspector/监视│ - 触发具体光效                │
│                       │ - 游戏配置 CFG 一键安装  │ - 组合逻辑运算                │
│                       │ - 字段字典 (Dictionary)  │ (只提供纯净数据，不做决策)    │
└───────────────────────┴──────────────────────────┴───────────────────────────────┘
```

---

## 3. 核心判定规则与心智模型

### 3.1 判断归属的统一测试法则
1. **“现在应该播放哪个 Effect / 使用哪个 Profile？”**  
   $\rightarrow$ **归属于 `Automation`**  
   *(例: 前台切到 cs2.exe 应该激活哪个方案？检测到 kill 应该触发哪个光效？血量低于 15 应该触发什么警告？)*

2. **“这个 Effect 在当前输入值下应该画成什么？”**  
   $\rightarrow$ **归属于 `Effect` (由 Studio 制作)**  
   *(例: 当前输入 health 是 72，第 12 号按键应该计算成什么 RGB 色彩？当前已运行 350ms，白色应该淡化到百分之几？)*

### 3.2 最终用户心智模型
- **Lighting** = 普通灯效设置（开箱即用，怎么好看怎么选）
- **Studio** = 我想自己造一个灯效（怎么绘制动画、怎么插值渐变）
- **Automation** = 什么时候触发什么（规则、应用绑定、游戏事件联动）
- **Game Integration** = 游戏当前告诉 Aura 什么数据（连接状态、字段监控、配置安装）

---

## 4. 十项架构深度审计与事实核查

针对代码库（C++ 守护进程、ABI 头文件、Blockly 前端与 WinUI 项目），逐项核查事实：

---

### 问题 1：当前 Effect 是如何获得 GSI 的？
**代码事实**：
- 在 [`include/engine/plugin_interface.h`](file:///g:/Aura/include/engine/plugin_interface.h#L45-L53) 中定义了 `EffectContext`：
  ```cpp
  struct EffectContext {
      uint64_t elapsed_ms{0};
      const Keymap& keymap;
      const IGsiReader* gsi{nullptr};
      EffectContext(uint64_t ms, const Keymap& km, const IGsiReader* reader = nullptr)
          : elapsed_ms(ms), keymap(km), gsi(reader) {}
  };
  ```
- 在 [`src/main.cpp`](file:///g:/Aura/src/main.cpp#L807) 主渲染循环中：
  ```cpp
  effect_engine.Tick(frame_buf, keymap, &gsi_adapter.GetState());
  ```
- 在 [`src/engine/effect_engine.cpp`](file:///g:/Aura/src/engine/effect_engine.cpp#L76-L84) 中：
  ```cpp
  profile->Render(elapsed_ms >= started ? elapsed_ms - started : 0, out_frame, keymap, gsi);
  overlay_manager_.ApplyOverlays(elapsed_ms, out_frame, keymap, gsi);
  ```
- `GsiState` 继承并实现了 `IGsiReader` 虚接口（提供 `GetNumber(field, def)`、`GetString(field, def)`、`GetBool(field, def)`）。
- **审计结论**：技术管道完备，Effect 已经能够直接通过 `EffectContext::gsi` 零拷贝读取 GSI 连续数值。

---

### 问题 2：当前 Studio Blockly 中哪些 GSI block 属于 Effect rendering input，哪些属于 Automation conditions？
**代码事实**（见 [`frontend/src/blockly/customBlocks.js`](file:///g:/Aura/frontend/src/blockly/customBlocks.js) 和 [`frontend/src/blockly/toolboxes.js`](file:///g:/Aura/frontend/src/blockly/toolboxes.js)）：

| 积木类型 (Block Type) | 积木文案/功能 | 当前所处位置 | 正确归属边界 | 处置措施 |
| :--- | :--- | :--- | :--- | :--- |
| `gsi_get_number` | 获取 GSI 数值 %1 缺省: %2 | Studio Toolbox / Sensing | **Effect rendering input** | **保留在 Studio**，用于颜色插值计算 |
| `gsi_get_string` | 获取 GSI 文本 %1 缺省: %2 | Studio Toolbox / Sensing | **Effect rendering input** | **保留在 Studio**，用于阵营/武器名称映射 |
| `gsi_get_boolean` | 获取 GSI 布尔值 %1 缺省: %2 | Studio Toolbox / Sensing | **Effect rendering input** | **保留在 Studio**，用于特定键位是否高亮 |
| `gsi_player_health_condition` | 玩家血量 %1 (OP) %2 (VAL) | Studio Toolbox & Orchestrator | **Automation condition** | **移出 Studio Toolbox**，属于 Automation 规则条件 |
| `gsi_c4_state_condition` | C4 状态是 (planted/...) | Studio Toolbox & Orchestrator | **Automation condition** | **移出 Studio Toolbox**，属于 Automation 规则条件 |
| `gsi_state_match` | GSI 状态 %1 %2 %3 | Studio Toolbox & Orchestrator | **Automation condition** | **移出 Studio Toolbox**，属于 Automation 规则条件 |
| `gsi_numeric_compare` | GSI 数值 %1 %2 %3 | Studio Toolbox & Orchestrator | **Automation condition** | **移出 Studio Toolbox**，属于 Automation 规则条件 |
| `orch_event_triggered` | 发生突发事件 %1 ? | Orchestrator Toolbox | **Automation condition** | **归属于 Automation**，用于 GSI Event 判定 |
| `match_process` | 匹配前台进程 %1 激活方案: %2 | Orchestrator Toolbox | **Automation Rule** | **归属于 Automation**，用于 Process 判定 |
| `condition_and / or / not` | 逻辑组合积木 | Orchestrator Toolbox | **Automation condition** | **归属于 Automation**，用于组合条件树 |
| `orch_action_switch_profile` | 切换方案为: %1 | Orchestrator Toolbox | **Automation Action** | **归属于 Automation (Activate Profile)** |
| `orch_action_overlay_pulse` | 当事件发生时播放覆盖 | Orchestrator Toolbox | **Automation Action** | **归属于 Automation (Trigger Effect)** |

---

### 问题 3：当前 event_overlay 生命周期由谁控制？
**代码事实**：
- 查阅 [`include/engine/overlay_manager.h`](file:///g:/Aura/include/engine/overlay_manager.h#L23-L39) 与 [`src/engine/overlay_manager.cpp`](file:///g:/Aura/src/engine/overlay_manager.cpp#L10-L38)：
  - `ActiveOverlay` 结构体内维护 `duration_ms` (默认 1200ms)、`fade_out_ms` (默认 400ms)、`attack_ms` (默认 0ms)、`start_ms`。
  - 权重由 `ActiveOverlay::ComputeWeight(current_ms)` 完全主导（线性爬坡、维持 1.0、线性衰减至 0.0）。
  - 到期判断由 `ActiveOverlay::IsExpired(current_ms)` 完成：`delta_t >= duration_ms`。
  - 清理机制在 `OverlayManager::ApplyOverlays` 第 156 行执行：
    ```cpp
    active_overlays_.erase(std::remove_if(..., [current_ms](const ActiveOverlay& o) { return o.IsExpired(current_ms); }), ...);
    ```
- **审计结论**：当前 event_overlay 的生命周期完全由 `OverlayManager` 和外部规则配置参数控制，Effect 自身无法决定何时结束，也无法控制非线性淡出曲线。

---

### 问题 4：当前 OverlayManager 如何保留 base frame、blend overlay、fade、duration、priority？
**代码事实**（见 [`src/engine/overlay_manager.cpp`](file:///g:/Aura/src/engine/overlay_manager.cpp#L141-L213)）：
1. **保留 Base Frame**：`EffectEngine::Tick` 每一帧先由 `active_profile_->Render(...)` 计算出底色帧写入 `out_frame`；然后将 `out_frame` 直接作为 `in_out_frame` 传入 `ApplyOverlays`，底色完全保留在缓冲区中。
2. **Blend Overlay**：
   - 内部使用预分配的 `temp_overlay_buf_`（零堆内存分配）调用 `overlay.effect->RenderWithContext(...)`。
   - 模式 `"blend"`（Alpha 混合）：
     ```cpp
     if (or_r == 0 && or_g == 0 && or_b == 0) continue; // 叠加层为纯黑时跳过，透传底色
     in_out_frame.buffer[i] = clamp(b_r * (1.0 - w) + or_r * w, 0.0, 255.0);
     ```
   - 模式 `"replace"`：无视纯黑，全键盘执行 `ov_val * w + base_val * (1.0 - w)`。
   - 模式 `"add"`：执行线性加色饱和 `min(255.0, base_val + ov_val * w)`。
3. **Fade**：由 `ComputeWeight` 在最后 `fade_out_ms` 时间窗口内计算出递减权重 $w \in [1.0, 0.0]$。
4. **Duration**：从 `start_ms` 计时，一旦 $current\_ms - start\_ms \ge duration\_ms$ 则判定过期并清除。
5. **Priority**：在 `UpdateBindingsFromGsi` 第 137 行按 `priority` 升序排序 (`std::stable_sort`)，高优先级叠加在低优先级之上。

---

### 问题 5：当前 architecture 是否支持 HealthGradient base + kill white overlay，并且 kill fade 时实时露出最新 HealthGradient frame？
**审计结论与事实求证**：
- **结论：当前底层 C++ 渲染流水线【完全支持且已天然实现】这一行为！**
- **执行推导**：
  1. 在每一帧 `EffectEngine::Tick` 中，第一步调用的就是：
     ```cpp
     profile->Render(..., out_frame, keymap, gsi);
     ```
     如果 Base Effect 是 `HealthGradient`，它在**当前帧**读取最新的 `gsi->GetNumber("player_state.health")`，算出当前血量（例如击杀发生后血量从 80 降到了 35）对应的最新颜色写入 `out_frame`。
  2. 第二步 `overlay_manager_.ApplyOverlays` 在此基础上应用 kill overlay。
     Kill overlay 输出纯白 $(255, 255, 255)$。
     混合公式为：
     $$\text{FinalPixel} = \text{BaseFrame}(\text{current\_health}) \times (1.0 - w) + \text{White} \times w$$
  3. 当 kill 刚发生时 $w = 1.0$，输出全白；
  4. 当 kill 处于淡出阶段（例如 $w = 0.4$），输出 $0.6 \times \text{BaseFrame}(\text{current\_health}) + 0.4 \times \text{White}$。
     这里的 `BaseFrame` **就是当前 35 HP 算出的最新颜色**，绝不是击杀发生前 80 HP 的旧颜色！
  5. 当 kill 结束时 $w = 0.0$，无缝完全显示 35 HP 的真实血量颜色。

---

### 问题 6：当前 Plugin ABI v1 是否能够支持 One-shot Effect、alpha / opacity、finished state？如果不能，给出最小演进方案。
**代码事实与版本编码审计**：
- 查看 [`include/engine/plugin_interface.h`](file:///g:/Aura/include/engine/plugin_interface.h#L63)：
  ```cpp
  constexpr uint32_t AURA_PLUGIN_API_VERSION = 1;
  ```
- 查看 [`frontend/src/blockly/cppTranspiler.js`](file:///g:/Aura/frontend/src/blockly/cppTranspiler.js#L100-L102)：
  ```javascript
  __declspec(dllexport) uint32_t AuraGetPluginApiVersion() {
      return 0x00010000;
  }
  ```
  **版本编码不一致**：C++ 头文件定义的值为整型 `1`，而 Studio Transpiler 生成的十六进制为 `0x00010000` (65536)！
  在进行任何 ABI 扩展前，必须先规范化版本编码处理（例如在 `PluginManager` 中兼容识别 `1` 与 `0x00010000`）。在此之前，**不正式宣称 ABI “v1.1” 协议已定义**。
- **OverlayManager 现状**：
  当前 `ActiveOverlay` 仅持有一个 `std::shared_ptr<Effect> effect`，**完全没有任何插件生命周期或可选导出的 capability metadata**！
- **演进设计（Automation v2 阶段）**：
  需要引入 sidecar/包装层结构（如 `TriggeredEffectInstance`），承载：
  ```cpp
  struct TriggeredEffectInstance {
      std::shared_ptr<Effect> effect;
      std::function<bool(uint64_t)> is_finished_fn; // 探测自 AuraIsEffectFinished
      std::function<float(uint64_t)> get_opacity_fn; // 探测自 AuraGetEffectOpacity
      bool has_custom_lifecycle{false};
      uint64_t watchdog_timeout_ms{5000};
  };
  ```
  采用**可选 C 导出符号探测 (Optional C Export Probe)**，绝对不改动 `class Effect` 虚表内存布局，对纯 ABI v1 插件零破坏。

---

### 问题 7：Trigger Effect 资产引用关系纠偏 (修正版)
**代码事实审计**：
查阅 [`src/main.cpp`](file:///g:/Aura/src/main.cpp#L607-L616) 的真实 `sync_event_overlays` 解析逻辑：
```cpp
auto prof = rule_engine.GetProfile(r.effect);
if (prof && prof->base_effect) {
    b.effect = prof->base_effect;
} else {
    b.effect = aura::PluginManager::Instance().CreateEffect(r.effect);
}
```
- **关键事实纠正**：
  代码实况**并不是**“Plugin -> BuiltinEffects -> Profile fallback”。
  当前实际解析是：**Profile resolution -> PluginManager fallback**！
  目前底层**根本不存在**通用的“根据内建效果名称与参数动态实例化内置 Effect (Builtin Effect)”的通用资产解析层（内建灯效目前只能由 Profile JSON 节点通过 `type: "breathing"` 等解析）。
- **修正后的决策**：
  1. **Studio 自制光效 / 编译插件**：可直接使用稳定的 `effect_name`（由 `PluginManager` 实例化）。
  2. **现有 Legacy event_overlay**：严格保持既有的“优先从 Profile 取 Base Effect，其次尝试 PluginManager”的兼容解析。
  3. **内置效果 (Builtin Effect) 直接触发**：需要先建立明确的参数来源规范（是引用 canonical preset 还是参数化 EffectSpec），留待 Automation v2 统筹决定。
  4. **严禁在 Phase 4 为此设计发明新的 Effect Asset 体系**。

---

### 问题 8：当前 event_overlays 生命周期迁移步骤
1. **阶段 1 (Phase 4 兼容期)**：保留老字段 `duration_ms`, `fade_out_ms`。所有旧配置保持解析并生效。
2. **阶段 2 (Automation v2 双轨期)**：引入 `TriggeredEffectInstance` 探测 One-shot 自主声明；若 Effect 自带时钟，配置中的 `duration_ms` 退居为防死锁看门狗上限。
3. **阶段 3 (配置与 UI 收敛)**：Studio 声明 One-shot 时自定曲线；Automation UI 默认隐藏冗余时长输入，只显示高级覆盖。

---

### 问题 9：Automation v2 对底层规则引擎的复用与最小扩展点 (修正版)
- **正确表述**：
  - 复用 `ConditionNode`
  - 复用 `orchestration.rules`
  - 复用 `event_overlays` / `OverlayManager`
  - 不创建第二套规则引擎
  - **允许针对 Trigger Effect、通用条件触发、Effect-owned lifecycle、capability metadata 做小范围 daemon extension**（不宣称“零 daemon 重构”）。
- **核心审计与最小扩展点发现**：
  当前 [`OverlayManager::UpdateBindingsFromGsi`](file:///g:/Aura/src/engine/overlay_manager.cpp#L104-L120) 中：
  - `trigger == "event"`：依赖于特定的 `event_name` 对应的 `event_sequence` 递增或布尔上升沿。
  - `trigger == "state"`：表现为持续性的 persistent overlay（条件成立一直存在，不成立立刻移除）。
  - **当前无法直接表达**：“`WHEN (任意 GSI 状态条件，如 health < 20) DO Trigger (单次 One-shot 光效)`”！
  - **Daemon 最小扩展点**：在 `OverlayManager` / 规则评估器中增加**条件真值跃迁（Condition Rising Edge）检测器**（记录 `prev_condition_state_[binding_id]`）。当条件从 `false -> true` 跃迁时，触发单次 One-shot Overlay 播放。

---

### 问题 10：Studio Orchestrator UI 安全退场顺序 (修正版)
- **Phase 4 顺序调整**：
  - Studio **默认进入 Effect 制作工作区**。
  - 将 Orchestrator 标为 **`[经典 / 进阶 (Legacy / Advanced)]`** 标签。
  - **严禁完全锁定或隐藏到无法访问**！必须确保已有 `blockly_orchestrator` 用户在迁移完成前仍能查看和导出其规则数据。
- **后续 Automation v2 阶段**：
  - 在 Automation 页面正式提供：1) Legacy 规则只读查看，或 2) 一键迁移/导入到 Automation 规则列表。
  - 待用户平滑迁移完成后，再从 Studio 导航中彻底隐藏 Orchestrator。
  - `OrchestratorSerializer.js` 与 `orchestration.js` 持续保留在代码库中作为兼容解码器。

---

## 5. 经典全场景端到端范例

### 范例 A：Kill One-shot Overlay (击杀瞬间全键盘白光并平滑淡出)
- **Studio (HOW TO RENDER)**: 制作 `kill_pulse` 光效，内部时间轴 0~100ms 纯白，100~450ms 衰减至透明，450ms 标记结束。
- **Automation (WHEN TO RUN & WHAT TO DO)**: `WHEN Foreground == cs2.exe AND Event == kill` $\rightarrow$ `DO Trigger Effect: kill_pulse (Overlay, Alpha)`。
- **Runtime 流水线**: `EffectEngine::Tick` 每一帧重新计算当前血量的底色，叠加层按权重淡出，最新血量颜色自然露出。

### 范例 B：HealthGradient Continuous Base Effect (实时动态血量颜色渲染)
- **Studio (HOW TO RENDER)**: 制作 `cs2_health_gradient` 连续光效，实时由 `ctx.gsi->GetNumber("player_state.health")` 计算 `Lerp(Red, Green, hp / 100.0)`。
- **Automation (WHEN TO RUN & WHAT TO DO)**: `WHEN Foreground == cs2.exe` $\rightarrow$ `DO Activate Profile: CS2_Gaming`（Base Effect 即为该渐变）。
- **Runtime 流水线**: 无事件触发开销，随血量扣减毫秒级平滑变色。

---

## 6. 当前代码差距分析 (Current Technical Gaps)

1. **Studio 积木混杂**：Studio Toolbox 仍暴露条件判定积木；需清理并将条件归属于 Automation。
2. **生命周期归属权倒置**：Overlay 时长硬编码于配置；需在 Automation v2 演进 `TriggeredEffectInstance` 支持自主声明。
3. **版本号编码不统一**：C++ 头文件为 `1`，Transpiler 为 `0x00010000`；需做兼容归一化。
4. **Overlay 缺少条件上升沿检测**：当前仅支持事件名脉冲和持续 state；需增加条件跳变检测以支持任意条件触发 One-shot。
5. **配置并发写入覆盖风险**：Web Studio (19898) 尚无乐观锁 revision 检查，存在覆盖风险（详见第 11 节）。

---

## 7. 向后兼容与 ABI 保护承诺

1. **绝不破坏 ABI v1**：绝不修改 [`class Effect`](file:///g:/Aura/include/engine/effect.h#L12) 虚表偏移，所有既有 DLL 保持 100% 兼容。
2. **保留底层数据结构**：`ConditionNode`、`orchestration.rules`、`orchestration.event_overlays` 保持可用。
3. **历史配置无损读取**：旧版 `rules` 与 `gsi_bindings` 保持兼容读取回退。

---

## 8. 分阶段渐进式迁移方案

- **第一阶段 (本轮已完成)**：边界审计、10 项事实纠偏、确立架构基线文档。
- **第二阶段 (Phase 4 实施)**：聚焦 WinUI 原生 Application Rule CRUD（`process -> profile`），引入独立 `AutomationControlService`，Studio 默认进入 Effect 视图但保留 Legacy 编排入口。
- **第三阶段 (Automation v2 演进)**：构建可视化 WHEN...DO 规则设计器，实现条件上升沿脉冲触发，引入 `TriggeredEffectInstance`。

---

## 9. Phase 4 范围回退与严格锁定

> [!IMPORTANT]
> **Phase 4 范围红线 (Scope Lock)**:  
> Phase 4 **严格且仅实现 Application Rule CRUD (`Foreground Process -> Activate Profile`)**！  
> 
> **Phase 4 明确不实现 (Out of Scope)**:  
> - ❌ 不实现 DND / `suppress_web_ui` 的 UI 编辑  
> - ❌ 不实现 Priority 优先级数字编辑器  
> - ❌ 不实现 Reorder / 拖拽排序  
> - ❌ 不实现 GSI Condition 判定  
> - ❌ 不实现 Event Trigger 触发  
> - ❌ 不实现 Overlay / Replace 播放模式  
> - ❌ 不实现 高级 ConditionNode AST 编辑器  
> 
> **Phase 4 细粒度 REST API 规范 (禁止使用 PUT 整体覆盖)**:  
> - `GET /api/automation/rules` $\rightarrow$ 获取规则列表与当前 revision  
> - `POST /api/automation/rules` $\rightarrow$ 追加规则 `{ expected_revision, process, profile }` (若 process 重复返回 409)  
> - `PATCH /api/automation/rules/{index}` $\rightarrow$ 局部修改 `{ expected_revision, process?, profile? }` (保留 suppress_web_ui 及未知字段)  
> - `DELETE /api/automation/rules/{index}` $\rightarrow$ 精确删除 `{ expected_revision }`  
> - **硬性校验要求**：`index` 仅在对应 revision 下有效；版本不符返回 409；`profile` 必须在 profiles 中存在；`process` 自动规范化为可执行文件小写基名（例如 `"cs2.exe"`）。

---

## 10. Automation v2 详细设计与 Daemon 最小扩展点

在 Phase 4 验收后，Automation v2 将实现：
1. **统一模型**：
   - `WHEN`: Process / GSI State / GSI Event / AND OR NOT
   - `DO`: `Activate Profile` 或 `Trigger Effect`
2. **Playback / Composition Mode**: `Overlay` (叠加) / `Replace` (置换)
3. **Blend Mode**: `Alpha` / `Additive`
4. **Daemon 最小扩展点**：
   - 在 `OverlayManager` 引入 `TriggeredEffectInstance` sidecar。
   - 增加条件跃迁检测，支持任意条件触发 One-shot。

---

## 11. 配置并发与多端写入限制审计 (Config Concurrency Caveat)

### 11.1 审计事实
- 当前 WinUI 与 daemon 交互采用细粒度带 `expected_revision` 乐观锁的 REST 接口（如 `/api/lighting/base` 及即将引入的 `/api/automation/rules`）。版本冲突时 daemon 正确返回 409 Conflict。
- 但是，**Web Studio (端口 19898)** 当前的 [`/api/config` POST 接口](file:///g:/Aura/src/web/web_server.cpp#L857) **完全没有检查 `expected_revision` 或 `If-Match` 头**！
- 跨进程 `NamedConfigLock`（命名互斥锁）**只能保证写文件操作物理串行**，无法解决业务层并发覆盖：
  ```text
  时间线:
  T1: Web Studio (19898) 读取 config.json (Revision A)
  T2: WinUI AutomationPage 通过 PATCH /api/automation/rules 修改规则 -> config.json 更新为 Revision B
  T3: Web Studio 发生整份配置保存 -> 将持有的 Revision A 旧 JSON 整体覆盖回写 config.json (Revision C)
  结果: WinUI 在 T2 所作的自动化规则修改被无声冲掉 (Lost Update)！
  ```

### 11.2 处置策略与工程边界 (Phase 4 必修提升)
1. **合规记录与安全断言**：明确记录该底层限制；`NamedConfigLock` 只解决文件写入的物理串行，不能防止业务级读写时序覆盖。
2. **Phase 4 前提裁定 (提升为同批必修项 - Phase 4 Prerequisite)**：
   因为 Phase 4 期间 Legacy Orchestrator 仍保持可访问，Web Studio (19898) 与 WinUI Automation 将同时作为正式的 config writer 运行。
   为杜绝 Web Studio 发生旧整份配置静默覆盖 WinUI 新 Automation rules 的事故，**必须在 Phase 4 同批为 Web Server (19898) 的 `/api/config` 引入乐观并发检查**：
   - `GET /api/config` 响应体严格保持原始配置 JSON，版本哈希仅通过 HTTP 标头 `ETag: "<revision>"` 返回（绝对不污染 JSON 响应体）；
   - `POST /api/config` 强制要求携带 `If-Match: "<current revision>"` 请求头（缺失直接拒绝且不落盘，返回 HTTP 428 `precondition_required`；过期返回 HTTP 409 `revision_conflict`）；
   - 彻底封堵多端覆盖漏洞。

---

> [!IMPORTANT]
> **本设计基线为后续开发的唯一指导规范。请 Owner 审阅并批准本修订版基线。**
