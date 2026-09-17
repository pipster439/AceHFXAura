# Aura 文档索引与规范导航 (docs/README.md)

本文档是 Aura（ROG Falchion Ace HFX 键盘硬件灯效控制系统）的**文档体系总索引与单一事实来源（Single Source of Truth, SSOT）规范导航**。

为了确保长期维护过程中架构设计、代码实现、硬件协议与测试验收之间的高度一致性，杜绝多份报告结论冲突或过时信息误导，本项目所有文档严格遵循分层治理规范。

---

## 1. 文档体系层级与单一事实来源规范

```
G:/Aura/
├── README.md                      # [层级 1] 生产基线：项目总览、双进程架构、配置指南、CS2 GSI 规范
├── AGENT.md                       # [层级 1] 演进与决策：Phase 1–3 里程碑、核心技术决策、已知风险与取舍
├── CMakeLists.txt                 # [层级 1] 构建基线：C++17 CMake 工程配置
├── calibrated_keymap.json         # [层级 1] 硬件基准：68 键物理键位与硬件 LED ID 权威映射（唯一事实来源）
├── config.json                    # [层级 1] 运行时配置
├── config.example.json            # [层级 1] 配置模板
│
├── tools/                         # [运维与工具]
│   ├── py/aura_hal.py             # 统一底层硬件驱动封装
│   ├── gui_calibrator.py          # 可视化键位标定器
│   ├── run_gui_calibrator.bat     # 标定器快速启动脚本
│   ├── set_per_key.py             # 独立单键控制 CLI
│   └── e2e_key_test.py            # 硬件端到端校验脚本
│
├── tests/                         # [测试体系]
│   ├── fixtures/                  # 固化测试数据
│   ├── test_aura_hal.py           # Python 驱动单元与回归测试 (CTest)
│   ├── test_cs2_gsi.py            # CS2 GSI 自动化测试套件
│   ├── test_gsi_rules.cpp         # 规则引擎与 GSI 核心单元测试 (CTest)
│   ├── test_diag_hook.cpp         # 交互式前台监控诊断工具
│   ├── test_com.cpp               # COM 驱动加载探针
│   └── test_util.h                # 测试断言宏辅助库
│
└── docs/                          # [文档中心]
    ├── README.md                  # 本索引导航文件
    ├── calibrated_keymap.md       # 硬件物理按键分布与通道对照说明
    ├── reports/                   # [审计报告与修复总纲]
    │   ├── AURA_CODE_REVIEW_REPORT.md
    │   ├── REMEDIATION_PLAN.md
    │   ├── REPORT_CROSSCHECK.md
    │   ├── ANTIGRAVITY_CROSSCHECK.md
    │   └── HANDOVER.md
    └── archive/                   # [历史归档] 早期探索、证伪与过渡期报告
```

---

## 2. 当前有效规范文档索引

### 2.1 核心生产与架构规范（Active Production Specifications）

| 文档 | 路径 | 核心范围与职责 |
| :--- | :--- | :--- |
| **全量技术手册** | [`COMPLETE_DOCUMENTATION.md`](COMPLETE_DOCUMENTATION.md) | **全系统终极技术与使用手册**。涵盖双进程架构、硬件直通逆向、安全兼容性 Gate、CS2 GSI 联动状态机、Blockly Studio、Plugin SDK 原生编译、配置规范、构建打包与质量验收全流程。 |
| **项目生产总览** | [`README.md`](../README.md) | 针对生产部署与使用者：C++17 双进程架构、MSVC 构建与运行参数、配置中心与热重载、CS2 GSI 字段与游戏事件状态机规范。 |
| **决策演进日志** | [`AGENT.md`](../AGENT.md) | 针对架构师与维护者：完整记录 Phase 1（驱动逆向与通道寻址）、Phase 2（网页配置与监护体系）、Phase 3（CS2 GSI 适配器与批次 A–D 加固治理）的全流程工程决策与技术考量。 |
| **任务交接指南** | [`HANDOVER.md`](reports/HANDOVER.md) | 针对后续修复与功能开发者：总结 6 大不可逾越的架构红线、0 error / 0 warning 构建基线、CTest 自动化回归测试方法、关键编码陷阱（显式定界符 raw string、宽字符路径链路、MSVC NDEBUG 规避）。 |

