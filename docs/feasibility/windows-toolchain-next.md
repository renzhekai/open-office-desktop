# 下一步 Windows 构建工具链

状态快照：2026-11-01 10:09 UTC。固定源码、build_tools 与六个子模块均已 checkout。**用户批准的 Qt 6.11.1 MSVC2022 x64 与 Multimedia 已从官方归档恢复、部署并通过实际编译和运行验证；自有程序已在真实原生引擎内完成 DOCX 打开、修改、保存、正常关闭和新进程重开。** 本机 150% 缩放的默认/最大化显示通过，完整引擎源码构建、多显示器及复杂文件保真尚未完成。

## 当前证据

- 已有 `D:\Qt\6.11.1\mingw_64`，可构建自有原型；本轮异步接口/UI 的 CTest 2/2 通过，smoke 退出码 0。它不提供真实 ONLYOFFICE DOCX 编辑保存能力。[本机验证](../../evidence/native-async-validation.json)
- `D:\Qt\6.11.1\msvc2022_64\bin\qmake.exe` 已存在，实际查询报告 Qt `6.11.1`、`win32-msvc` 和正确的 SDK 路径。手动 SDK 部署未注册到 MaintenanceTool，不能记成 MaintenanceTool 安装成功。[qmake 查询](../../evidence/qt-msvc-qmake-query.txt)、[SDK 验证记录](../../evidence/qt-msvc-manual-sdk.json)
- 实际使用已有 Visual Studio 环境中的 MSVC 工具集 `14.44.35207`（cl `19.44.35228`）、Windows SDK、CMake `3.30.5` 和 Ninja；未重装 Visual Studio。Release 构建及 `CMAKE_TRY_COMPILE_CONFIGURATION=Release` 已通过；Debug 的 PDB 工具链组合未验证通过，不因此修改系统配置。
- 独立 Qt 探针实际加载 Widgets、SVG、Network、Multimedia/MultimediaWidgets，完成 SVG 渲染和 1 秒 WAV 加载，观察到 `LoadedMedia`，退出码 0；没有音频播放或网络请求。它验证 SDK 可用，不验证办公引擎视频功能。[执行结果](../../build/qt-sdk-msvc/execution-summary-release.json)、[运行报告](../../build/qt-sdk-msvc/runtime-release.stdout.json)
- 固定 ONLYOFFICE Windows qmake 配置使用 MSVC 参数，build_tools 使用 vcvarsall/nmake。Qt6 源码分支存在，但不能把当前 MinGW Qt DLL/导入库直接供 MSVC 使用。[core/base.pri](https://github.com/ONLYOFFICE/core/blob/ed63e94de814ff1d4ea28cddfaba6939bc9df9ed/Common/base.pri#L125)

## 已获批准的新增组件与执行结果

已批准的目标位置：`D:\Qt\6.11.1\msvc2022_64`，保留现有 MinGW kit。

1. Qt **6.11.1 MSVC2022 x64**：`qt.qt6.6111.win64_msvc2022_64`。
2. 同一 kit 的 **Qt Multimedia**：`qt.qt6.6111.addons.qtmultimedia.win64_msvc2022_64`，默认上游 videoplayer 需要 Multimedia/MultimediaWidgets。

官方组件目录：[download.qt.io](https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6111/qt6_6111_msvc2022_64/)。Qt 6.11 官方支持 MSVC2022，先选择已装的 MSVC 14.44 实验验证。[Qt 支持表](https://doc.qt.io/qt-6.11/windows.html)

以下 MaintenanceTool 组件安装命令曾实际尝试但未成功。历史执行会话 **63973** 退出码 **1**，报 `HTTP/2 protocol error`、`Connection closed`、`Failed to connect`；随后改用已校验的官方组件归档完成 SDK 部署。历史网络错误不是当前阻塞，也不是自动审批拒绝、安装权限拒绝或商业许可证不足的证据。

```powershell
& 'D:\Qt\MaintenanceTool.exe' install `
  qt.qt6.6111.win64_msvc2022_64 `
  qt.qt6.6111.addons.qtmultimedia.win64_msvc2022_64
```

较早的 [qt-msvc-install-recovery.log](../../evidence/qt-msvc-install-recovery.log) 记录另一轮中止，不是会话 63973 的完整错误日志。最终恢复采用 [`get_qt_msvc_archives.py`](../../scripts/get_qt_msvc_archives.py) 和 [`prepare_qt_msvc_sdk.py`](../../scripts/prepare_qt_msvc_sdk.py)：9 份官方归档共 287,643,078 字节，逐一匹配官方 SHA256，安全提取后部署至新的 MSVC kit 目录。未接受新协议、未注册安装器、未修改原 MinGW 或系统 PATH。[归档清单](../../evidence/qt-official-metadata/payload-manifest.json)、[部署日志](../../evidence/qt-msvc-sdk-preparation.log)

官方元数据和归档清单保留在 [evidence/qt-official-metadata](../../evidence/qt-official-metadata/)。到位结论依据实际 qmake、构建和运行报告，不仅依据元数据下载。

## 已实际构建、链接与部署

- 基线 MSVC 自有程序编译通过，CTest 2/2 通过。[构建日志](../../evidence/native-msvc-build.log)
- 从固定 desktop-sdk 源码编译 `openoffice_qt6_documentscore.dll` 和 `openoffice_qt6_videoplayer.dll`，使用真实原生 DLL 导出生成并验证导入库，两个包装 DLL 均链接成功，导入表没有 Qt 5/libVLC。这里的“源码构建”只指这两个包装层及自有程序。[构建说明](../../tests/integration/native-qt-wrapper/README.md)、[链接日志](../../build/native-onlyoffice-qt6/dll-build-2.log)
- `scripts\build-native-engine.cmd` 以 `OPEN_OFFICE_WITH_ONLYOFFICE=ON` 构建真实 Native Adapter 与主程序成功，最终 CTest 2/2 通过，engine/UI 分别有 20/17 个 QtTest 条目（含 init/cleanup）。这些测试不初始化 CEF，真实编辑视图和 DOCX 保存另由桌面验收证明。[最终宿主构建日志](../../evidence/native-engine-build-final.log)
- 官方 v10.0.0 原生运行包通过锁定 SHA256 和文件 CRC 校验，`scripts/stage_native_engine.py` 将真实 native DLL、helper、converter、编辑资源与本项目入口部署到 `build/native-engine-run`。使用官方预编译原生引擎，不启动官方 `DesktopEditors.exe` 充当本项目程序。[运行包验证](../../evidence/onlyoffice-release-directory/runtime-verification.json)、[部署记录](../../build/native-engine-run/openoffice-development-stage.json)
- 首次部署曾使 Qt 6 宿主读到官方 Qt 5 平台插件，现将 Qt 6 插件部署至独立 `qt6-plugins` 并由 `qt6.conf` 指定；已有 DOCX 打开也不再被可选模板图库目录阻断。修复后收到 `doc:onready`，真实正文修改和保存已完成，正常关闭退出码 0。[Qt 插件部署日志](../../evidence/native-engine-staging-qt6-plugins.log)、[修复后启动报告](../../evidence/native-docx-launch-templatefix.json)、[首次运行事件](../../evidence/native-docx-first-events.log)
- 新进程 PID 19736 使用全新 `build/native-docx-reopen-profile` 重开保存文件，实际界面确认新增中文标记、原中文/English、粗斜体及完整 2×2 表格；中文工具栏和 ONLYOFFICE/Ascensio 署名可见，选择/打开禁用、保存启用状态正确。本机 150% 缩放的默认和最大化窗口中 CEF 填满编辑区，关闭收到 `DESTROYWINDOW`、循环返回和 shutdown 完成，退出码 0。多显示器和其他缩放组合未验证。[闭环结果](../../evidence/native-docx-roundtrip-result.json)、[重开事件](../../evidence/native-docx-reopen-events.log)

当前开发目录入口为 `build/native-engine-run/OpenOffice.exe`。运行验收使用测试文档及独立 profile；每次进程限定一份已有 DOCX，暂未开放新建、另存为、XLSX/PPTX。已保存并重开产物为 [`native-docx-roundtrip.docx`](../../fixtures/results/native-docx-roundtrip.docx)；最小样本通过不代表复杂文档全面保真。

## 源码和 qmake 适配已推进

DesktopEditors v10.0.0、`upstream/DesktopEditors/build_tools` 及全部六个子模块已按固定提交 checkout；`web-apps` 会话 55054 已退出 0。09:19:33 UTC 的 [八仓库核验](../../evidence/onlyoffice-source-checkout-verified.json) 记录提交、tracked 文件数量和状态：顶层及六子模块 tracked 干净，build_tools 只有三处预期补丁。精确提交见 [STATUS.md](STATUS.md) 和 [sources.lock.json](../../sources.lock.json)；正式构建依赖未因此全部就绪。

本地 build_tools 已应用 qmake 工具链适配：可指定完整 MSVC 工具集版本和 CMake 目录，处理空格路径，遇到版本不符或命令失败即中止。8 项隔离替身测试通过；尚未调用真实上游构建验证。[补丁与测试范围](windows-toolchain-patch.md)

另有直接 MSVC 构建的真实 `cfcpp` 最小组件：编译、链接、启动成功，10 字节流通过但 8192 字节流内容比较失败；未构建完整 kernel 或编辑引擎。[原生构建报告](native-cfcpp-build-result.md)

## 完整引擎源码构建仍待验证

固定 build_tools 的 Boost 1.72.0 / ICU 74.2 构建规则只选择 v140/v142，core 的 Boost 链接名也相应固定。需在隔离修改中补 v143 适配，并核对 CRT 和实际产物，不能只覆盖 VS 路径。[Boost](https://github.com/ONLYOFFICE/build_tools/blob/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/scripts/core_common/modules/boost.py#L72)、[ICU](https://github.com/ONLYOFFICE/build_tools/blob/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/scripts/core_common/modules/icu.py#L42)

本轮运行所需 CEF、转换核心、helper 与生成后的编辑资源已从官方发布包到位；它们尚未由本机从完整源码重建。Git 附带的 Perl 文件存在，但此前沙箱内运行退出 `-1073741502`，尚不能当作完整构建链的可用 Perl，也不能据此断言需要另装。V8 脚本中的系统 Git 配置修改不得随整体 make.py 自动执行。

已批准的 Qt 范围仅为上述两个组件，现已完成 SDK 到位核验；不包含旧 VS 安装、系统设置修改或整个上游自动安装链。自有宿主的最小 DOCX 运行闭环现已通过，后续按需求逐项推进文档功能及完整依赖源码构建并分别验证。
