# Open Office

面向 Windows 的开源办公应用。代码仓库：<https://github.com/renzhekai/open-office-desktop>（公开，AGPL-3.0）。需求原文保留在 [requirements.md](requirements.md)。默认入口带一个 AI 助手悬浮球，可以接上这台电脑里的 Ollama 本地模型，对话和文档内容只在本机处理（见下文“AI 助手”）。

当前入口使用 C++17 / Qt 6 Widgets，嵌入 ONLYOFFICE 原生编辑视图。已从固定源码编译的部分：
- Windows 原生核心、13 个格式静态库、V8、转换器、桌面核心和 Qt 包装层。
- 编辑器使用的 5 个 WASM 引擎（字体、zip/图像、拼写、哈希、PDF），以及它们的 Emscripten 胶水代码。

默认入口（2026-11-07 起为 `build/full-source-run-local-ai`）已在真实编辑器中通过以下验证：
- 本地 AI：用本机 Ollama 提问，确认后插入文字，再保存，[结果](evidence/host-candidate/local-ai-session1-result.json)；连不上 Ollama 时给出启动引导。
- AI 助手悬浮球的插入、撤销、重做、读取选中、状态、保存，以及摆放和显示规则，[复查记录](evidence/host-candidate/assistant-ball-r7-session-result.json)（在上一版 r7 上做的；本地 AI 版只增加了 AI 对话，界面测试继续覆盖）。
- DOCX 新建、首次保存、另存为、原路径保存、同窗口换文档、编辑器内保存、关闭提示，[复查记录](evidence/host-candidate/host-saveas-r2-recheck-result.json)（在更早的宿主上做的；之后的宿主没有改这些流程的功能，界面测试继续覆盖，后来的复查里也实际执行了保存）。
- 同一套 WASM 与核心的 DOCX 中文编辑、保存、全新进程重开，[验收记录](evidence/wasm-source/candidate-docx-roundtrip-result.json)。

构建边界：
- CEF、Qt 属于第三方预编译依赖。
- PDF 加载器中的第一方模板 JS 没有公开源码，取自固定 SDKJS 的官方生成文件，只替换了其中的胶水代码。
- `fonts_native.js`、`drawingfile_native.js` 等少数预生成 JS 沿用官方文件。
- 因此不能称所有第一方资产都已从源码重编。详见 [WASM 来源审计](docs/feasibility/full-source-wasm-provenance.md)。

## 安装

安装程序：`dist/OpenOffice-Setup-0.1.0-dev-20261107-2-win64.exe`（594 MB，SHA256 `aa91dbeb…2656`，同目录 `.sha256.txt` 有完整值）。安装包和运行目录太大，不放进 git 仓库，只在这台电脑的 `dist/` 里。

1. 双击运行，按中文向导安装。
2. 默认装到 `%LOCALAPPDATA%\Programs\OpenOffice`，只为当前用户安装，不需要管理员权限。可以选择是否创建桌面快捷方式；开始菜单里会有“Open Office”。
3. 卸载：在 Windows“设置 → 应用 → 已安装的应用”里卸载“Open Office（开发版）”，或用开始菜单里的“卸载 Open Office”。程序生成的 `openoffice-profile`（配置和缓存）会保留，需要时手动删除。

说明：
- 安装包里装的是默认入口 `build/full-source-run-local-ai`（带悬浮球和本地 AI），未做任何修改。另外附带了 VC++ 运行库，只放在程序文件夹里，没装过运行库的电脑也能启动。本地 AI 还需要电脑上有 Ollama，没有时助手会引导安装。
- 旧安装包原样保留：`…-20261107-win64.exe` 装的是 r7（不含本地 AI），`…-20261106-win64.exe` 装的是 `build/full-source-run-host-saveas-r2`。
- 安装程序没有数字签名。从网上下载后运行，Windows 可能提示“Windows 已保护你的电脑”，需点“更多信息 → 仍要运行”。
- 自动安装测试只在这台电脑上做过，覆盖了安装、逐文件比对、启动和卸载，结果见 [测试记录](evidence/installer/OpenOffice-Setup-0.1.0-dev-20261107-2-win64.json)。还没在别的电脑上装过。
- 重新生成：`python -I scripts/build_installer.py --test`；同一天再生成时加 `--same-day 2`（3、4…），文件名会带上编号，不覆盖当天已有的安装包。用到项目内的 Inno Setup 6.7.4（`dependencies/innosetup-6.7.4`）。

