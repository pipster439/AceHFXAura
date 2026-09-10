# Aura 项目交接文档 —— 批次 B 之后的任务执行指南

- **交接时间**：2026-09-10 21:44 (GMT+8)
- **交接范围**：批次 C / D / E 剩余修复项（R12–R23）
- **前序成果**：批次 A（R1/R2/R3）+ 版本库治理 + 批次 B（R4–R11）已全部完成并验收，HEAD = `1aa6ab9`

---

## 1. 项目一句话

**Aura** 是 ROG Falchion Ace HFX 键盘的硬件级单键 RGB 灯效控制系统：双进程架构（`aura_daemon.exe` 主控 + `aura_web_ui.exe` Web 配置服务），通过 CS2 Game State Integration 接收游戏状态、驱动 ASUS HAL DLL 实现逐键灯效。

### 架构红线（不可破坏）

| 红线 | 原因 |
|---|---|
| **COM/HAL 只在主线程** | `AacKbHal_x64.dll` 非线程安全，COM STA 套间绑定主线程 |
| **GSI 网络线程不碰硬件** | httplib I/O 线程只做 JSON 解析与状态快照，硬件推流封闭在主循环 25FPS |
| **配置热重载用 shared_ptr** | 防止读写竞争期间悬垂指针 |
| **监听地址固定 127.0.0.1** | GSI 端口 19897 / Web 端口 19898，不开放局域网 |
| **WebUI 子进程由 Job Object 监护** | daemon 退出时子进程自动回收，用命名 Event 平滑关闭 |
| **禁改 vendored 第三方** | `include/third_party/`（httplib、json.hpp）只用公开 API，不碰源码 |

---

## 2. 当前状态快照

```
HEAD = 1aa6ab9 (fix(daemon): 批次 B 轮 4 —— R8 全链路非 ASCII 路径支持)

commit 链（新→旧）:
  1aa6ab9  批次 B 轮 4: R8 非 ASCII 路径（6 文件宽字符全家桶）
  7c7fef9  批次 B 轮 3: R6 路径穿越 + R7 四层鉴权
  1c937ce  批次 B 轮 2: R9 COM 生命周期 RAII
  c8adb46  批次 B 轮 1: R4/R5/R10/R11 四项
  7dac58a  Phase 3 源码纳管（HEAD 自洽收官）
  b00e4e5  Phase 3 构建定义
  468dfcd  .gitignore + 解除 115 项产物跟踪
  ce232a3  批次 A: R1/R2/R3（P0 阻断性修复）
```

- **构建基线**：0 error / 0 warning（MSVC 19.51，VS 生成器）
- **测试基线**：`test_gsi_rules` 退出码 `0x00000000`，24 PASS / 0 FAIL
- **工作树未提交文件（5 个，是用户自己的内容，不要提交也不要动）**：
  `AURA_CODE_REVIEW_REPORT.md`、`README.md`、`config.example.json`、`config.json`、`web/index.html`
  （R12/R14 会涉及 config.json 与 web/index.html——动手前先 `git diff` 确认内容，改动保持最小，**这些文件改完后提交前要询问用户**）
- **已完成项不要回退**：R4 的 `set_payload_max_length`、R5 的快照比对谓词、R6/R7 的词法检查与四层鉴权、R8 的宽字符签名（`CreateProcessW`/`wmain`/`MoveFileExW`/`filesystem::path`）、R9 的 `ComScope`、R10 的日志轮转、R11 的 `--log-level`

---

## 3. 剩余任务总览与决策依赖

```
批次 C（P1 功能闭环）—— R15 需 D1 拍板
  R13 测试体系修复（CHECK 宏 + fixtures + CTest）【最先做，后续安全网】
  R12 config.json 补 coding + 引用校验（含 R12b 热重载防刷屏）
  R14 前端 handleSave 语义（需 npm run build 重建 web/index.html）
  R15 brightness/speed_index 契约  ← 决策点 D1（未拍板，先跳过）

批次 D（P2 结构与治理）—— R18 需 D3 拍板
  R16 参数下界校验统一（ClampPeriod 33ms）
  R17 .gitignore + 源码纳管   【已完成（468dfcd + 7dac58a），跳过】
  R18 用户本机 CS2 cfg 补 bomb ← 决策点 D3（未拍板，先跳过）
  R20 构建加固 /W4 + sanitizer（单独 commit，便于回滚）
  R21 Python 工具链抽共享模块（18 个脚本）
  R22 文档合并与勘误

批次 E（需真机测量）
  R19 CreateLedDevice 内存泄漏判别实验 ← 决策点 D4（需用户拔键盘，排最后）

R23（独立课题，不在本次范围）栈伪造 std::vector 替代方案
```

