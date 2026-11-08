> 2026-11-05 更新：编辑器的 5 个 WASM 引擎已从源码重建，并与官方逐项差分测试通过。默认入口改为 `build/full-source-run-wasm-candidate/OpenOffice.exe`，已通过真实 DOCX 中文编辑、保存与重开回归，见 [验收记录](../../evidence/wasm-source/candidate-docx-roundtrip-result.json)；`build/full-source-run` 保留为回退。来源边界见 [WASM 来源审计](full-source-wasm-provenance.md)。

> 2026-11-02 更新：当前已完成 Windows 原生核心源码构建，入口为 `build/full-source-run/OpenOffice.exe`，真实 DOCX 中文编辑、保存、正常退出和独立进程重开通过。详见 [本轮验收](../../evidence/full-source-docx-roundtrip-result.json)。5个WASM及生成JS等仍为固定上游预生成资产，不能称所有第一方资产源码构建完成，见 [审计](full-source-wasm-provenance.md)。以下早期记录保留为历史阶段依据。

# 第一阶段实际进度

状态快照：2026-11-01 10:09 UTC。**自己的 C++17 / Qt 6 Widgets 程序已在真实 ONLYOFFICE 视图内完成 DOCX 打开、修改、保存、正常关闭及新进程重开验证，最小编辑保存闭环通过。** 本机 150% 缩放下默认窗口与最大化显示均已复验。顶层源码、build_tools 与六个子模块已完整 checkout，Qt 6.11.1 MSVC2022 x64 与 Multimedia 已到位并通过实际编译和运行验证。本轮使用官方 v10.0.0 原生引擎二进制，Qt 6 包装层及自有程序由本地源码构建；完整引擎源码构建仍未完成。

## 已完成与证据