### 2.2 缺陷修复与审计体系（Remediation & Review Specifications）

| 文档 | 路径 | 核心范围与职责 |
| :--- | :--- | :--- |
| **修复方案总纲** | [`REMEDIATION_PLAN.md`](reports/REMEDIATION_PLAN.md) | **缺陷处理的权威基准**。覆盖 R1 至 R23 项完整问题定位、经双向交叉验证的最小化改动方案及逐项验收标准。 |
| **跨代理核查报告** | [`REPORT_CROSSCHECK.md`](reports/REPORT_CROSSCHECK.md) | 记录独立代理对审查意见的交叉审核、可行性判定与分歧裁决。 |
| **交叉核查意见** | [`ANTIGRAVITY_CROSSCHECK.md`](reports/ANTIGRAVITY_CROSSCHECK.md) | 记录对方案机理的更正说明、边界条件防范与防引入新缺陷的论证过程。 |
| **审查全景报告** | [`AURA_CODE_REVIEW_REPORT.md`](reports/AURA_CODE_REVIEW_REPORT.md) | 初始 80 项审查发现的完整记录（仅供比对查阅）。 |

### 2.3 硬件驱动与键位标定基准（Hardware & Keymap SSOT）

| 文件 | 路径 | 权威性与使用准则 |
| :--- | :--- | :--- |
| **68 键硬件映射表** | [`calibrated_keymap.json`](../calibrated_keymap.json) | **全工程唯一权威事实来源**。记录 ROG Falchion Ace HFX 键盘全部 68 个物理键的按键名称、硬件 LED ID 以及点对点点亮校验状态。严禁在代码或脚本中硬编码未经标定的 LED ID。 |
| **标定对照表** | [`calibrated_keymap.md`](calibrated_keymap.md) | 硬件键位矩阵与通道映射的视觉对照说明文档。 |
| **统一 Python 驱动** | [`tools/py/aura_hal.py`](../tools/py/aura_hal.py) | Python 工具链的单一底层硬件驱动模块，封装 COM/CLSID、VTable 偏移以及硬件寻址逻辑。 |

---

## 3. 历史归档库 (docs/archive/)

位于 [`docs/archive/`](archive/README.md) 目录，完整保留了项目早期的探索历程：
1. **官方 SDK 证伪报告** ([`AURA_HARDWARE_VERIFICATION_REPORT.md`](archive/AURA_HARDWARE_VERIFICATION_REPORT.md))：早期证明官方 `AuraSdk_x64.dll` 无法控制目标硬件的实验记录。
2. **底层直通方案报告** ([`WORKING_SOLUTION_REPORT111.md`](archive/WORKING_SOLUTION_REPORT111.md))：首次逆向发现 `AacKbHal_x64.dll` 专属 `ClaymoreHal` 驱动接口的突破记录。
3. **灯效与按键钩子排查** ([`LIGHTING_FIX_REPORT.md`](archive/LIGHTING_FIX_REPORT.md))：早期针对旧二进制文件导致按键钩子未生效的问题排查。
4. **核查 Prompt 规范** ([`request-working-solution-report-prompt.md`](archive/request-working-solution-report-prompt.md))：证据型文档撰写规范。
5. **早期单键点灯说明** ([`README_PER_KEY.md`](archive/README_PER_KEY.md))：已被批次 D (R21) 共享驱动层全面取代的早期独立点灯脚本说明。

---

## 4. 维护与演进准则

1. **修改代码前先读规范**：修改核心逻辑必须对照 `HANDOVER.md` 架构红线与 `REMEDIATION_PLAN.md` 验收标准。
2. **杜绝分散信息源**：新的工程决策写入 `AGENT.md`，使用说明更新到 `README.md`，严禁在根目录新建临时报告文件。
3. **键位数据禁止分叉**：所有 C++ 效果、单元测试及 Python 辅助工具涉及按键映射时，必须消费 `calibrated_keymap.json`。