### 建议执行顺序

```
1. R13 测试体系（安全网，最先）
2. R12 + R12b（config 补全 + 校验）
3. R14 前端 handleSave（含 npm build）
4. R16（独立，可穿插）
5. R20 /W4 加固（单独 commit）
6. R21 Python 治理、R22 文档（收尾）
7. R15（等 D1）、R18（等 D3）、R19（等用户配合）——遇到未拍板就停下问用户
```

---

## 4. 逐项执行卡片

每项的**完整问题定位、经双向交叉验证的改法、验收标准**都在 `G:/Aura/REMEDIATION_PLAN.md` 对应章节——**动手前必读该章节，严格按方案实施，不要自由发挥**。此处只列执行要点与陷阱。

### R13 测试体系修复（最先做）

- `tests/test_util.h` 提供 `CHECK(cond, msg)` 宏：失败时打印文件/行/表达式并 `++failures`，`main` 末尾 `return failures == 0 ? 0 : 1`；替换全部 `assert`（**现状陷阱**：Release + `/O2` + NDEBUG 把 assert 编译掉，测试跑出错误结果也报 PASS）
- 测试基线与发布配置解耦：`tests/fixtures/test_config.json`（含 coding、chrome.exe 等测试所需映射），测试加载 fixture 而非仓库根的 `config.json`
- `CMakeLists.txt`：`enable_testing()` + `add_test(NAME gsi_rules COMMAND test_gsi_rules)`（工作目录设为 `tests/fixtures`）
- 补用例：`period_ms=1` 不崩溃（R16 护栏）、`/web/..%2f` 404（R6，需起服务）
- **验收**：ctest 一键执行；**故意破坏一个断言时返回非零**（证明校验力真实存在，做完这个实验再恢复）

### R12 config.json 补 coding + 引用校验

- `config.json` 的 `profiles` 补入 `config.example.json` 里的 `coding`（type: static、color:[10,30,50]、keys: ESC/ENTER/TAB）——**注意 config.json 是用户未提交的工作树状态**，先 `git diff config.json` 看清再动
- `RuleEngine::LoadConfig` 引用完整性校验：`rules[].profile` / `gsi_bindings[].profile` 在 `profiles` 中不存在 → `LOG_ERROR` 指明"哪个规则引用了未定义方案" + `LoadConfig` 返回 false（替代静默回退 desktop）；未知 `type` 值同样报错（当前静默全黑）
- R12b 配套：`CheckAndReload` 解析失败时把 `last_write_time_` 同步为当前文件时间，避免每秒重试刷屏；仅在 mtime 再变时重试。热重载失败保留旧配置只报一次错
- ⚠ 行为变化：静默降级 → 显式失败。错误信息要给出修复指引（指明缺哪个 profile 名）

### R14 前端 handleSave 语义

- `frontend/src/App.jsx:206` 附近：`handleSave` 中 `default_profile` 改为沿用现值（`default_profile: config.default_profile`），保存只写回当前方案参数；"设为默认"仍由 `handleSetDefaultProfile`（约 :325，含独立提示语）承担
- **改完必须在 `frontend/` 下 `npm run build`**（输出到 `../web/index.html`），否则网页不变；`web/index.html` 是用户未提交状态，构建前先备份
- 验收：编辑 cs2_gamer 保存后 default_profile 不变；点"设为默认"后正确变更；保存后灯效热重载生效

### R15 brightness/speed_index（等 D1，先跳过）

- 方案 A（实现）：`Profile` 加 `uint8_t brightness = 255;`；`rule_engine` 解析（0.0–1.0 浮点 × 255）；`Profile::Render` 在 base_effect 与 key_overrides 全部写入后统一缩放（`out_frame` 逐通道 `v * brightness / 255`）；`speed_index` 映射为 `period_ms` 倍率或从 UI 移除
- 方案 B（移除）：前端移除滑块与写入、config 删字段
- **D1 未拍板，不要擅自选方案**

### R16 参数下界校验

- `static constexpr uint64_t ClampPeriod(uint64_t v, uint64_t def, uint64_t min = 33)`，所有效果构造函数统一走它（下界 33ms ≈ 30FPS）；`CurrentEffect` 内 `const uint64_t half = period_ms_/2 ? period_ms_/2 : 1;` 双保险
- 报错哲学统一（与 R12 一致）：构造钳制只防崩溃；`LoadConfig` 解析 `period_ms < 33` 或非正整数时 `LOG_WARN` 指明"哪个 profile 的 period_ms 非法、已被钳制为 X"
- 验收：`period_ms` 为 0/1/2/33 时 daemon 均不崩溃且灯效合理；新增测试用例（配合 R13）

