# Aura (ROG Falchion Ace HFX) 完整技术与使用手册
> **版本**：v0.1.0-alpha.1  
> **适用硬件**：ASUS ROG Falchion Ace HFX (魔导士 Ace HFX 机械键盘，VID `0x0B05`, PID `0x1B7E`)  
> **支持系统**：Windows 11 / Windows 10 (x64)  
> **文档性质**：项目全景架构、驱动兼容性、游戏联动、光效开发与运维全量技术手册

---

## 目录
1. [项目定位与核心特性](#1-项目定位与核心特性)
2. [总体系统架构与双进程模型](#2-总体系统架构与双进程模型)
3. [硬件直通与底层驱动安全 Gate](#3-硬件直通与底层驱动安全-gate)
4. [CS2 游戏状态集成 (GSI) 与事件编排引擎](#4-cs2-游戏状态集成-gsi-与事件编排引擎)
5. [Blockly 光效工作室与 Plugin SDK 体系](#5-blockly-光效工作室与-plugin-sdk-体系)
6. [配置文件全量规格说明 (config.json)](#6-配置文件全量规格说明-configjson)
7. [构建、打包与独立分发体系](#7-构建打包与独立分发体系)
8. [质量防护网与验收规范](#8-质量防护网与验收规范)
9. [故障排查与常见问题 (FAQ)](#9-故障排查与常见问题-faq)
10. [免责声明与第三方资产声明](#10-免责声明与第三方资产声明)

---

## 1. 项目定位与核心特性

**AceHFXAura (Aura)** 是一套专为 **ASUS ROG Falchion Ace HFX**（魔导士 Ace HFX）有线机械键盘打造的高性能、低延迟、完全独立运行的硬件灯效与游戏状态联动系统。

### 痛点与解决思路
传统官方控制软件（如 Armoury Crate / 奥创中心）存在安装体积庞大（数 GB）、后台常驻服务众多、对 Counter-Strike 2 等竞技游戏的实时状态联动能力有限、以及用户无法自定义底层渲染着色算法等痛点。

本项目通过直接调用华硕底层硬件抽象层动态库（`AacKbHal_x64.dll`），实现了：
- **免奥创独立直通**：内存占用仅约数十兆，无需后台堆叠大型臃肿套件；
- **微秒级高刷推流**：以 60 FPS (16.6ms) 高精度平滑刷新 68 颗按键与 15 颗顶部独立 Touch Bar 灯珠；
- **全深度 CS2 GSI 联动**：深度解析游戏实时状态，将低血量、闪光致盲、燃烧灼烧、C4 倒计时、击杀/爆头/胜负等事件毫秒级投射到键盘硬件；
- **零门槛双引擎 Studio**：内置网页版 Google Blockly 视觉化积木开发环境，既能在浏览器端纯 JS 实时预览，又能一键本地转译为原生 C++17 动态链接库（DLL）热加载运行；
- **严格的安全与容灾防护**：内置驱动兼容性安全 Gate、内存防崩补丁（Fail-closed 原则）、进程异常自动重启监护与多级回退容灾机制。

---

## 2. 总体系统架构与双进程模型

为了兼顾**高频硬件实时渲染的绝对稳定性**与**网页配置界面的灵活性**，Aura 采用了物理隔离的双进程体系：

```
                              ┌──────────────────────────────────────┐
                              │  单文件启动器 (Aura.exe)            │
                              │  - 运行时自解压核心资产与 SDK        │
                              │  - 统一拉起后台守护并托管进程生命周期  │
                              └──────────────────┬───────────────────┘
                                                 │
                        ┌────────────────────────┴────────────────────────┐
                        ▼                                                 ▼
     ┌────────────────────────────────────┐             ┌──────────────────────────────────┐
     │ 核心硬件守护 (aura_daemon.exe)     │             │ 配置与编译服务 (aura_web_ui.exe) │
     │                                    │             │                                  │
     │ 1. 硬件通信与流控 (AuraAdapter)    │             │ 1. 嵌入式 HTTP 服务 (端口 19898) │
     │    - AacKbHal_x64.dll 直通         │             │    - 托管前端单文件 web/index.html│
     │    - 128 通道硬件对齐缓冲推流      │ 配置文件监控 │    - RESTful API 状态管理        │
     │ 2. 渲染引擎 (EffectEngine)         │ (config.json)│ 2. 实时监护引擎 (WebSupervisor)  │
     │    - 60 FPS 稳定定时帧驱动         │ ────────────│    - 前端活跃度检测与自动拉起    │
     │    - 原生动态插件热加载            │             │    - 游戏对局中 DND 免打扰静默   │
     │ 3. 游戏遥测接收 (GsiAdapter)       │             │ 3. 原生 C++ 编译器前端           │
     │    - HTTP 接收 CS2 GSI JSON        │             │    - 发现 MSVC 工具链            │
     │    - 状态机比对与事件生成          │             │    - 定位 Plugin SDK 并编译 DLL  │
     │ 4. 覆盖层管理器 (OverlayManager)   │             │ 4. 前端 Google Blockly 工作室    │
     │    - 瞬时击杀/安包脉冲光效         │             │    - 纯 JS 实时仿真              │
     │    - Attack/Sustain/Fade 混合      │             │    - C++17 零堆分配代码生成器    │
     └────────────────────────────────────┘             └──────────────────────────────────┘
```

### 2.1 核心进程职责划分
1. **守护进程 (`aura_daemon.exe`)**：
   - 拥有硬件 COM 句柄，独占物理键盘的灯效写入权限；
   - 维持高精度时钟循环，持续调用当前活跃 Profile 的渲染逻辑，并将计算出的 RGB 缓冲区通过 HAL 写入硬件；
   - 监听本地端口（默认 `3000`）接收 CS2 发送的 HTTP POST 遥测报文，触发内部事件流；
   - 通过文件监视器监听 `config.json` 的修改，实现毫秒级无感知热重载。
2. **Web 与配置管理进程 (`aura_web_ui.exe`)**：
   - 内置轻量级多线程 HTTP 引擎（基于 `cpp-httplib`），监听本地 `19898` 端口；
   - 服务单文件前端网页 `web/index.html`（基于 React 18 + Vite + Tailwind CSS + Google Blockly 单文件打包）；
   - 提供配置读取、保存草稿、遥测诊断数据下发等 API 接口；
   - 作为 Studio 的原生编译后台：调用本机 MSVC 编译器将 Blockly 转译生成的 C++ 代码编译为 `plugins/effect_*.dll`，并通知 daemon 加载。
3. **独立单文件主程序 (`Aura.exe`)**：
   - 采用 Win32 原生 C++ 编写，将两个核心可执行文件、华硕驱动、键位映射表、网页资源以及 **Aura Plugin SDK** 全部内嵌为 PE 资源；
   - 首次启动自动将运行时解压至 `%LOCALAPPDATA%\Aura\runtime`；
   - 使用 Job Object 将双子进程编组托管，主程序关闭时操作系统自动回收所有后台子进程，绝不残留孤儿僵尸进程。

---

## 3. 硬件直通与底层驱动安全 Gate

### 3.1 硬件特征与 128 通道对齐规范
ASUS ROG Falchion Ace HFX 是一款 68 键紧凑型电竞机械键盘，并带有顶部 15 颗独立触控灯条（Touch Bar）：
- **物理按键数量**：68 键（美式 ANSI 配列）；
- **独立灯条数量**：15 颗连续 LED（自左至右分布于键盘顶侧）；
- **驱动写入规范**：华硕底层 `AacKbHal_x64.dll` 要求输入严格定长的 128 个 `AuraLedColor` 元素结构体数组。任何超出 128 或未对齐的数据均会导致驱动越界写入崩溃。
- **填充对齐机制**：Aura 在初始化阶段通过权威映射表 `calibrated_keymap.json` 将 68 颗按键映射到指定通道，并将第 68~82 通道映射为灯条 LED，剩余未使用的通道强制补零填充（Zero-padding）至 128 长度，确保物理安全。

### 3.2 驱动兼容性安全 Gate (Fail-Closed)
底层驱动通过逆向解析获得，盲目修补或调用未经测试的 DLL 版本极易导致蓝屏、固件死锁或内存访问违规。因此系统构建了严格的**两阶段兼容性 Gate**：

```
                              [ 加载底层 AacKbHal_x64.dll ]
                                            │
                                            ▼
                    ┌───────────────────────────────────────────────┐
                    │ 阶段 1：文件白名单 Gate (ValidateHalFileGate) │
                    │ - 计算磁盘 DLL 的 SHA-256 散列值               │
                    │ - 读取 PE 版本资源 (FileVersion)              │
                    └───────────────────────┬───────────────────────┘
                                            │
                     ┌──────────────────────┴──────────────────────┐
                     │ 白名单匹配？ (v1.3.46.0 / 52d575bf...)       │
                     ▼                                             ▼
                  [ 是 ]                                        [ 否 ]
                     │                                             │
                     ▼                                             ▼
  ┌─────────────────────────────────────┐            ┌───────────────────────────┐
  │ 阶段 2：内存签名 Gate                │            │ 坚决拒绝 (Fail-Closed)    │
  │ (ValidateHalModuleGate)             │            │ 阻断硬件驱动与内存修补      │
  │ - 验证 PE 镜像边界与 NT 头           │            │ 提示: Unsupported ASUS    │
  │ - 校验 Logger::Log 函数入口指令序言 │            │       HAL version         │
  └──────────────────┬──────────────────┘            └───────────────────────────┘
                     │
         ┌───────────┴───────────┐
         │ 指令序言匹配？         │
         ▼                       ▼
      [ 是 ]                   [ 否 ] ──► 触发 SignatureMismatch 拦截
         │
         ▼
  ┌─────────────────────────────────────┐
  │ 应用内存防崩补丁 (ApplyAacDriverPatch)│
  │ - Logger::Log 写入 0xC3 (ret)       │
  │ - EnableLog 标志位置 0               │
  └──────────────────┬──────────────────┘
                     │
                     ▼
             [ 允许进入硬件推流 ]
```

- **官方已验证驱动信息**：
  - **版本号**：`1.3.46.0`
  - **SHA-256**：`52d575bf942b7551b3f120c446bf0d853e36f9225c6b9a17407a80e0b1829f04`
  - **Logger::Log RVA**：`0x7ABE0`（16 字节序言：`40 55 57 41 54 41 56 41 57 48 8D AC 24 E0 EF FF`）
  - **EnableLog RVA**：`0x1CB85C`
- **内存读取防御**：在解析 PE 头部和比对指令序言时，全部采用封装有结构化异常处理（SEH `__try/__except`）的底层读取函数，杜绝因格式畸变引发的未捕获访问违规崩溃。

---

## 4. CS2 游戏状态集成 (GSI) 与事件编排引擎

### 4.1 CS2 GSI 配置部署
Counter-Strike 2 通过 HTTP 协议向本机发送 JSON 遥测报文。用户需将配置文件 `gamestate_integration_aura.cfg` 放置于 CS2 的配置目录下：
`[Steam安装目录]\steamapps\common\Counter-Strike Global Offensive\game\csgo\cfg\gamestate_integration_aura.cfg`

配置关键内容：
```vdf
"Aura Integration v1.0"
{
    "uri" "http://127.0.0.1:3000"
    "timeout" "5.0"
    "buffer"  "0.05"
    "throttle" "0.02"
    "heartbeat" "1.0"
    "data"
    {
        "provider"                  "1"
        "map"                       "1"
        "round"                     "1"
        "player_id"                 "1"
        "player_state"              "1"
        "player_weapons"            "1"
        "player_match_stats"        "1"
    }
}
```

### 4.2 GSI 状态机与瞬时事件脉冲机制
GSI 适配器不仅扁平化解析 JSON 字段，还负责执行状态跃迁分析，生成**瞬时脉冲事件 (Transient Pulses)**：

| 触发事件 | 触发条件判断（上升沿） | 持续时间 (Window) | 典型灯效应用 |
| :--- | :--- | :--- | :--- |
| `event.kill` | `player.match_stats.kills` 递增 | 1500 ms | 键盘全键红色爆破扩散涟漪 |
| `event.headshot` | `player.match_stats.headshots` 递增 | 1500 ms | 金色星芒高频频闪 |
| `event.damage` | `player.state.health` 突减 | 800 ms | 屏幕与按键红色受击震荡波 |
| `event.bomb_planted` | `round.bomb` 状态跃迁为 `planted` | 2000 ms | 警报式全键橙红色交替呼吸 |
| `event.bomb_defused` | `round.bomb` 状态跃迁为 `defused` | 2000 ms | 拆包成功蓝色恒定波纹 |
| `event.round_won` | 回合胜负判定，且赢家阵营等于自身阵营 | 3000 ms | 胜利彩虹涌动 |
| `event.round_lost` | 回合胜负判定，且赢家阵营不等于自身阵营 | 3000 ms | 失败灰色暗淡渐隐 |

### 4.3 条件 AST 规则树与 DND 抑制
在 `config.json` 的 `orchestration.rules` 中，支持多层嵌套的布尔逻辑树：
- **操作符**：`and`、`or`、`not`、比较符（`==`、`!=`、`<`、`<=`、`>`、`>=`）；
- **免打扰模式 (DND)**：当某条规则设置了 `dnd: true`（如用户正在全屏打字或处在极高危险对局中），守护进程将发出通知抑制 Web UI 的后台轮询，降低系统扰动，保障竞技帧率极致纯净。

### 4.4 瞬态叠加层 (OverlayManager) 混合生命周期
叠加层（`event_overlays`）覆盖在基础 Profile 之上，具有完整的 ADSR 权重衰减曲线：
1. **Attack（渐入期）**：权重从 `0.0` 线性增长至 `1.0`（如 100ms）；
2. **Sustain（维持期）**：权重保持 `1.0`（如 500ms）；
3. **Fade Out（渐出期）**：权重从 `1.0` 平滑衰减至 `0.0`（如 800ms）；
4. **混合模式**：支持 `replace`（覆盖替换底色）与 `alpha`（透明度混合），结束后自动销毁并平滑还原底色。

---

## 5. Blockly 光效工作室与 Plugin SDK 体系

### 5.1 视觉化积木开发流
进入 Web UI 的“工作室”页面，用户可以通过拖拽积木完成复杂动态光效的设计。积木体系划分为 6 大核心领域：
1. **时钟与时间 (Clock)**：流逝绝对毫秒数、正弦周期振荡、锯齿波与三角波；
2. **几何拓扑与矩阵 (Geometry)**：按键物理坐标 `(x, y)`、矩阵行/列、中心欧几里得距离计算、径向涟漪相位；
3. **色彩与调色板 (Color)**：RGB/HSV 色彩构造、渐变色线性插值 (Lerp)、亮度缩放；
4. **动态动力学 (Dynamics)**：时间等待 (`wait`)、次数循环 (`repeat`)；
5. **CS2 遥测原子传感器 (Atomic GSI)**：读取数值型（生命值、护甲、弹药）、字符型（阵营、武器名）、布尔型及瞬态脉冲；
6. **逻辑编排 (Orchestration)**：前台进程绑定、复杂组合条件判断。

### 5.2 双转译器架构 (Dual Transpiler)
- **JavaScript 转译器 (`jsTranspiler.js`)**：在前端浏览器沙箱中执行，为 Studio 右上角虚拟键盘提供高帧率（60 FPS）纯前端纯仿真渲染，支持调速与时间倒流；
- **C++17 转译器 (`cppTranspiler.js`)**：将积木树一键翻译为标准 C++17 源代码。转译器遵循**零堆分配（0-Heap Allocation in Render Loop）**原则，所有运算均基于寄存器与栈内存就地完成，无任何 `malloc/new` 开销。

### 5.3 最小 Plugin SDK 与头文件依赖
导出的插件必须实现标准 C-ABI 接口与面向对象虚基类：
```cpp
// 核心接口位于 include/engine/plugin_interface.h
#define AURA_PLUGIN_EXPORT extern "C" __declspec(dllexport)

extern "C" {
    AURA_PLUGIN_EXPORT uint32_t AuraGetPluginApiVersion() { return 1; }
    AURA_PLUGIN_EXPORT const char* AuraGetEffectName()   { return "my_custom_effect"; }
    AURA_PLUGIN_EXPORT aura::Effect* AuraCreateEffect()  { return new MyCustomEffect(); }
    AURA_PLUGIN_EXPORT void AuraDestroyEffect(aura::Effect* p) { delete p; }
}
```
**生命周期契约**：`IGsiReader::GetString()` 返回的指针底层为 `thread_local` 静态缓冲，只保证在同一线程下一次 `GetString()` 调用前有效，长期保存须深拷贝。

### 5.4 发行版 SDK 自动解压与发现机制
在独立发布的单文件 `Aura.exe` 中，内置包含 `include/` 头文件资产：
- 启动时解压至 `%LOCALAPPDATA%\Aura\runtime\include`；
- Web UI 编译后台按严格优先级寻源，杜绝因进程工作目录（CWD）被篡改而引用错误头文件；
- 带有尾部斜杠处理与命令行转义加固，防止 MSVC `cl.exe` 参数错乱。

### 5.5 MSVC 前置依赖与发布就绪诊断 (Readiness UX)
系统明确将 MSVC（Visual Studio / Build Tools 的 C++ Desktop workload）作为原生编译的外挂依赖，并在 UI 层面建立清晰的状态分级：

| SDK 状态 | MSVC 状态 | Studio 界面表现 | 可用功能 |
| :---: | :---: | :--- | :--- |
| ✅ 存在 | ✅ 存在 | 绿色就绪，发布按钮完全可用 | 积木编辑、草稿保存、网页预览、**一键原生编译发布** |
| ✅ 存在 | ❌ 缺失 | 顶部黄标：`缺少 MSVC (可编辑/存草稿)` | 积木编辑、草稿保存、网页预览、**C++ 源码查看导出** |
| ❌ 损坏 | — | 顶部红标：`缺少 SDK 头文件` | 提示重新解压或检查发行包完整性 |

---

## 6. 配置文件全量规格说明 (config.json)

`config.json` 是整个系统的单一事实配置源（统一版本规范 `version = 2`），支持运行时热重载：

```json
{
  "version": 2,
  "default_profile": "desktop",
  "profiles": {
    "desktop": {
      "title": "默认桌面方案",
      "type": "native",
      "base_effect": "wave",
      "speed": 1.0,
      "color": [0, 150, 255]
    },
    "cs2_base": {
      "title": "CS2 对局基础",
      "type": "native",
      "base_effect": "breathing",
      "speed": 0.8,
      "color": [255, 120, 0]
    },
    "studio_custom_laser": {
      "title": "工作室定制激光",
      "type": "plugin",
      "plugin_name": "effect_studio_laser",
      "speed": 1.0
    }
  },
  "blockly_effects": {
    "studio_custom_laser": {
      "name": "studio_custom_laser",
      "applied_plugin_name": "effect_studio_laser",
      "blockly_json": { "/* Blockly 序列化工作区 JSON */": {} },
      "cpp_source": "// CppTranspiler 生成的 C++ 代码",
      "published": true
    }
  },
  "orchestration": {
    "version": 2,
    "rules": [
      {
        "id": "rule_cs2_focus",
        "name": "CS2 激活主对局",
        "enabled": true,
        "profile": "cs2_base",
        "dnd": false,
        "conditions": {
          "op": "and",
          "conditions": [
            { "field": "process", "op": "==", "value": "cs2.exe" },
            { "field": "player.state.health", "op": ">", "value": 20 }
          ]
        }
      }
    ],
    "event_overlays": [
      {
        "id": "overlay_kill_burst",
        "name": "击杀波纹",
        "enabled": true,
        "event": "event.kill",
        "effect": "ripple",
        "color": [255, 0, 0],
        "blend_mode": "replace",
        "attack_ms": 100,
        "sustain_ms": 500,
        "fade_ms": 800
      }
    ]
  }
}
```

---

## 7. 构建、打包与独立分发体系

### 7.1 本地编译环境要求
- **操作系统**：Windows 11 / Windows 10 (x64)
- **编译工具链**：Microsoft Visual Studio 2022 (v17) 或 Visual Studio 2026 (v18)，安装有 `C++ 桌面开发` 工作负载
- **构建系统**：CMake 3.20+
- **前端工具**：Node.js 18+ 与 npm

### 7.2 源码级构建命令
```cmd
# 1. 编译前端单文件产物
cd frontend
npm install
npm run build
cd ..

# 2. 编译 C++ Release 二进制
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### 7.3 自动化独立发布打包 (`package_release.py`)
运行根目录下的一键打包脚本：
```cmd
python tools/package_release.py --version v0.1.0-alpha.1
```
脚本执行全自动流水线：
1. **工具链智能探测**：优先使用 `vswhere.exe` 探测本机安装的 Visual Studio 版本，动态匹配生成器，并检验既有 `CMakeCache.txt` 匹配性，绝不静默破坏开发者的 build 目录；
2. **资产校验**：核验驱动 DLL、键位表、`web/index.html` 以及 4 个 Plugin SDK 头文件；
3. **编译单文件 Aura.exe**：将全部资产与版本元数据编译写入 `launcher.rc`，生成一体化免安装程序；
4. **生成便携包**：输出 `dist/Aura-v0.1.0-alpha.1-windows-x64.zip`，内含单文件、驱动备份与使用说明。

---

## 8. 质量防护网与验收规范

### 8.1 自动化测试矩阵

| 测试套件 | 对应文件 / 脚本 | 测试覆盖与核心断言 |
| :--- | :--- | :--- |
| **C++ 核心单元测试** | `tests/test_gsi_rules.cpp` | GSI 报文扁平化、AST 递归逻辑、DND 规则、OverlayManager 混合、COM UAF 防护、**HAL 驱动两阶段 Gate 及 A/B/C 约束**。 |
| **CTest 集成套件** | `ctest -C Release` | `plugin_runtime`（DLL 热重载/容灾）、`gsi_rules`、`aura_hal_py`。 |
| **Python 运行时接入点** | `tests/test_runtime_entrypoints.py` | 守护进程与 Web UI 命令行参数解析、默认配置生成、CWD 污染防护、端口与日志级校验（29 项用例）。 |
| **发行版集成回归** | `tests/test_studio_release_regression.py` | 纯净临时目录解压、无源码环境 SDK 自动定位、尾部反斜杠转义加固、MSVC 缺失诊断、**独立编译并加载最小 DLL**（8 项场景全绿）。 |
| **前端自动化套件** | `frontend/tests/studio.test.mjs` | 积木序列化、Transpiler 执行正确性、GSI 字典权威对齐、模拟器前缀自适应（27 项用例）。 |

### 8.2 物理真机与 CS2 人工验收指引
> **重要规约**：自动化 CI 测试通过仅代表模拟护栏通过；由于自动化流水线中未连接真实 USB 键盘，项目整体状态标记为 **`NOT VERIFIED ON REAL HARDWARE`**。

在准备发布正式发行版前，测试人员须遵照 [`docs/MANUAL_TESTS.md`](docs/MANUAL_TESTS.md) 在连接物理键盘的真机上逐项完成核验并签字确认：
1. 物理键盘 68 键与 15 独立 Touch Bar 发光均匀，无闪烁；
2. 网页 Studio 实时预览与真机同步；
3. 保存草稿（真机保持运行）与发布插件（真机平滑热重载）；
4. 构造语法错误触发编译失败，确认真机安全维持原方案不崩溃；
5. 进入 CS2 真实对局，验证血量警报、C4 频闪与击杀爆头覆盖层。

---

## 9. 故障排查与常见问题 (FAQ)

### Q1: 启动时报错 `Unsupported ASUS HAL version / Hardware control disabled for safety`？
- **原因**：当前加载的 `AacKbHal_x64.dll` 散列值或版本未在受支持的白名单表中（例如华硕推送了更新的驱动，或 DLL 文件遭到了修改）。
- **解决**：系统遵循 Fail-closed 原则主动阻断硬件控制以保护硬件安全。请确认使用的 DLL 版本为 `1.3.46.0`（SHA-256: `52d575bf942b7551b3f120c446bf0d853e36f9225c6b9a17407a80e0b1829f04`）。

### Q2: 在 Studio 点击“发布”提示“缺少 MSVC 编译环境”？
- **原因**：原生光效发布需要将 C++ 代码即时编译为本地 DLL，当前机器未安装 Visual Studio 或 MSVC C++ 工具链。
- **解决**：前往微软官方下载安装 **Visual Studio Build Tools**，勾选安装“使用 C++ 的桌面开发”工作负载。安装完成后切回浏览器即可即时点亮发布状态。未安装期间，草稿保存与网页端实时预览完全不受影响。

### Q3: CS2 游戏内的状态无法在键盘上体现？
- **排查步骤**：
  1. 确认 `gamestate_integration_aura.cfg` 已正确复制到 CS2 的 `cfg` 目录下；
  2. 启动 CS2 并进入对局，打开浏览器访问 `http://127.0.0.1:19898`，进入“遥测诊断”页面；
  3. 检查 GSI 状态是否显示绿色“在线”，若离线请检查 Windows 防火墙是否放行了 3000 端口的本机回环流量。

---

## 10. 免责声明与第三方资产声明

1. **商标归属**：
   - **ASUS**、**ROG (Republic of Gamers)**、**Armoury Crate (奥创中心)** 及其相关标识均为 ASUSTeK Computer Inc.（华硕电脑股份有限公司）的注册商标或商标。
   - **Counter-Strike**、**CS2**、**Valve** 及其相关标识均为 Valve Corporation 的注册商标或商标。
   - 本项目为独立第三方开源项目，与华硕或 Valve 均无官方关联、合作、赞助或背书关系。
2. **底层驱动资产与再分发说明**：
   - 本项目对键盘的底层控制依赖华硕官方硬件抽象层动态库（如 `AacKbHal_x64.dll`）。该动态链接库版权与专有权利完全归华硕所有。
   - 本仓库当前未持有该 DLL 的官方再分发授权（`NO REDISTRIBUTION AUTHORIZATION FOUND`）。
   - 当前独立单文件 `dist/Aura.exe` 及便携包内包含该 DLL，仅供技术研究与个人学习使用；若用于公开发行，建议最终用户从已安装的官方驱动目录中获取该组件。
3. **开源许可协议**：
   - 本项目代码本身的开源许可类型由项目所有者（Owner）最终决策与发布。
