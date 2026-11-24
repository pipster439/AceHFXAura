# Aura MCP 外部接入服务 (`aura-mcp`)

将原本内置于 Studio 的 AI 对话面板替换为标准 **stdio MCP Server**（纯 Python 标准库实现，零第三方依赖），供 Antigravity、Claude Desktop、Cursor 等外部 AI 客户端直接操作 Aura 的灯效编排、插件编译与磁轴遥测能力。

## 快速配置

在支持的 MCP 客户端配置文件中添加以下条目（或在 Aura「插件」页 / Studio「MCP 外部接入」一键复制）：

```json
{
  "mcpServers": {
    "aura": {
      "command": "python",
      "args": [
        "G:\\Aura\\plugins\\mcp\\aura_mcp_server.py"
      ],
      "env": {
        "AURA_DAEMON_URL": "http://127.0.0.1:19897",
        "AURA_STUDIO_URL": "http://127.0.0.1:19898"
      }
    }
  }
}
```

> 运行前提：请先启动 `Aura.exe` 或 `aura_daemon.exe`，使 `127.0.0.1:19897`（守护进程）与 `127.0.0.1:19898`（Studio 编译/网页服务）处于可用状态。

## 暴露能力概览

### Tools
| 工具名 | 用途 |
|---|---|
| `aura_status` | 探测守护进程与 Studio 连通性、全局 FPS、插件/IR 程序数量与磁轴状态 |
| `aura_list_plugins` | 查看已加载原生 DLL / Studio IR 插件及外部桥接；支持 `rescan: true` |
| `studio_list_programs` | 查询 `.aurair` 受限灯效程序目录（`GET /api/programs`） |
| `studio_publish_ir_program` | 安装或更新 EffectProgram IR v1 程序并可选立即启用 |
| `studio_manage_program` | 对指定 IR 程序执行 `enable` / `disable` / `uninstall` / `rollback` |
| `studio_compile_cpp_effect` | 编译 C++ ABI-v1 / Hall Analog 插件为 `plugins/effect_<name>.dll` 并自动重扫 |
| `studio_list_blockly_projects` | 读取 `blockly_effects` 工程列表或单个工程 JSON |
| `studio_save_blockly_project` | 基于 `ETag` + `If-Match` 安全保存或更新指定 Blockly 工程 |
| `studio_preview_frame` | 向 `POST /api/preview` 推送实时单帧 RGB 预览 |
| `aura_lighting_get` | 读取全局 FPS、当前基础灯效、方案列表与预设清单 |
| `aura_lighting_set_base` | 使用乐观锁 `expected_revision` 修改基础灯效或全局 FPS |
| `aura_hall_telemetry` | 读取 68 键霍尔行程快照或安全启停共享内存广播流 |
| `aura_automation_rules` | 查看 Automation v2 规则与能力清单，或校验规则 JSON |

### Resources
- `aura://schemas/effect-ir-v1` — EffectProgram IR v1 完整指令/表达式规范与示例
- `aura://schemas/native-plugin-cpp` — 原生 C++ 插件 + `AuraBindHallAnalog` v1 模板
- `aura://keymap/m605-68` — ROG Falchion Ace HFX 68 键精简键位与 `led_id` 坐标表
- `aura://overview` — 实时状态快照

### Prompts
- `create_ir_effect` — 生成并发布 EffectProgram IR v1 灯效
- `create_hall_reactive_cpp_plugin` — 编写并编译结合霍尔深度的原生 C++ 插件