便携压缩包：`dist/OpenOffice-portable-win64-20261106.zip`（757.5 MB，装的是 r2，没有重新生成），解压后双击 `OpenOffice\OpenOffice.exe`。它不带 VC++ 运行库，需要电脑已装“Microsoft Visual C++ 2015-2022 Redistributable (x64)”。[打包记录](evidence/portable/OpenOffice-portable-win64-20261106.json)

## 启动（不安装，直接从项目运行）

找不到版本时，双击项目根目录的启动文件：`打开稳定版.cmd`（默认入口）、`打开最新版.cmd`（最近构建的版本，可能还没验证完）、`版本一览.cmd`（列出全部版本）。

在资源管理器中打开 `D:\open office\build\full-source-run-local-ai`，双击 **OpenOffice.exe**。默认启动和文件选择可用已验证（[首页记录](evidence/full-source-gui/local-ai-default-home/session.json)）；不需要命令行参数。

1. 打开现有文档：点击“选择本地文件”，选一个 DOCX，再点“打开所选文件”，等界面确认文档就绪。
2. 新建：点首页“新建文档”或“文件 → 新建”，得到“文档1.docx”。
3. 保存：点顶部“保存”，等界面确认完成；新文档第一次保存时选择位置。“另存为…”（Ctrl+Shift+S）存到新路径后继续编辑新文件。编辑器内的 Ctrl+S 也可以保存。
4. 换文档：在同一窗口打开另一份 DOCX 或新建；当前文档有修改时会提示保存、不保存或取消。点“编辑器”可从首页回到当前文档。
5. AI 助手：打开文档后，窗口里的玻璃球就是助手。点开后可以直接用中文提问，也可以输入 / 开头的指令；第一次用时它会一步步引导装好 Ollama 和模型，见下文“AI 助手”。
6. 使用窗口关闭按钮退出。

PowerShell 等效入口（从项目目录运行）：

```powershell
& '.\build\full-source-run-local-ai\OpenOffice.exe'
```

可选直接打开本次已保存的测试样本：

```powershell
& '.\build\full-source-run-local-ai\OpenOffice.exe' --open 'D:\open office\fixtures\results\wasm-candidate-docx-roundtrip.docx'
```

请保留整个 `build/full-source-run-local-ai` 文件夹。EXE 依赖同目录核心、CEF、Qt、converter、编辑资源、字体和词典；默认 profile 在首次启动时写到该目录的 `openoffice-profile`。当前仅在这台电脑验收；现有安装包装的是上一个默认入口，也还没在干净系统上验证。

目录来历：
- 由 `scripts/stage_host_candidate_runtime.py` 从 `build/full-source-run-wasm-candidate` 复制而来，只替换了 `OpenOffice.exe`，记录在 `openoffice-host-candidate.json`。
- `build/full-source-run-wasm-candidate` 又由 `scripts/stage_wasm_candidate_runtime.py` 从 `build/full-source-run` 复制，只替换了 15 个 WASM 引擎文件，并删除一个不再使用的 `spell.js.mem`，记录在 `openoffice-wasm-candidate.json`。
- `openoffice-full-source-stage.json` 原样复制，描述的是最初的源码运行目录。

暂未开放或验收：
- XLSX/PPTX/PDF 的打开与新建（新建表格、新建演示按钮置灰）。
- 导出为其他格式（编辑器菜单里的导出 PDF 等会被取消，只支持同格式另存为）。
- 多文档标签页、打印和媒体功能。

