# 完整源码运行目录装配

`scripts/stage_full_source_runtime.py` 将新运行目录固定为 `build/full-source-run`。它不会运行 GUI 或文档转换器；装配成功也不表示 DOCX 回归通过。原 `build/native-engine-run` 保持独立，不作为此脚本的原生核心来源。

## 执行顺序

先检查引擎库存；缺少任何核心、导入库、格式静态库、工具、V8 数据或构建资源时返回非零，不创建运行目录：

```powershell
python -B scripts/stage_full_source_runtime.py --engine-only --dry-run
```

完整清单通过之后，显式执行引擎装配：

```powershell
python -B scripts/stage_full_source_runtime.py --engine-only
```

此时清单状态为 `engine-only/incomplete-app`，有意不含 `OpenOffice.exe`。父构建随后在 `build/full-source-host` 编译自有入口，明确使用：

```text
OPEN_OFFICE_WITH_ONLYOFFICE=ON
CMAKE_BUILD_TYPE=Release
ONLYOFFICE_SOURCE_ROOT=D:/open office/build/full-source/DesktopEditors
ONLYOFFICE_RUNTIME_ROOT=D:/open office/build/full-source-run
ONLYOFFICE_CORE_IMPORT_LIBRARY=D:/open office/build/full-source/DesktopEditors/core/build/lib/win_64/ascdocumentscore.lib
ONLYOFFICE_QT_WRAPPER_BUILD=D:/open office/build/full-source-qt6
```

根 `CMakeLists.txt` 新增可选的 `ONLYOFFICE_CORE_IMPORT_LIBRARY`；留空时维持已有导入库目录布局。

最后校验原装配内容未改变，并加入 `build/full-source-host/bin/OpenOffice.exe`：

```powershell
python -B scripts/stage_full_source_runtime.py --finish-host
```

每次默认写一份带时间戳的 `evidence/full-source-runtime-inventory-*.json`，也可用 `--report evidence/<新的文件名>.json` 指定。报告拒绝覆盖既有文件。运行目录标记是 `openoffice-full-source-stage.json`，逐文件记录目的路径、来源、类别、字节数和 SHA-256；可执行文件另记录 x64 PE 导入表。`docx_roundtrip_verified` 始终是 `false`，实际验收由独立工具完成。

## 固定来源

| 内容 | 来源 |
| --- | --- |
| ONLYOFFICE 核心、辅助进程、转换器和工具 | `build/full-source/DesktopEditors/core/build/{lib,bin}/win_64` |
| Qt6 包装 DLL | `build/full-source-qt6/bin`；核验 CMakeCache 中的隔离源码和导入库路径 |
| ICU 74 DLL | 隔离 `core/Common/3dParty/icu/win_64/build` |
| V8 单体库和 converter ICU 数据 | 隔离 V8 `out.gn/win_64/release`；与 CEF 根目录的 ICU 数据分开 |
| CEF | 固定官方缓存与 `desktop-build-evidence/cef-stage.json` 的逐文件记录；明确属于第三方预编译依赖 |
| Qt 与插件 | 已安装 `D:/Qt/6.11.1/msvc2022_64`；不修改 Qt DLL 字节，不运行安装程序 |
| SDKJS / web-apps | 此次 deploy，逐文件与既有构建清单比对 |
| 本地图片、词典、许可证 | 固定源码工作树 |
| 字体及许可证、空白模板、断线页面 | 已校验 v10.0.0 ZIP 的严格白名单：`fonts/**` 21 项、`converter/empty/**` 192 项、`editors/webext/noconnect.html` 1 项 |

ZIP 不允许补回任何原生 DLL、EXE、静态库或编辑器主体 JS。SDKJS 中随源码提供的 WASM 没有重新编译；脚本保留其原始来源分类。插件和宏仍由现有 host 禁用。

`AllFonts.js` 和字体索引由新 host 的 `CheckFonts(false)` 在独立 profile 中生成，不复制旧用户缓存。`converter/templates` 是可选图库，与 `converter/empty` 的空白种子不是同一目录。

## 失败与恢复

装配不覆盖已存在的运行目录。新目录先在同级独立事务目录写入并逐文件复核，全部完成后再重命名。失败目录保留，不自动清理。`--finish-host` 如果在 host 写入后、清单提交前中断，允许在 host 哈希完全匹配时继续提交清单；不会替换不同内容的文件。

首次研究时观察到 Qt 的 `windeployqt --dry-run` 仍会建立指定的空输出目录。该次只留下 `build/full-source-run/qt6-plugins` 两层空目录，没有复制文件；当时核验为空后已移除。此后按任务约束不再清理任何目录。当前脚本将 Qt 模拟指向已存在的 `build/full-source/desktop-build-evidence`，按 Qt 安装来源计算最终目的路径，不让模拟工具创建运行目录，也不让它执行实际拷贝。