### R18 CS2 cfg 补 bomb（等 D3，先跳过）

- 优先建议用户在 WebUI 点"安装 GSI 配置"（走 R7 白名单内的 install-cfg）
- 若用户授权直改：先备份 `.bak`，再补一行 `"bomb" "1"`
- 路径：`D:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\cfg\gamestate_integration_aura.cfg`

### R20 构建加固

- `/W3` → `/W4`；third_party 头用 `/external:W0` 抑制；新增 `AURA_ENABLE_ASAN` 选项（仅 Debug `/fsanitize=address`）；`aura_daemon` 加 `/permissive-`；`CMAKE_BUILD_TYPE` 未设时显式默认值
- **⚠ 单独一个 commit**：/W4 会暴露一批既有告警，逐个修或显式抑制并注明理由，便于回滚

### R21 Python 工具链治理

- 新建 `tools/py/aura_hal.py`：封装 CLSID/VTable 索引/`0x6C`/`0x74` 偏移与 `CreateLedDevice`/`Set_L_STD_SINGLE_XY`/`Release` 安全包装
- 先迁移 `set_per_key.py`、`gui_calibrator.py`、`e2e_key_test.py` 三个最常用脚本；其余加头部注释指向模块
- `set_per_key.py:48-87` 的 5 组 LED ID 冲突（I=66↔ENTER、K=67↔INS、O=74↔PGUP/MINUS、L=75↔DEL、P=82↔BACKSPACE/EQUAL）：以 `calibrated_keymap.json` 为权威覆盖；无法确定的**显式报错**而非静默点错灯
- 验收：迁移脚本同参对照日志一致；冲突键报错而非错点

### R22 文档合并与勘误

- README "平滑关闭耗时 1 毫秒"→ 14ms（AGENT.md §14.2 结论）
- AGENT.md Phase 状态推进到 Phase 3（README 已到位，仅 AGENT.md 滞后）
- 5 份并行报告（LIGHTING_FIX_REPORT、AURA_HARDWARE_VERIFICATION_REPORT、WORKING_SOLUTION_REPORT111、request-working-solution-report-prompt、README_PER_KEY）归并单一事实来源 + `docs/archive/` 存档
- 本方案与三份审查报告纳入 `docs/`

### R19 内存泄漏判别（等用户拔键盘，最后）

1. 拔键盘（或拨码切走）→ `aura_daemon.exe --test-init 100` → 观察 PrivateBytes 曲线
2. 仍线性增长 → "1 小时 OOM"外推成立：`CheckReconnect` 改指数退避（1.5s→5s→15s→60s 封顶）+ 失败上限，重连升 P0
3. 走平 → 撤回外推，仅补注释"失败路径不泄漏"的实测依据
- 第二步（若需根治）：小变体压测区分泄漏在 HAL 实例还是设备对象；若在 HAL 实例，重连间复用 `pHal_` 只重建设备层

---

## 5. 构建与自验环境

### 5.1 构建（必须 0 err 0 warn 才算过）

```bash
CM="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"

# 配置（首次或 CMakeLists 变更后）
env -u https_proxy -u HTTPS_PROXY -u http_proxy -u HTTP_PROXY -u all_proxy -u ALL_PROXY \
  "$CM" -S "G:/Aura" -B "G:/Aura-build-verify" -G "Visual Studio 18 2026" -A x64 -T host=x64

# 构建
env -u https_proxy -u HTTPS_PROXY -u http_proxy -u HTTP_PROXY -u all_proxy -u ALL_PROXY \
  "$CM" --build "G:/Aura-build-verify" --config Release
```

⚠ **铁律**：
- 每条 cmake 命令都必须 `env -u` 剥掉全部代理变量，否则 MSB6001（.NET 大小写字典冲突）
- 构建目录用仓库外 `G:/Aura-build-verify/`，**不要**用 `G:/Aura/build/`
- VS 生成器不用传 `CMAKE_CXX_COMPILER`

### 5.2 回归测试

```bash
"C:/Users/ROG/.workbuddy/binaries/python/versions/3.13.12/python.exe" -c "
import subprocess
p = subprocess.run([r'G:\Aura-build-verify\Release\test_gsi_rules.exe'],
                   cwd=r'G:\Aura', capture_output=True, text=True,
                   encoding='utf-8', errors='replace')
print('0x%08X  PASS=%d FAIL=%d' % (p.returncode & 0xFFFFFFFF,
      p.stdout.count('[PASS]'), p.stdout.count('[FAIL')))
"
# 期望：0x00000000 / 24 / 0（R13 完成后以 ctest 为准）
```