- 程序入口、原生窗口、文件元数据选择、主题切换已实现。已有本地运行包：[`dist/OpenOffice-0.1.0/OpenOffice.exe`](../../dist/OpenOffice-0.1.0/OpenOffice.exe)。这是未接引擎的开发原型。
- 首轮实际 Windows 启动证据：[`native-final-smoke.json`](../../evidence/native-final-smoke.json)、[`native-home-final.png`](../../evidence/native-home-final.png)。JSON 明确记录 `engine_connected=false`、`office_open_edit_save=not_connected`；图片是本程序窗口渲染，不是 ONLYOFFICE 编辑画面。
- 源码中的引擎边界已改为带请求 ID 的异步操作：请求受理不代表完成，只有匹配的完成事件更新打开/保存状态。界面保留保存失败后的未保存状态，阻止忙碌时关闭及未保存时替换文档。该准备工作不能证明真实引擎已接通。
- 本轮异步接口与 UI 验证：CTest **2/2 通过**，本程序启动 smoke **退出码 0**。证据：[`native-async-validation.json`](../../evidence/native-async-validation.json)、[`native-async-smoke.json`](../../evidence/native-async-smoke.json)、[`native-async-home.png`](../../evidence/native-async-home.png)。测试使用受控事件，记录仍为 `onlyoffice_integrated=false`、`docx_edit_save_reopen=not_executed`。
- `OPEN_OFFICE_WITH_ONLYOFFICE=ON` 现选择真实 Native Adapter，已实现 DOCX 打开、原路径保存、dirty 通知和关闭握手；未启用此选项时仍保留拒绝文档操作的基线适配器。当前最小范围是每次进程处理一份已有 DOCX，新建与另存为未开放。代码存在及链接通过尚不能证明实际编辑成功。
- 本机工具与固定官方版本记录在 [`environment.json`](../../evidence/environment.json)、[`toolchain-extra.json`](../../evidence/toolchain-extra.json)、[`sources.lock.json`](../../sources.lock.json)。原 MinGW kit 保留；本轮真实引擎宿主使用 MSVC 工具集 `14.44.35207`、Qt `6.11.1 msvc2022_64`。
- Qt SDK 通过 9 份官方归档逐一 SHA256 校验后部署到 `D:\Qt\6.11.1\msvc2022_64`。这是手动 SDK 部署，未注册到 MaintenanceTool；未接受新协议、未修改原 MinGW。实际探针完成 Widgets/SVG 和 Multimedia 加载，1 秒本地 WAV 达到 `LoadedMedia`、退出码 0，没有播放音频。证据：[`qt-msvc-manual-sdk.json`](../../evidence/qt-msvc-manual-sdk.json)、[`执行结果`](../../build/qt-sdk-msvc/execution-summary-release.json)、[`运行报告`](../../build/qt-sdk-msvc/runtime-release.stdout.json)。
- 固定源码的 Qt 6 包装 DLL `openoffice_qt6_documentscore.dll`、`openoffice_qt6_videoplayer.dll` 已真实链接，导入表确认没有 Qt 5/libVLC 依赖。最终自有 Native Adapter 与主程序构建成功，CTest **2/2 通过**；engine/UI 分别记录 20/17 个 QtTest 条目（含 init/cleanup）。这些测试覆盖接口/UI 行为，不初始化 CEF，真实文档往返另行验证。证据：[`包装层构建说明`](../../tests/integration/native-qt-wrapper/README.md)、[`DLL 链接日志`](../../build/native-onlyoffice-qt6/dll-build-2.log)、[`最终宿主构建与测试日志`](../../evidence/native-engine-build-final.log)。
- 官方 v10.0.0 ZIP（397,286,158 字节）SHA256 已与锁定值匹配，4,179 个文件通过 CRC 与安全路径检查；引擎、CEF helper、converter 和生成后的编辑资源已部署到 [`build/native-engine-run`](../../build/native-engine-run/)。入口是本项目的 `OpenOffice.exe`，不是启动官方 `DesktopEditors.exe`。证据：[`归档验证`](../../evidence/onlyoffice-release-directory/runtime-verification.json)、[`部署记录`](../../build/native-engine-run/openoffice-development-stage.json)。
- 本轮真实桌面验证已收到 `doc:onready`，在正文插入“原生引擎编辑保存验证 2026-11-01”，使用自有窗口的保存按钮写入 [`native-docx-roundtrip.docx`](../../fixtures/results/native-docx-roundtrip.docx)（25,868 字节）。独立 ZIP/XML 检查确认标记存在、原表格 4 个单元格文字保留、CRC 通过；随后收到 `DESTROYWINDOW`、`message_loop_returned` 和 `shutdown_completed`，进程退出码 0。[首次启动报告](../../evidence/native-docx-launch-templatefix.json)、[首次运行事件](../../evidence/native-docx-first-events.log)
- 新进程 PID `19736` 使用全新的 `build/native-docx-reopen-profile` 重开保存文件，排除旧恢复缓存影响。实际界面显示新增中文标记、原中文/English、粗斜体和完整 2×2 表格；中文工具栏、ONLYOFFICE/Ascensio 署名，以及打开后选择/打开禁用、保存启用状态均已检查。本机 150% 缩放的默认窗口和最大化窗口中 CEF 均填满编辑区域；正常关闭再次收到销毁和 shutdown 完成事件，退出码 0。该结论限于本机这份测试 DOCX，不代表多显示器或复杂排版全面兼容。[闭环结果](../../evidence/native-docx-roundtrip-result.json)、[重开事件](../../evidence/native-docx-reopen-events.log)
- OOXML 样本及语义检查工具已准备，16 项工具单测通过。此前实际 `compare` 结果为 `not_executed`、退出码 2：没有真实编辑器保存输出，不能计为兼容性通过。
- Windows qmake 工具链适配补丁已应用于本地固定 `build_tools`，8 项替身测试通过。补丁和范围见 [`windows-toolchain-patch.md`](windows-toolchain-patch.md)；它验证命令生成、工具集选择和失败传递，未验证真实 ONLYOFFICE 编译、链接或启动。
- 真实 `cfcpp` 原生组件已用 MSVC 编译、链接并运行消费者；10 字节流往返一致，8192 字节流内容不一致，整体往返未通过。它不是完整 kernel 或编辑引擎，也未接入本程序。[构建结果与失败证据](native-cfcpp-build-result.md)
- 原始 `requirements.md` 保持不变，SHA256：`1e7d43e339deb30766561fb315e87585ea3bb06c32cb11678f84059d379c19ff`。

## 源码下载现状

用户后续已授权助手下载源码。`upstream/DesktopEditors` 内八个仓库已于 **2026-11-01 09:19:33 UTC** 核对提交对象、实际 tracked 文件及工作区状态，以下提交已 checkout：

