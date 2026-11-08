# Windows 原生宿主与 ONLYOFFICE 集成路线

2026-11-01。自有 `OpenOffice.exe` 已在同一窗口内嵌入真实 ONLYOFFICE 编辑器，完成一个小型 DOCX 样本的打开、中文编辑、原路径保存、正常关闭和新进程重开验收。当前是单 DOCX 原生集成里程碑；第一阶段无 AI 的边界保持不变。验收结论及范围见 [native-docx-roundtrip-result.json](../../evidence/native-docx-roundtrip-result.json)。

## 当前路线与构建组成

程序使用自有 C++17 / Qt 6.11.1 Widgets 入口、窗口和操作栏，通过 `desktop-sdk` 的 `QAscApplicationManager`、`CApplicationCEF` 和 `QCefView` 承载真实编辑会话；文档编辑仍由 ONLYOFFICE 原生核心、`sdkjs` 与 `web-apps` 完成。入口为 `build/native-engine-run/OpenOffice.exe`，未启动官方 `DesktopEditors.exe`。

固定上游入口 [win-linux/src/main.cpp](https://github.com/ONLYOFFICE/desktop-apps/blob/332c1905a5361dd4139481827b7ad4f4a9facd85/win-linux/src/main.cpp) 使用 SingleApplication、CApplicationCEF、AscAppManager，并配置本地转换器与 web-apps 资源。宿主创建逻辑见 [cascapplicationmanagerwrapper.cpp](https://github.com/ONLYOFFICE/desktop-apps/blob/332c1905a5361dd4139481827b7ad4f4a9facd85/win-linux/src/cascapplicationmanagerwrapper.cpp)。这些源码用于核对接口和生命周期；当前自有窗口并未完整移植上游桌面应用的全部功能。

[defaults.pri](https://github.com/ONLYOFFICE/desktop-apps/blob/332c1905a5361dd4139481827b7ad4f4a9facd85/win-linux/defaults.pri) 连接 ascdocumentscore、qtascdocumentscore 与 core 库，并包含 Qt6 兼容分支。实际开发版本采用以下组成：

| 层次 | 当前来源与验证 |
| --- | --- |
| 自有宿主、引擎适配层 | 本项目源码；Qt 6.11.1 / MSVC 工具集 14.44.35207 编译和链接通过 |
| Qt 包装层 | 从固定 ONLYOFFICE 源码本地编译 `openoffice_qt6_documentscore.dll` 与 `openoffice_qt6_videoplayer.dll`；独立名称避免覆盖官方 Qt5 包装层 |
| 原生核心、CEF helper、转换器、编辑器资源 | 官方 v10.0.0 Windows x64 ZIP；完整 SHA-256 和归档成员校验通过，作为固定开发依赖使用 |
| 原生核心全量源码构建 | 尚未完成；本次验收不代表已从源码重建全部 ONLYOFFICE 二进制 |

源码和版本见 [sources.lock.json](../../sources.lock.json)；源码已按固定提交取得，`build_tools` 也已取得，但完整第三方依赖图与全量构建仍待验证。官方 ZIP 的 SHA-256 为 `54979d04f1cdca56deb0c62e0356355a45f392cdb54f04f6b7e436b1f44c3605`，证据见 [runtime-verification.json](../../evidence/onlyoffice-release-directory/runtime-verification.json)。Qt 包装层的对象编译、真实导入库生成和 DLL 链接过程见 [native-qt-wrapper/README.md](../../tests/integration/native-qt-wrapper/README.md)。

## 已实现的宿主与会话边界

- `OfficeEngine` 使用调用方提供的请求 ID、异步完成信号及原生修改状态。请求返回 `ok` 仅表示接受；界面只在匹配 ID 和操作类型的完成事件后更新成功状态，失败保留当前路径和修改状态。
- 启用 `OPEN_OFFICE_WITH_ONLYOFFICE` 时，`OnlyOfficeEngineAdapter` 执行真实 CEF 初始化、嵌入视图创建、事件复制与排队转译、保存确认、关闭握手和正常退出收尾。未启用原生后端的基线构建仍明确返回 `engine_unavailable`，不写入文档。
- 原生后端每个进程只打开一个已有 DOCX。接受首次打开后，`capabilities().open` 变为 false；收到 `doc:onready` 并确认真实文件路径后，才提供编辑和保存能力。新建和另存为尚未提供。
- 保存仅支持当前已打开 DOCX 的原路径。原生路径询问按同一 view ID 回复已接受的目标；未实现的内部另存为明确取消，不能静默换目标或悬挂等待。修改标记由原生事件驱动。
- 界面在关闭前处理保存、放弃或取消。适配层等待原生销毁完成，再结束会话；正常路径已观察到 `DESTROYWINDOW`、消息循环返回、收尾日志及进程退出码 0。异常情况下仍保留防止过早析构的保护，不据此宣称所有崩溃或强制退出路径均已验证。
- 编辑器使用 `lang=zh-CN`。自有 `EditorHost` 以 Win32 客户区物理尺寸调整 CEF 子窗口，修正 Qt6 逻辑像素在 150% 缩放下导致的尺寸不足；当前显示器的普通和最大化窗口已验收，跨显示器切换尚未测试。

CEF helper 要求位于宿主可执行文件目录。隔离运行目录保留官方资源布局，并部署所需原生 DLL；Qt6 插件放入 `qt6-plugins`，通过 `qt6.conf` 选择，避免误加载官方 Qt5 插件。独立 profile 保存缓存、恢复目录及开发事件日志。键盘轮询仍执行，但不再每 100 ms 写日志；日志不记录正文。

## 已接入的真实接口与源码依据

固定源码：desktop-apps `332c1905a5361dd4139481827b7ad4f4a9facd85`；desktop-sdk `ae6cf2a039355183c512fc314f7a9bd1a05a6a11`。

- **嵌入视图**：`QCefView(parent)` → `Create(manager, cvwtEditor)` → `CCefViewEditor::OpenLocalFile(path, GetFileFormat(path), params)`。宿主在创建视图后再打开文档，不启动外部办公程序。见 [CTabPanel](https://github.com/ONLYOFFICE/desktop-apps/blob/332c1905a5361dd4139481827b7ad4f4a9facd85/win-linux/src/components/ctabpanel.cpp#L115)。
- **就绪与修改**：`CAscCefMenuEventListener` 接收 `CEF_EXECUTE_COMMAND` / `doc:onready`；页面加载完成不等于文档打开完成。修改状态来自 `CEF_MODIFY_CHANGED`。适配层复制事件字段，释放上游事件一次，再排队投递 Qt 线程，并核对 view ID。见 [事件转译](https://github.com/ONLYOFFICE/desktop-apps/blob/332c1905a5361dd4139481827b7ad4f4a9facd85/win-linux/src/ccefeventstransformer.cpp#L75)。
- **保存请求与目标**：普通保存发送 `ASC_MENU_EVENT_TYPE_CEF_SAVE`。若收到路径请求，通过 `CEF_LOCALFILE_SAVE_PATH` 和 `CAscLocalSaveFileDialog(Id, Path, FileType)` 经 `manager.Apply` 回复；真正序列化由编辑器完成。见 [保存入口](https://github.com/ONLYOFFICE/desktop-apps/blob/332c1905a5361dd4139481827b7ad4f4a9facd85/win-linux/src/windows/cmainwindow.cpp#L672) 和 [路径回传](https://github.com/ONLYOFFICE/desktop-sdk/blob/ae6cf2a039355183c512fc314f7a9bd1a05a6a11/ChromiumBasedEditors/lib/src/cefview.cpp#L7689)。
- **保存完成或失败**：关联 `DOCUMENT_NAME` 的实际路径与 `ONSAVE`，确认未取消且保存路径匹配后才报告成功。`ONSAVE.IsCancel=true` 也可能代表转换失败，不能一概标为用户取消。见 [保存完成分支](https://github.com/ONLYOFFICE/desktop-sdk/blob/ae6cf2a039355183c512fc314f7a9bd1a05a6a11/ChromiumBasedEditors/lib/src/cefview.cpp#L6225)。
- **启动与关闭**：`Init_CEF` 主进程也可能返回 0，必须用 `IsChromiumSubprocess()` 区分；初始化流程返回并不独立证明编辑成功。传入 `--external-message-loop=1` 接入 Qt 事件循环。关闭发送 `CEF_CLOSE`，按 `ONCLOSE` / `ONBEFORECLOSE` 决策调用 `DestroyCefView`，以 `DESTROYWINDOW` 作为完成依据；不能在原生监听回调内直接析构视图或 manager。见 [CEF 初始化](https://github.com/ONLYOFFICE/desktop-sdk/blob/ae6cf2a039355183c512fc314f7a9bd1a05a6a11/ChromiumBasedEditors/lib/src/cefapplication.cpp#L360)。

另存为仍属于后续范围。已读到的 macOS 菜单路径使用 `CEF_EXECUTE_COMMAND_JS` 与 `CAscExecCommandJS(Command=file:saveas, FrameName=frameEditor)`，见 [macOS 另存为](https://github.com/ONLYOFFICE/desktop-apps/blob/332c1905a5361dd4139481827b7ad4f4a9facd85/macos/ONLYOFFICE/AppDelegate.mm#L699)；不能据此将未经当前 Windows 后端验收的 `CEF_EDITOR_EXECUTE_COMMAND / saveAs` 方案写成已实现接口。

## 实际验收与证据

[native-docx-roundtrip-result.json](../../evidence/native-docx-roundtrip-result.json) 记录了以下真实操作：

1. 启动自有 `OpenOffice.exe`，嵌入原生编辑器并收到文档就绪事件。
2. 通过 Windows GUI 在正文开头插入中文标记，点击自有保存按钮，收到原生保存成功及修改状态清除。
3. 独立检查保存后的 OOXML 包，确认标记只增加一次，原有段落及 2×2 表格保留。结果见 [native-docx-saved-semantics.json](../../evidence/native-docx-saved-semantics.json)。
4. 经原生关闭握手正常退出；再以新进程和全新 profile 打开保存文件，目视确认标记、原文与表格仍在，并检查中文工具栏、上游署名及编辑器尺寸。第二次进程也正常退出，退出码 0。

保存产物为 [native-docx-roundtrip.docx](../../fixtures/results/native-docx-roundtrip.docx)，25,868 字节，SHA-256 为 `0f1c78c8d53f8de66d0eda2de4fafd15c7a7a9fcfda8e587578182e1c3d9059e`。构建和两组 CTest 通过，见 [native-engine-build-final.log](../../evidence/native-engine-build-final.log)。GUI 图像证据保存在本次任务的实际 Windows CUA 对话截图中，未声称另有独立截图文件。

这次验收仅覆盖小型文字与表格样本，不代表复杂 Office 文档排版普遍保真。包装层成功编译、宿主成功链接和真实文档往返是分别完成的门槛；完整原生引擎源码构建仍是另一项未完成工作。

## 下一步与未完成范围

保留当前已验证的 DOCX 基线，继续完成多文档会话切换、新建、另存为，以及 XLSX/PPTX/PDF、打印和媒体等需求。每项都应连接真实原生操作并独立验收；当前未提供这些能力。复杂文档保真、跨显示器 DPI、异常关闭与恢复也需要专项验证。

源码和固定运行依赖已取得，原先等待用户自行下载的状态已结束。后续全量源码构建仍需逐项核对 `build_tools` 的第三方依赖和构建行为；当前没有以现成核心依赖的验收替代源码全量构建。阶段一没有加入 AI，未执行外部发布或安装器注册，并保留上游许可与署名。
