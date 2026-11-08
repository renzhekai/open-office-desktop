# Qt 6.11.1 MSVC 2022 x64 + Multimedia：恢复路线核查

核查日期：2026-11-01。范围：官方元数据、归档来源/校验、安装脚本和现有安装记录的只读检查。本研究代理没有下载大型 payload，没有安装 SDK、执行安装脚本、接受协议或修改 D:\Qt。根代理另行负责 payload 下载与后续工程验证。

结论：官方公开仓库包含这两个指定组件的全部九个 payload，可在保留官方校验的前提下准备同版本手动 SDK。具体是否可编译、能加载多媒体插件，必须在落盘后实测。手动 SDK 不等于 MaintenanceTool 登记；不得伪造 components.xml。MaintenanceTool 支持 file:/// 本地仓库，但不能由此保证 QtAccount 初始化错误消失。

## 已有交付与实测

- payload-manifest.json：九个精确 URL、字节大小、SHA-256、官方 .mirrorlist 来源、SHA-1 侧文件 URL、解压目标。JSON 解析通过；九项求和通过：287,643,078 字节（约 274.318 MiB）。
- inspection/qt.qt6.6111.win64_msvc2022_64/installscript.qs：安全解压的原始官方组件元数据脚本。只读取，未执行。
- 系统 C:\Windows\System32\tar.exe 为 bsdtar 3.8.9；实际成功列出及解压两份小型 .7z 元数据。尚未对九个大型归档运行解压验证，因此不能声称所有大型 payload 已实验通过。
- 元数据声明解压总量 2,743,295,083 字节；需另留下载、暂存及构建空间。XML CompressedSize 合计比九个实际 payload 多 360 字节，恰好等于九份 40 字节 SHA-1 侧文件；实际下载大小以清单为准。

本机元数据 SHA-256 与官方 mirrorlist 一致：

| 文件 | 字节 | SHA-256 |
| --- | ---: | --- |
| Updates.xml | 120967 | 8f81fe88b1c48c0e6a4c3cb32981d1dae3a11131ef384764efba794d27bab98b |
| base-meta.7z | 2137 | 2cfcc79b6eee6b47db1007d628df1681bf641545fda4249dfef28bbbc5fd9688 |
| multimedia-meta.7z | 187 | 970a831a61b661b009b7ab13a4c06fc20d20ad3d3730ad04293b59079f6d12a5 |
| 2026-05-09-0701_meta.7z | 4386 | fa76411f5a81b2b956041a30120fdbfae751c59a5d2524453d7e2378b9e00eec |

[官方仓库根目录](https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6111/qt6_6111_msvc2022_64/)；[根元数据校验](https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6111/qt6_6111_msvc2022_64/2026-05-09-0701_meta.7z.mirrorlist)。

## 九个 payload

下表 Qt 模块归档完整后缀均为 -Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z，所有文件名前缀均为 6.11.1-0-202605090529。完整不缩写文件名及 SHA-256 见 JSON。

| 归档 | 字节 | 相对 SDK 的解压目录 |
| --- | ---: | --- |
| qtbase | 39469569 | . |
| qtsvg | 689094 | . |
| qtdeclarative | 166957267 | . |
| qtdoc | 1858329 | . |
| qttools | 27133575 | . |
| qttranslations | 1946027 | . |
| d3dcompiler_47-x64.7z | 1380117 | bin |
| opengl32sw-64-mesa_11_2_2-signed_sha256.7z | 5474526 | bin |
| qtmultimedia | 42734574 | . |

这九项是两个指定组件的 payload 闭包；不代表 MaintenanceTool 整笔事务一定只选择九项。安装器还会计算已安装虚拟父组件及 AutoDependOn。

## 手动 SDK 的最小恢复步骤（供根代理执行，本文未执行）

1. 从 JSON 的官方 HTTPS URL 获取九个 payload；逐项核对 size 和 SHA-256。保留下载归档、许可材料及证据。若源端要求新的协议/身份访问，停止该步骤；不通过修改安全设置或替换来源绕过。
2. 用系统 tar -tf 列出每个归档，检查没有绝对路径、盘符、.. 越界或链接越界，确认条目根布局。只向全新、空的 SDK 暂存目录解压；Qt 七个模块归档提取到其根，两个图形 DLL 归档提取到 bin。不能向现有 mingw_64 混装。
3. 最终 SDK 目录为 D:\Qt\6.11.1\msvc2022_64。根据官方 Qt 6.11 路径规则和现有同版 mingw_64/bin/qt.conf 实物，在新 SDK bin/qt.conf 写入：

~~~ini
[Paths]
Documentation=../../Docs/Qt-6.11.1
Examples=../../Examples/Qt-6.11.1
Prefix=..
~~~

