# Windows 工具链配置补丁

观察与验证日期：2026-11-01。上游基线：`build_tools` 提交 `43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda`。

本补丁已应用于 `upstream/DesktopEditors/build_tools` 的三个文件；可审查差异另存为 [build-tools-windows-toolchain.patch](./build-tools-windows-toolchain.patch)。这是配置与命令生成适配，尚未运行或通过 ONLYOFFICE 引擎构建。

## 已实现的范围

- `configure.py` 新增可选 `--vcvars-ver` 与 `--cmake-dir`，保留原有默认行为与 `--vs-version` 默认值。
- `scripts/config.py` 校验完整 MSVC 版本格式、`vcvarsall.bat` 与 `cmake.exe` 路径。指定 CMake 目录只会前置到当前 Python 进程的 `PATH`，重复解析不重复添加；没有改用户或系统环境变量。
- `scripts/qmake.py` 提取可独立测试的 `windows_build_commands`。调用 vcvarsall 时传入完整 `-vcvars_ver=14.44.35207`，立即检查退出码，并核对实际 `VCToolsVersion`，防止继续使用默认 14.51。CMake 目录在 vcvarsall 之后再次前置，避免该脚本改变优先级。
- qmake、工程文件、Makefile、jom/nmake 参数均按参数边界加引号，覆盖本工作区的空格路径以及 `Program Files (x86)`。明确执行 `.exe`，仅 `.bat/.cmd` 使用 `call`。禁止不能安全直写批处理的 `%`、换行、NUL、内嵌双引号；被 `call` 的字面参数还禁止 `!^&|<>`。这项限制会显式报错，不会悄悄改写路径。
- 两次 qmake 使用相同参数，修复 clean 后重跑时遗漏 `QT_QMAKE_ADDON` 的问题。vcvars/qmake 任意非零退出码（包括负数）立即传出；最终 make 退出码原样传出。保留上游对 clean/distclean 失败的容忍。

## 本机路径选择

以下是待正式构建配置采用的路径片段，并非已经运行的完整构建配置：

```text
qt-dir="D:\Qt\6.11.1"
compiler="msvc2022"
vs-path="D:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build"
vcvars-ver="14.44.35207"
cmake-dir="D:\Qt\Tools\CMake_64\bin"
```

`compiler=msvc2022` 由上游 `check_compiler` 映射到 `msvc2022_64`。不要传 `msvc2022_64`，原函数会再追加 `_64`。此前只读执行已确认此目录的 CMake 为 **3.30.5**；全局 PATH 上的 CMake 4.3.4 不应被误当作唯一可用版本。补丁按目录选择 CMake，没有新增下载或安装行为。

本次没有写真实 `build_tools/config`。`--vs-version` 影响多个尚未适配的依赖分支，不能只凭本补丁把它改为 2022/2026 并宣布 v143 构建成立。MSVC Qt kit 的安装与到位验证由主任务单独推进；这里未验证其真实 qmake。

## 验证证据

```powershell
python -B tests/test_onlyoffice_windows_toolchain.py -v
# Ran 8 tests in 0.523s
# OK

git -C 'D:\open office\upstream\DesktopEditors\build_tools' diff --check
# exit 0, no output
```

测试文件：[test_onlyoffice_windows_toolchain.py](../../tests/test_onlyoffice_windows_toolchain.py)。配置与 qmake 模块使用隔离的 `base`、`android_ndk` 替身加载；`configure.py` 只在临时 `__file__` 路径执行序列化，输出始终写入测试目录。

实际 `cmd.exe` 仅执行测试临时目录里的 vcvarsall/qmake/jom **替身**，验证含空格和括号的路径、批处理返回、版本错误中止、正数及负数错误传递，以及 clean 失败后仍允许成功重建。没有调用真实 vcvarsall、qmake、CMake、nmake/jom，也没有运行上游 `make.py`、依赖脚本或下载器。测试还覆盖旧选项为空、x86/x64/ARM64 参数、CMake 路径缺失及不完整工具链版本。

首次测试使用系统默认临时目录时被当前沙箱的 `AC\Temp` 权限阻挡；已改成工作区 `tests` 下的临时目录后全部通过。这是测试隔离路径问题，不是上游构建结果。测试用 `-B` 避免在上游生成 `__pycache__`。

## 尚未解决的门槛

1. MSVC 14.44 的锁定目前覆盖 **qmake 这一条调用链**。OpenSSL 等模块另有 vcvars 调用，未被本补丁改写。不能据此声称整个构建都锁定了 14.44。
2. Boost 1.72、core 中的 Boost 库名、ICU 74.2 仍有 v140/v142 分支；尚未证实与 v143 的实际产物匹配。
3. V8 对 VS2026 安装位置和 14.44 工具集的发现方式尚未验证；其脚本还含系统 Git 设置修改，不能直接启动完整上游 make 流程。
4. HEIF 等依赖仍有旧 Visual Studio CMake generator，选择 CMake 3.30.5 本身不会修复 generator。
5. `check_jom()` 原有自动下载逻辑未改、未调用。`base.run_as_bat` 严格失败后可能留下 `tmp.bat`，属于范围外既有行为。
6. 本补丁只保证这里生成的命令对当前空格路径经过验证；没有证明所有依赖脚本及 Makefile 的空格路径支持，也没有验证 CEF/Qt/core/V8 ABI、链接、编辑器启动或 DOCX 往返。

下一步应先完成源码与 MSVC Qt kit 到位检查，再独立处理依赖的 v143 和 generator 选择，逐一形成实际构建证据。

## 回滚与来源

确认三个文件没有新的人工改动后，可在 `build_tools` 内对保存的补丁使用 `git apply --reverse`。本次仅执行 `--reverse --check` 检查可逆性，未执行回滚、提交或重置。

固定官方源文件：[configure.py](https://github.com/ONLYOFFICE/build_tools/blob/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/configure.py)、[config.py](https://github.com/ONLYOFFICE/build_tools/blob/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/scripts/config.py)、[qmake.py](https://github.com/ONLYOFFICE/build_tools/blob/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/scripts/qmake.py)、[base.py](https://github.com/ONLYOFFICE/build_tools/blob/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/scripts/base.py)。本次依据下载到本机的固定源码审查，没有联网命令。
