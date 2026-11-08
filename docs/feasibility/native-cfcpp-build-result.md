# ONLYOFFICE 最小原生组件实际构建结果

2026-11-01，固定 `core` 提交 `ed63e94de814ff1d4ea28cddfaba6939bc9df9ed`。

**真实源码已编译、链接并启动；完整往返验证未通过。** 已生成 `CompoundFileLib.lib`、两个消费者 EXE 和仅包含真实 File/Base64 实现的支持 DLL。10 字节流保存并重新读取一致，8192 字节流内容不一致。没有构建完整 kernel、CEF、DOCX 编辑器，也没有把支持 DLL 当作完整 kernel。

## 目标与边界

采用 [Common/cfcpp/cfcpp.pro](https://github.com/ONLYOFFICE/core/blob/ed63e94de814ff1d4ea28cddfaba6939bc9df9ed/Common/cfcpp/cfcpp.pro#L48) 的完整 **13 个 CPP** 清单，直接用现有 MSVC 工具构建静态库。清单已与 `compile.rsp` 逐项比较，13/13 一致，源码全部存在。该库处理 OLE Compound File；此验证没有涉及 DOCX 编辑。

这是独立的直接 MSVC 构建实验，未执行官方 qmake 工程生成或 `build_tools/make.py`。构建参数为 x64、C++17、`/MD`、`/O2`、`/EHsc`，采用原 `base.pri` 中相关 Windows 宏及 `/Zc:strictStrings-`。实际日志确认：

```text
MSVC_TOOLS=14.44.35207
compiler=19.44.35228.0
WINDOWS_SDK=10.0.26100.0
```

所有输出位于 `build/onlyoffice-cfcpp-msvc144`；临时目录也在其中。检查祖先目录、core/desktop-sdk、build/evidence/docs 后未发现适用 AGENTS。本实验结束时 `git -C upstream/DesktopEditors/core status --porcelain` 为空。没有改上游源码、下载、安装、运行系统 Git 设置修改或发布产物。

## 实验记录

| 记录 | 实际结果 | 退出码 |
| --- | --- | --- |
| [attempt-01](../../evidence/onlyoffice-native-cfcpp/build-attempt-01.log) | 13 个 CPP 编译、静态归档成功；消费者链接报 LNK2019/LNK1120，缺少 `__imp_NSFile::CUtf8Converter::GetUtf8StringFromUnicode` | 2 |
| [attempt-02](../../evidence/onlyoffice-native-cfcpp/build-attempt-02.log) | 编译真实 File.cpp/Base64.cpp 支持 DLL，消费者链接并启动；内容校验失败 | 4 |
| [attempt-03](../../evidence/onlyoffice-native-cfcpp/build-attempt-03-diagnostic.log) | 增加消费者诊断，确认 `getData()` 期望 10 字节，实际返回 0 字节 | 4 |
| [attempt-04](../../evidence/onlyoffice-native-cfcpp/build-attempt-04-read-api.log) | 独立消费者使用现有 `Read()`：10 字节返回 10 且内容一致；8192 字节返回 8192 但内容不一致 | 4 |

第二次构建加入的是已有完整源码 [File.cpp](https://github.com/ONLYOFFICE/core/blob/ed63e94de814ff1d4ea28cddfaba6939bc9df9ed/DesktopEditor/common/File.cpp) 和 [Base64.cpp](https://github.com/ONLYOFFICE/core/blob/ed63e94de814ff1d4ea28cddfaba6939bc9df9ed/DesktopEditor/common/Base64.cpp)，用原 `KERNEL_USE_DYNAMIC_LIBRARY_BUILDING` 宏生成明确命名的 `kernel-file-support.dll`。没有伪造符号或 stub。库消费者仍使用原始 13 个源码产生的静态库；[DLL 导出表](../../evidence/onlyoffice-native-cfcpp/support-exports.txt) 记录了真实 UTF-8 方法，[依赖表](../../evidence/onlyoffice-native-cfcpp/smoke-dependents.txt) 记录了实际 DLL 链。

`getData()` 的失败已有直接源码依据：[compoundfile.cpp:1614](https://github.com/ONLYOFFICE/core/blob/ed63e94de814ff1d4ea28cddfaba6939bc9df9ed/Common/cfcpp/compoundfile.cpp#L1614) 的 `GetData()` 在两个分支中仅 `reserve()`，之后按仍为零的 `result.size()` 读取并返回。原文件没有修改；失败消费者 `smoke.cpp` 和其 EXE 均保留。

8192 字节 `Read()` 不一致尚未定位，不将其解释成同一个问题，也不宣称通过。另一个独立消费者 `smoke-read.cpp` 保留了两个长度的严格内容比较，没有删掉失败断言。生成的 `.cfb` 文件也保留供检查。

## 可审查产物

目录：[build/onlyoffice-cfcpp-msvc144](../../build/onlyoffice-cfcpp-msvc144)。主要产物：

- `CompoundFileLib.lib`：3,674,640 字节；[dumpbin headers](../../evidence/onlyoffice-native-cfcpp/archive-headers.txt) 确认 x64 对象。
- `kernel-file-support.dll`：73,728 字节；只含 File/Base64 支持实现。
- `cfcpp-smoke.exe`、`cfcpp-read-smoke.exe`：两个实际链接并运行的消费者。
- `native-roundtrip.cfb`、`native-read-roundtrip.cfb`：保留的生成文件。

各产物 SHA-256、大小、工具版本、源码提交及所有成功/失败布尔字段见 [result.json](../../evidence/onlyoffice-native-cfcpp/result.json)。其中 `overall_roundtrip_pass=false`、`full_kernel_built=false`、`editor_engine_integrated=false`。

命令和 response files 均保存在输出目录。原始执行顺序如下，前两条的预期失败必须分别记录，不能用 `&&` 把后续探索跳过：

```powershell
& $env:ComSpec /d /c '"D:\open office\build\onlyoffice-cfcpp-msvc144\build.cmd"'
# exit 2: archive succeeds, original consumer cannot resolve kernel import

& $env:ComSpec /d /c '"D:\open office\build\onlyoffice-cfcpp-msvc144\build-with-kernel-file-support.cmd"'
# exit 4: compile/link succeed, getData comparison fails

& $env:ComSpec /d /c '"D:\open office\build\onlyoffice-cfcpp-msvc144\build-read-smoke.cmd"'
# exit 4: 10-byte Read passes, 8192-byte comparison fails
```

## 完整 kernel 的明确缺项

`kernel.pro` 包含 libxml 并依赖 UnicodeConverter；后者要求 ICU。此次实际检查以下文件不存在，机器可读清单见 [missing-kernel-inputs.json](../../evidence/onlyoffice-native-cfcpp/missing-kernel-inputs.json)：

```text
core/Common/3dParty/libxml/libxml2/parser.c
core/Common/3dParty/libxml/libxml2/include/libxml/parser.h
core/Common/3dParty/icu/icu/include/unicode/utypes.h
core/Common/3dParty/icu/win_64/icu/include/unicode/utypes.h
core/Common/3dParty/icu/win_64/build/icuuc.lib
core/build/lib/win_64/UnicodeConverter.lib
```

zlib 源码已存在；graphics 又依赖 UnicodeConverter 和 kernel，当前不能把它视为依赖齐全的更小目标。下一步可继续补齐和审计这些正式构建依赖；本次结果只证明现有 MSVC/SDK 能产生并执行真实 ONLYOFFICE 原生组件，同时暴露了具体功能验证失败。
