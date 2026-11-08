# SDKJS / WebAssembly 源码构建边界审计

审计日期：2026-11-02。结论：当前 SDKJS 部署中有 **5 个官方预生成 WASM（合计 16,675,319 字节）和 10 个配套生成 JS / asm.js 文件**，均未在本轮从 C/C++ 源码生成。它们包含 ONLYOFFICE 自己实现的接口和逻辑，不能统称“第三方依赖”。原生核心、转换器的源码编译成功，也不能据此宣称整个引擎的全部第一方资产均已源码重编。

本次只读检查源码、产物和官方历史，新增本说明与 [机器可读证据](../../evidence/full-source-wasm-provenance.json)。没有安装或执行新工具、启动编译、修改生产源码、requirements.md 或回退运行目录。此审计没有待批准的工具操作，也没有审批拒绝。一次可选的 urllib 元数据读取超时；相同固定官方页面已通过浏览工具成功读取，不影响下述版本结论。

固定工作树为 `D:\open office\build\full-source\DesktopEditors`：

| 仓库 | 实际固定提交 |
| --- | --- |
| core | `ed63e94de814ff1d4ea28cddfaba6939bc9df9ed` |
| sdkjs | `9d2138866d929be32f1d107e8b5bfae36156e097` |
| build_tools | `43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda` |

以下路径均相对 `sdkjs/deploy/sdkjs`。相同相对路径的 `sdkjs` 源仓库文件与部署文件逐字节相同，并用 `git hash-object` 与固定 HEAD 的 blob 核对。JSON 保存全部 20 个相关文件的路径、大小、SHA-256、Git blob 和 `built_this_run:false`。

| WASM 路径 | 字节 | 组成及第一方部分 | Windows 桌面使用边界 |
| --- | ---: | --- | --- |
| `common/hash/hash/engine.wasm` | 65,115 | ONLYOFFICE hash/hash2 包装与 worker 集成；OpenSSL 哈希实现 | 保护/哈希功能按需使用浏览器 worker；本审计没有证明最小 DOCX 测试加载了它 |
| `common/libfont/engine/fonts.wasm` | 3,614,021 | ONLYOFFICE ASC_FT/ASC_HB、ZIP/Raster/Hyphen 接口；FreeType、HarfBuzz、Brotli、Hyphen、zlib 及图像代码 | 普通 CEF DOCX 前端走字体 WASM 路径；见下文加载源码证据 |
| `common/spell/spell/spell.wasm` | 510,644 | ONLYOFFICE Spellchecker 包装、worker 胶水；Hunspell | 通常 `AscDesktopEditor` 路径调用原生 SpellCheck；浏览器路径使用此资源 |
| `common/zlib/engine/zlib.wasm` | 1,617,419 | 除 zlib/minizip、图像库外，含 ONLYOFFICE ZipBuffer、Raster 接口和 WMF/EMF 处理 | 按功能加载；字体模块亦导出 ZIP/Raster，不能说每次 DOCX 都单独加载此 WASM |
| `pdf/src/engine/drawingfile.wasm` | 10,868,120 | ONLYOFFICE PDF/绘制/编辑/表单/拆分合并等接口，以及 Xpdf、DjVu、FreeType、ICU、CryptoPP、图像库 | PDF 前端路径；普通 DOCX 不必经过它，原生转换器另有 native bridge |

配套资产也必须计入未重编边界：五个目录中的 `engine.js` / `fonts.js` / `spell.js` / `zlib.js` / `drawingfile.js` 是 Emscripten 生成内容与第一方模板组成的加载脚本；对应五个 `*_ie.js` 包含 asm.js 回退引擎，不能因扩展名是 JS 就视为本轮由原始 C/C++ 重新编译。另有 `spell.js.mem` 147,495 字节。这 11 个文件与 WASM 一样直接来自固定仓库。

