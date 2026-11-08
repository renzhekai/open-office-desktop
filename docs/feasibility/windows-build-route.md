# ONLYOFFICE Windows 底座：官方构建路线核验

观察日期：2026-11-01（UTC）。范围：官方文档、官方仓库源码静态阅读、官方发布资产元数据。本文不代表 Windows 源码构建、下载校验或应用启动已通过。

## 结论与门槛状态

| 门槛 | 本报告状态 | 已有证据 |
| --- | --- | --- |
| 有可固定版本的官方 Windows 运行基线 | 文档确认，未下载/启动 | DesktopEditors v10.0.0 官方发布包含 Windows x64 ZIP、EXE |
| 有官方完整 Windows 原生源码构建教程 | 未找到，门槛未通过 | build_tools README 仅给 Linux 流程；Windows 构建说明请求 #105 仍开启 |
| 当前机器工具链可直接构建 | 未验证，存在明显版本差距 | 现有 Qt 6 / VS 2026 与脚本中 Qt 5、VS 2015/2019 路径不一致 |
| Windows 源码构建成功 | 未实验 | 本研究未运行任何上游构建脚本 |
| 官方二进制等于可重现源码产物 | 未证明 | 两项必须分别验证 |

这不是更换 ONLYOFFICE 的依据；现阶段应保留底座选择，把“官方二进制行为基线”与“源码可重建性”分别记账。

## 固定的运行与源码候选

