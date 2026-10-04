# Gear Link Lighting Audit

## 1. Actual product modes

HFX设备main的effect选择表与用户观察一致；不是按Armoury经验补齐。数字ID在当前shared enum及本次USB replay均出现。

| ID | UI 中文 | 协议名 | 参数包数 |
|---:|---|---|---:|
| 0 | 恒亮 | Static | 1 |
| 1 | 呼吸 | Breathing | 1 |
| 2 | 彩色循环 | ColorCycle | 1 |
| 3 | 触发 | Reactive | 1 |
| 4 | 彩虹 | Rainbow | 3 |
| 5 | 涟漪 | Ripple | 3 |
| 6 | 星空 | StarryNight | 1 |
| 7 | 流沙 | Quicksand | 2 |
| 8 | 电流 | Current | 1 |
| 9 | 雨滴 | RainDrop | 1 |

**STATIC CONFIRMED + OFFICIAL USB CAPTURE PASS（字节）+ USER UI OBSERVATION（名称）**。大部分effect字节来自全槽同步重放，不表示每种独立物理效果都已测试。普通编辑段本次明确有Static与Breathing。

用户确认无独立逐键或灯条编辑。HFXlighting keyboard view为不可点击展示。没有Custom effect条目；不要因共享perKeyRGB能力/generic侧灯API就宣称HFX页提供Custom/逐灯条API。

## 2. Host lighting state

每Profile有currentLightingEffect和lightEffects数组。各记录：type、speed、brightness、isSingleColor/isRandomColor、colors、backgroundColor/isBackgroundColor；Rainbow/Ripple另有direction/width/separateColors的location+RGB梯度；Quicksand有最多六个颜色；部分mode有analogEffect。

颜色/速度/亮度的UI转换与wire值应分开。例如当前Static速度255是该mode占位，不可当用户255档速度。captureframe312 brightness50，对应50%；frame304 RGB红，frame296则绿，操作示例值不覆盖实际capture。不是所有模式都可编辑每个字段。

页面缓存每个effect参数与最后选择记录；乐观mutation失败会回previousData。query merge对随机颜色/梯度有保留host值逻辑，UI颜色不是保证完整固件readback。

## 3. Exact configuration framing

USB vendor payload64bytes。WebHID reportID0由API参数携带；WindowsWriteFile额外dummy0属于另一transportframing，不能把host65byte混成USB65byte。

### 普通单包

```
0:51  1:2C  2:effectID  3:0
4:speed  5:brightness
6:((single ? 0 : 1)<<4) | random
7:FF 8:FF
9..11:RGB1 12..14:RGB2 15..17:backgroundRGB
remaining:zero
```

上述适用于Static/Breathing/ColorCycle/Reactive/StarryNight/Current/RainDrop；实际参数可用性由effect决定，不把保留/无效字段开放给generic用户sender。

### Rainbow / Ripple 三包

byte2=effect，byte3按 **2→1→0** countdown。

- 第一包4speed/5brightness/6random/7direction/8width/9gradientCount；10起两个 `[location,R,G,B]`。
- 第二包4起四个gradient point。
- 第三包4起最后一个point，再backgroundRGB。
- 未用位置按源码占位；不同包的byte4含义不同，不能当单一结构重复解码。

### Quicksand 两包

byte3 **1→0**。第一包speed/brightness/random/direction/width后前三个RGB；第二包后三个RGB+background。不足六个颜色时左侧补黑。源码promise发送经queue，实际capture显示连续packet，但并发/重发须参考timelines。

### 模拟灯效

`51 2D selectorLE16 effectID enabled +zero`；HFXmain仅Static、ColorCycle、Rainbow路径显示/处理。独立mutation后Apply。本次34条均flag0；**不能把analogON物理行为标为本轮验收**。Aura既有effect0模拟灯效实机证据见[alpha.6边界](../../hardware/M605_ALPHA6_LIGHTING_BOUNDARY.md)，不是本轮扩张到其它effect许可。

## 4. Profile transitions / persistence

普通4↔5窗口没有51 2C/2D重发，用户确认随Profile一同恢复，BasicInfo查询effect随着bank变化。全槽sync段则为每槽保存所有effect参数、当前effect最后写。两类必须分开；也不能从sync的每effect写入顺序推断用户逐个点击这些effect。

掉电后本机回默认槽位，不代表其它bank数据消失；本轮没有BIOS/完整断电下每槽lighting验证。已知上一轮Static掉电保留不能泛化十种effect的持久化和save时点。

## 5. Ownership / cleanup

普通模式切换写新effect参数及Apply，没有看到HFX该路径显式先清所有前effect的表。analog在部分mode选择路径有truthy检查，而fullsync/reset会显式写ON/OFF；这提示需要独立状态转换验证，不能直接宣布旧flag泄漏。

另有SW mode：query2700、set7400（静态），event驱动AuraSync状态；退出legacySync会等待4000ms后恢复host最后effect及analog设置。这是官方host协调逻辑，不是firmware物理switch，不是Aura可照搬的settle值。

本次collector没留存2700/7400，因此不以capture宣称是否发生software ownership切换；这个链条只有source证据。连接窗口可能已退出官方页面但native服务仍存在，不能因为网页关闭就证明所有写入来源停止。

Aura当前Direct RGB `C0 81 countLE16 [index,R,G,B]...` 是逐LEDstream；GearLink `51 2C` 是固件effect参数。两条owner和cleanup不同。并无本轮证据证明只停止C081再写modifier就可以安全接管firmwarelighting。

## 6. LED / lightbar mapping

HFX当前产品页未提供独立lightbar editor；generic `setSideLightEffect51 2E` 不足以确认HFX可达，shared其它设备布局也不能移植。

本轮没有提取独立83-slot RGB→磁轴key→15lightbar索引闭环。MI04 OUT0F存在descriptor，不证明当前Effect API使用MI04；该接口body被privacycollector移除，不能计为未发送。保留Aura当前mapping，不发布猜测表。磁轴68wire audit与RGB83slots不是同一种count。

## 7. Batching / timing

一mode参数1/2/3个stage；普通edit随后自动Apply，独立analog另一次Apply；reset/fullsync可一次提交多effect记录。capture全部46Apply有identicalIN；不少相同packet约500ms重发，即使早已有USBecho。可能是host响应分发/queue竞争，**未定位caller，不称firmware失败**。

不据URB间隔减少Aura210/400ms。未来若batch，应先证明每opcode的stage原子性、response关联、partial failure、generation与quarantine，再考虑性能。全槽sync的并发/重发策略不直接复用。

## 8. 下一步gate

独立owner测试：DirectRGB→firmwareStatic→analog→reactive→DirectRGB；仅测试Profile限定scope；确认Stop/clear/restore顺序及错误行为。再验证main/lightbar相互独立、bank/powercycle持久化、new effect exactpayload，才设计typedproduction接口。**本轮未新增51 2C/2E/74 writer或allowlist。**

本地截图 `audit_artifacts/gear-link-full-audit/screens/gear-link-current-key-editor.jpg` 实际为用户操作后的lighting页（实时截图时页面已从按键转到灯光），保留原文件名并在索引纠正其含义，不以截图文件名推断页面。
