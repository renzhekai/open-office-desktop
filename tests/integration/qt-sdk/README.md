# Qt MSVC SDK 独立运行验证

这个项目只验证指定 Qt SDK 的配置、链接与运行依赖。它不编译 Open Office、ONLYOFFICE，也不代表编辑、保存或 Office 文件兼容性通过。

等待 `D:\Qt\6.11.1\msvc2022_64` 安装完成后，在已有 MSVC x64 开发者终端中执行。使用新的独立构建目录，避免与其他编译器或应用构建缓存混合：

```powershell
cmake -S tests/integration/qt-sdk -B build/qt-sdk-msvc-check -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DCMAKE_PREFIX_PATH=D:/Qt/6.11.1/msvc2022_64
cmake --build build/qt-sdk-msvc-check
$env:PATH = 'D:\Qt\6.11.1\msvc2022_64\bin;' + $env:PATH
ctest --test-dir build/qt-sdk-msvc-check --output-on-failure -V
```

没有自动下载、安装、部署或修改 SDK 的步骤。运行环境应使用新 SDK 的 DLL 和插件，避免混入旧 MinGW 套件。Ninja 和 MSVC 已有可用路径由主任务配置。

程序创建 `QApplication`、不显示的 `QVideoWidget`、不发请求的 `QNetworkAccessManager`，并把一个内嵌 SVG 实际渲染到内存。它只在进程内指定 `QT_MEDIA_BACKEND=ffmpeg`，在构建目录下创建一个全新 UUID 子目录及一秒静音 WAV，以 `NewOnly` 方式写入，保留产物供检查，不覆盖旧文件。

媒体检查只调用 `setSource`，没有 `QAudioOutput`，不调用 `play`。等待 `LoadedMedia` 和约 1000 ms 时长后输出 JSON；Windows 下同时列出本进程已经加载的 Qt/FFmpeg DLL 路径，要求 FFmpeg 插件实际加载。默认超时 10 秒，CTest 另有 20 秒总超时。

退出码：`0` 为这些限定检查通过，`1` 为失败，`2` 为媒体等待超时。若 DLL 或 Qt 平台插件在进入程序前缺失，进程可能无法输出 JSON；此时应保留外部启动错误，不能当作通过。

## 本机运行记录

2026-11-01 UTC 已在 `build/qt-sdk-msvc` 使用 CMake 3.30.5、Ninja、MSVC 工具集 14.44.35207 完成配置、编译和直接运行，最终退出码为 **0**。Qt 编译及运行版本均为 6.11.1；七个模块与 FFmpeg 插件、关联 DLL 实际来自新的 `msvc2022_64` 目录。SVG 渲染通过，媒体状态为 `LoadedMedia`，时长 1000 ms、可用状态 true、错误码 0，窗口未显示、未播放、未发网络请求。

首轮编译器自检虽指定 `CMAKE_BUILD_TYPE=Release`，仍使用 Debug 的 `/Zi /FS`，遭遇 C1902。加入 `CMAKE_TRY_COMPILE_CONFIGURATION=Release` 后通过；没有停止服务、修改系统配置或替换共享工具。

证据保存在 `build/qt-sdk-msvc`：`runtime-release.stdout.json`、`runtime-release.stderr.log`、`configure-release.log`、`build-release.log` 和 `execution-summary-release.json`。首次失败的 `configure.log` 和 `execution-summary.json` 仍保留。`run-validation-release.cmd` 记录了实际命令及仅对该子进程生效的环境设置。

此次检查不覆盖视频解码、音频播放、完整 SDK 每个模块或 ONLYOFFICE 的源码构建与办公功能。

依据：[QMediaPlayer](https://doc.qt.io/qt-6/qmediaplayer.html)、[QVideoWidget](https://doc.qt.io/qt-6/qvideowidget.html)、[SVG 渲染](https://doc.qt.io/qt-6/qsvgrenderer.html)、[Qt 6.11 的 FFmpeg 后端选择](https://doc.qt.io/qt-6.11/qtmultimedia-index.html#changing-backends)。文档为滚动/维护分支页面；本项目 CMake 要求实际包为 **6.11.1 EXACT**。