[官方 qt.conf 文档](https://doc.qt.io/qt-6.11/qt-conf.html)说明 Prefix 相对于可执行文件目录，其他路径相对于 Prefix；Windows 推荐正斜杠。此配置使 Prefix 指向新 SDK 根，文档/示例指向 D:\Qt\Docs 和 D:\Qt\Examples。若归档已有 qt.conf/qt6.conf，应先读取；qt6.conf 优先于 qt.conf，不能盲目覆盖。

4. 官方 base installscript.qs 第 94 行先调用 component.createOperations()，第 130 行调用 createQtConfFiles；还创建 qtenv2.bat/快捷方式及 QtCreator addQt/addKit，并可能处理 mocwrapper。这个组件脚本没有 QtPatch 或 qmake 二进制补丁操作。createQtConfFiles 的定义不在两份组件 meta 或 4386 字节根 meta 中；因此这里只确认调用及有官方文档支持的 bin/qt.conf 最小配置，不能声称已完整复现 helper 所有平台行为。无需为了 CLI 构建自动修改 QtCreator 设置或 installer 注册。
5. 在真实 MSVC 2022 x64 开发者环境中，先执行新 SDK 的 qmake.exe -query，核对 QT_VERSION=6.11.1、QT_INSTALL_PREFIX、QT_INSTALL_PLUGINS、QT_INSTALL_HEADERS、QT_INSTALL_LIBS 全部指向新 SDK。用现有 CMake 3.30.5 的干净构建目录和 CMAKE_PREFIX_PATH 指向该 SDK，验证 Core、Gui、Widgets、Network、Svg、Multimedia、MultimediaWidgets 可发现并编译链接。此阶段如报错再根据错误修复，不能预先声称免 patch 必然成功。
6. 运行 GUI/多媒体最小验证并检查插件依赖。使用官方部署工具保留 Qt、Multimedia 后端和附带第三方库的许可文件。完成后记录“手动 SDK 已准备/测试结果”，不写“MaintenanceTool 安装完成”。

不需要新装 pip/aqt/7-Zip；已有系统 tar 的小元数据读取已验证。大型归档兼容性仍以实际列出/解压为准。

## MaintenanceTool 本地仓库候选路线

官方 IFW 4.11 支持 --set-temp-repository file:///...，此选项仅替换本次 fetch 的仓库，不改变系统网络配置。原样镜像官方 Updates.xml、其引用的根 2026-05-09-0701_meta.7z、必要组件目录中的版本前缀归档及 .sha1；不要用本地别名 base-meta.7z/multimedia-meta.7z 伪装成完整仓库，也不要删改官方依赖/许可元数据。[官方命令行文档](https://doc.qt.io/qtinstallerframework/ifw-cli.html)

现有 D:\Qt\components.xml 只读检查显示 QtCreator、6.11.1 doc/examples、Multimedia 的虚拟父组件和它的 doc/examples 已安装。base 元数据依赖 qt.tools.qtcreator, qt.qt6.6111.doc, qt.qt6.6111.examples；目标 Multimedia 依赖 base。它们的依赖没有精确版本限制；让安装器实际计划确认，不主动降级现有 docs/Creator。

候选只读查询（尚未运行；此前 QtAccount/HTTP2 失败可能在仓库 fetch 之前再次发生）：

~~~powershell
& 'D:\Qt\MaintenanceTool.exe' --set-temp-repository 'file:///D:/open%20office/dependencies/qt-local-repository' --type package search 'qt.qt6.6111.win64_msvc2022_64'
~~~

查询成功后可用包管理 UI 查看计划，只增加批准的 MSVC 2022 x64 和 Multimedia。已选择其他 add-on 的虚拟父组件可能通过 AutoDependOn 激活该架构版本；如果计划扩大，先查清而非默认执行。不能自动接受新许可。若 QtAccount 检查仍失败，本地 repo 并不能被说成已解决认证；手动公开 SDK 路线与账号受限功能不可混同。

## 许可与边界

- 此仓库 Updates.xml 中 Licenses/License 元素计数为 0。Multimedia metadata 仅有空目录，无自定义安装脚本；base 脚本没有新增许可提示。由此只能说“这两组件元数据未声明新的交互式协议”，不能说“没有许可义务”。
- 当前本机安装记录含 qt.license.acceptance=1.0.0、qt.license.lgpl=1.0.2-1、qt.license.thirdparty=1.0.0-1。仅凭记录不能推定未来任意新协议已获授权。
- Qt Multimedia 官方文档列明自由软件许可和第三方归属；网页当前为 6.11.3，不能把它的具体 FFmpeg 小版本当作 6.11.1 payload 的证据。6.11.1 归档落盘后应保留并检查其实际 license/attribution 文件。[Qt Multimedia 官方许可段落](https://doc.qt.io/qt-6.11/qtmultimedia-index.html#licenses-and-attributions)
- 手动解压不自动维护卸载/升级记录；独立 SDK 应有独立清单，不修改 components.xml。QtCreator 集成可后续显式配置，不应阻塞当前 CLI 可行性验证。

## 未证实与阻塞状态

- 已证实：官方精确 URL、大小/哈希、元数据来源、组件提取目标、脚本声明、系统 tar 可解析小型 7z。
- 待根代理实际验证：九个 payload 的完整获取和校验、成员安全检查、解压、qmake/CMake/MSVC 构建和运行、Multimedia 插件依赖。
- 尚不可证明：createQtConfFiles helper 完整定义；本地 repo 是否消除 MaintenanceTool 的 QtAccount/HTTP2 故障；手动 SDK 是否无需任何额外路径修复。
- 本轮无自动审批拒绝、无待批准安装操作、无不确定安装结果。曾用普通沙箱获取官方小元数据遇 DNS 解析失败；根代理随后已成功获取并提供四份元数据，因此不再将该已解决的数据获取问题视为当前阻塞。