| 仓库或子模块 | 本地路径（相对项目） | 已核对提交 |
|---|---|---|
| DesktopEditors v10.0.0 | `upstream/DesktopEditors` | `daf8c6948148827201b2907ac1134d222fbfa7b4` |
| build_tools | `upstream/DesktopEditors/build_tools` | `43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda` |
| core | `upstream/DesktopEditors/core` | `ed63e94de814ff1d4ea28cddfaba6939bc9df9ed` |
| desktop-apps | `upstream/DesktopEditors/desktop-apps` | `332c1905a5361dd4139481827b7ad4f4a9facd85` |
| desktop-sdk | `upstream/DesktopEditors/desktop-sdk` | `ae6cf2a039355183c512fc314f7a9bd1a05a6a11` |
| dictionaries | `upstream/DesktopEditors/dictionaries` | `d3223bbb777883db66ac3cd249f71c6ebdc992c7` |
| web-apps | `upstream/DesktopEditors/web-apps` | `f79972a400c3a696490094961772d1f86fac1b82` |
| sdkjs | `upstream/DesktopEditors/sdkjs` | `9d2138866d929be32f1d107e8b5bfae36156e097` |

`web-apps` 恢复会话 **55054** 已完成，退出码 **0**；六个子模块均已到位。顶层与六个子模块的 tracked 工作区干净，build_tools 只有 `configure.py`、`scripts/config.py`、`scripts/qmake.py` 三处预期补丁。证据：[`onlyoffice-source-checkout-verified.json`](../../evidence/onlyoffice-source-checkout-verified.json)、[`sources.lock.json`](../../sources.lock.json)、[`source-webapps-recovery.log`](../../evidence/source-webapps-recovery.log)。核验仍明确记录 `full_build_dependencies_verified=false`，不能记成全部构建依赖就绪。

较早的 [`onlyoffice-inputs.json`](../../evidence/onlyoffice-inputs.json) 和 [`network-preflight.json`](../../evidence/network-preflight.json) 是历史检查，不能继续据此宣称当前没有 `upstream`、没有源码或尚未获下载授权。源码获取脚本仍见 [`get-onlyoffice-source.ps1`](../../scripts/get-onlyoffice-source.ps1)，本轮实际下载及恢复结果以上述锁定文件和日志为准。

## 当前验证边界与待解决项

1. **最小 DOCX 闭环已通过，功能范围仍有限**：当前每次进程处理一份已有 DOCX、原路径保存；新建、另存为、XLSX/PPTX 和复杂文档保真尚未验证。本机 150% 缩放的默认/最大化显示已通过，多显示器及其他缩放组合未验证。
2. **首次启动配置问题已排除**：部署时曾混用官方 Qt 5 与本地 Qt 6 平台插件，现将 Qt 6 插件隔离到 `qt6-plugins`；适配器也已移除对不存在的 `converter/templates` 的强制检查，因为打开已有 DOCX 不依赖新建模板图库。修复后程序已进入真实编辑视图。[Qt 插件部署日志](../../evidence/native-engine-staging-qt6-plugins.log)、[修复后启动报告](../../evidence/native-docx-launch-templatefix.json)
3. **完整 Windows 引擎源码构建尚未完成**：官方发布包现已提供本轮运行依赖，但 Boost/ICU/V8、转换核心等完整源码构建仍需逐项验证。qmake 补丁及最小 `cfcpp` 实验不代表完整引擎自行构建通过。
4. **历史 Qt 安装网络失败已恢复**：MaintenanceTool 会话 63973 的 HTTP/2/连接错误属于历史失败；经官方归档恢复，SDK 已实际到位和运行通过。它不是当前权限待批事项，也不是商业许可证阻塞。

## 已达到的完成标准与后续范围

当前可运行入口为 [`build/native-engine-run/OpenOffice.exe`](../../build/native-engine-run/OpenOffice.exe)，使用独立开发配置目录和测试文件。按 [原生宿主路线](../architecture/native-host.md) 实际验证了真实视图、文件加载完成事件、保存完成事件和关闭握手。自有程序及 Qt 6 包装层的本地源码构建与官方预编译原生引擎来源分别记录。

首个完成标准已达到：本程序宿主内打开 `fixtures/generated/text-table.docx` 的独立测试副本，手动修改可见正文、保存并正常关闭，再以新进程和全新 profile 重开，确认新增内容和原表格确已落盘且可见。后续按需求逐项扩展文档操作和 XLSX/PPTX，并分别补齐测试；本次小样本通过不替代复杂文件保真与完整引擎源码构建验收。

## 范围

第一阶段不实现 AI、MCP、模型或偏好学习。保留 ONLYOFFICE 来源署名及适用法律信息；当前目录是本地开发构建，正式分发前补齐实际依赖许可。源码获取和指定 Qt 组件已有授权且本轮已到位；付费、外部发布、新账号/凭据、额外权限或安全设置改变仍按用户既定确认边界处理。Windows 完整引擎源码构建、多显示器及复杂文件保真仍待验证。