小型文字/表格样本通过不代表所有 Office 文件完整保真。

## AI 助手

默认入口带有 AI 助手悬浮球：可以用 / 指令直接操作文档，也可以接上这台电脑里的 Ollama，用中文提问、让它帮忙写进文档。

- **悬浮球**：打开文档后，窗口里会浮着一颗玻璃球，回首页时隐藏。它可以在文档区域里拖到任意位置，但出不了窗口。工具栏的"助手"（Ctrl+Shift+A）可以开关它。
- **指令**：点球展开输入胶囊，输入以 / 开头的指令：`/插入 文字`、`/选中`、`/撤销`、`/重做`、`/状态`、`/保存`、`/新建`、`/命令`（英文别名也可以，如 `/undo`）。编辑器执行完并回报后，结果才会显示出来。
- **不以 / 开头的话**：交给这台电脑上的本地 AI（见下一节）；还没配好时，球会给出配置步骤，不会编造回答。
- **命令日志**：写在 profile 目录的 `assistant-commands.jsonl`，只记来源、命令名和结果，不记正文。
- **深色外观**：编辑器界面和文档页面会一起变暗（只改显示，不改文件）。

复查记录见 [r7 结果](evidence/host-candidate/assistant-ball-r7-session-result.json)，设计预览见 [docs/design](docs/design/assistant-ball-preview-contrast.png)。

### 办公助手：常用活一键完成（候选 `build/full-source-run-office-ai-r3`，2026-11-08，还不是默认入口）

> 2026-11-08 用户决定：Office 之后不再用 Ollama，改用软件自带的本地引擎（llama.cpp，正在另一处进行）。下面的办公功能与后端无关，接好内置引擎后照样使用；本节和下一节里关于 Ollama 的检测、下载和选模型的说明，届时会换成内置引擎的版本。

不用会写提示词。打开球，输入框上方会有一排按钮，跟着你在做的事变化：

- **选中了文字**：润色、纠错、精简、扩写、正式（改成书面语）、翻译（中英互译，自动判断方向）。结果先显示在球里，再由你选：
  - **替换原文**：保留原来的字体、加粗、段落格式；如果你在这期间改了选区，它会提醒，不会改错地方；
  - **插到后面**、**复制**、**重写**（再出一版）。
- **没选文字**：
  - **总结全文**：主旨加要点；**提取待办**：事项、负责人、截止时间的表格；
  - **全文校对**：逐条列出错别字、用错的词、标点和语病，只列文档里真找得到的。可以一条条点“改”，也可以“全部改正”；
  - **起草…**：通知、会议纪要、工作总结、邮件、请假条、活动方案。选一种，写几句要点，生成带标题和列表的初稿。没给的信息用【】标出让你填，不会编造；“插入文档”后标题是真正的标题样式，表格是真正的表格。
- **直接提问**：会自动带上文档内容和选中的文字，所以可以问“这份通知的时间地点是什么”“帮我把第二段改短一点”。要改文档时仍会先弹出确认。
- **更快**：打开球时就提前把模型装进内存，常用功能一次生成，不再“模型→工具→模型”来回两轮。
- 所有修改都能用撤销恢复；命令日志只记命令名和结果，不记正文。
- **慢了会说清楚原因**：选模型时先看显卡——模型能整个放进显卡才快，放不进去的大部分要靠处理器算，会慢几十倍（这台电脑上 gpt-oss:20b 每秒不到 1 个词）。所以会优先选放得进显卡的模型，推荐下载的模型也按显存挑（显存 7.5 GB 以上推荐 `qwen3:8b`，4.5 GB 以上推荐 `qwen3:4b`，没有独立显卡按内存推荐 `qwen3:4b` 或 `qwen3:1.7b`）。现在用的模型太慢时，状态里会标“较慢”，回答后会弹出一次说明，并给出“下载更合适的模型”的按钮。
- **等的时候能看到进度、能停**：“正在思考…”后面会显示已经等了几秒，超过 15 秒会说明原因；发送按钮会变成 ■，点一下就停。
- **下载失败说人话**：例如代理软件（Clash 的 Fake-IP / 虚拟网卡模式）把模型库地址解析成内网地址时，会直接说明原因和解决办法，而不是显示英文报错。

