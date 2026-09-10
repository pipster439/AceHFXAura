# Aura 项目历史文档归档库 (docs/archive/)

本目录归档了 Aura 项目在早期探索（Phase 1 硬件逆向与证伪、Phase 2 网页配置服务初期）过程中形成的 5 份并行报告与阶段性文档。

随着项目进入 Phase 3（CS2 GSI 联动与双进程架构生产级加固），系统的架构红线、接口契约与硬件控制链路已完全收敛，主干代码与单一事实来源（Single Source of Truth）已在仓库根目录与相关源码模块中确立。为避免多份报告结论重叠、时间戳与数据冲突给后续维护者造成困扰，特将这 5 份历史报告归档于此。

---

## 归档文档索引总表

| 归档文档 | 原始日期 | 核心主题与历史价值 | 当前权威替代（单一事实来源） |
| :--- | :---: | :--- | :--- |
| [AURA_HARDWARE_VERIFICATION_REPORT.md](AURA_HARDWARE_VERIFICATION_REPORT.md) | 2026-08-28 | **官方 SDK 证伪报告**：通过实测严密证明了华硕官方 `AuraSdk_x64.dll` COM 接口在 ROG Falchion Ace HFX 键盘上返回 `S_OK` 伪成功但物理灯效无效，揭示其专用内核驱动与奥创服务的独占管控。 | [`AGENT.md`](../../AGENT.md#11-新发现aura-sdk-路径也已实测失败转向-windows-dynamic-lighting-api) §11–§12、[`WORKING_SOLUTION_REPORT111.md`](WORKING_SOLUTION_REPORT111.md) |
| [WORKING_SOLUTION_REPORT111.md](WORKING_SOLUTION_REPORT111.md) | 2026-08-29 | **硬件驱动直通突破报告**：首次记录绕过通用 SDK，直接挂载 `AacKbHal_x64.dll` 的 `CLSID_ClaymoreHal`、调用 `Set_L_STD_SINGLE_XY`（VTable Index 19）以及 `+0x6C`/`+0x74` 内存寻址实现 25 FPS 易失性 RAM 推流的可行方案。 | C++ 驱动层 [`include/aura/aura_adapter.h`](../../include/aura/aura_adapter.h) / [`src/aura/aura_adapter.cpp`](../../src/aura/aura_adapter.cpp) 以及共享 Python 驱动 [`tools/py/aura_hal.py`](../../tools/py/aura_hal.py) |
| [LIGHTING_FIX_REPORT.md](LIGHTING_FIX_REPORT.md) | 2026-08-30 | **灯效管线与按键钩子排查**：定位了早期“前端有效果但键盘不亮”的根因（测试运行了未编入 `WH_KEYBOARD_LL` 钩子的旧二进制），并验证了 11 种灯效及 25 FPS 推流稳定性。 | [`src/monitor/foreground_monitor.cpp`](../../src/monitor/foreground_monitor.cpp)、[`src/engine/effect_engine.cpp`](../../src/engine/effect_engine.cpp) 及根目录 [`README.md`](../../README.md) |
| [request-working-solution-report-prompt.md](request-working-solution-report-prompt.md) | 2026-08-29 | **核查 Prompt 规范**：要求产出“可验证的事实”而非“叙事性猜测”，制定了包含 Process Monitor 记录、真实代码、带时间戳日志的严谨核查标准。 | 归档留存，作为工程审计历史规范参考 |
| [README_PER_KEY.md](README_PER_KEY.md) | 2026-08-29 | **早期单键 CLI 工具说明书**：早期 Python 独立点灯脚本 `set_per_key.py` 的用法与键位命名说明。 | 批次 D (R21) 治理后的 [`tools/py/`](../../tools/py/) 工具集与权威键位表 [`calibrated_keymap.json`](../../calibrated_keymap.json) |

---

## 历史背景与演进说明

1. **硬件控制路径演进**：
   - 早期尝试：华硕官方 `AuraSdk_x64.dll` / Windows 动态照明 (WDL) → **实测证伪，完全无效**（见 [`AURA_HARDWARE_VERIFICATION_REPORT.md`](AURA_HARDWARE_VERIFICATION_REPORT.md)）。
   - 逆向突破：直接操作底层驱动 `AacKbHal_x64.dll` 的 `CLSID_ClaymoreHal`（见 [`WORKING_SOLUTION_REPORT111.md`](WORKING_SOLUTION_REPORT111.md)）。
   - 生产落地：C++ 原生重构为 `AuraAdapter` 守护进程组件，并于批次 B 引入 `ComScope` 严格 RAII 生命周期守卫、批次 D (R21) 抽取 Python 共享模块 [`tools/py/aura_hal.py`](../../tools/py/aura_hal.py)。
2. **文档与数据单一事实来源**：
   - 键盘 68 键硬件 LED ID 与物理键位映射，**必须以 [`calibrated_keymap.json`](../../calibrated_keymap.json) 为唯一权威来源**。任何早期文档中的临时映射表均已作废。
   - 生产构建、配置规范与使用方式，以根目录 [`README.md`](../../README.md) 为准；架构演进与历史决策以根目录 [`AGENT.md`](../../AGENT.md) 为准；任务修复与代码治理以根目录 [`HANDOVER.md`](../../HANDOVER.md) 与 [`REMEDIATION_PLAN.md`](../../REMEDIATION_PLAN.md) 为准。
