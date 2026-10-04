# Gear Link M605 Protocol Reference

**Evidence reference, not production permission.** 部分opcode仅在sharedlibrary可见；必须检查M605 specialization和页面callsite。所有布局以下为64-byte USB vendor payload offset；ReportID0由WebHID参数携带，Windowshostdummy0另算。端点来自本次descriptor：MI01OUT0D/IN85、MI02IN8C；重新连接address10→11，不硬编码地址解析。

## Protocol matrix

| Feature | Command / direction | Fields | Apply / persistence | Evidence |
|---|---|---|---|---|
| Basic status | OUT12 00 /INresponse | IN4..7fw，8count，9effect，10active | query；少量实际状态 | CAPTURED + STATIC |
| Layout | 12 12 | IN4layout/5nation | query | CAPTURED + STATIC |
| Connection | 12 03 | IN4type | query | CAPTURED + STATIC |
| Serial | 12 14 index1/2 | response serial | 隐私过滤，值不保存 | STATIC；禁导出 |
| Active bank | 51 00 | selector0，byte4slot | 普通切换窗口不见50 55；其它edit随目标slot | CAPTURED + USER restore |
| Apply | 50 55 | 2..63zero | currenttransaction gate；持久化范围未知 | CAPTURED，46identicalIN |
| Current Profile reset | 50 40 | byte4=0 | reset后replay+Apply | CAPTURED6次；非51 52 |
| All Profile reset | 50 40 | byte4=1 | 未执行 | STATIC ONLY |
| Key binding reset | 50 60 | zero | 当前Profile | STATIC ONLY |
| Lighting reset | 50 61 | zero | 当前Profile | STATIC ONLY |
| Common AP | 51 50 | rawLE16 at4 | 自动Apply；bank | CAPTURED |
| Key AP | 51 4F | wire4..5，raw6..7 | 自动Apply；bank | CAPTURED A |
| AP query | 25 05 /04 | common/key query；reply4..5raw | 普通refresh仍hostcache | STATIC，未捕到 |
| Common DZ | 51 58 | bottom4，top5 | 自动Apply；bank | CAPTURED |
| Key DZ | 51 59 | wire4..5，bottom6，top7 | 自动Apply；bank | CAPTURED B |
| DZ query | 25 0A /09 | replybottom4/top5 | sharedAPI | STATIC，未捕到 |
| Per-key RT | 51 54 | selector2..3，wire4..5，raw6，continuous7，enable8，9..63zero | 统一0或press1/release2；列表后Apply | CAPTURED；本轮enable1，0引用此前证据 |
| RT gate query | 25 00 | response4=0/1 | 只读gate | CAPTURED |
| RT gate event | IN MI02 report3 | `03 76 00 00 state` +zero，USBstate4 | 无Apply；物理开关不由Profile拥有 | CAPTURED + STATIC |
| RT key query | 25 06 /A6 | key parameter query；newerV2格式另有 | sharedAPI，HFXrefresh不用来全表read | STATIC，未捕到 |
| RT grouped metadata | 25 22 /23 | genericmeta/pagedkeys | 非证明的M605normalUIreadback | STATIC ONLY |
| Bulk RT | 51 53 | selector2..3，raw4，continuous5，enable6，layer7 | 没有production认可 | STATIC ONLY；本次0，BLOCKED |
| Generic RT master | 51 5C | genericenable | 未证明是物理GPIOcontrol；不可用于Aura假master | STATIC ONLY |
| Standard | 51 21 | sourcewire2..3，target4..5，AP6 | DKS/RT authoring不同产品动作 | M605 STATIC；本次0 |
| DKS slot | 51 23 | sourcewire2..3，start4，end36byte5，target6..7，positions8，slot9 | 四slot后Apply | CAPTURED V |
| DKS/mode query | 25 02 | sourcekey request，mode/slot回复 | sharedAPI；不保证Unknown可当Standard | STATIC，未捕到 |
| Toggle / ModTap | 51 22 /24 | specialized source/target/threshold/timing | 正常UI能力；不扩Aura | STATIC ONLY |
| Simple remap | 51 20 | source / target wire | native/service与direct职责分开 | STATIC ONLY |
| SpeedTap pairs | 51 55 | bodywire1LE16/wire2LE16/enable | sync段，是否需所有Apply独立gate | CAPTURED25次 |
| SpeedTap reset/master | 51 56 /57 | generic pairreset /master | 51 56在留存allowlist中但本次0；51 57被过滤，不能报absence | STATIC ONLY |
| SpeedTap read | 25 01 /08 | master/pagedpair | 本次master真实query，pair未抓 | CAPTURED master；STATIC pair |
| Polling rate | 51 31 | byte4index | refresh也会写；本次53OUT | CAPTURED + STATIC |
| Polling queries | 12 15 /25 24 | index/mode | 不是当前refresh实际路径 | STATIC ONLY |
| Lighting params | 51 2C | effect2，countdown3，effect-specificbody | 1/2/3reports后Apply；bank | CAPTURED10IDs，主要syncreplay |
| Analog lighting | 51 2D | selector0，effect4，enable5 | 别名ECbrightness存在共享库，必须按HFXcallsite解释 | CAPTURED OFF；本轮ON NOT VERIFIED |
| Lighting query | 25 0E /03 | effectparameter /analogflag | 真实queryAPI与cachemerge并存 | STATIC，未捕到 |
| Generic side lighting | 51 2E | effect-specific | HFXproduct暴露未确认 | STATIC ONLY，不能当独立lightbar |
| SW ownership status | 27 00 | response4flag | AuraSync协调 | STATIC ONLY，body被过滤 |
| SW ownership set | 74 00 | byte4bool | exitSync后hostrestore；不是physicalRTswitch | STATIC ONLY，不能照搬 |
| Analog reset dispatcher | 51 52 | typeLE16及type-specificbody | type0/1/2/3/4等在库；未执行 | STATIC ONLY本轮；type1/4以前单独证据 |

