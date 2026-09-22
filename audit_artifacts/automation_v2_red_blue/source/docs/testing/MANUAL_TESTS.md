# Aura 真机硬件与 CS2 联动最终手动验收清单 (Manual Acceptance Checklist)

> [!WARNING]
> **NOT VERIFIED ON REAL HARDWARE**  
> **当前状态：待人工真机执行 (PENDING HUMAN VERIFICATION)**  
> 依照工程安全规约，CI 自动化流水线及 AI 编码 Agent **绝不可**替人类宣布真机验收完成。  
> 任何模拟测试或 mock 均不得替代下列各项在物理硬件与真实运行游戏环境下的最终复核。

---

## 验收环境要求

- **操作系统**：Windows 11 x64
- **物理硬件**：ASUS ROG Falchion Ace HFX (魔导士 Ace HFX 机械键盘，VID `0x0B05`, PID `0x1B7E`)
- **后端**：先显式验证 `--backend native_hid`；另行验证默认 auto 的回退行为。仅 legacy HAL 验收需要已安装且通过当前代码 Gate 的 ASUS DLL。
- **编译工具链**：Visual Studio 2022 或 2026 (MSVC x64 C++17)
- **测试游戏**：Counter-Strike 2 (已安装并配置 GSI `gamestate_integration_aura.cfg`)

---

## 详细手动验收检查项

### 1. 基础启动与硬件直通
- [ ] **1.1 Aura 启动**  
  - 操作：在终端运行 `aura_daemon.exe --backend native_hid`。
  - 预期：控制台确认 active backend 为 native_hid，守护进程与键盘建立连接并拉起 `aura_web_ui.exe`；此模式不应加载 ASUS HAL。
- [ ] **1.2 真键盘基础灯效**  
  - 操作：观察键盘物理按键发光。
  - 预期：键盘 68 键按当前基础灯效点亮，无意外黑屏、闪烁或错位。记录 Light Bar 随 Row 1 的表现；独立灯条控制不属于当前已完成能力。

### 2. Studio 创作、预览与草稿恢复
- [ ] **2.1 Studio 创建简单光效**  
  - 操作：打开浏览器访问 `http://127.0.0.1:19898`，进入“工作室”，点击“新建”，使用积木搭建一个单键/全键填色效果。
  - 预期：Blockly 画布响应流畅，参数输入无报错。
- [ ] **2.2 浏览器预览**  
  - 操作：点击 Studio 播放/预览按钮。
  - 预期：网页右上角虚拟键盘画布与桌面下方的真实键盘硬件实时同步显示预览光效；停止预览后，真键盘无缝回退至原基础方案。
- [ ] **2.3 保存草稿**  
  - 操作：修改积木颜色，点击“保存草稿”。
  - 预期：右上角状态标签更新为“草稿已保存”，`config.json` 中的 `blockly_json` 已持久化，但真键盘继续运行当前已发布版本（草稿与运行时物理隔离）。
- [ ] **2.4 刷新恢复**  
  - 操作：关闭或强制刷新当前浏览器标签页 (F5)，重新进入该光效。
  - 预期：保存的积木工作区完整无损重新加载，变量与参数完全一致。

### 3. 动态发布、热重载与容灾
- [ ] **3.1 发布**  
  - 操作：在 Studio 点击“发布插件”。
  - 预期：触发 C++ 转译并调用本机 MSVC 编译生成 `plugins/src/effect_studio_*.cpp` 和对应 DLL；发布进度条完成，状态更新为“已发布”。
- [ ] **3.2 真键盘生效**  
  - 操作：观察真键盘。
  - 预期：真键盘立即切换并开始呈现新发布的自定义光效。
- [ ] **3.3 再次发布 / DLL 热重载**  
  - 操作：再次微调光效（例如改变流动速度或颜色），再次点击“发布插件”。
  - 预期：系统自动生成新版本插件名并热加载，无需重启 `aura_daemon`，真键盘在 1 秒内平滑过渡到新版本效果。
- [ ] **3.4 发布失败时旧版本继续工作**  
  - 操作：构造一个触发编译或加载失败的边界操作（例如断开工具链或注入错误），触发发布。
  - 预期：Web 界面清晰提示发布失败原因，daemon 日志告警但**不崩溃**，键盘继续无缝运行上一版已发布光效。
- [ ] **3.5 重启 daemon 后配置恢复**  
  - 操作：关闭 `aura_daemon.exe` 控制台窗口并重新启动。
  - 预期：配置从 `config.json` 自动加载，先前发布的插件重新挂载，键盘直接恢复上次发布的方案。

### 4. CS2 游戏状态集成 (GSI) 自动化验证
- [ ] **4.1 CS2 GSI online**  
  - 操作：启动 Counter-Strike 2 并进入一张练习对局（如 `map de_dust2`）。
  - 预期：打开 Web UI 的“CS2 遥测诊断”，页面连接状态变为绿色“在线 (ONLINE)”，收到 CS2 心跳报文，显示实时血量、护甲、金钱与阵营。
- [ ] **4.2 低血量条件**  
  - 操作：在游戏中通过控制台或受击使自身血量降低至 20 以下。
  - 预期：触发低血量状态叠加光效（如全键盘红色呼吸警示）；血量回升（如新回合开始）后警示光效立即退出。
- [ ] **4.3 C4 状态**  
  - 操作：在游戏中作为 T 阵营下包，使 C4 进入 `planted` 状态。
  - 预期：键盘触发 C4 倒计时/安放频闪叠加；C4 爆炸或拆除后叠加光效按设定平滑退出。
- [ ] **4.4 event.kill 瞬时 Overlay**  
  - 操作：在游戏中击杀一名 Bot 或敌方玩家。
  - 预期：键盘立刻产生指定的瞬时波纹或扩散脉冲（持续约 800~1500ms），并在淡出时间结束后平滑回退至主游戏基础光效。
- [ ] **4.5 游戏退出后 Overlay 清理**  
  - 操作：退出 CS2 或切换至桌面窗口。
  - 预期：CS2 相关的全部事件与状态叠加光效被立即清空，方案自动切回桌面方案（`desktop`），无灯光冻结或残留。

---

## 验收签字记录

| 验证项 | 验证人 | 硬件序列号 / 固件版本 | 验证日期 | 结论 (PASS / FAIL) | 备注 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1. 基础启动与硬件直通 | | | | | |
| 2. Studio 创作与预览 | | | | | |
| 3. 发布与热重载容灾 | | | | | |
| 4. CS2 GSI 完整对局联动 | | | | | |

> **最终声明**：在上述表格由人工实机填写真实验收数据之前，本项目状态必须保持标记为 `NOT VERIFIED ON REAL HARDWARE`。

## Native WinUI Lighting acceptance

- [ ] Home shows the actual configured/active backend and runtime state.
- [ ] Lighting loads preset schemas; color, boolean, enum and number controls match the selected effect.
- [ ] Draft edits, apply, refresh and conflicting revisions behave correctly; errors preserve a recoverable draft.
- [ ] Studio opens through WebView2 on 19898 and can preview/publish with the required MSVC tools.
- [ ] Exit and restart follow the intended daemon ownership/lifecycle; verify on real Windows hardware.