- 官方发布：[DesktopEditors v10.0.0](https://github.com/ONLYOFFICE/DesktopEditors/releases/tag/v10.0.0)，发布时间 2026-10-13。
- 本次页面中该 tag 对应完整提交：[`daf8c6948148827201b2907ac1134d222fbfa7b4`](https://github.com/ONLYOFFICE/DesktopEditors/commit/daf8c6948148827201b2907ac1134d222fbfa7b4)。后续获取源码时仍要核对 tag 与 SHA；顶层仓库锁定了组件子模块，不能只拿不包含子模块的顶层源码 ZIP 就声称获得完整可构建源码。
- 官方 x64 ZIP：[DesktopEditors_x64.zip](https://github.com/ONLYOFFICE/DesktopEditors/releases/download/v10.0.0/DesktopEditors_x64.zip)，发布页显示约 379 MB。
- 发布方提供的 ZIP SHA-256：`54979d04f1cdca56deb0c62e0356355a45f392cdb54f04f6b7e436b1f44c3605`。本研究未计算本地文件哈希，因为未下载该资产。
- 资产与摘要来源：[展开后的官方资产列表](https://github.com/ONLYOFFICE/DesktopEditors/releases/expanded_assets/v10.0.0)。同一发布有 x64 EXE、x86 和 ARM64 包，当前环境应优先评估 x64。
- ZIP 是候选免安装运行基线；尚未检查归档内容、首次启动协议、注册表/AppData 写入或配置隔离，不能仅凭扩展名认定“完全便携”。
- 官方下载安装页的搜索缓存仍显示 9.4.0，晚于它的 GitHub 发布已列 10.0.0；实验记录应固定具体发布资产，避免使用自动变化的“latest”下载地址。

## 官方可用路线与 Windows 缺口

当前 [build_tools README](https://github.com/ONLYOFFICE/build_tools/blob/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/README.md) 推荐 master，并给出经过验证的 Linux 环境：Ubuntu 24.04 amd64、4 核、8 GB RAM、4 GB swap、100 GB SSD。流程是克隆 build_tools，进入 `tools/linux`，运行 `python3 ./automate.py desktop`；产物目录为 `out/linux_64/onlyoffice/desktopeditors`。这些是 Linux 构建说明，不能据此承诺 Windows 可执行文件。Docker 路线也不能自动视为 Windows 交叉编译路线。

[帮助中心编译页](https://helpcenter.onlyoffice.com/desktop/installation/desktop-compile.aspx) 仍写 Ubuntu 16.04 和 40 GB 等较旧要求，与当前仓库 README 不同。当前评估优先参考仓库及固定快照，而不是混用两个年代的依赖。

[Windows 构建说明主 issue #105](https://github.com/ONLYOFFICE/build_tools/issues/105) 在本次读取时仍为 Open；官方成员 2026-01-30 表示会再次提醒团队这个请求。2026 年新开的 [#915](https://github.com/ONLYOFFICE/build_tools/issues/915) 被关闭为 #105 的重复项，不能把它的 Closed 状态视为 Windows 构建问题已解决。2024 年官方社区的[答复](https://community.onlyoffice.com/t/desktop-editor-for-windows/10174)也明确当时没有可用 Windows 官方教程；这里只作为历史佐证。

## 从当前源码能够确认的构建结构

本次将 build_tools 引用固定为提交 [`43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda`](https://github.com/ONLYOFFICE/build_tools/commit/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda)，[提交补丁](https://github.com/ONLYOFFICE/build_tools/commit/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda.patch) 日期为 2026-10-17，标题为 Fix build，仅修改 v8_89.py。已重新读取该提交下的 README、configure.py 和 v8_89.py：Linux-only 文档范围、Windows 配置参数、Python bootstrap 版本、V8 8.9 分支及写系统 Git 配置的行为均与本文结论一致。此补丁调整的是非 Windows 的 Python bootstrap 路径，不能理解为补齐 Windows 官方构建流程。

固定 build_tools 提交只锁定了这份构建脚本；它与 DesktopEditors v10.0.0 及全部组件/第三方依赖的配套关系仍未通过实验。请求 `build_tools/v10.0.0/README.md` 曾返回 404，本研究未假设两个仓库必然有同名 tag。下述其他 build_tools 源文件链接也统一指向此固定快照，依赖归档、depot_tools 和被拉取的源仓库仍需另行锁定。

1. [`configure.py`](https://raw.githubusercontent.com/ONLYOFFICE/build_tools/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/configure.py) 有 `--module=desktop`、`--platform=win_64`、`--qt-dir`、`--compiler`、`--vs-version`、`--vs-path`、`--update` 与 `--clean` 参数。参数存在只说明代码有 Windows 分支。
2. [`make.py`](https://raw.githubusercontent.com/ONLYOFFICE/build_tools/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/make.py) 在更新仓库后，依次准备第三方依赖、编译原生项目、构建 JavaScript 资源并部署产物；因此不存在只编译一个桌面壳即可得到完整套件的证据。
3. [`qmake.py`](https://raw.githubusercontent.com/ONLYOFFICE/build_tools/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/scripts/qmake.py) 的 Windows 分支调用 `vcvarsall.bat`、qmake 和 nmake。检查不到 qmake 时打印平台不支持，不能因此把缺依赖误诊为编辑器本体不支持 Windows。
4. [`make_common.py`](https://raw.githubusercontent.com/ONLYOFFICE/build_tools/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/scripts/core_common/make_common.py) 列出 Boost、libxml2、CEF、ICU、OpenSSL、V8、字体/拼写/图片等原生依赖；这不是纯 Node 项目。
5. [`build_js.py`](https://raw.githubusercontent.com/ONLYOFFICE/build_tools/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/scripts/build_js.py) 调用 npm、Grunt 和 Python 构建 web-apps、sdkjs 以及桌面登录页；至少部分路径使用 npm install，部分使用 npm ci。依赖下载、生命周期脚本和全局工具安装必须在执行前单独审查。

## 工具版本与本机差距

本机数据由主代理同日检查提供，本研究未重复执行工具链验证：Windows 10.0.26200 x64；Python 3.14.5、Node 24.15.0、Git 2.55、CMake 4.3.4；VS 2026 Community 位于 `D:\Microsoft Visual Studio\18\Community`，含 MSVC 14.44.35207 / 14.51.36231；qmake 实测为 Qt 6.11.1、`mingw_64` kit，位于 `D:\Qt\6.11.1`；Ninja 1.12.1 位于 `D:\Qt\Tools\Ninja`；Windows SDK 在 `D:\Windows Kits\10\Include`，包含 26100、28000 系列；D 盘可用 56.37 GB，C 盘可用 33.3 GB。常见位置未发现已安装 ONLYOFFICE。

| 项目 | 官方源码观察 | 对本机的判断 |
| --- | --- | --- |
| Qt | [Linux automate.py](https://raw.githubusercontent.com/ONLYOFFICE/build_tools/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/tools/linux/automate.py) 连接 Qt-5.9.9；[base.py](https://raw.githubusercontent.com/ONLYOFFICE/build_tools/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/scripts/base.py) 的 ARM64 特例指向 Qt-5.15.2；Qt目录按编译器后缀选择 | 实测现有 Qt 6.11.1 是 mingw_64 kit，而 Windows 脚本使用 MSVC/nmake；它不是已验证的配套 Qt kit。也不能把 Linux Qt 5.9.9 直接当作已确认 Windows 版本 |
| MSVC / Windows SDK | [configure.py](https://raw.githubusercontent.com/ONLYOFFICE/build_tools/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/configure.py) 默认 vs-version 为 2015；[config.py](https://raw.githubusercontent.com/ONLYOFFICE/build_tools/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/scripts/config.py) 仅在值为空时默认 2019，并含两代路径自动探测 | VS 2026 和 26100/28000 系列 SDK 已存在，但未验证被脚本发现、选中及编译成功；仍需明确工具集、SDK、Qt ABI 的组合 |
| Python | [Linux python.sh](https://raw.githubusercontent.com/ONLYOFFICE/build_tools/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/tools/linux/python.sh) 获取专用 Python 包；[V8脚本](https://raw.githubusercontent.com/ONLYOFFICE/build_tools/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/scripts/core_common/modules/v8_89.py) 为 Windows depot_tools 固定 Python 2.7.18、3.11.8 bootstrap | 主机 Python 3.14.5 不能自动替代这些构建子工具。未找到当前 Windows 主驱动 Python 的完整支持矩阵 |
| Node / npm / Grunt | [Linux deps.py](https://raw.githubusercontent.com/ONLYOFFICE/build_tools/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/tools/linux/deps.py) 检查 Node 主版本至少 20 | Node 24 满足该数值下界，但 Windows npm/Grunt 全链路仍未实测 |
| CEF | [cef.py](https://raw.githubusercontent.com/ONLYOFFICE/build_tools/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/scripts/core_common/modules/cef.py) 默认从 ONLYOFFICE-data 的 `cef/5414/win_64` 取预编译归档，另有 5304/4280 分支 | 需保存下载校验与匹配版本；不能把预编译第三方包计为本机编译产物 |
| V8 | [v8_89.py](https://raw.githubusercontent.com/ONLYOFFICE/build_tools/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/scripts/core_common/modules/v8_89.py) 默认同步 branch-heads/8.9，并使用 GN/Ninja 构建 Windows 库 | 本机已找到 Ninja 1.12.1；仍需 GN、匹配的 V8/depot_tools 工具链和编译实验，不能仅凭 Ninja 存在判通过 |
| 空间 | 当前 Linux 官方参考为 100 GB SSD | D 盘 56.37 GB 低于该参考；这不是精确 Windows需求，但不足以直接承诺完整源码构建空间够用 |

未发现可据以给出完整、经过官方验证的 Windows x64 依赖版本矩阵。因此本报告不提供伪装成已验证方案的一键安装命令。

## 执行前需要明确处理的行为

- **保护源码改动**：configure.py 的分支参数说明会强制切换并丢弃本地改动，默认 update=1。必须使用与用户改动分离的固定源码副本，先核对每个依赖提交；适当使用 update=0，不能在用户修改过的仓库里直接套默认更新流程。
- **系统配置**：v8_89.py 的 Windows 分支包含 `git config --system core.longpaths true`。这会写系统 Git 配置；当前授权要求额外权限/安全设置变更另行确认，不能通过直接运行上游 make.py 间接绕过。
- **安装与协议**：[官方 Windows 安装说明](https://helpcenter.onlyoffice.com/desktop/installation/desktop-install-windows.aspx) 的 EXE 流程包含接受许可协议，且可能需要管理员或扩展权限；还可改变文件关联。任何实际出现的协议、提权、系统安装需单独确认，不自动点击同意，也不改默认文件关联。
- **依赖安装**：Qt/MSVC/SDK 等的具体候选版本、来源、许可证、安装位置、下载与磁盘需求尚未锁定。不能为“看起来缺工具”直接批量安装；如有必要，应先形成具体安装项供确认。
- **运行边界**：第一阶段不启用/配置 AI 服务，不输入账号或 API 凭据。上游包含 AI 能力并不等于本项目此阶段实现了 AI。

## 本次实际动作与未完成项

已完成：读取官方发布、帮助中心、问题跟踪和相关源文件，核对官方 x64 ZIP 摘要与根源码提交；固定 build_tools 提交并重读 README/configure.py/v8_89.py；加入主代理提供的 qmake、Ninja、Windows SDK 实测结果；仅写本报告。没有克隆、下载应用二进制、安装工具、接受协议、运行上游代码或更改系统设置。

本地只读网络尝试：使用 PowerShell `Invoke-RestMethod` 查询 `api.github.com/repos/ONLYOFFICE/build_tools/commits/master`、DesktopEditors releases/latest 与构建树，均返回“不知道这样的主机”。没有得到有效远端 JSON、没有产生下载文件；这是网络/DNS访问失败，不是审批拒绝。未提升权限重试。官方网页通过 web 工具可读取。

仍未完成：build_tools 与 DesktopEditors v10.0.0 的配套验证，以及组件/第三方依赖锁定；ZIP下载/哈希/签名及首启验证；Windows Qt/MSVC/SDK 组合试验；源码构建与桌面启动；文件往返保真和插件编辑测试（由主任务其他实验负责）。运行基线通过后也不能提前勾选“Windows从源码构建成功”。
