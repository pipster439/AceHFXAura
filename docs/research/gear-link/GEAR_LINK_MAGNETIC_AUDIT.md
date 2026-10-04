# Gear Link Magnetic Audit

## Evidence boundary

设备1.00.59；当前GearLinkM605源码1.00.28；旧firmware1.00.58只作为已有审计交叉参考。本轮新capture/hash见总报告。产品范围来自publicmanifest，不从firmwarebitmask发明范围。

## Actuation / Deadzone

| 特性 | 产品范围 / 模型 | M605路径 | 本轮证据 |
|---|---|---|---|
| AP | .1–4mm，raw/.1；common + keylist | 51 50 common LE16，51 4F wireLE16+rawLE16 | common10/11/30；A wire31 raw20 |
| DZ | 0–.5mm，top/bottom各自 | 51 58 bottom/top，51 59 wire+bottom/top | common2/2、1/1；B wire50 bottom2/top4 |
| AP reset | separate dispatchertype1 | library写common、layer | 本轮51 52count0；已有Aura实机type1另有独立证据 |
| DZ reset | dispatchertype4 | bottom/layer/top | 同上，本輪不重复宣称捕获 |
| 单键移除 | host删除keylist，先写当前common AP/DZ | 同时走51 4F/59 | STATIC CONFIRMED；真正inheritance NOT VERIFIED |

页面AP/DZ阈值可以多选；DKS键会有相应控制限制。默认publicmanifest AP1/DZ.2/.2，preloadraw10/2/2一致。这是host/defaultUI，不证明所有factory bank都同样，更不能覆盖用户durablebaseline。

## Rapid Trigger

### 产品对象

`rapidTriggerSetting.enable` 被physicalgate query/event更新；`globalKeyList`和group sensitivity仅host多选编辑压缩；`perKeyList`记录有不同设置的键。独立press/release是editor操作模式。共享旧名global不能决定firmwarecontract。

HFXfactory选M605 specialization，其per-keysetter逐keywire转换后发：

```
51 54 selectorLE16 wireLE16 sensitivity continuous enable +55 zeros
```

selector0统一、1press、2release；independent先1后2。main groupwriter映射globalKeyList为完整per-key对象，也调同一setter。main sync同样用54。M605库另有53bulksetter，但未找到正常HFX产品调用；本次53为0，保持STATIC/unverified/productionblocked。

本轮captured W(18) unified5/10和press5/release15，A(31) batch/unified及独立参数；D(33)也有旧/默认配置写入。不能根据操作清单就称最终selected集合只有W。66个54 OUT全是enable1；**取消/disable0本轮未独立捕到**，已验证的旧54disable契约不被本次零样本否定。连续continuous字段本轮均0，不扩张continuousON物理claim。

### Hardware gate

MI02 IN8C 21bytes：ReportID3，event76，USBbyte4=0/1；WebHID data剥ReportID后offset3。query2500 responseoffset4。frame224→225及1116→1117有Off→On；eventhandler仅更新observedenable并保留key lists与参数。

OFF不等于disable每个key，也不是Profileunmanage。当前页面配置可编辑的产品语义与以前用户实机gate证据一致；本轮没有重新完成所有四格effectiveAND物理矩阵，不写“全部重新验收”。1ms事件defer是hostdispatch，不等于已测hardwaredebounce；无法由事件间隔推断开关机械延迟。

## DKS / validation

四slot Normal/DKS/Toggle/MT是hosttrigger对象type。M605DKS包sourcewire在selector位2/3，start byte4，fixedend36byte5，targetwire6/7，四position各2bit合到byte8，slot1..4byte9。

V wire49被捕到四slot51 23；设置动作后50 55。用户本轮确认启用DKS后RT不可添加该key，源码disabledKeys来自nonStandardtriggerlist并有specifictooltip；不是隐式DKS→Standard。RT选择还排除MT/Toggle，Fn不可用集合来自layout。Copilot限制需当前HFX选择/布局路径验证，不拿上轮Armoury禁选列表直接套本网页。

UI DKS阈值修改包含editor确认/limits，simpleDKSstart1.0/endbuilder3.6只是当前观察/静态，不保证全部模式/endpoints只能如此。实际DKS行为/四slotmixedrelease/preservation本轮没有单独逐项测量。

Aura应保留：Unknown Standard不能猜；manageddisabledRT不能prune；显式DKSStandard+RT合法；真实nonStandard冲突必须用户选择，不能silentStandardize。本轮没有修改这些planner规则。

## SpeedTap / SOCD

可达共享M605路径：2501master、2508键对query分页、5155wirepair+enable、5156resetpair、5157switch。sync先enable、清A/D默认pair、写desiredpair、回desiredmaster。5155本轮25个OUT来自sync；5156在审查留存集合中，本次0；5157不在集合，不能说它没发。最多五pair读槽位为静态capacity，不是已测产品上限。

没有独立测试keypriority、同时按压DSP裁决、DKS/RT/SpeedTap交叉冲突；不泛化为所有SOCD模式，不纳入Aura生产。

## Reset与批量

sharedresetAnalogTrigger暴露type0/1/2/3/4及其它generic选项。实际UI resetAPDZ先type1再type4；RTresettype2/layerAll来自source。**本轮无任何51 52 packet**。不因为官方有builder就授权Aura type0/2/3或任意layer。

多keyRT/DKS可多个stage后Apply，官方queue会按opcode关联并retry，sync callbacks可能并行。Aura一operation一safe transaction是已验证框架；本轮数据只为未来batchdesign参考，不减210/400ms、不改变quarantine/reconnect。

## Remaining gates

AP/DZ移除后再改common的inheritance；RTcontinuousON；DKS完整preservation；SpeedTap物理规则；overrideownership查询；同步后perKeyList恢复；独立hardwarebank读写作用域。全部作为专项计划，不在本次扩大production。
