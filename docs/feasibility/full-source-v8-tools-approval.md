# V8 源码构建所需新增工具

状态：用户已于 2026-11-02 批准 V8 工具；Perl 也已单独批准。Python 来源按后续明确授权使用 python.org 官方 3.11.8，2.7.18 仅旧脚本确需时使用；不得将 Chromium 重新打包版本自动视为这一精确授权。

当前已有 MSVC 14.44.35207、Windows SDK、Qt 6.11.1、Ninja、Git 和 Python 3.14；本次申请只补 V8 构建需要的项目内工具，不替换现有版本或改系统 PATH。

## 目标与固定版本

V8 8.9.255.25，提交 `a2d8eca22ad478a95ca67bb2421a9a2535113925`。目标产物为 `v8_monolith.lib`，供 ONLYOFFICE doctrenderer 使用。CEF 内置 V8 不替代该产物。

拟新增目录：`D:\open office\build\full-source\v8-tools`。必要依赖缓存也仅放项目目录。新增工具包括：

- Chromium depot_tools/gclient 与其 CIPD 客户端，用于按固定 DEPS 同步依赖；以 V8 DEPS 中提交 `82b992a1656d7d1cd0ee3cbea8ff609ffdfed380` 为兼容性起点，任何调整记录准确提交。
- Windows x64 GN，CIPD 包 `gn/gn/windows-amd64`，版本 `git_revision:595e3be7c8381d4eeefce62a63ec12bae9ce5140`。
- 隔离 Python 3.11.8.chromium.35，以及旧脚本确需时的 Python 2.7.18.chromium.39。版本来自当前 ONLYOFFICE build_tools 的 V8 Windows 配方；只用于本次构建。

来源限定为 Chromium 官方 `chromium.googlesource.com`、`chrome-infra-packages.appspot.com` 及其官方 Google Storage 下载端点；Python 使用 `www.python.org` 官方发布端点。下载后记录精确实例、大小和哈希，先审阅同步 hooks，再执行所需构建步骤。复用现有 Git/MSVC/Ninja，不安装额外 VS/Clang，不运行 `git config --system core.longpaths`，不修改安全或网络设置。

官方依据：[V8 源码获取说明](https://v8.dev/docs/source-code)、[GN 构建说明](https://v8.dev/docs/build-gn)、[固定 V8 DEPS](https://github.com/v8/v8/blob/a2d8eca22ad478a95ca67bb2421a9a2535113925/DEPS)。这些资料证明工具用途；当前机器的完整 V8 构建尚未实测通过。

需要单独授权的原因：本任务既有约束要求新增工具安装单独确认，先前批准仅涵盖 Qt MSVC 和 Multimedia。普通源码下载、工作区编译授权继续有效。

## 补充：OpenSSL 所需原生 Windows Perl

已实测现有 Git Perl 5.42.2 为 `x86_64-cygwin-thread-multi`。OpenSSL `Configure VC-WIN64A` 退出码 127，并明确报错：该 Perl 不生成 Windows 反斜杠路径。证据：`build/full-source/dependency-evidence/openssl-git-perl-configure.log`。

拟新增 Strawberry Perl 5.40.0.1 x64 Portable，解压到 `D:\open office\build\full-source\tools\strawberry-perl-5.40.0.1`，只通过绝对路径运行其 Perl。不运行 MSI、不注册系统 PATH，不使用随包附带的 GCC。来源为 [Strawberry Perl 官方发布](https://github.com/StrawberryPerl/Perl-Dist-Strawberry/releases/tag/SP_54001_64bit_UCRT)及[官方版本说明](https://strawberryperl.com/release-notes/5.40.0.1-64bit.html)。部署前记录所选 portable 资产及官方校验信息。

此项是独立新增工具授权；V8 工具的批准不自动包含 Perl。未下载或部署该工具。