验证（2026-11-08）：
- CTest 三组共 84 项全过。用假的 Ollama 服务和假引擎走通了：选中润色 → 替换原文（带原文核对）、全文校对 → 只列文档里找得到的错 → 逐条改、起草通知 → 带格式插入、按显卡选模型、慢模型判断、停止、下载失败的解释。
- 新增的编辑器脚本（读全文、替换选区、带格式插入、查找替换）从宿主源码原样取出，在真实编辑器里逐条执行：替换选区保留了粗斜体，选区变了会拒绝，插入后标题是真正的“标题 2”、列表是真正的列表，每步一次撤销就恢复。
- 真实编辑器会话（r2，这台电脑）：选中一句话后球里的按钮变成润色等六个；状态标出“gpt-oss:20b · 较慢”（它放不进 12 GB 显卡）；“正在思考”计秒、发送键变 ■、点一下显示“已停止”和“重试”。
- **还没在真实编辑器里走完整的校对 / 润色 / 起草**：本机唯一能用的模型 gpt-oss:20b 每秒不到 1 个词，第一次会话里全文校对等了 6 分钟没结果（这正是加上面这些改进的原因）。推荐的 `qwen3:8b` 因为代理的 Fake-IP 模式没下载成功，等网络调整后再验证。

### 本地 AI（2026-11-07 起在默认入口）

