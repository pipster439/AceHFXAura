# Offline Test Results

日期2026-10-09。**OFFLINE TEST 49/49 PASS；PHYSICAL NOT RUN。** 独立observer仅构建，今晚没有运行任何模式。没有PostMessage WM_INPUT、SendInput、OS输入注册或实际keyboard report读取。

## 构建和执行

```powershell
cmake -S tools/research/physical-key-stage-b -B audit_artifacts/physical-key-overnight-20261009/build -G "Visual Studio 18 2026" -A x64
cmake --build audit_artifacts/physical-key-overnight-20261009/build --config Release
ctest --test-dir audit_artifacts/physical-key-overnight-20261009/build -C Release -V
```

MSVC19.51.36257、Windows SDK10.0.26100、C++17、`/W4 /WX /utf-8`。Release两个目标构建通过；CTest只执行stage_b_offline_tests，1/1测试程序通过，包含49个命名断言场景，约0.02秒。Windows ABI fixture使用本机SDK定义：RAWINPUTHEADER24，HID data offset32，pointer8 bytes；这里只构造内存，未验证OS投递ABI。非Windows分支是synthetic x64布局，今晚未在其它平台运行。

初次新增测试编译失败：std::fill的int值触发C4244 `/WX`。显式uint8_t修正后最终无warning；未改变生产代码或任何输入权限。源码变更后的最终build与CTest日志作为本轮证据保存。

## Fixture等级

| Fixture | 来源 / 能证明什么 |
|---|---|
| 21字节03 76、state0/1、其它字节0 | 已有历史RT物理契约的**重建**fixture；不是raw capture replay。复用现有纯ParseHardwareRtGateReport |
| 03 73 / 03 74，假设ID字段与参数 | **SYNTHETIC**，只测候选framing/计数，不证明USB格式、物理身份或Down/Up |
| 00前缀65字节control | **SYNTHETIC** 缓存契约假设；真实RAWHID prefix/length UNKNOWN |
| RAWINPUT Header + size/count + padding及错误返回 | **MOCK ACQUISITION**，已抽离Windows API层；测试同一InputDelivery逻辑 |
| fakeSubscription、GetMessage返回分类、entry计数 | **OFFLINE LIFECYCLE**，没有真正Window/message pump/WTS/注册注销 |

## 测试覆盖

原25项保留：注册snapshot flags0/2000、拒绝错误class/window/flags/count；73/74参数仅计bins；非目标/旧generation不解引用payload；重复/乱序只计数；整batchframing校验；zero/truncated/oversize/overflow、deadline、monotonic、disconnect/rearm；200000报告预算；不安全注册拒绝、add失败、重复清理、异常RAII与remove失败。

新增24项：

| 类别 | 场景与实际断言 |
|---|---|
| RT正确契约 | mock header→size→copy→core：ON/OFF各1，真实共用纯parser；三roundtrip重复状态统计；单条重复计数 |
| RT坏数据 | state>1、reserved2/3、tail非0为UNKNOWN；null/不足marker/错误ID不当RT；长度20为recognized-malformed |
| 状态边界 | 缺OFF不合成OFF，silence不推release；timeout/disconnect/Exception为STALE；新arm为UNKNOWN，旧RT counts不复用 |
| 候选报文 | SYNTHETIC73/74通过同一acquisition；74先到、重复73、缺74只计结构，不建立key状态 |
| 隐私filter | 同usage非目标或non-HID header，size/copy调用数必须0；stale generation同样不fetchpayload |
| 超时 | acquisition在deadline前置，不调用mock header；timer无prior-report的时钟倒退停止 |
| API故障 | header错误/short；size错误/过小/65537；copy错误/short/changed size；每类有单独计数，不计有效report |
| Header/边界 | copy header的type/size/device改变拒绝；dwCount2造成truncation、UINT_MAX乘积拒绝；尾7字节padding不当report |
| 诊断 | unknownID99和length20分别计数；count0停止；entry即使inactive也计total，并分active/inactive |
| 生命周期 | GetMessage -1与unexpected0分类及subscription close一次；mock copy抛异常释放owned subscription、raw/state reset；disconnect更新generation，拒绝非目标与old-session，重新arm重新确认RT |

完整49个名称与结果在`logs/offline-tests.log`；错误代码仅为mock输入，不冒充Windows发生过该错误。

## 正确性范围与未覆盖

解析/过滤链在以上fixture域和实际共用代码中通过，不能证明“所有任意输入正确”，更不能证明USB→HIDClass→Raw Input→WndProc可达。NativeInputSource包装的真正GetRawInputData返回、实际foreground、GetMessage queue、窗口启动、DEVNOTIFY、WTS锁屏及设备重连 **NOT RUN / BLOCKED_PHYSICAL**。

临时copy buffer由RAII volatile wipe覆盖正常及异常出口；测试确认coreprevious-report清零。没有越界读取释放后内存来伪造heap擦除PASS，也不声称能清除OS/驱动内存或所有compiler副本。断连和reset不能补齐缺失的73/74物理释放；不存在物理protocol state machine。

真实01/FF00长度未知，遇到合法但不同layout会安全停止。对数据乱序/丢失没有firmware序号，无法通用检测；测试验证不凭时间配对、不造release。RT current在stop标STALE，last_valid仅是旧聚合值。长期只有结构metadata及计数，raw报文不存盘、不上传AI。

构建、imports/source静态检查、既有文件保护、patch隔离验证和ZIP检查汇总见`evidence/OVERNIGHT_VALIDATION.json`及证据包manifest。没有重复执行生产全套测试；本轮未修改生产文件。

初次patch check/apply通过但byte-hash因新增JSON的CRLF/LF差异失败；仓库JSON生成显式LF后，以新隔离目录重新验证exact hashes。保留`logs/patch-apply-initial.json`，最终结果以`logs/patch-apply-check.json`为准。
