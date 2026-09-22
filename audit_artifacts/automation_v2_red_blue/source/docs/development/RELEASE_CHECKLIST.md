# Release checklist — legacy launcher and manual hardware gates

> 版本号以 [`VERSION`](../../VERSION) 为单一事实源。下列真机项目必须由 Owner 在候选发行产物上手工确认。

- [ ] `VERSION` 、`CHANGELOG.md` 与拟创建的 Git tag 一致（tag 格式为 `v<VERSION>`）。
- [ ] GitHub Actions 的 Windows 构建/运行时测试和 Frontend 测试/构建全部通过。
- [ ] 在全新 clone 中按 README 完成 `npm ci`、前端测试/构建、CMake Release 构建和 CTest。
- [ ] 运行 `python tools/package_release.py`，确认产生 `dist/Aura.exe` 与版本化 ZIP。
- [ ] 检查 `Aura.exe` 资源和 ZIP 内容：不得包含 `AacKbHal_x64.dll` 或其他 ASUS 专有二进制。
- [ ] 默认 Native HID 在不依赖 ASUS HAL 的环境启动；仅在显式 `legacy_hal` 模式检查已安装官方 DLL 的 Gate。
- [ ] 仅在 legacy HAL 模式检查缺失/不支持 DLL 的诊断；不得把默认 Native HID 描述为需要 HAL。
- [ ] 在安装 MSVC Build Tools + C++ Desktop workload + Windows SDK 的机器上，从单文件运行时完成 Studio 草稿保存、原生发布、daemon 热加载和方案切换。
- [ ] 在 ROG Falchion Ace HFX 真机上检查启动、预览、应用、退出恢复以及双 USB-C 重连。
- [ ] 在 CS2 真实对局中确认 GSI 在线、基础方案、低血量/C4 持续叠加和击杀/爆头事件叠加。
- [ ] 检查 `Aura.exe` Windows 文件/产品版本、ZIP 文件名和包内 README 版本均来自 `VERSION`。
- [ ] 从 `CHANGELOG.md` 整理 GitHub Release notes，明确 alpha 状态、ASUS HAL 不随包分发、MSVC 要求和已知限制。
- [x] 项目代码采用 GPL-3.0-only，根目录已包含标准 LICENSE 文件。

WinUI 不在当前 launcher 包内；发布 WinUI 前先处理 [packaging debt](PACKAGING.md)，另行验收原生 Lighting 参数、配置冲突、Studio WebView2 与退出生命周期。
