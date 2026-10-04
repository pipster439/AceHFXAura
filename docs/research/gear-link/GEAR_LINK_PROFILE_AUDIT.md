# Gear Link Profile Audit

## 1. Schema / authority

完整六个公开默认对象保存在研究目录 `schemas/profile-defaults.json`；符号引用保持 `$source_expression`，不执行 bundle，不导出真实用户 store。字段契约见 `official-profile-contract.json`。

| 字段 | 语义 | Authority / caveat |
|---|---|---|
| `value` | slot string1..6 | 硬件 index，非 Profile GUID |
| `currentLightingEffect` | 当前 effect enum | 普通刷新读 BasicInfo byte9 |
| `defaultLightingEffect` | host 默认灯效 | preload 初始值，非 firmware factory证明 |
| `lightEffects[]` | 每 effect 的参数记录 | host cache，另有 query能力 |
| `actuationPointDeadZone` | common + AP/DZ key lists | 普通refresh来自host |
| `rapidTriggerSetting` | observed gate + grouped/per-key editor data | gate与RT配置不同 authority |
| `allKeyTriggers[]` | Normal/DKS/Toggle/MT + Fn sources | host意图；mode readback API另有 |
| `assigned*` / `*KeyTriggers` | legacy field兼容 | 当前host导入合并到allKeyTriggers |
| `speedTap` | master + key-pair settings | master查询；pair多为host配置 |
| `touchPanel` | lever模式与分配 | runtime事件 + host配置 |
| `pollingRate` | host频率意图 | refresh会写硬件，不是假纯readback |
| `isSWMode` | software-mode协调状态 | SW mode query/事件，非灯光全表readback |
| `oled` / `power` | 通用模板字段 | 不是HFX能力保证 |
| `latest*Settings` | 软件快捷功能编辑缓存 | host/editor元数据 |

LocalStorage 是 serial-partitioned 数据/时间戳 envelope；序列号值未收集。Companion `readConfig(profile_index,model_name,file_name)` / `writeConfig(...file_data)` 提供native配置存储。浏览器 migration 合并旧 assigned/trigger列表，fallback可能选择默认6；不能移植成 Aura migration规则。

## 2. Bank lifecycle

BasicInfo查询的firmware解析bytes4–7；profile count byte8，本机6；active index byte10。本机UI槽位6标为默认，1–5为可选槽位。

普通选择 `51 00 00 00 slot`（共64bytes），随后gate2500、SpeedTap2501、BasicInfo1200和polling5131刷新。frame411→445→479→509分别4→5→4→5，**没有AP/DZ/RT/DKS/灯效重放，也没有该窗口内50 55**。用户确认磁轴/灯光一同恢复。这证明不是每次选择都由host materialize所有键。

选择亦成为后续写入目标：编辑4时出现A AP20、V DKS、lightingStatic；5时AP30/DZ1/1/Breathing。尚不能把它拆成“仅选编辑bank”和“仅选活动bank”两个API；当前officialUI只用一个命令。槽位6/default 到firmware内部bank0的关系，仅见旧1.00.58静态参考，不在本轮创造新production契约。

## 3. Full state ≠ full host write

硬件bank提供行为恢复范围；host Profile对象覆盖多个产品领域，但仍可能缓存不完整/陈旧。普通selection恢复B Standard或RToff如果由bank已有内容完成，不必给每键发disable/Standard。

这不同于Aura的sparse-managed intent：Aura unmanage需要trusted prior，DKSUnknown不能默认Standard。不要删除guard来模拟officialbank。要取得同样产品“完整Profile”语义，应先定义可信完整defaults/ownership，或者经独立bank正式gate，两者都不能在本次研究直接实施。

## 4. Sync / Save / reset

源码main `sync_all_slots` / `sync_settings`（anchors）：逐1..count→select→`50 40` current reset→等待1000ms→按键/APDZ/RT/SpeedTap/触控条/lighting/performance→`50 55`→返回原slot。

本次frame521/582/658/742/844/897依次选择1..6，各有current reset及最终Apply。普通刷新callbacks在sync段还穿插AP/DZ/RT/lighting/polling写入，不能把所有OUT都归因于单一串行sync函数。没有完整runtime trace证明每个重复写的caller。

`50 40` byte4=0为current reset，byte4=1为all reset（静态）；`50 60` keybinding、`50 61` lightingreset；均不等同`51 52`磁轴resetdispatcher。本次只捕到六次currentreset，不能把all reset或未知type0标为验证。操作仅为用户官方UI，本工具不发送这些命令。

Sync需要覆盖所有bank，不能在Aura复用作单Profile Save。部分RT replay源码只显式遍历globalKeyList，未看到同段遍历perKeyList：这是静态潜在replay缺口，不声称用户现有per-key数据已损坏。同步前不应把压缩host cache当fullreadback。

## 5. Persistence / cold start / reconnect

用户报告拔插后回默认；capture重连descriptorframe1029，BasicInfo1032 active6，后续manualselect5仍可工作；firmware1.00.59与count6保持。重连前后deviceaddress10→11通过descriptor重新确认。

只证明这次active槽位恢复，不证明所有bank内容掉电均保留，也不证明Flashwear/Save时点。用户需手动连接与源码具有reinit事件不矛盾：浏览器授权/用户连接、deviceadapter重建和activebank恢复是不同层。

没有未知packet重放、裸NVMread、serial导出或factoryreset实验。冷启动withoutCompanion/allbanksverify/powercycleeditflag作为remaininggate。

## 6. 对 Armoury 的对照

[上轮官方Profile选择](../../hardware/OFFICIAL_PROFILE_SWITCH_AUDIT.md)同样看到短51 00选择恢复行为。重核前端/SDK/HALhash一致，但这不是证明GearLink运行经ArmouryDLL。GearLinkhostschema使用slot+allKeyTriggers+分组RT，不能当XML字段逐字同构。两条host实现的common协议、不同validation/cache/reconnect行为应分别建adapter。

## 7. 验收边界

| 项 | 状态 |
|---|---|
| 6槽/默认6/active查询 | OFFICIAL USB CAPTURE PASS + STATIC |
| 普通切换无需逐键重放 | OFFICIAL USB CAPTURE PASS，限上述窗口 |
| 磁轴/lighting一同恢复 | USER BEHAVIOR REPORT，非逐字段测量 |
| 全槽reset/replay | OFFICIAL USB CAPTURE PASS +源吻合；exact按钮点击未记录 |
| 51 00 独立edit-bank vsactive-bank分离 | NOT VERIFIED；不要公开两个API |
| 完整NVM/powercycle持久化 | NOT VERIFIED |
| host空对象对应firmwareStandard/disabled | 需bank内容前提，不能泛化readback |