这些目录还有两个 `*_native.js` 原生桥接文件、一个已生成的 PDF `viewer.js` 和 `cmap.bin` 数据。整个 SDKJS 部署的 153 个 `.bin` 中，152 个是 SmartArt 数据，另一个为 CMap；它们不应误计为机器指令引擎。部署还复制了 `common/Drawings/Format/path-boolean-min.js`（70,495 字节，含 Ascensio 署名与 jscomp 生成内容），本轮拼接也没有从其开发源重新生成压缩版本。本审计没有扩展为全部第三方前端依赖的作者审计。`web-apps/deploy/web-apps` 内没有发现 `.wasm`，也未发现本次检查的 `.exe/.dll/.node/.bc/.a/.lib/.data/.cache/.mem` 文件。

固定官方桌面构建本来就采用此边界。`sdkjs/build/build.py` 明确进行 JS 拼接，`OTHER_FILES` 包含上述引擎目录；`deploy_assets()` 使用 `shutil.copy2`。`build_tools/scripts/build_js.py` 的 desktop 分支执行 `build.py --desktop`，随后复制 SDK 输出，`deploy_desktop.py` 再把它放进 `editors/sdkjs`。这些步骤没有执行 emcc。[固定 SDKJS 构建脚本](https://github.com/ONLYOFFICE/sdkjs/blob/9d2138866d929be32f1d107e8b5bfae36156e097/build/build.py#L211)、[桌面 JS 构建](https://github.com/ONLYOFFICE/build_tools/blob/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/scripts/build_js.py#L56)、[桌面部署](https://github.com/ONLYOFFICE/build_tools/blob/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/scripts/deploy_desktop.py#L235)。

DOCX 桌面前端与原生转换器必须分开描述。`common/apiBase.js` 调用 `AscFonts.load`；`common/libfont/loader.js` 的跳过条件是 native editor 环境标志，不是仅检测到 `AscDesktopEditor`。`fonts.js` 专门支持通过 `ascdesktop://fonts/` 读取 `fonts.wasm`，CEF scheme handler 负责读取该文件。因此从源码路径判断，正常桌面 DOCX 前端确实依赖字体 WASM；这不是“只在网页版使用”。这里是静态调用链证据，本次没有新增实际网络请求或文件访问跟踪。[字体加载](https://github.com/ONLYOFFICE/sdkjs/blob/9d2138866d929be32f1d107e8b5bfae36156e097/common/libfont/loader.js#L94)、[桌面字体读取分支](https://github.com/ONLYOFFICE/sdkjs/blob/9d2138866d929be32f1d107e8b5bfae36156e097/common/libfont/engine/fonts.js#L67)。本地 handler：`desktop-sdk/ChromiumBasedEditors/lib/src/cefwrapper/client_scheme_wrapper.cpp`，约 209–242 行。

原生 `core/DesktopEditor/doctrenderer/editors.cpp` 在启用 HarfBuzz 时使用 `fonts_native.js`，PDF 使用 `drawingfile_native.js`，并由 `embed/Default.cpp` 注册 CZipEmbed、CTextMeasurerEmbed、CHashEmbed、CGraphicsEmbed。当前 MSVC 构建具备 HarfBuzz 条件。官方 builder 部署甚至主动移除 WASM、IE JS 和 CMap 等资源。这支持“原生转换路径独立”，但不消除 CEF 前端的 WASM 依赖。[原生编辑器脚本选择](https://github.com/ONLYOFFICE/core/blob/ed63e94de814ff1d4ea28cddfaba6939bc9df9ed/DesktopEditor/doctrenderer/editors.cpp#L112)、[builder 资源裁剪](https://github.com/ONLYOFFICE/build_tools/blob/43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda/scripts/deploy_builder.py#L137)。

当前固定 core / build_tools 树中没有完整 WASM 生成配方：fontengine/js、OfficeUtils/js、Hunspell WASM 包装、xmlsec hash WASM 包装与 Common/js 通用生成器都已删除。官方历史可解释这一点：core 的 [2026-05-12 Refactoring](https://github.com/ONLYOFFICE/core/commit/2da2866862000c6fd180e159701d5aee95448107) 删除多组包装和配方；[2026-05-17 Remove unused files](https://github.com/ONLYOFFICE/core/commit/b77b3dc7e21e7d33b5fbe6711b7c1cb1a11ae447) 又移除通用 JS 构建文件。不能把“当前树没有配方”写成“上游从未公开过源码”。

已找到删除前的固定官方快照 `075052dd3f41373affa533317d6734a0d34e0897`，包含全部五组历史配方：

| 模块 | 该历史快照内配方 | 对当前源码树 / 加载器的核对 |
| --- | --- | --- |
| fonts | `DesktopEditor/fontengine/js/libfont.json` | 导出集合匹配；所列 251 个源文件路径存在、46 个缺失 |
| zlib/image | `OfficeUtils/js/zlib_image.json` | 导出集合匹配；157 个存在、7 个缺失 |
| spell | `Common/3dParty/hunspell/hunspell.json` | 导出集合匹配；9 个存在、2 个包装文件缺失 |
| hash | `DesktopEditor/xmlsec/src/wasm/hash/hash.json` | 导出集合匹配；所列 17 个专用路径缺失，含 main.cpp 与尚未在该位置准备的 OpenSSL 源码 |
| drawingfile | `DesktopEditor/graphics/pro/js/drawingfile.json` | 749 个存在、46 个缺失；当前加载器另有 6 个导出未被旧配方列出 |

“缺失路径”不等于所有第三方原始代码都不存在。例如多项 FreeType 缺失是配方准备步骤生成的补丁副本；普通 OpenSSL 源码也可存在于其他目录。真正需要恢复的是相应版本的准备脚本、补丁、第一方包装及布局。完整缺失路径已写入 JSON。导出集合比对忽略 Emscripten 运行时内部符号，匹配只代表接口覆盖，不证明具体实现版本或二进制可重复性。[历史通用 JSON 生成器](https://github.com/ONLYOFFICE/core/blob/075052dd3f41373affa533317d6734a0d34e0897/Common/js/make.py)、[历史字体配方](https://github.com/ONLYOFFICE/core/blob/075052dd3f41373affa533317d6734a0d34e0897/DesktopEditor/fontengine/js/libfont.json)、[历史 PDF 配方](https://github.com/ONLYOFFICE/core/blob/075052dd3f41373affa533317d6734a0d34e0897/DesktopEditor/graphics/pro/js/drawingfile.json)。

当前 PDF 加载器多出的接口是 `_BuildPdf`、`_ClearFontsStorage`、`_ClearImagesStorage`、`_GetMacros`、`_IsImageBinaryExist`、`_SetImageBinary`。所以不能直接用这份旧配方覆盖当前 PDF 引擎并宣称等价。

固定 SDKJS 历史中，五份 WASM 最后变更提交如下；这些是资产的来源提交，不是已经证实的 C/C++ 编译输入提交：

| 资产 | 最后变更提交 | 日期 |
| --- | --- | --- |
| fonts | `cfc208d80ad7b093a3cc6e988e15e5411685901a` | 2025-12-17 |
| spell | `8acfe8a21eb551b9a63ece92fb4c8f112d771084` | 2021-05-21 |
| hash | `1dbcc65201cd87b6b7ac1849dbe11bafae4aaf41` | 2021-11-12 |
| zlib | `0fd923384fea32ae5cb83888307940ab4c824cf3` | 2022-08-10 |
| drawingfile | `05fd481c7bc5154c307ccef9718c47b783ddd6b9` | 2026-10-08 |

字体资源与 core 同日的 `0da5ad2f4869c4a6439502395370be0c647175d2` “Fix libfont”存在时间关联，尚不足以断言准确对应。PDF 资源则在旧配方删除后仍更新，当前匹配配方仍未找到。检查过固定 build_tools、历史 build_tools 树与官方 ONLYOFFICE wasm/webassembly 仓库名检索，未找到替代生成仓库；这不是对全部官方仓库不存在替代实现的证明。JSON 保留官方 API URL、提交元数据及历史配方内容。[当前 PDF 资产变更](https://github.com/ONLYOFFICE/sdkjs/commit/05fd481c7bc5154c307ccef9718c47b783ddd6b9)。

历史通用配方的工具版本现在可以准确说明：`Common/js/make.py` 先 checkout **emsdk `f677ef915645c09794f0ae88f21d3cba2886459f`**，再执行 `install latest` / `activate latest`。该固定 emsdk 的本地别名将 latest 解析为 **Emscripten 3.1.48**，release hash 为 `694434b6d47c5f6eff2c8fbd9eeb016c977ae9dc`。emsdk 从本地固定 tags JSON 解析版本，不是这里自动选择今天的 latest。早先只看 install 行得出的“未固定版本”判断已在本报告纠正。[固定版本映射](https://github.com/emscripten-core/emsdk/blob/f677ef915645c09794f0ae88f21d3cba2886459f/emscripten-releases-tags.json#L1)、[解析实现](https://github.com/emscripten-core/emsdk/blob/f677ef915645c09794f0ae88f21d3cba2886459f/emsdk.py#L1980)。

固定官方 [Windows manifest](https://github.com/emscripten-core/emsdk/blob/f677ef915645c09794f0ae88f21d3cba2886459f/emsdk_manifest.json#L689) 的 SDK 组合如下；以下仅为准备未来授权的清单，没有下载、安装或执行：

| 工具 | 固定版本 / 官方来源 |
| --- | --- |
| emsdk 管理器 | `f677ef915645c09794f0ae88f21d3cba2886459f`，`https://github.com/emscripten-core/emsdk.git` |
| Emscripten / LLVM / Binaryen 工具包 | 3.1.48，Google 官方 webassembly storage 的 `win/694434b6d47c5f6eff2c8fbd9eeb016c977ae9dc/wasm-binaries.zip` |
| Node.js | 16.20.0 x64，manifest 的 `node-v16.20.0-win-x64.zip` |
| Python | 3.9.2 nuget x64，manifest 的 `python-3.9.2-4-amd64+pywin32.zip` |
| Java runtime | 8.152 x64，manifest 的 `portable_jre_8_update_152_64bit.zip` |

后三个相对下载地址的官方前缀为 `https://storage.googleapis.com/webassembly/emscripten-releases-builds/deps/`；JSON 保存解析后的完整 URL。仅验证了官方配方映射，没有下载包体或核验包哈希。历史 `Common/js/min.py` 还调用 `sdkjs/build/node_modules/google-closure-compiler-java/compiler.jar`；该脚本自身没有固定 JAR 版本，当前固定 SDKJS 的原位置也没有 package.json / package-lock.json，此项仍需单独恢复版本证据。历史独立 `graphics/pro/js/make.py` 确实没有 emsdk 固定提交，但其 `deploy.py` 走上述通用 JSON 生成器，二者不可混写。

上述 3.1.48 只证明已找到的历史通用配方工具链。五份现存 WASM 都没有可用于识别版本的 producers/custom section，且资源更新时间横跨 2021–2026，不能说它们实际都由 3.1.48 编译。也不能在仍缺当前匹配源的情况下安装最新 emsdk来替代版本核验。

真正剩余的源码构建缺口是：恢复与现有接口匹配的第一方 WASM 包装、模板、准备补丁和生成配方；确定尤其 PDF 的新增接口来源；准备固定工具链后分别生成 WASM、加载 JS、asm.js 及所需 memory 文件；再做字体排版、ZIP/图像、哈希、拼写与 PDF 功能验证。旧配方的导出集合一致不足以替代这些验证。此缺口不应拖停已经授权的原生核心构建和 DOCX 回归，但在完成前整体状态必须保持“尚未全部源码构建”。

## 2026-11-02 上游复核补充

只读复核，见 [机器可读证据](../../evidence/full-source-wasm-upstream-recheck.json)。固定 core `ed63e94` 就是上游 master 当前 HEAD；master 的 `graphics/pro/js/wasm/src` 只剩 4 个辅助文件，`drawingfile.cpp` 等导出层仍不存在。SDKJS 的 2026-10-08 `05fd481` 只提交了二进制（WASM 与 IE JS），没有对应配方。

PDF 新增接口的来源已部分确认。历史 `drawingfile.cpp` 只是 6,886 字节的 `extern "C"` 导出层，业务逻辑在 `DesktopEditor/doctrenderer/drawingfile.h` 的 `CDrawingFile` 中。当前固定源码的该类已有 `BuildPdf`、`SetImageBinary`、`IsImageBinaryExist`、`GetMacros`，缺的只是导出层。`_ClearFontsStorage`、`_ClearImagesStorage` 在固定 core（排除 3dParty）中没有公开实现，只能按加载器用法重建。因此即使重建成功，也只能称为“按公开源码重建的等价接口”，不能称与官方二进制同源。

固定 emsdk 3.1.48 的 Windows 下载合计约 554 MB（工具包 441,957,173 字节，Node 28,623,474，Python 14,413,267，JRE 69,241,499）；D 盘剩余约 45 GB。仍未下载或安装。

## 2026-11-03 历史包装恢复与接口对照

按接手文档 8.1 第 2 步执行，仍未下载或运行任何新工具，也没有编译。

**恢复方式。** 官方 core `075052dd` 用独立的稀疏仓库恢复到 `build/wasm-source/history-075052dd`，共 383 个文件，覆盖五组配方、`Common/js` 生成器、各包装源码和 Metafile 目录。没有放进 `build/wasm-source/core`，因为它和 `upstream` 共用同一个 git 对象库。恢复来的脚本一律没有执行。[检出记录](../../evidence/wasm-source/history-checkout-075052dd.json)

**对照方法。** 新脚本 `scripts/check_wasm_history_abi.py` 直接解析当前部署的 5 个 WASM 二进制的导出函数签名，并通过 JS 胶水把压缩后的导出名映射回完整名称；再和历史配方的导出清单、历史包装源码的 C 函数参数，以及固定 core 树中的类方法逐项比较。源文件变化按 git blob 判断，不需要下载文件内容。[完整结果](../../evidence/wasm-source/history-abi-check-20261103.json)

| 模块 | 配方源文件 | 未变 / 已改 / 之后删除 | 需准备或另行获取 | 导出接口 |
| --- | ---: | --- | --- | --- |
| fonts | 297 | 124 / 42 / 7 | 85 个第三方源码在原生树已有；39 个 FreeType 补丁副本由 before.py 生成；1 个大小写不一致 | 加载器所用接口全部存在于当前 WASM，参数类型与历史包装一致 |
| zlib | 164 | 122 / 35 / 4 | 3 个 `EmfClip/EmfPath/WmfClip` 在 075052dd 本身就不存在，属于历史配方遗留；1 个大小写不一致 | 同上，一致 |
| spell | 11 | 0 / 0 / 2 | 9 个 Hunspell 源码在原生树已有 | 一致 |
| hash | 17 | 0 / 0 / 1 | 16 个 OpenSSL 副本由 before.py 生成 | 一致 |
| drawingfile | 795 | 548 / 126 / 3 | 75 个 ICU 源码在原生树已有；39 个 FreeType 和 4 个 libxml 副本由 before.py 生成 | 1 处签名变化，另有 6 个新接口 |

PDF 模块的结论：
- `SetScanPageFonts` 历史包装是 `(pFile, nPageIndex)`；当前 WASM 是 3 个参数并带返回值，与当前 `CDrawingFile::SetScanPageFonts(int* arrPageIndex, int nLength)` 加实例指针完全对应。
- `_BuildPdf`（6 参数）、`_GetMacros`（1 参数）与当前 `CDrawingFile` 方法加实例指针对应。`_SetImageBinary`（3 参数）、`_IsImageBinaryExist`（1 参数）与同名方法的参数一致，属于不带实例的全局包装，和历史 `SetFontBinary` 写法相同。
- `_ClearFontsStorage`、`_ClearImagesStorage` 都是 0 参数。当前 `CDrawingFile` 有静态 `GetFontsStorage()` / `GetImagesStorage()`，`MemoryStorage.h` 的存储接口有纯虚 `Clear()`。所需行为都能由公开源码表达，缺的只是导出层。

以上只证明接口形状和参数个数可以对应，不证明重建后行为与官方二进制相同。真正的功能等价仍要靠编译后的模块测试。

**准备脚本的风险。** `graphics/pro/js/before.py` 会就地修改共享的 ICU、libpng 和 OpenSSL 源文件；Hunspell 的 before.py 在提交不一致时会删除目录并重新克隆。两者都只能在隔离副本中、经审查改写后运行，不能直接对 `build/full-source` 执行。

## 2026-11-03 字体 WASM 源码重建与差分测试

用户在 2026-11-03 批准：只下载固定版本 Emscripten 3.1.48 的 Windows `wasm-binaries.zip`，复用已批准的 Python 3.11.8 和现有 Node 24，不装 Java，不改系统 PATH。

**工具链。** 官方包 441,957,173 字节，SHA256 `9276435e…651d` 与固定 emsdk `revisions.bzl` 的 `sha_win` 一致。经路径安全和 CRC 检查后，解压到 `dependencies/emscripten-3.1.48/install`，共 9,258 个文件。配置文件和缓存放在 `build/wasm-source/toolchain`，只在子进程中设置 `EM_CONFIG`、`EM_CACHE`、`EMSDK_PYTHON`。最小测试 5 项全部通过：C 编译到 WASM、C++ 异常、asm.js、包内原生 Closure、版本号。[下载记录](../../evidence/wasm-source/emscripten-3.1.48-download.json)、[最小测试](../../evidence/wasm-source/toolchain-smoke-pass1.json)

**源码准备。** `scripts/prepare_wasm_module_source.py` 按历史 before.py 的步骤逐项重做，但没有执行历史脚本本身：
- 补回 83 个历史包装文件；与当前源码重名的 4 个文件保留当前版本。
- 从原生树复制 HarfBuzz `894a1f72`、Brotli `a47d7475`、hyphen `53820b5d`，提交号已核对。HarfBuzz 只有官方 make.py 补丁带来的 `hb-ft.cc` 修改；hyphen 在 build_tools 中没有固定提交，沿用原生构建时的检出。
- 复制 FreeType 并应用 9 个历史补丁，每个都断言只匹配一处。
- 修改 `pnglibconf.h` 一处。这也是 `build/wasm-source/core` 中唯一被改动的已跟踪文件。

[准备记录](../../evidence/wasm-source/fonts-prepare-pass1.json)

**编译。** `scripts/build_wasm_module.py` 按历史 `Common/js/make.py` 的流程编译，同时保留全部中间产物和日志，并断言模板占位符和后处理替换的次数。
- 第一次（pass1）297 个源文件都编译成功，但链接失败：缺少 `ima_jpeg_cmyk_to_rgb`。它定义在 075052dd 之后新增的 `APP14cmyktorgb.cpp` 中，官方 `raster.pri:268` 也编译这个文件。
- 编译脚本因此加了一条有官方依据的配方适配，并记入证据。第二次（pass2）298 个源文件的 WASM 和 asm.js 两个版本都编译、链接成功，约 2 分钟。
- 产物大小：`fonts.wasm` 3,641,996 字节（官方 3,614,021）；`fonts.js` 60,227 字节（官方 60,400）；`fonts_ie.js` 6,878,154 字节（官方 6,899,162）。

[构建记录](../../evidence/wasm-source/fonts-build-pass2.json)、[pass1 失败记录](../../evidence/wasm-source/fonts-build-pass1.json)

**接口一致性。** 加载器用到的 50 个导出，名称和参数、返回类型与官方 `fonts.wasm` 完全相同；从 JS 导入的 83 个函数也完全相同。两个 JS 的文本差异来自 Closure 换行位置和代码排列，二进制本身不是逐字节相同。

**功能差分。** `scripts/test_wasm_fonts.py` 在 Node 中模拟网页环境，分别加载官方版和重建版（WASM 和 asm.js 各一次），用同一组输入直接调用导出函数，结果逐项比较：
- FreeType：6 个字体，含可变字体和 TTC；3,628 次字形加载的度量、轮廓和位图哈希。
- HarfBuzz：拉丁文（开、关连字和字距）、中文、阿拉伯文从右到左，共 5 组、78 个字形。
- 图像：PNG、GIF、JPEG 各种格式的识别、BGRA/RGBA 解码像素哈希、PNG/JPEG 重新编码字节。
- 断词：英、德、俄三种词典，13 个词。
- zip：DOCX 样本 5 个条目的读取。

结果：两种版本的五个方面全部与官方逐项相同。两边新建的 zip 都通过了 Python zipfile 的 CRC 和内容校验。另外发现，官方版和重建版的 `Zlib_GetPaths` 都不列出空文件，且都不设置 UTF-8 文件名标志；这是现有引擎行为，不是重建差异。[差分测试结果](../../evidence/wasm-source/fonts-functional-pass5.json)

**仍未完成。**
- 重建产物还没有装配进任何运行目录，也没有在真实 CEF 编辑器中做 DOCX 回归。官方 SDKJS 和两个已验收运行目录都没动。
- 测试覆盖的是上述样本，不代表所有字体、图像格式和文字体系都已验证。
- 其余 zlib、spell、hash、drawingfile 四个 WASM 仍是上游预生成资产。

## 2026-11-04 其余四个 WASM 的重建与差分测试

工具链、准备脚本、编译脚本与字体模块相同。每个模块的准备、编译、测试都有独立证据，位于 `evidence/wasm-source/`。所有配方适配都写在 `scripts/build_wasm_module.py` 的 `RECIPE_ADAPTATIONS` 中，每条都引用官方构建文件或 Emscripten 日志。

| 模块 | 产物（重建 / 官方） | 主要适配 | 差分测试结论 |
| --- | --- | --- | --- |
| hash | `engine.wasm` 64,846 / 65,115 | OpenSSL 1.1.1f 以 OpenSSL 自己的 Configure（linux-generic32）生成配置头；历史 after.py 改写为逐步断言 | 9 种算法 × 6 组输入两版一致；36 个摘要与 Python hashlib、20 组 Office 密码哈希与 Python 独立实现一致（`hash-functional-pass1.json`） |
| spell | `spell.wasm` 524,422 / 510,644 | Hunspell 改动证实与历史 before.py 相同；模板转发参数（依据 `desktop_fetch.js:48`）；`-sSTACK_SIZE=5MB`（3.1.27 起默认栈 64KB，导致 pass1 栈溢出） | en/de/ru/fr/es 五种词典的拼写判断与建议词两版一致（`spell-functional-pass2.json`） |
| zlib | `zlib.wasm` 1,917,250 / 1,617,419 | 补齐 075052dd 之后改名或新增的图元、矩阵、路径、PICT、CMYK 源文件；`IMAGE_CHECKER_DISABLE_XML`；5MB 栈 | zip 读写与 4 个 Office 样本（Python zipfile 交叉验证）、PNG/GIF/JPEG 解码编码两版一致；WMF→SVG 不同（见下） |
| drawingfile | `drawingfile.wasm` 10,611,404 / 10,868,120 | 导出层与 metafile 占位类由历史文件加断言修改重建；libxml、ICU、OpenSSL 按当前布局；补 OFD、FontsWasm、OverlapOutputDev、ProcessEnv 等新增源文件 | 自建 PDF 的渲染、文字、链接、目录、表单与外观、拆分/另存为、存储接口两版一致（`drawingfile-functional-pass2.json`） |

**zlib 的 WMF→SVG 差异（`zlib-wmf-svg-analysis.json`）：**
- 3 个 WMF 的重建 zlib 输出与重建 fonts 逐字节相同，而官方 zlib（2022）、官方 fonts（2025-12）各自不同。这是 SVG 输出代码随源码版本演进造成的，不是重建缺陷。
- `image10.wmf` 触发上游缺陷：`Raster_Encode` 以 `MetaFile::Create(NULL)` 创建图元对象，字体管理器为空；`CInterpretatorSvgBase.cpp:336` 与 `CEmfInterpretatorSvg.cpp:1255` 不判空就调用虚函数。重建 zlib 因此中止，重建 fonts 对该文件的输出也不可信。
- 该代码在 075052dd 已存在，未打补丁。

**drawingfile 的来源边界：**
- `drawingfile.wasm` 与 Emscripten 胶水代码从源码重建。导出层 `drawingfile_reconstructed.cpp` 由历史文件经 6 处有依据的修改生成。
- 第一方加载器模板 JS 的公开源码已于 2026-05 删除，075052dd 版又缺少当前接口，所以 `drawingfile.js`、`drawingfile_ie.js` 的模板部分取自官方生成文件：剥去许可证头，按两段式边界换掉胶水代码。
- `drawingfile_native.js` 不含胶水代码，沿用官方文件。

**仍未完成：**
- 重建产物尚未装配进任何运行目录，也未在真实编辑器中做 DOCX 或 PDF 回归。
- 测试只覆盖上述样本。
- 官方 SDKJS 与两个已验收运行目录均未改动。

## 2026-11-05 zlib 修复与候选运行目录回归

**更正与修复（`zlib-wmf-svg-fix.json`）：**
- 上一节说重建 fonts 对 `image10.wmf` 的输出不可信，这是错的。两处字体查找代码包在 `#ifndef BUILDING_WASM_MODULE` 中，fonts 与 drawingfile 配方都定义了这个宏，从不执行这段代码；只有 2022 年那一代的 zlib 配方漏了它。
- 按用户“修复后装配”的决定，给 zlib 配方补上 `BUILDING_WASM_MODULE`，不改上游源码。
- 结果：`image10.wmf` 不再中止，4 个 WMF 转 SVG 均与重建 fonts 逐字节相同。GIF/JPEG 重新编码为 PNG 时，改为与官方及重建 fonts 一样返回无数据，只有官方 2022 版 zlib 会抛异常。

**候选运行目录（`candidate-runtime-full-source-run-wasm-candidate.json`）：**
- `scripts/stage_wasm_candidate_runtime.py` 从已验收的 `build/full-source-run` 复制出 `build/full-source-run-wasm-candidate`，不复制用户配置。
- 先核对 18 个原引擎文件与官方 SDKJS 逐字节相同，再替换 5 个模块共 15 个文件，并删除不再被引用的 `spell.js.mem`。其余 20,956 个文件逐一核对与原目录相同。

**真实编辑器 DOCX 回归通过（`candidate-docx-roundtrip-result.json`）：**
- 候选版打开 DOCX，用重建的字体引擎正确显示中英文、粗斜体与表格。
- 在文档开头输入中文标记并保存，正常关闭，退出码 0。
- OOXML 独立检查 4 项通过。
- 新进程加全新配置重开，目视确认内容，退出码 0。
- 回归只覆盖这一个 DOCX 流程，PDF、拼写、哈希、图元在界面中的路径未测。默认入口仍是 `build/full-source-run`。

当原生链路另行验证成功后，可准确表述为：**“Windows 原生核心和转换器已从源码编译；前端 JS 从源文件拼接；5 个 WASM 及配套生成 JS 仍复用固定官方 SDKJS 预生成资产。”** 本报告没有替其他工作线程宣告原生编译或 DOCX 回归完成。