悬浮球可以接上电脑里的 [Ollama](https://ollama.com)，直接用中文提问，也可以让它往文档里写字。安装包 `…-20261107-2-win64.exe` 已带有这个功能（上面的办公助手还没进安装包）。

- **第一次用**：点球，再点“本地 AI”，或者直接提问。助手会自己检测这台电脑，并给出下一步：
  - 没装 Ollama：给出“打开下载页”（Ollama 官网 Windows 下载页）和“重新检测”。
  - 装了但没运行：给出“启动 Ollama”和“重新检测”。
  - 没有可用模型：按内存推荐一个模型并写明大小（内存不到 7.5 GB 推荐 `qwen3:1.7b` 约 1.4 GB，不到 15 GB 推荐 `qwen3:4b` 约 2.5 GB，再大推荐 `qwen3:8b` 约 5.2 GB；标称 8 GB 的电脑落在第二档，16 GB 的落在第三档）。只有点了“下载”按钮才会下载，进度显示在球里。
  - 已就绪：直接回答。卡片上可以换用本机已有的其他模型，选择会记住。
- **自动选模型**：优先能操作文档的模型（Ollama 标明支持工具调用），再看中文能力和大小。模型文件超过内存 55% 的不选，嵌入模型不选。
- **只在本机**：只连本机回环地址，不走代理。Ollama 转发到云端的模型（如 `xxx:cloud`）不会被列出，也不会被使用。
- **改文档要你同意**：模型想插入文字、保存或新建文档时，球里会出现一张卡片，写明要做什么。点“执行”才会做，点“不要”就不做，模型也会被告知。读取选中文字、查看文档状态不需要确认。执行结果会告诉模型，由它用一句话说明。
- **命令日志**：模型提议并经你同意的命令，在日志里来源记为 `assistant`；仍然不记正文。
- **高级设置**：环境变量 `OPEN_OFFICE_OLLAMA_URL`（或 Ollama 自己的 `OLLAMA_HOST`）可指定端口，但只接受本机地址。

验证：
- CTest 三组共 75 项全过，其中用假的 Ollama 服务测试了检测、选模型、工具调用、拒绝执行、连不上和无模型。
- 真实编辑器会话（这台电脑，Ollama 0.40.15，自动选中 `gpt-oss:20b`）：提问 → 确认卡片 → 执行 → 保存，6 项检查全过，[结果](evidence/host-candidate/local-ai-session1-result.json)。
- 另用一个连不上的地址启动，确认显示“启动本地 AI 引擎”卡片、不编造回答（`evidence/full-source-gui/local-ai-session2`）。
- 没在没装 Ollama 的电脑上试过，也没实际点过“下载”按钮。

## 证据与复现

默认入口的新宿主（新建、另存为、同进程换文档）：

- [r2 复查结果](evidence/host-candidate/host-saveas-r2-recheck-result.json)：同一进程内依次新建并首次保存、另存为、打开另一份 DOCX、原路径保存、改动后新建并选“不保存”、编辑器内 Ctrl+S（先取消再保存）、无修改时保存、改动后关闭并选“不保存”。8 项文件与事件日志检查全部通过，退出码 0。截图：[中文提示](evidence/full-source-gui/host-saveas-r2-recheck/replace-prompt-chinese.jpg)。
- [上一版回归](evidence/host-candidate/host-saveas-session1-result.json)、[另存为截图](evidence/full-source-gui/host-saveas-session1/saveas-window.jpg)：同一流程，修正中文按钮与首页状态之前的宿主。
- 装配记录：`evidence/host-candidate/candidate-runtime-*.json`；复核命令 `python -I scripts/verify_host_saveas_session.py <标签> …`（会写新的结果文件，已存在时拒绝）。

源码重建 WASM（默认入口沿用）：

- [DOCX 端到端结果](evidence/wasm-source/candidate-docx-roundtrip-result.json)：两个独立进程均退出 0，中文段落保留，原 2×2 表格一致；[重开截图](evidence/full-source-gui/wasm-candidate-docx-reopen/reopen-window.jpg)。
- [保存文件语义检查](evidence/wasm-source/candidate-docx-saved-semantics.json)：可用 `python -B scripts/verify_full_source_docx_sample.py --after fixtures/results/wasm-candidate-docx-roundtrip.docx --marker "源码重建WASM编辑保存验证 2026-11-05"` 复核。
- 5 个 WASM 的准备、编译和与官方逐项比对的差分测试：`evidence/wasm-source/`，汇总见 [WASM 来源审计](docs/feasibility/full-source-wasm-provenance.md)。

原生核心源码版本（`build/full-source-run`，现为回退）：

- [DOCX 端到端结果](evidence/full-source-docx-roundtrip-result.json)：两个独立进程均退出 0，中文段落保留，原 2×2 表格一致。
- [保存文件语义检查](evidence/full-source-docx-saved-semantics.json)：可用 `python -B scripts/verify_full_source_docx_sample.py` 复核。
- [运行库来源检查](evidence/full-source-runtime-probe-pass1.json)：76 DLL 校验、21 目标实际加载；另有运行中宿主模块路径记录。
- [源码构建进展与边界](docs/feasibility/full-source-progress.md)、[装配说明](docs/feasibility/full-source-runtime-staging.md)。
- [源码适配补丁与重放步骤](evidence/full-source-patches-final/README.md)：8 个主工作树、37 个依赖仓库。
- [默认首页运行记录](evidence/full-source-gui/default-home/result.json)、[首页截图](evidence/full-source-gui/default-home/home.png)。截图是自有 QWidget 首页；真实 CEF 文档截图已在任务中观察。

新入口编译与现有两组 CTest：

```powershell
.\scripts\build_full_source_host.cmd manual-rebuild-1
```

原生构建使用现有 MSVC 14.44 / Qt 6.11.1。源码与构建目录在 `build/full-source`，装配脚本拒绝使用旧预编译核心补齐缺项。既有运行目录不会被该脚本覆盖；重新装配的前置条件和来源记录见装配说明。

## 回退与工程位置

五个已验证的回退都原样保留：
- `build/full-source-run-assistant-ball-r7`：上一个默认入口（2026-11-07），有悬浮球和 / 指令，不接模型；安装包 `…-20261107-win64.exe` 装的就是它。
- `build/full-source-run-host-saveas-r2`：更早的默认入口（2026-11-06 至 11-07），有新建、另存为、换文档，没有 AI 助手；安装包 `…-20261106-win64.exe` 和便携包装的是它。
- `build/full-source-run-wasm-candidate`：更早的默认入口，宿主只支持每个进程打开一份现有 DOCX。
- `build/full-source-run`：原生核心源码构建、WASM 使用官方 SDKJS 资产的版本，原记录在 [full-source-docx-roundtrip-result.json](evidence/full-source-docx-roundtrip-result.json)。
- `build/native-engine-run`：官方预编译核心的版本，原记录在 [native-docx-roundtrip-result.json](evidence/native-docx-roundtrip-result.json)。

原 `dist/OpenOffice-0.1.0` 是早期未接引擎的 MinGW 界面原型。

| 路径 | 用途 |
| --- | --- |
| `app/main.cpp`、`app/ui/` | 自有入口、窗口、保存与关闭状态 |
| `app/engine/onlyoffice_engine_adapter_native.cpp` | 真实 CEF 视图和引擎事件桥 |
| `scripts/build_full_source_core.cmd` | 隔离原生模块构建入口 |
| `scripts/full_source_v8.py` | 固定 V8 工具、构建和功能验证 |
| `scripts/stage_full_source_runtime.py` | 逐文件来源、哈希和运行目录装配 |
| `scripts/prepare_wasm_module_source.py`、`scripts/build_wasm_module.py` | WASM 隔离源码准备与编译（Emscripten 3.1.48） |
| `scripts/test_wasm_fonts.py`、`scripts/test_wasm_engines.py` | WASM 重建版与官方版的差分测试 |
| `scripts/stage_wasm_candidate_runtime.py` | 装配 WASM 候选 `build/full-source-run-wasm-candidate`（上一个默认入口） |
| `scripts/stage_host_candidate_runtime.py` | 从 WASM 候选复制并只替换新宿主，装配宿主候选（当前默认入口 `build/full-source-run-local-ai` 也由它装配） |
| `scripts/verify_host_saveas_session.py` | 只读核对新建/另存为回归留下的文件和事件日志 |
| `scripts/build_installer.py`、`installer/` | 用 Inno Setup 生成并测试安装程序（中文向导、按用户安装、附带 VC++ 运行库） |
| `scripts/package_portable_zip.py` | 生成便携 zip，并解压试启动 |
| `build/full-source-host`、`build/full-source-qt6` | 自有宿主及 Qt 包装层产物 |
| `upstream/DesktopEditors` | 保留的固定官方源码 |

## 许可证

本项目自己的代码按 [GNU AGPL v3.0](LICENSE) 发布。程序链接并分发 ONLYOFFICE 的组件，它们按 ONLYOFFICE 的许可证（AGPL v3.0 加 Ascensio System SIA 的附加条款）提供，原文见 [licenses/ONLYOFFICE-LICENSE.txt](licenses/ONLYOFFICE-LICENSE.txt)。按其附加条款：保留 ONLYOFFICE / Ascensio 的署名和许可证；修改版要标明修改日期并说明基于 ONLYOFFICE；界面里要能看到 ONLYOFFICE 是原始开发者，并能查到许可信息；不授予 ONLYOFFICE 商标的使用权。其他第三方组件见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

新运行目录的 `OPEN_OFFICE_THIRD_PARTY_NOTICES.md` 与来源清单描述此次实际组成；本项目不是 ONLYOFFICE 官方发行版。需求及后续范围见 [TASK.md](TASK.md)。