## Exact examples

实际留存fixture中包含：

```
frame11 bank4: 51 00 00 00 04 +59 zeros
frame70 A AP2.0: 51 4F 00 00 1F 00 14 00 +56 zeros
frame112 B DZ: 51 59 00 00 32 00 02 04 +56 zeros
frame224 gateOFF: 03 76 00 00 00 +16 zeros (21bytes)
frame225 gateON:  03 76 00 00 01 +16 zeros (21bytes)
```

29条完整payload及其实际timestamps/echo在 `tests/fixtures/gear_link/captured_reference.json`，全部sequence在研究目录。disable0 syntheticfixture只测已知parsercontract，不冒充本轮捕获；resetType1/4既有productiontest不是本轮新增协议。

## Batch / transport / error

公开DeviceService使用controlReportID0、事件ReportID3，两条queue分别command与batch；response按reportID/family/subcommand关联，非完整transactionID。当前sendCommand实际timeout500ms、maxRetries3；executeCommand默认1000ms并非普通入口使用值；batch默认2000ms，需按具体callsite。失败可能resolve syntheticFFAA，而非总reject。

capture存在约500ms重复stage，部分在前一USBecho已出现后；单靠bytes不能确认是timeout、另一个caller或同步/刷新race。timeline保留全部副本，不去重伪造更短官方sequence。

Apply分组只是“上一Apply后观测到的stage集合”，不是主机互斥或firmware原子性的证明。普通selection没有Apply并不允许Aura绕过现有manual/Profile事务。echo仍不是readback、Flash提交证明或physicalsettledtimestamp。

没有generic rawsender、replay、51 53生产化、type0授权或M605timing变更。所有原件hash在resource-manifest；美化行号是派生文件行号，不是原件minified行号。
