# Aura MCP 外部接入服务 (`aura-mcp`)

将原本内置于 Studio 的 AI 对话面板替换为标准 **stdio MCP Server**（纯 Python 标准库实现，零第三方依赖），供 Antigravity、Claude Desktop、Cursor 等外部 AI 客户端直接操作 Aura 的灯效编排、插件编译、磁轴参数、闭环校验、Profile 与 Automation v2 规则。

## 快速配置

在支持的 MCP 客户端配置文件中添加以下条目（或在 Aura「插件」页 / Studio「MCP 外部接入」一键复制）：

```json
{
  "mcpServers": {
    "aura": {
      "command": "python",
      "args": [
        "<AURA_DATA_DIR>\\plugins\\mcp\\aura_mcp_server.py"
      ],
      "env": {
        "AURA_DAEMON_URL": "http://127.0.0.1:19897",
        "AURA_STUDIO_URL": "http://127.0.0.1:19898"
      }
    }
  }
}
```

> 路径说明：标准部署下 `<AURA_DATA_DIR>` 默认为 `%LOCALAPPDATA%\Aura`（或从源码目录直接指向 `<REPO_ROOT>\plugins\mcp\aura_mcp_server.py`）；也可运行安装包根目录 `deploy_aura.bat -ExportMcpConfig` 自动生成带本机绝对路径的 `aura-mcp-config.json`。
>
> 运行前提：请先启动 `Aura.exe` 或 `aura_daemon.exe`，使 `127.0.0.1:19897`（守护进程）与 `127.0.0.1:19898`（Studio 编译/网页服务）处于可用状态。

## 暴露能力概览

### Tools (22)
| 分类 | 工具名 | 用途 |
|---|---|---|
| 总览与插件 | `aura_status` | 探测守护进程与 Studio 连通性、全局 FPS、插件/IR 数量与磁轴状态 |
| 总览与插件 | `aura_list_plugins` | 查看已加载原生 DLL / IR 插件及 Wooting/MCP 桥接；支持 `rescan: true` |
| Studio / IR | `studio_list_programs` | 查询 `.aurair` 受限灯效程序目录（`GET /api/programs`） |
| Studio / IR | `studio_publish_ir_program` | 安装或更新 EffectProgram IR v1 程序并可选立即启用（支持 `hall_depth`/`hall_key_depth`/`hall_feature` 实时霍尔原语，启用即自动拉起霍尔流） |
| Studio / IR | `studio_manage_program` | 对指定 IR 程序执行 `enable` / `disable` / `uninstall` / `rollback` |
| Studio / C++ | `studio_compile_cpp_effect` | 编译 C++ ABI-v1 / Hall Analog 插件为 `plugins/effect_<name>.dll` 并自动重扫（激活含 `AuraBindHallAnalog` 插件时自动拉起霍尔流） |
| Studio / Blockly | `studio_list_blockly_projects` | 读取 `blockly_effects` 工程列表或单个工程 JSON |
| Studio / Blockly | `studio_save_blockly_project` | 基于 `ETag` + `If-Match` 安全保存或更新指定 Blockly 工程 |
| 实时预览 | `studio_preview_frame` | 向 `POST /api/preview` 推送实时单帧 RGB 预览（或停止预览） |
| 基础灯效 | `aura_lighting_get` | 读取全局 FPS、当前基础灯效、方案列表与预设清单 |
| 基础灯效 | `aura_lighting_set_base` | 使用乐观锁 `expected_revision` 修改基础灯效或全局 FPS |
| 基础灯效 | `aura_lighting_hall_overlay` | 读取或配置叠加在任意 Aura 灯效之上的霍尔压感联动层（支持 `glow` / `gradient` / `halo`） |
| 霍尔与闭环校验 | `aura_hall_telemetry` | 读取 68 键霍尔行程快照或安全启停共享内存广播流 |
| 霍尔与闭环校验 | `aura_hall_verify` | 启动/查询/停止物理按键触发点（AP）、RT 或死区闭环校验 |
| 磁轴设置 | `aura_magnetic_set_actuation` | 设置单键 / 批量 / 全局触发行程（0.1–4.0 mm，支持按键名自动解析） |
| 磁轴设置 | `aura_magnetic_set_rapid_trigger` | 开启/关闭单键或批量 Rapid Trigger（0.1–2.5 mm，支持显式 DKS 冲突消解） |
| 磁轴设置 | `aura_magnetic_set_deadzone` | 设置单键 / 批量 / 全局上下死区（0.0–0.5 mm），或重置全部单键死区 |
| 磁轴设置 | `aura_magnetic_set_dks` | 配置 4 槽位 DKS 动态按键或恢复 Standard 模式（自动校验并拦截 FN 目标） |
| 磁轴设置 | `aura_magnetic_set_speedtap` | 管理 SpeedTap 急停配对、主开关或重置运行时基线 |
| 磁轴设置 | `aura_magnetic_maintenance` | 开关固件静态压感灯效，或确认解除持久安全隔离（quarantine） |
| 设备方案 | `aura_device_profiles` | 读取/激活/创建修改设备方案或刷新硬件槽位观察 |
| 自动化 | `aura_automation_rules` | 读取/校验/创建/稀疏更新/删除/替换 Automation v2 规则 |

### Resources (4)
- `aura://schemas/effect-ir-v1` — EffectProgram IR v1 完整指令/表达式规范与示例
- `aura://schemas/native-plugin-cpp` — 原生 C++ 插件 + `AuraBindHallAnalog` v1 模板
- `aura://keymap/m605-68` — ROG Falchion Ace HFX 68 键 `led_id`、几何坐标、`logical_id` 与 `hid_usage`
- `aura://overview` — 实时状态快照

### Prompts (3)
- `create_ir_effect` — 生成并发布 EffectProgram IR v1 灯效
- `create_hall_reactive_cpp_plugin` — 编写并编译结合霍尔深度的原生 C++ 插件
- `tune_magnetic_profile` — 读取当前磁轴/方案状态并安全调整 AP/RT/死区/急停与闭环校验
