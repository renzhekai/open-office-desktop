# ONLYOFFICE v10.0.0：官方原生 DLL + 本地 Qt6 包装的候选路线

核查日期：2026-11-01。源码基线 DesktopEditors commit daf8c6948148827201b2907ac1134d222fbfa7b4；desktop-sdk commit ae6cf2a039355183c512fc314f7a9bd1a05a6a11。目标是自有 Qt6 窗口嵌入真实编辑引擎；官方原生引擎作为明确标注的预编译构建依赖，不能宣称完整引擎已经本地编译。

本轮最终结果：完整 ZIP 已下载到 dependencies/onlyoffice-v10.0.0/DesktopEditors_x64.zip，397,286,158 字节，固定 SHA-256 校验通过。已安全提取到同目录 runtime：4,911 个条目、4,179 个常规文件、1,070,010,975 字节，路径检查与所有文件 CRC 通过。详细逐文件 SHA-256 见 [runtime-verification.json](runtime-verification.json)，七个关键二进制静态摘要见 [verified-native-summary.json](verified-native-summary.json)。未执行官方 EXE/DLL，未安装系统软件，未改 app/dist。网络超时已通过保留分段并恢复解决，本子任务没有遗留下载阻塞。

完整归档提取后再次解析确认：kernel.dll 有 567 个具名导出/9 个 DATA，graphics.dll 有 1,544 个具名导出/44 个 DATA，ascdocumentscore.dll 有 200 个具名导出/10 个 DATA；三者均为 x64/linker 14.29。官方 videoplayer.dll 明确导入 Qt5 与 libvlc；editors_helper.exe 导入原生 ascdocumentscore 而不导入 Qt。范围读取的两个 DLL 与最终归档提取文件的 SHA-256、imports、exports 完全一致。可将相应 JSON 交给本地 Qt6 包装的导入库生成与链接步骤；ABI 运行正确性仍须由实际宿主和文档闭环测试证明。

## 发布包与目录证据

