# 工作室：制作光效与设置联动

Studio 通过本地 Web 服务提供图形化编辑与运行行为，既可在浏览器使用，也由 WinUI 的 WebView2 承载。硬件后端由 daemon 选择；当前默认 auto 优先 Native HID，HAL 为回退兼容路径。

## Legacy 联动示例

1. 在 Windows 用本分支重新构建 `aura_daemon` 和 `aura_web_ui`（或运行现有发布打包脚本）。不能只替换网页沿用旧 daemon；新版网页会检测运行时版本。
2. 进入“工作室 → 设置联动”，点击“载入 CS2 完整示例”。此时仅加载草稿，现有配置不变。
3. 点击“保存并应用”：准备血量底色与 WASD 附近扩散动画，添加低血量红色呼吸，最后保存联动。
4. 在 CS2 前台验证：血量驱动底色；每次击杀重新播放 800ms 扩散；血量低于 20 时持续叠加呼吸。退出 CS2 前台或 GSI 离线时结束游戏叠加，回到匹配的桌面方案。
5. 在“制作光效”编辑对应光效。保存草稿只保存编辑数据；保存并应用会更新联动使用的版本。预览暂停后停止网页预览推流，让实际方案显示出来。

## 行为约定

- 光效脚本按积木顺序执行。等待会保留颜色、变量和循环位置，下一次渲染到达截止时间后继续；不会睡眠或阻塞硬件推流。
- 旧脚本与 continuous 发布到末尾后，下一帧从头执行；显式 one-shot 发布会结束并报告 finished/opacity。变量持续保存，直至重新启动该光效；需要每轮归零时应显式赋值。多个顶层积木堆按位置顺序执行，尚未实现 Scratch 的独立并发脚本。
- 每帧最多执行 4096 个指令，超出后下一帧继续；重复次数上限 10000，单次等待上限 60000ms。“等待”应放在光效脚本内，联动表只做持续条件评估，旧的联动等待积木会提示迁移。
- 以下叠加描述适用于 legacy 联动执行器。基础方案从上到下首个命中。事件叠加保留外层条件，触发后播放一次；条件叠加在条件成立期间持续运行，条件失效立即移除。
- 同一事件再次发生时重启它自己的叠加层，即使前一次 GSI 布尔脉冲还没结束。一个渲染周期内收到多次同类事件会合并为一次最新触发。
- 多个叠加可以共存，优先级数值越大越后合成。进入游戏时不会重放离开期间积累的事件。持续条件会即时重新评估。

## 配置与应用

- 仅在旧编辑器明确保存 legacy-only 配置时，将原 `orchestration.rules → gsi_bindings → rules` 按已有优先次序导入 `orchestration.version = 2` 的统一列表。旧数组清空，简单表格是统一列表的投影。打开新 Automation 编辑器不会自动迁移；含 v2 规则时旧序列化器拒绝覆盖。
- 进程规则表仅显示无附加条件的进程规则；简单 GSI 表显示可直接表示的单字段规则；其他组合条件在积木里编辑。修改表格会使旧画布缓存失效，重新打开时从当前规则恢复。
- 应用先生成唯一命名的插件版本并要求 daemon 确认加载，成功后才切换配置引用。失败不会覆盖之前的 DLL 或方案引用。历史插件版本暂保留在 plugins 目录，不自动删除。
- 页面切换时，未保存画布保存在当前浏览器标签页的 sessionStorage；联动画布在外部规则变更后会重新从配置恢复。

## 主要代码对应

| 文件 | 职责 |
| --- | --- |
| `frontend/src/blockly/sequenceCompiler.js` | 将顺序、循环、条件、等待统一转换为可跨帧继续的控制流 |
| `jsTranspiler.js` / `cppTranspiler.js` | 共享控制流，分别生成浏览器预览与原生灯效 |
| `orchestratorSerializer.js` | 将联动积木转换成条件规则并恢复画布 |
| `frontend/src/utils/orchestration.js` | 旧配置迁移与简单表格的读写投影 |
| `frontend/src/utils/applyEffect.js` | 生成独立插件版本、核实加载结果、保存应用版本信息 |
| `src/engine/overlay_manager.cpp` | 事件重触发、条件叠加、优先级合成和游戏退出清理 |
| `frontend/src/blockly/studioExample.js` | 可编辑的 CS2 完整示例 |

## 验证

在 `frontend` 目录执行 `npm ci`、`npm test`、`npm run build`。

测试通过真实 Blockly 工作区覆盖等待、循环、条件往返、旧配置迁移、应用失败保留旧版本、完整示例；使用 g++ 对生成的 C++ 执行等待序列并链接示例，使用原生 OverlayManager 验证叠加行为。便携测试替换 Windows 类型和 GSI 输入，不调用 ASUS 驱动；需要本机有 g++。

Windows CI 构建真实 Studio fixture DLL 并运行 reconciliation 与 dry-run daemon publication 测试；这不代表浏览器可视化、键盘灯光或真实 CS2 验收。以当前 CI run 和候选报告为准。

## Automation v2

新编辑器通过 capabilities/CRUD 管理 state / rising / event：state → Activate Profile 或持续 Trigger Effect，rising/event → one-shot Trigger Effect。一次性效果支持 restart、ignore_while_active、stack、queue；队列/堆叠有固定容量，过期与作用域丢失会取消相关工作。

Legacy 来源与新规则共存；遮蔽风险须确认，Application Rule → V2 Promote/Convert 是显式事务。continuous/one-shot 发布模式也是显式选择，保存草稿不会改变运行版本。热重载保留兼容的活动/排队实例及其旧 generation，失败保留原发布引用。

完整的层顺序、freshness、限额和 reload 语义见 [当前 Automation v2](../architecture/AUTOMATION_V2.md)。