⚠ cwd 必须是 `G:\Aura`（要读 config.json 与 calibrated_keymap.json）。

### 5.3 运行期验收（起 daemon 发请求）

```python
import subprocess, time, urllib.request
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))  # 必须禁代理！
p = subprocess.Popen([r"G:\Aura-build-verify\Release\aura_daemon.exe", "--dry-run"],
                     cwd=r"G:\Aura",
                     stdout=open(r"G:\Aura-build-verify\_daemon.log","w",encoding="utf-8"),
                     stderr=subprocess.STDOUT)
time.sleep(5)
# 用 opener 发请求验证；GSI 端口 19897 / Web 端口 19898
# 结束时：p.terminate(); p.wait(timeout=10) —— 确认真退出后再做任何文件操作
```

⚠ **本地 HTTP 请求必须禁代理**（`ProxyHandler({})` 或 `curl --noproxy '*'`），否则代理劫持 127.0.0.1 返回 502 假失败。

### 5.4 ⚠ daemon 文件操作的 race condition 防护（血泪教训）

`terminate()` 是**软信号**——WebUI 子进程清理、日志 flush 可能仍在后台执行。**不要**在 terminate 后立即恢复/覆盖 config.json 等文件（会与 partial write 竞争把文件写坏）。正确做法：`p.terminate(); p.wait(timeout=10)` 确认真退出后再动文件，或用 `git checkout` 恢复。

### 5.5 提交规范

- 每完成一个 R 项（或一组小项）→ 构建 0/0 + 测试 24/0 + 该项验收标准通过 → `git commit`
- commit message 写清：改了什么、为什么、验证证据（构建/测试/运行期用例）、已知小瑕疵
- **工作树里 5 个用户未提交文件**（§2 列出）——R12/R14 会碰 config.json 与 web/index.html，这两个文件改完**先问用户再提交**；其余 3 个不要动
- 涉及前端改动（R14/R15）必须 `cd frontend && npm run build` 后才算完成

---

## 6. 关键编码陷阱（必读）

1. **C++ 原始字符串字面量**：`R"({...})"` 无定界符形式的终止符是 `)"`——**内容含 `)"` 子串（如 `(null)"`）时编译器误判终止，引发 80 个雪崩语法错误**（批次 B 轮 3 实际发生过）。**所有 JSON 字符串一律用 `R"json({...})json"` 显式定界符形式**。写完自检：`grep -cE 'R"\([^a-zA-Z_)]' <文件>` 应为 0。
2. **Git Bash 退出码不可信**：RC=127 可能实际是 `0xC0000409` 崩溃；用 Python subprocess 取 32 位真实值（见 5.2）。
3. **MSVC assert 陷阱**：Release + NDEBUG 会把 assert 编译掉——测试断言必须用自实现的 CHECK 宏（R13 的核心动机）。
4. **`std::filesystem::path` 拼接陷阱**：`path("web") / "C:/xxx"` 因右侧是 rooted path 而**整体替换**左侧——路径校验必须同时拒绝绝对路径/盘符/UNC（R6 已实现，勿回退）。
5. **宽字符链路**（R8 已完成，新代码保持一致）：文件路径用 `std::filesystem::path` / `std::wstring`，进程创建用 `CreateProcessW`，原子替换用 `MoveFileExW`，子进程入口用 `wmain`；**不要**用 `MultiByteToWideChar(CP_UTF8,...)` 转换 `path::string()`（它是 ACP 编码，按 UTF-8 解会引入新乱码）；日志/JSON 输出用 `path.u8string()`。
6. **`cv_.wait_for` 谓词**（R5 已完成，勿回退）：谓词若不含"状态已变化"只能等满原定剩余超时；快照比对谓词是经过验证的正确形态。

---

## 7. 文件索引

| 路径 | 说明 |
|---|---|
| `G:/Aura/REMEDIATION_PLAN.md` | 修复方案总纲（R1–R23 定位/改法/验收，**每项动手前必读对应章节**） |
| `G:/Aura/AURA_CODE_REVIEW_REPORT.md` | 80 项问题审查报告 |
| `G:/Aura/REPORT_CROSSCHECK.md` | 跨代理核查（§五为实施基线） |
| `G:/Aura/HANDOVER.md` | 本文档 |
| `G:/Aura/.workbuddy/memory/2026-09-10.md` | 今日全程日志（8 轮完整复盘，含所有教训细节） |
| `G:/Aura-build-verify/` | 仓库外构建目录 |
| `G:/Aura-batchA-backup-20260910-201411/` | 批次 A 前快照（回滚兜底） |