[官方 release asset 页面](https://github.com/ONLYOFFICE/DesktopEditors/releases/expanded_assets/v10.0.0)的 DesktopEditors_x64.zip SHA-256 与 sources.lock.json 相同：

54979d04f1cdca56deb0c62e0356355a45f392cdb54f04f6b7e436b1f44c3605

官方 HTTPS HEAD 返回精确大小 397,286,158 字节、Accept-Ranges: bytes。只读 Range 请求取得尾部 65,557 字节和中央目录 688,643 字节（均 HTTP 206）；EOCD 指向 4,911 个条目，无 ZIP64，未发现目录名越界。目录文件 zip-directory.json 记录全部条目、压缩/原始大小、CRC 和本地头偏移。中央目录 SHA-256：0fe4493de54924bcbb05c3683c6afab6aec7632dc50a20b59721ec75ae86cdbf。该 Range 目录取证本身不替代完整 ZIP 的 SHA-256 校验。

| 内容 | 包内位置 | 原始字节 |
| --- | --- | ---: |
| 原生引擎 | ascdocumentscore.dll | 3297264 |
| 官方 Qt5 包装层 | qtascdocumentscore.dll | 142320 |
| 原生图形库 | converter/graphics.dll | 9556464 |
| 原生基础库 | converter/kernel.dll | 1031664 |
| 转换器 | converter/x2t.exe | 39928816 |
| CEF | libcef.dll | 194895856 |
| 浏览器辅助进程 | editors_helper.exe | 24048 |
| 官方视频包装 | videoplayer.dll | 211952 |
| 文档编辑入口 | editors/web-apps/apps/documenteditor/main/index.html | 126015 |
| 表格编辑入口 | editors/web-apps/apps/spreadsheeteditor/main/index.html | 148381 |
| 演示编辑入口 | editors/web-apps/apps/presentationeditor/main/index.html | 123311 |

还包含三种编辑器的 sdkjs/*/sdk-all.js、CEF 的 pak/dat/bin/locales、字体、字典和其他转换 DLL。未发现 .lib 导入库。官方包包含 Qt5Core/Gui/Widgets/PrintSupport 等，不能将现成 Qt5 包装层用于 Qt6 对象边界。

## 真实 PE 静态元数据

对 ascdocumentscore.dll 和 qtascdocumentscore.dll 的小型 ZIP 条目进行了内存解压，核对目录 CRC 后按 PE 字节结构解析；从未 LoadLibrary 或执行 DLL。两份 *.pe-metadata.json 保留全部导入/导出。

- ascdocumentscore.dll：x64、linker 14.29、200 个具名导出、无 forwarder；静态导入不含 Qt。导出包括 CAscApplicationManager 构造/析构、CreateCefEditor、CApplicationCEF::Init_CEF、CCefViewEditor::OpenLocalFile。10 个 vtable 导出位于 .rdata，生成导入库须保留真实名字和 DATA 属性。
- 原生直接依赖包括 libcef.dll、icuuc74.dll、graphics.dll、kernel.dll、kernel_network.dll、PdfFile.dll、XpsFile.dll、DjVuFile.dll、OFDFile.dll、hunspell.dll、ooxmlsignature.dll、doctrenderer.dll 和 MSVCP140/VCRUNTIME140/VCRUNTIME140_1/UCRT。除 libcef/hunspell 外，上述业务 DLL 位于 converter/。
- qtascdocumentscore.dll：x64、linker 14.29、270 导出；明确静态导入 Qt5PrintSupport/Widgets/Gui/Core、videoplayer、kernel、graphics、ascdocumentscore。必须本地编译 Qt6 包装和视频库。
- 范围读取所得原生 DLL SHA-256 为 e0c4ba5680c5fa45f5425b0dc7a6944f09d1637163c1c9bc4bbbe57a6e3e4796；Qt5 wrapper 为 689ec2e7473a2f210f683b5e636894032dd966eea49f22b1801a23af22981a2b。最终完整 ZIP 校验后须与隔离提取结果再次核对。

## 源码支持的实际宿主路线

- desktop-sdk/ChromiumBasedEditors/lib/ascdocumentscore.pri 第 1–2 行移除 Qt core/gui，原生层与 Qt 包装分离。
- lib/qt_wrapper/qtascdocumentscore.pro 第 14 行链接 kernel、graphics、ascdocumentscore；videoplayerlib_deps.pri 第 8 行增加 videoplayer。视频库还依赖 kernel 和 Qt Multimedia/MultimediaWidgets。
- Qt6 分支已存在于 core/Common/base.pri 第 135 行、qascprinter.cpp 第 132 行以及 videoplayerlib/src/qmultimedia.h 第 46 行；仍需实际编译解决旧源码兼容问题。
- lib/qt_wrapper/src/qcefview.cpp 第 154–165 行的 QCefView::Create 调用 CreateCefEditor(this)，第 248–250 行把 Qt 窗口 winId()作为 Windows HWND。
- lib/include/cefview.h 第 192 行的 CCefViewEditor::OpenLocalFile 是打开本地文档入口。
- desktop-apps/win-linux/src/main.cpp 第 145 行附近示范 Prepare、dictionaries/converter/编辑资源路径、Init_CEF 和消息循环。lib/src/cefapplication.cpp 第 395 行使用宿主目录中的 editors_helper.exe；不能只复制两枚 DLL 后忽略 helper 和匹配资源。

## ABI 与加载门槛

接口直接公开 std::wstring/vector/map、内嵌 settings 和虚函数，不是布局隔离的稳定 C ABI（applicationmanager.h 第 58–98、199 行）。事件对象跨 DLL 释放也要一致。固定版本标签只是起点，需要真实导出名、x64、CRT/Release STL、packing 和生命周期实测。

[Microsoft 官方二进制兼容文档](https://learn.microsoft.com/en-us/cpp/porting/binary-compat-2015-2017?view=msvc-170)允许 VS2015+ 的 v14 工具链组合，但链接器和 VC Redistributable 不能早于参与构建的最新工具集；所以 PE linker 14.29 不要求盲目降级到旧编译器。这不证明应用层结构布局或编译开关自然一致。

没有 .lib 并不自动否定路线：[Microsoft LIB /DEF 文档](https://learn.microsoft.com/en-us/cpp/build/reference/building-an-import-library-and-export-file?view=msvc-170)支持从导出规格创建导入库。只能复现 DLL 实际导出；不得伪造符号、改变头文件字段或硬改 ABI 让链接“通过”。.rdata 中的导出应作为 DATA 处理。

自有宿主若启动即静态链接 ascdocumentscore，Windows 在 main 之前便会解析依赖。converter/ 中的 DLL 必须通过受控部署或启动环境可发现；不能假定进入 main 后才加搜索目录一定来得及。保留官方 runtime 原样作基准，本地 Qt6 产物另行部署，避免覆盖取证依赖。

## 可检验的最小里程碑

1. 固定官方完整 ZIP SHA-256 通过；只在 dependencies/onlyoffice-v10.0.0/runtime 隔离解压，安全检查路径并逐项通过 ZIP CRC。
2. 从核验后的 kernel/graphics/ascdocumentscore 真实导出生成 MSVC 导入库；编译 Qt6 videoplayer → qtascdocumentscore → 自有小宿主，保留依赖出处。
3. 自有窗口完成 DOCX 打开 → 真实修改 → 保存 → 关闭 → 重开并核对内容，之后才宣称首个真实编辑闭环通过。XLSX/PPTX、排版保真和功能覆盖另行回归。
4. 不启动外部 DesktopEditors.exe 充当项目成果，不宣称官方预编译原生引擎已自编译。

完整下载/解压的最终事实以 runtime-verification.json 为准：文件不存在时，完整归档尚未完成验证和提取。源码及 PE 静态证据只证明候选路线可尝试，不代表 ABI、运行与文档往返已经通过。

