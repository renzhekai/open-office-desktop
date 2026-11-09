# Open Office AI 接手文档（持续维护）

最后更新（UTC）：2026-11-09T04:20Z（本机时间 2026-11-08 20:20），更新者：Claude Code（Opus 5.5）  
项目根目录：`D:\open office`  
初版：2026-11-03T04:53:07Z 由 Codex 生成，依据当时读取的项目文件和用户指令。此后由接手智能体持续维护，初版校验记录的适用范围见第 11 节。

## 维护规则（每个接手智能体都要遵守）

- 这是用户明确要求维护的唯一接手文件。每次更改后都要更新“0. 当前速览”、相关正文和“13. 变更日志”，写清已完成、未完成、这次改了什么、用户的新要求。
- 变更日志只追加，不删历史条目。结论变化时，改正文并在日志里写明原因。
- 附录 A 必须与 `requirements.md` 逐字节一致。改完运行 `python -I scripts/check_handoff_doc.py` 校验。
- 本文必须保持 LF 换行。在 Windows 上用 Python 改本文时，用 `write_bytes` 或 `open(..., newline="\n")`；`write_text` 会把整份改成 CRLF，校验会失败（2026-11-04 踩过一次）。
- 和用户交流全部用中文，包括摘要、问题和按钮。
- 每次更新都要同步到 git 和 GitHub（用户 2026-11-07 要求）：改完、测完、更新本文后，在项目根目录提交并推送到 GitHub 仓库 `open-office-desktop`。提交前确认 `.gitignore` 仍挡住 build/、dist/、upstream/、dependencies/ 和 evidence 里 5 MB 以上的文件；提交邮箱用仓库本地配置的 GitHub 隐私邮箱，不改全局 git 配置。

## 0. 当前速览

**最终目标：** 用户自己的 Windows 开源办公软件。第一版不含 AI，功能接近 Microsoft Office，所有功能免费，文件兼容性高；第二阶段再加本地部署的 AI 助手和专用 MCP。底座已定为 ONLYOFFICE v10.0.0，保留其署名和标识。详见第 2 节和附录 A。

**已经达到：** 默认入口 `build/full-source-run-local-ai/OpenOffice.exe`（2026-11-07 用户第二次要求“设为默认入口”，第 13 节第 33 条）能启动，能打开、编辑、保存现有 DOCX，退出后能重开。它还能新建 DOCX、首次保存时选位置、另存为，在同一窗口换文档（第 26、27 条）。它带有 AI 助手悬浮球（第 29、30 条），并能接本机 Ollama 本地模型（第 32 条）。原生核心、V8、转换器、桌面核心以及编辑器的 5 个 WASM 引擎均已从源码编译（边界：PDF 加载器模板 JS 与两个原生桥 JS 取自官方生成文件；CEF、Qt 为第三方预编译）。已验收的 `build/full-source-run-assistant-ball-r7`（上一个默认入口，2026-11-07 的安装包装的是它）、`build/full-source-run-host-saveas-r2`（更早的默认入口，2026-11-06 的安装包和便携包装的是它）、`build/full-source-run-wasm-candidate`、`build/full-source-run` 与 `build/native-engine-run` 保留为回退。

**安装包（2026-11-07 第二次生成）：** `dist/OpenOffice-Setup-0.1.0-dev-20261107-2-win64.exe`（SHA256 `aa91dbeb…2656`），装的是默认入口 `build/full-source-run-local-ai`（带悬浮球和本地 AI）。
- 用 Inno Setup 6.7.4 生成：中文向导，按当前用户安装，附带 VC++ 运行库，带卸载。
- 本机静默安装、逐文件比对、启动、卸载测试通过（第 13 节第 34 条）。
- 旧包原样保留：`…-20261107-win64.exe`（装 r7）、`…-20261106-win64.exe`（装 r2）、便携包 `dist/OpenOffice-portable-win64-20261106.zip`（r2）。
- 都没有数字签名，也没在别的电脑上测过。见第 13 节第 28、31、34 条。

**代码仓库（2026-11-07 用户要求“把代码放入 git 和在 GitHub 建立仓库”）：** <https://github.com/renzhekai/open-office-desktop>（公开，AGPL-3.0，分支 `main`，远端名 `origin`）。项目根目录就是工作区。放代码、文档和 5 MB 以下的证据；安装包、运行目录、源码树和下载的工具都不进 git。提交邮箱是仓库本地配置的 GitHub 隐私邮箱。每次更新都要提交并推送（见“维护规则”，第 35、36 条）。

**AI 助手（2026-11-06～07，用户要求“尝试接入 AI 助手，先做接口”）：** `build/full-source-run-assistant-ball-r7`（宿主 `039299d6…`，构建 `free-ball-1`），现为回退。
- 第一版接口已完成：8 条带参数校验和来源标记的命令，编辑器命令由编辑器回报后才算完成；只记元数据的命令日志。没有接模型，也没有 MCP 服务。
- 交互形态是悬浮球：玻璃球图标、限制在窗口里的文档区域内任意摆放、只在文档页面显示；深色外观会让编辑器和文档页面一起变暗。
- 三组测试共 67 项通过；r7 真实编辑器复查 6 项全过（第 13 节第 29 条）。
- 2026-11-07 曾按用户要求设为默认入口（第 30 条），同日被本地 AI 版取代（第 33 条）。

**本地 AI（2026-11-07，用户要求“接入本地 Ollama，做好通用性适配，让小白也能快速使用”）：** `build/full-source-run-local-ai`（宿主 `3eb9a92a…`，构建 `local-ai-2`），**2026-11-07 起为默认入口**（第 33 条），已打进安装包 `…-20261107-2-win64.exe`（第 34 条）。
- 悬浮球接上本机 Ollama：自动检测、按状态给出“打开下载页 / 启动 Ollama / 下载推荐模型 / 重新检测”卡片；按内存自动选模型；流式回答。
- 模型把助手命令当工具调用；改文档、保存、新建都要用户点“执行”。只连本机回环地址，云端模型不列出。
- CTest 共 75 项通过；真实编辑器里用本机 `gpt-oss:20b` 完成“提问 → 确认 → 插入 → 保存”，6 项检查全过；连不上时显示启动卡片。见第 32 条。

**办公助手（2026-11-08，用户说“AI 的功能还是很拉，无法满足实际办公需求，要结合实际、从用户角度做”）：** 最新候选 `build/full-source-run-office-ai-r3`（宿主 `76ab8d1a…`，构建 `office-ai-5`），**还不是默认入口，也不在安装包里**。
- 常用活一键完成，不用写提示词：选中文字后润色、纠错、精简、扩写、改正式、翻译；没选文字时总结全文、提取待办、全文校对；起草通知、会议纪要、工作总结、邮件、请假条、活动方案。结果先给人看，再选“替换原文 / 插到后面 / 插入文档 / 复制 / 重写”。
- 编辑器新命令：读全文、替换选区（保留格式、核对原文）、带格式插入（Markdown 转成真正的标题、列表、表格）、查找替换。直接提问时自动带上文档内容和选区。
- 快慢：按显卡显存选模型和推荐下载；太慢时标“较慢”并说明原因、给下载按钮；“正在思考”计秒；发送键变 ■ 可停止；下载失败用中文说明（例如代理 Fake-IP）。
- CTest 共 84 项通过；编辑器脚本在真实编辑器里逐条验证过；真实会话验证了按钮随选区变化、“较慢”标记、计秒和停止。**完整的校对 / 润色 / 起草还没在真实编辑器里走完**：本机只有很慢的 gpt-oss:20b，推荐的 qwen3:8b 因代理 Fake-IP 没下载成功，用户说稍后自己调整网络。见第 37、38 条。

**内置 AI 引擎（2026-11-08，用户决定 Office 不再用 Ollama，改用软件自带的 llama.cpp；Ollama 只作可选）：** 代码已完成并通过测试，**还没有设为默认入口，也没进安装包**。详见第 13 节第 39 条。
- **模型位置（2026-11-09 用户决定，第 47 条）：** 模型固定下载到软件文件夹里的 `models`，设置页不能再单独改；要换盘就在“本地 AI 设置 → 软件位置 → 迁移到其他位置…”把整个软件（程序、编辑器配置、模型）一起搬走，搬完自动重新打开，快捷方式和卸载信息跟着改。开发版本由 `OPEN_OFFICE_MODEL_DIR` 指到 `dependencies/local-models/download`（`打开最新版.cmd` 等启动文件会自动设）。
- **对话（2026-11-09 用户要求，第 48 条）：** 每次启动软件、每打开或新建另一份文档，都自动开一个新对话；`/新对话` 或悬浮球里的“新对话”手动开；`/历史` 或“历史”列出以前的对话（标题、工作文件、时间、条数），可以打开（AI 接着记得）、重命名、删除（点两次）、删除其他所有。对话存在程序配置文件夹的 `conversations`，只在本机。输入 `/` 时在输入框上方弹出指令列表，可筛选、↑↓ 选、回车或 Tab 确认。
- **打字提要求（2026-11-09 用户反馈，第 49 条）：** 选中文字后打“详细点”“改正式一些”“翻译成英文”这类修改要求，由程序判定为“按要求改写”，先给结果再选“替换原文/插到后面/复制/重写”；“写一篇…”“帮我起草…”按“按要求起草”先给全文再“插入文档”；提问和其他话照旧对话。模型插入没标格式的 Markdown 时自动按格式插入；插入后的回答不再重复整篇。
- 最新候选：`build/full-source-run-builtin-ai-r8`（构建 `route-4`，CTest 101 项全过；2026-11-09 装配，第 49 条），在 r7 基础上加了上面的分流。
- 上一个候选：`build/full-source-run-builtin-ai-r7`（构建 `conv-4`，CTest 98 项全过；2026-11-09 装配，第 48 条），在 r6 基础上加了对话和 `/` 列表。
- 上一个候选：`build/full-source-run-builtin-ai-r6`（构建 `relocate-2`，CTest 95 项全过；2026-11-09 装配，第 47 条），在 r5 基础上加了上面的模型位置和迁移。
- 上一个候选：`build/full-source-run-builtin-ai-r5`（构建 `builtin-ai-6`，CTest 92 项全过，带 `ai-engine/vulkan` 和 `ai-engine/cpu`；2026-11-09 装配，第 45 条）。比 r4 多修了“改写结果行尾多两个空格”。`-builtin-ai-r4`（宿主 `05919c83…`，构建 `builtin-ai-5`，第 44 条）修了悬浮球快捷按钮被压扁（用户截图发现），没在真实编辑器里测过。`-builtin-ai-r3`（宿主 `7f5cbb10…`，构建 `builtin-ai-4`，第 43 条）含第 39 条最后修的“校对字段顺序”，但有按钮被压扁的问题。旧候选 `-builtin-ai`、`-builtin-ai-r2`、`-r3` 原样保留。
- 引擎：`dependencies/llama.cpp-b12202`（官方 Vulkan 版和 CPU 版，已核对 SHA256）。测试模型：`dependencies/local-models/download/Qwen3-1.7B-Q8_0.gguf`（1.83 GB，用户自己下载；侧边会话已与魔搭公布的 SHA256 核对一致，见第 40 条）。4B、8B 没下载（4B 下载时连接被重置）。
- 本机实测（RTX 5070 Ti Laptop 12 GB）：1.7B 模型 1.6–4.6 秒就绪，每秒约 170–185 个词；gpt-oss:20b 走 Ollama 时每秒不到 1 个词。显存空闲时，校对、润色、正式、总结每项连引擎启动 6–8 秒、每秒约 155 词（第 43 条）。
- **已知质量问题：** 1.7B 输出不稳定；显存空闲那次润色漏掉了“带好笔记本、认真听讲”一句和落款“行政部”（上一次没漏），改正式时加了原文没有的“笔和”“负责人”。值得做程序检查：结果比原文少了句子或改了实词时提醒用户（第 41、43 条）。

**找版本（2026-11-08，用户说 build 里版本太多、找不到最新版）：** 项目根目录的 `打开最新版.cmd`（最近构建的版本，可能是还没验证完的候选）、`打开稳定版.cmd`（默认入口，即 `scripts/build_installer.py` 里 `SOURCE` 指的目录）、`版本一览.cmd`（列出全部可运行版本，新的在上，标出“最新”和“稳定版·默认入口”）。见第 40 条。

**正在做（2026-11-09）：** `local_ai_smoke` 复查（第 41、43 条）、r3/r4/r5 装配和两处修复（第 43–46 条）、模型位置固定加迁移功能（第 47 条）已完成；r3 会话 `builtin-ai-session3` 的结果见第 45 条。r6 已装好（第 47 条），用户在 r6 窗口（会话 `builtin-ai-session4`，进程 35872）里试用时提出了对话方面的新要求（第 48 条），r7 已装好。接着用 r7 开会话 `builtin-ai-session4`（顺便请用户试一次迁移）：样本 `fixtures/results/office-ai-sample-6.docx`，用 r6 自带的 `ai-engine`，只设 `OPEN_OFFICE_MODEL_DIR`；本会话没有屏幕控制工具，由用户手动操作（全文校对 → 全部改正 → 选第三段润色 → 替换原文 → `/保存` → 关窗），之后用 `scripts/verify_office_ai_session.py builtin-ai-session4 --document fixtures/results/office-ai-sample-6.docx` 核对。开会话前先查显存（`nvidia-smi`）：用户另一个项目 `D:\claude视频` 会跑 Qwen3-TTS，占约 9 GB 显存，那时内置引擎只能用处理器。更早的记录：2026-11-06 已完成新建、另存为、同进程换文档：
- 宿主 `54b84083…` 通过两组 CTest（契约 21 项、界面 24 项）和 r2 真实编辑器复查（8 项全过）。
- 已设为默认入口，README 已改。

5 个 WASM 的状态（2026-11-05 已完成，默认入口在用）：

| WASM | 当前状态 |
| --- | --- |
| fonts | 已重建；WASM 与 asm.js 两版与官方差分测试全部一致，已在候选运行目录的真实编辑器里通过 DOCX 回归 |
| zlib | 已重建并修复（`out/zlib/pass8`）：补 `BUILDING_WASM_MODULE` 后不再触发空指针；zip 与图像结果与官方一致，WMF→SVG 与重建 fonts 逐字节相同（与官方 2022 版的差异来自源码演进） |
| spell | 已重建并通过（`out/spell/pass2`）：pass1 的崩溃是栈溢出（3.1.27 起默认栈 64KB），按 Emscripten 日志加 `-sSTACK_SIZE=5MB` 后，5 种语言拼写与建议词两版都与官方一致 |
| hash | 已重建并通过：两版与官方一致，摘要与 hashlib、密码哈希与 Python 独立实现一致（`hash-functional-pass1.json`） |
| drawingfile | 已重建并通过（`out/drawingfile/pass6`，`drawingfile-functional-pass2.json`）：46 个导出签名一致，两份自建 PDF 的渲染、文字、链接、目录、表单与外观、拆分/另存为、存储接口两版都与官方一致。边界：加载器 JS 模板取自官方生成文件（无公开源码），`drawingfile_native.js` 沿用官方 |

**下一步（待用户确定方向）：**
0. AI 助手（内置引擎，见第 39 条“接手先做”）：`local_ai_smoke` 复查和 r3–r6 装配已完成（第 41、43–47 条）；在真实编辑器里走完整流程（含迁移）并用 `scripts/verify_office_ai_session.py` 核对（进行中，见“正在做”）；补上 llama.cpp 的 MIT 许可证原文；之后问用户是否设为默认入口、重新生成安装包。再往后：把命令清单接成真正的 MCP 服务。
1. 在一台干净的 Windows 电脑（或虚拟机）上试装安装包；考虑代码签名。
2. XLSX/PPTX/PDF 的界面流程与回归：打开、新建（目前只开放新建文字文档）、保存、另存为。
3. 导出为其他格式（如 PDF）：编辑器自己菜单里的导出目前会被取消，见第 26 条。
4. 复杂文档兼容性与回归样本集；在默认入口上补测 PDF、拼写、图元在界面中的路径。
5. 许可证与源码交付清单（安装包已有，尚未正式发布）。

**不要做：**
- 不覆盖默认入口 `build/full-source-run-local-ai`，以及回退 `build/full-source-run-assistant-ball-r7`、`build/full-source-run-host-saveas-r2`、`build/full-source-run-wasm-candidate`、`build/full-source-run`、`build/native-engine-run`。样本 `fixtures/results/local-ai-*.docx`、`fixtures/results/office-ai-sample*.docx` 是证据，也不要覆盖。办公助手候选 `build/full-source-run-office-ai`、`-r2`、`-r3` 原样保留。
- 测试里不要点“下载 Ollama”“下载模型”这类按钮，除非用户批准。
- 不覆盖 `requirements.md`、`upstream`、官方 SDKJS。
- `build/full-source-run-host-saveas` 和 `fixtures/results/host-saveas-*.docx` 是回归证据，也不要覆盖。
- 不运行历史 before.py、openssl.py。
- 未经用户批准不下载新工具。

## 1. 接手时先掌握的结论

这是用户自己的 Windows 开源办公程序，底座已经选定 ONLYOFFICE v10.0.0。当前入口为 **C++17 / Qt 6 Widgets 自有宿主，嵌入真实 ONLYOFFICE 原生编辑视图**，不是启动外部官方成品，也不是用普通文本框模拟编辑器。编辑区使用 CEF 和 SDKJS 等网页技术，不改变“Windows 桌面程序、不做网页版产品”的目标。

**已完成并有证据：**Windows 原生核心、13 个格式静态库、V8、转换器、桌面核心、Qt 包装层和自有入口的源码构建；已有 DOCX 在真实界面中打开、中文编辑、原路径保存、退出，再以新进程和全新 profile 重开验证。

**尚未完成：**全部第一方引擎资产源码重建。5 个 WASM、10 个相邻生成 JS/asm.js 和 `spell.js.mem` 仍来自固定 SDKJS 上游预生成资产，其中包含 ONLYOFFICE 第一方实现；不能全部归为第三方库。CEF、Qt 是明确保留的第三方预编译依赖。这个最小 DOCX 里程碑也不等于已经完成接近 Microsoft Office 的完整产品。

**当前任务状态（2026-11-03 更新）：** 字体 WASM 已从源码重建并通过差分测试，其余 4 个正在按同样方法进行，进度见第 0 节和第 6 节。现有两个已验收运行目录没有改动。初版写于 04:53Z，当时没有启动编译、应用或回归测试。

## 2. 用户要求、阶段边界与建议

需求的权威原文是根目录 `requirements.md`。附录 A 直接嵌入本次读取的原始字节，保留原有【要求】【建议】及待决定标注。以下只是便于接手的摘要，不能取代附录或把建议升级为用户决定。

### 2.1 第一阶段 Office 要求

- 做自己的 Windows 桌面办公软件，第一版不含 AI；“完整”目标接近 Microsoft Office。具体功能差距和优先级尚需形成可核对清单。
- 开源、所有功能免费、文件兼容性高；包含文字、表格、演示及用户关注的图片/PDF 转换等能力。免费是产品目标，不能把未验证能力、收费 SDK 或外部服务当作已实现的免费功能。
- 不做微软 Office 插件；采用已经确定的 ONLYOFFICE 开源底座，保留来源标识、署名和法律文本。未通过关键门槛时先诊断此底座，不擅自切换 LibreOffice、Univer 或收费 SDK。
- 保留 Office 操作习惯：输入和光标响应、快捷键、选择和拖拽、功能区与菜单、撤销/格式刷/自动填充等细节。
- 视觉效果参考需求原文十条规范：明度分层、聚焦双环、深色字重、流式渐入、图标对齐、等宽数字、折叠动画、文本换行、弹簧动效、复制反馈。实现方式不限；经典 Office 手感加现代无边框外观是文档记录的可调整方向。
- 用户依赖 AI 编写代码，不能要求其自行手改来替代交付。原文写 Claude Code；后续用户已明确由当前 Codex 继续实际工程，保留原文而不擅改这段历史。
- 用户后来明确最低可运行验收是“启动、编辑文件、保存”，随后要求完成整个引擎源码编译。不能把下载源码、空壳窗口、接口存在或派发任务算作完成。

### 2.2 为 AI 预留接口，但不提前实现 AI

预留接口是明确要求；需求 4.1 的具体方案清单标为建议：统一命令通道、调用来源及前后值、撤销、稳定元素 ID/语义角色、主题令牌、排版度量、无界面渲染、本地样式日志。这些要逐项验证底座能力。

现有 `OfficeEngine` 是打开/保存/创建/关闭的异步宿主边界，**并不是所有文档编辑操作的统一命令总线**。不得把它描述为已完成 AI 接口、稳定元素模型、完整事务或来源追踪。缺少来源的事件应标 unknown，不能猜为人工操作。

### 2.3 第二阶段 AI 要求（大部分尚未实现）

2026-11-07 起已有本地 Ollama 对话和命令调用的候选版本（第 13 节第 32 条）；下列 MCP 服务、风格学习、记忆、多智能体仍未实现。

- 本地部署、低部署成本和资源占用，尽量让小模型也能完成任务。
- AI 通过专用 MCP 输出指令操作自己的编辑器，而不只是直接生成文件。
- 避免所有文档套同一模板；按文档主题参考用户常用颜色、字体、图表和形状，考虑带权重的候选方案及必要探索。
- 加入记忆、多智能体协同；后台分析 AI 修改后的人工修订、反复修改位置和稳定结果，学习用户风格。
- MCP 仅对外还是内置助手也走同一层，仍待决定。
- 学习信号表、稳定判定、至少 3 处/2 份文档或会话等阈值、衰减、团队/品牌优先级、临时记录升级规则都是建议或待调内容，不是已经实现或最终批准的契约。
- 文档建议只在本地记录样式而非正文，风格档案可查看、编辑、删除和暂停；建议由代码完成统计和检查，小模型做有限选择，先验证单智能体流水线再扩展多角色。这些不应在第一阶段悄悄落地。

### 2.4 用户在对话中的明确指令（按时间追加）

| 时间 | 指令 | 落实方式 |
| --- | --- | --- |
| 2026-11-01 前后 | 最低验收是自己的 Windows 程序能启动、编辑文件、保存；之后要求完成整个引擎源码编译 | 第 5 节证据 |
| 历史会话 | 禁止格式化；不得删除用户文件或清理失败目录；保护需求原文和已验收运行目录 | 第 7 节 |
| 历史会话 | 新工具、下载、协议、管理员安装需逐项批准，已有批准不能扩大解释 | 第 7 节 |
| 2026-11-03 | 回复全部用中文 | 包括摘要、问题和按钮 |
| 2026-11-03 | 按接手文档继续做 | 执行第 8.1 节 |
| 2026-11-03 | 批准下载：只下载固定版本 Emscripten 3.1.48 的 `wasm-binaries.zip`，按官方 SHA256 校验后解压到项目目录；复用已批准的 Python 3.11.8 和现有 Node 24，不装 Java，不改系统 PATH；先编译并测试字体 WASM | 已执行，见变更日志 |
| 2026-11-03 | 继续其余模块（zlib、spell、hash，再 drawingfile），全部通过后再统一装配候选运行目录 | 进行中 |
| 2026-11-03 | 维护一个 AI 接手文件，写进已完成、未完成、每次更改、用户要求和最终目标 | 本文即该文件，见“维护规则” |
| 2026-11-04 | 修复后装配（zlib 补 `BUILDING_WASM_MODULE`，再装配候选目录并做 DOCX 回归） | 第 13 节第 22–24 条 |
| 2026-11-05 | 把 WASM 候选目录设为默认入口 | 第 13 节第 25 条 |
| 2026-11-06 | 新建与另存为：做新建、另存为和同进程换文档，并做真实编辑器回归 | 第 13 节第 26 条 |
| 2026-11-06 | 第一次屏幕控制被拒后，用户选择“允许我控制屏幕”做回归；之后对第二版候选的再次申请被拒，没有再申请 | 屏幕授权只按次申请，不扩大解释 |
| 2026-11-06 | 复查后设为默认（r2 先做真实编辑器复查，通过后设为默认入口） | 第 13 节第 27 条；复查时用户批准了屏幕控制 |
| 2026-11-06 | 尝试接入 AI 助手，先做接口，做一个高级的艺术图案；随后选定：艺术图案指助手界面的视觉设计，先不接模型 | 第 13 节第 29 条；这是用户新指令，覆盖“第一版不含 AI”的历史安排，但仍未接任何模型 |
| 2026-11-06 | 样式和动效模仿苹果；后来澄清：只借鉴苹果的细节和设计，不模仿美术；要像苹果一样高级 | 悬浮球、连续曲率、模糊阴影、弹簧动效；图案为原创 |
| 2026-11-06 | AI 助手是一个球，不是侧边面板；历史对话要有背景；阴影不要僵硬，背景用连续曲率 | 第 29 条 |
| 2026-11-07 | 助手与背景的对比再强一点；暗色模式下文档页面太刺眼 | 第 29 条 |
| 2026-11-07 | 助手可以随意摆放、随文档页面显示；它被软件窗口限制住、不能移到窗口外，这是要求（中途一度被误解为缺陷） | 第 29 条：限制在窗口内的文档区域 |
| 2026-11-07 | 把 r7 设为默认入口 | 第 30 条 |
| 2026-11-06 | 问怎么打开软件、在哪找安装包；同意打成 zip 便携包；随后明确“要的是已经编译过的软件安装包” | 第 13 节第 28 条 |
| 2026-11-06 | 批准下载 Inno Setup 做安装程序：只从官网所链 GitHub 发布下载 6.x，校验签名后以便携方式装到项目目录，不要管理员权限、不改系统设置；把本机 VS 的 VC++ 运行库打进去 | 已执行 6.7.4（未用官网同时推荐的 7.x），见第 28 条 |
| 2026-11-07 | 做完后先尝试接入本地 Ollama；软件不是为这台电脑定制的，要做好通用性适配，让什么也不会的小白也能快速使用本地 AI 和这款软件 | 先用 r7 重新生成安装包（第 31 条），再做本地 AI 候选（第 32 条）；模型下载只在用户点按钮后进行，测试中没点 |
| 2026-11-07 | 把本地 AI 候选设为默认入口 | 第 33 条 |
| 2026-11-07 | 用新默认入口重新生成安装包 | 第 34 条：`…-20261107-2-win64.exe`，同日旧包保留 |
| 2026-11-07 | 把代码放入 git，在 GitHub 建仓库；选定公开 + AGPL-3.0、仓库名 `open-office-desktop`、代码和文档加小证据、GitHub 隐私邮箱 | 第 35 条 |
| 2026-11-07 | 每次更新记得同步到 git 和 GitHub | 写进“维护规则” |
| 2026-11-08 | AI 的功能还是很拉，无法满足实际办公时的需求，要结合实际来制作，在用户的角度看问题 | 第 37 条：办公助手（一键改写、全文、起草，结果先看再用） |
| 2026-11-08 | 好慢啊（看着全文校对等了几分钟） | 第 38 条：查明是模型放不进显卡；按显存选模型、慢模型提示、计秒、停止按钮 |
| 2026-11-08 | 同意下载 qwen3:8b；随后要求删除另一个中文很差的模型、新模型下到 D 盘 | 删除了 granite4.1:3b；Ollama 本来就设为 `OLLAMA_MODELS=D:\models`，没改任何设置；下载因代理 Fake-IP 失败，见第 38 条 |
| 2026-11-08 | 不要关闭 TUN，我自己稍后试试；跳过这步 | 没碰网络设置，跳过模型下载，验证改为只验证不依赖新模型的部分 |
| 2026-11-08 | 给我下载方式，我自己下，还有用中文 | 先按 Ollama 给了下载方法（错的，见下一条）；之后的回复全部用中文 |
| 2026-11-08 | 我不是说 office 不用 ollama 了吗 | 这条决定是在另一个会话（准备内置 llama.cpp 引擎）里说的，本会话之前不知道。撤回 Ollama 的下载方法，改给内置引擎测试模型的下载方式；“下一步”第 0 项已改 |
| 2026-11-08 | 侧边聊天说他已经告诉你了，你要不退回去按照他这个来 | 侧边会话的交接消息排在队列里，本会话重启时丢了，没收到。问用户后选定：保留办公功能，只把引擎换成内置 llama.cpp，Ollama 只作可选，加模型设置页（第 39 条） |
| 2026-11-08 | 赶快更新接手文档，我换个智能体继续做 | 写第 39 条并推送；未完成的事列在第 39 条“接手先做” |
| 2026-11-08 | （在侧边会话里）build 里版本太多，找不到最新版 | 侧边会话新建三个启动文件和 `scripts/open_runtime.py`，本会话核对后记入第 40 条并推送 |
| 2026-11-08 | 查看接手文档；同意装配 r3、卸载 gpt-oss:20b；截图指出悬浮球按钮显示有问题 | 第 42–44 条 |
| 2026-11-09 | 我记得我下载了一个模型，怎么没了 | 第 46 条：模型在 `dependencies/local-models/download`，软件按旧设置去 `D:/models` 找 |
| 2026-11-09 | 以后下载模型默认下载在软件本体所在的文件夹中，无法更改文件夹，但是可以去设置中迁移数据文件夹，将整个 openoffice 移动 | 第 47 条：模型固定在软件文件夹的 `models`；设置页去掉“更改…”，加“迁移到其他位置…” |
| 2026-11-09 | 有点问题：选中一段文字让它详细点，它却给了一大段完整的 md 字，还要在光标处插入，不符合需求 | 第 49 条：程序判定修改要求、改写结果先看再替换；写作要求走起草；Markdown 自动按格式；插入后不重复 |
| 2026-11-09 | 开新文件后上下文不会重置；新增开启新对话的指令；关闭软件后重新打开自动创建新对话；可以选择历史对话返回原来的对话；能看见对话所在的工作文件；能管理对话；打 / 后显示指令列表 | 第 48 条；另外按“开新文件后上下文不重置”的问题，打开或新建另一份文档时也自动开新对话（已告知用户，可改成只手动） |

## 3. 目录、代码与运行入口

| 路径 | 作用与边界 |
| --- | --- |
| `build/full-source-run-host-saveas-r2/OpenOffice.exe` | 回退：上一个默认入口（2026-11-06 至 11-07），安装包装的是它；在 `build/full-source-run-wasm-candidate` 基础上只换了宿主（新建、另存为、同进程换文档、中文提示按钮）；差异见目录内 `openoffice-host-candidate.json` |
| `build/full-source-run-wasm-candidate/OpenOffice.exe` | 回退：上一个默认入口（2026-11-05 至 11-06），在 `build/full-source-run` 基础上换用源码重建的 5 个 WASM；差异清单见目录内 `openoffice-wasm-candidate.json` |
| `build/full-source-run/OpenOffice.exe` | 回退：已验收的原生源码核心版本（WASM 为官方资产）；整个目录必须保留 |
| `build/native-engine-run/OpenOffice.exe` | 官方预编译核心的已验收回退版本；与源码核心版本分开 |
| `dist/OpenOffice-0.1.0` | 早期 MinGW 界面原型，未接通编辑引擎，不能当作当前成果 |
| `build/full-source-run-host-saveas/OpenOffice.exe` | 2026-11-06 的第一版宿主候选（修正中文按钮之前），回归证据；不是入口 |
| `app/main.cpp`、`app/ui/main_window.*` | 自有入口、窗口和打开/新建/保存/另存为/关闭状态；换文档前的保存提示 |
| `app/engine/office_engine.h` | 请求 ID、受理与完成区分、dirty 状态、能力声明（含 `saveAs`、`canCreate`）、关闭握手、编辑器发起的保存位置请求 |
| `app/engine/onlyoffice_engine_adapter_native.cpp` | 真实嵌入视图与 ONLYOFFICE/CEF 事件桥；每份文档一个 CEF 视图，新文档确认后才销毁旧视图 |
| `scripts/stage_host_candidate_runtime.py` | 从默认入口复制并只替换 `OpenOffice.exe`，装配宿主候选目录 |
| `dist/OpenOffice-Setup-0.1.0-dev-20261106-win64.exe` | 安装程序（2026-11-06），由 `scripts/build_installer.py` 用 `installer/OpenOffice.iss` 生成；`evidence/installer/` 是编译与测试记录 |
| `dist/OpenOffice-portable-win64-20261106.zip` | 便携压缩包，由 `scripts/package_portable_zip.py` 生成；`evidence/portable/` 是记录 |
| `installer/` | 安装脚本、自译的简体中文向导文字 `ChineseSimplified.isl`、安装前说明 |
| `dependencies/innosetup-6.7.4` | 用户批准下载的 Inno Setup 6.7.4（`download/` 原包，`install/` 便携安装） |
| `scripts/verify_host_saveas_session.py` | 只读核对新建/另存为回归留下的文件和事件日志 |
| `evidence/host-candidate/` | 宿主候选目录的装配记录与回归结果 |
| `build/full-source-run-local-ai/OpenOffice.exe` | **默认入口**（2026-11-07 起，第 33 条）：带 AI 助手悬浮球，能接本机 Ollama；只比 WASM 候选多一个新宿主，见目录内 `openoffice-host-candidate.json` |
| `build/full-source-run-assistant-ball-r7/OpenOffice.exe` | 上一个默认入口（2026-11-07 当天），现为回退：带悬浮球和 / 指令，不接模型；2026-11-07 的安装包装的是它 |
| `app/assistant/ollama_client.*`、`app/assistant/local_assistant.*` | 本地 AI：只连本机的 Ollama 客户端、按内存选模型、对话与工具调用状态机（第 32 条） |
| `app/assistant/assistant_commands.*` | 助手命令接口：命令清单、参数校验、来源标记、MCP 形式的工具清单 JSON、斜杠指令解析；不接模型和网络 |
| `app/ui/assistant_ball.*`、`app/ui/assistant_art.*` | 悬浮球窗口；玻璃球图标、光晕、执行中弧线、连续曲率圆角、模糊阴影、弹簧曲线（全部自绘） |
| `scripts/verify_assistant_session.py` | 只读核对助手会话：编辑器回报、命令日志不含正文、保存结果 |
| `app/ui/relocation.*` | 迁移：检查目标位置、算大小、内嵌的 PowerShell 搬家脚本（等程序退出、同盘改名或跨盘复制核对、改快捷方式和卸载信息、重新打开）、读取结果并给中文说明 |
| `app/assistant/conversation_store.*` | 对话存储：每个对话一个 JSON（消息、AI 记忆、工作文件、标题），原子写入；编号只能是固定格式，不会被当成路径 |
| `scripts/check_installer_relocation.py` | 用真实安装脚本给一个小的替身程序编译安装包，走“安装 → 迁移 → 从新位置卸载”和“安装 → 卸载”，证据在 `evidence/installer/relocation-check-*.json` |
| `docs/design/` | 助手图标和形状的真实渲染预览 |
| `app/engine/onlyoffice_engine_adapter.cpp` | 未启用原生集成时的不可用基线；不能冒充真实编辑 |
| `tests/native` | 引擎契约/UI 测试；不是 CEF 文档功能回归 |
| `upstream/DesktopEditors` | 保留的固定上游源码和六个子模块，build_tools 另有固定提交 |
| `build/full-source/DesktopEditors` | 原生编译使用的隔离源码与依赖，不要覆盖原 upstream |
| `build/full-source-host`、`build/full-source-qt6` | 自有宿主、Qt 包装层构建输出 |
| `build/full-source/DesktopEditors/core/build/lib/win_64` | 原生核心 DLL/静态库/导入库 |
| `build/full-source/DesktopEditors/core/build/bin/win_64` | 转换器等可执行产物 |
| `build/wasm-source/core` | WASM 隔离工作树，固定 core `ed63e94`，与 upstream 共用 git 对象库，不要在里面 fetch 或 commit。已加入历史包装、第三方源码和 FreeType 补丁副本；唯一被改的已跟踪文件是 `DesktopEditor/cximage/png/pnglibconf.h`（历史 before.py 的同款修改） |
| `build/wasm-source/history-075052dd` | 官方 core `075052dd` 的独立稀疏检出（383 个文件），只作恢复来源，脚本不执行 |
| `build/wasm-source/toolchain` | Emscripten 配置文件 `emscripten_config`、缓存和最小测试目录 |
| `build/wasm-source/out/<模块>/<标签>` | 各次 WASM 编译的目标文件、命令日志和 `deploy/` 产物；字体当前可用产物在 `out/fonts/pass2/deploy` |
| `build/wasm-source/test/` | 差分测试的输入、输出和新建的 zip |
| `dependencies/emscripten-3.1.48` | 用户批准下载的官方 Emscripten 3.1.48 工具包（`download/` 原包，`install/` 解压结果） |
| `scripts/check_wasm_history_abi.py` | 只读接口对照：历史配方、包装源码与当前 WASM 导出签名 |
| `scripts/wasm_toolchain.py` | 项目内 Emscripten 环境和最小测试 |
| `scripts/prepare_wasm_module_source.py` | 按模块准备隔离源码，替代历史 before.py |
| `scripts/build_wasm_module.py` | 按历史配方编译，含有官方依据的配方适配和后处理断言 |
| `scripts/wasm_fonts_harness.cjs`、`scripts/test_wasm_fonts.py` | 字体引擎官方版与重建版的功能差分测试 |
| `scripts/check_handoff_doc.py` | 校验本文附录 A 与 `requirements.md` 逐字节一致 |
| `evidence/wasm-source/` | WASM 重建的全部证据（下载、准备、编译、接口、测试） |
| `fixtures/generated/text-table.docx` | DOCX 回归输入样本 |
| `fixtures/results/full-source-docx-roundtrip.docx` | 已保存的源码核心 DOCX 回归产物，保留不要复用覆盖 |
| `evidence/` | 构建、运行、语义、来源和阶段结果 |
| `docs/feasibility/` | 路线、接口、许可、来源和进展说明；注意各自日期 |

正常启动（默认入口）：双击 `build/full-source-run-local-ai/OpenOffice.exe`，选择现有 DOCX，点击“打开所选文件”，等待就绪；编辑后点顶部“保存”，等待完成，再用窗口关闭按钮退出。也可以新建、另存为，并在同一窗口打开另一份文档，操作见 README。下面的命令和 profile 说明是 2026-11-03 针对 `build/full-source-run` 写的，保留原样。

```powershell
Set-Location -LiteralPath 'D:\open office'
& '.\build\full-source-run\OpenOffice.exe'
```

默认 profile 是 `build/full-source-run/openoffice-profile`。保留整目录，不能只拷贝 EXE；当前仅在本机验收，尚未完成安装包与干净 Windows 迁移验证。本轮没有再次执行上述启动命令。

## 4. 固定源码与工具链

| 仓库 | 固定提交 |
| --- | --- |
| DesktopEditors v10.0.0 | `daf8c6948148827201b2907ac1134d222fbfa7b4` |
| core | `ed63e94de814ff1d4ea28cddfaba6939bc9df9ed` |
| desktop-apps | `332c1905a5361dd4139481827b7ad4f4a9facd85` |
| desktop-sdk | `ae6cf2a039355183c512fc314f7a9bd1a05a6a11` |
| dictionaries | `d3223bbb777883db66ac3cd249f71c6ebdc992c7` |
| sdkjs | `9d2138866d929be32f1d107e8b5bfae36156e097` |
| web-apps | `f79972a400c3a696490094961772d1f86fac1b82` |
| build_tools | `43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda` |

依据 `sources.lock.json`、源码 checkout 证据及隔离构建记录。根 lock 文件还保留早期 `built_locally:false` 等阶段字段，它锁定版本，不是最新构建状态的唯一来源；最终进度由 full-source 验收记录判断。

原生工具：MSVC 14.44.35207、Qt 6.11.1 `msvc2022_64`，Qt 路径 `D:\Qt\6.11.1\msvc2022_64`。Qt SDK 是手动归档部署，未注册到 MaintenanceTool，原 MinGW 保留。主核心 SDK 28000、V8 SDK 26100 是既有记录，不能随意混用。CMake/Ninja/Jom 见既有构建脚本和环境证据。

已经单独获准的项目内工具包括 Strawberry Perl 5.40.0.1 portable、官方 depot_tools/CIPD/GN、Python 3.11.8；Python 2.7 只曾有条件授权，记录显示未安装。Qt MSVC2022 与 Multimedia 已批准并部署。不得把这些许可扩大解释为所有新工具、管理员安装、协议接受或系统设置修改都已获准。

V8 为 8.9.255.25，最终 monolith 已编译和测试。保留中文 `/showIncludes` 依赖前缀修复及 `v8.pri` 的 `PRE_TARGETDEPS`，避免头文件变化漏编或 doctrenderer 未重链最终库。AGG C++17、OpenSSL、第三方适配等以归档为准，不盲目重复打补丁。

`evidence/full-source-patches-final/` 记录 8 个主工作树、37 个依赖克隆、45 份补丁（19 份非空、41 个 tracked 文件）。已做反向可应用性与哈希检查，不代表在全新机器完整重放过。此归档不是完整离线 SDK，未穷举 untracked/generated 输入；接手需同时保留下载包、依赖清单、脚本和 receipts。

## 5. 已完成工作的可核验证据

| 已验证范围 | 证据及限制 |
| --- | --- |
| 原生核心和格式库构建 | `docs/feasibility/full-source-progress.md`，`build/full-source/module-runs/` 下各 result.json；包括格式、绘制、文档模块及最终 V8 重链 |
| V8 编译、JS/Intl/Wasm 测试 | `build/full-source/v8-evidence/monolith-smoke.json`；旧字段中的 1137/1682 不是当前仍在编译的证据 |
| SDKJS 与网页资源生成 | `evidence/full-source-js-sdk-result.json`、`full-source-web-build-result.json`；构建进展记录 419/19,861 文件，但没有重新编译五个 WASM |
| 自有宿主 | `scripts/build_full_source_host.cmd` 对应日志、两组 CTest 通过；20/17 QtTest 条目包含初始化与清理，不是 37 项业务功能验收 |
| 运行目录与 DLL 来源 | `build/full-source-run/openoffice-full-source-stage.json`，`evidence/full-source-runtime-probe-pass1.json`；20,970 文件、76 DLL 核验、21 目标实际加载 |
| DOCX 全流程 | `evidence/full-source-docx-roundtrip-result.json`；主 `docx_roundtrip_verified:true`，两个独立进程均退出 0 |
| 保存后的内容 | `evidence/full-source-docx-saved-semantics.json`；只增加一个标记段落，原文及 2×2 表格保留 |
| 独立配置重开和进程记录 | `evidence/full-source-gui/docx-first/`、`docx-reopen/` 中 session、launch-report、模块路径、stdout/stderr 和原生事件 |
| 默认首页 | `evidence/full-source-gui/default-home/result.json`、`home.png`；首页可选文件、退出 0。图片仅为 QWidget 首页，不是单独保存的原生文档截图 |

验收报告中的 nested `first_run/reopen_run.document_actions_verified:false` 或 `roundtrip_verified:false` 是启动器自身未验证文档操作的标记；主验收另外汇总 CUA 实际操作观察及 OOXML 语义检查，`launcher_scope_note` 已解释。既不能用启动成功代替编辑成功，也不能脱离字段作用域否定主验收。

本轮重新读取并核验的关键文件：

| 文件 | 字节 | SHA256 |
| --- | ---: | --- |
| `build/full-source-run/OpenOffice.exe` | 211456 | `06440c570995b41d4a1d24e6a65856f2c3c1d6a2611b5806831674e6a6db474d` |
| `build/native-engine-run/OpenOffice.exe` | 209408 | `75f5a57dc9ac46919af8933a7b2e23b3c021e78cda315ca4d46ef60d05e1dcc7` |
| `fixtures/results/full-source-docx-roundtrip.docx` | 25872 | `93b5853848245249f92862f7b632077af6177f13cbc98bb0a2991a93c051dfbe` |

样本新增段落是“自产原生核心编辑保存验证 2026-11-02”。这是合成测试样本，不能外推复杂分页、公式、图表、修订或全部 Office 文件保真。历史 `final-process-check.json` 在 2026-11-02 12:00:01 UTC 记录 0 残留进程；本轮当前进程查询因执行连接断开未完成。

CEF 109 与 Qt 没有在本项目从源码重编。字体、空白模板等非代码资源按装配白名单使用。ICU 74、V8 ICU 68.1 及 CEF 的数据位置不同，不可互相覆盖。SDKJS 未做 JS 压缩、部分预生成输入仍存在，完整来源边界见装配与审计文档。

## 6. 尚未完成的五项 WASM 与后续源码工作

下列路径相对于 `build/full-source/DesktopEditors/sdkjs/deploy/sdkjs`；本次重新计算的五份文件哈希均与旧来源审计一致，仍是上游资产。详细 SHA256 见 `evidence/full-source-wasm-provenance.json`。

| 相对路径 | 字节 | 主要作用 |
| --- | ---: | --- |
| `common/hash/hash/engine.wasm` | 65115 | 普通/Office 相关哈希 |
| `common/libfont/engine/fonts.wasm` | 3614021 | 字体解析、字形塑形及合并的 ZIP/图像/断词接口 |
| `common/spell/spell/spell.wasm` | 510644 | Hunspell 拼写检查 |
| `common/zlib/engine/zlib.wasm` | 1617419 | 压缩包及图像处理 |
| `pdf/src/engine/drawingfile.wasm` | 10868120 | PDF/绘制文件读取、渲染、编辑等 |

总计 16,675,319 字节；还要处理 10 个生成 JS/asm.js 和 `spell.js.mem`。普通桌面 DOCX 使用字体 WASM 的结论来自静态调用链审查，不是本次运行时逐模块加载追踪；最小 DOCX 回归不能证明这五组所有功能都经过测试。

**2026-11-03 更新：**
- 五组历史配方、`Common/js` 生成器和各包装源码已从 `075052dd` 恢复到 `build/wasm-source/history-075052dd`，并按模块补进 `build/wasm-source/core`。
- `075052dd` 中没有 `PdfFile/Wasm` 目录。PDF 导出层的历史版本是 `DesktopEditor/graphics/pro/js/wasm/src/drawingfile.cpp`。
- 字体模块已用 Emscripten 3.1.48 编译成功，差分测试全部一致。结果和证据见审计文档 `docs/feasibility/full-source-wasm-provenance.md` 的“2026-11-03”两节。

（初版记录：当时这些都还没恢复，也没有 emsdk/emcc。）

历史恢复来源是官方 core `075052dd3f41373affa533317d6734a0d34e0897`；相关目录后来在当前版本中删除。历史配方可作为恢复依据，不等于当前预编译资源的准确生成源码，也不保证字节可重复。

- **PDF：**旧导出缺 `_BuildPdf`、`_ClearFontsStorage`、`_ClearImagesStorage`、`_GetMacros`、`_IsImageBinaryExist`、`_SetImageBinary`。`evidence/full-source-wasm-upstream-recheck.json` 记录当前核心已有 BuildPdf、GetMacros 和图像存储实现；两个 Clear 需依据存储与缓存语义恢复包装。此前只读研究还发现 `SetScanPageFonts` 参数/返回值改变、OFD 依赖新增。不能用同名导出数量当 ABI 兼容证明，也没有证据据此断言底层功能源码未公开。
- **字体/压缩：**历史导出名称与当前 SDKJS 对应，仍需恢复共享 FreeType 准备目录、`wasm_jmp.cpp`、raster 包装和 Hyphen wrapper；仅名称匹配不等于功能通过。
- **hash：**旧 Windows OpenSSL 准备脚本会直接退出；应在隔离副本准备真实头文件，按配方逐源编译，不把退出码 0 当成功。保留全部算法并核对当前 JS 的 hash/hashOffice API。
- **spell：**保留当前 SDKJS 外围 adapter、语言表、Worker 错误处理和回退。旧准备脚本可能删除依赖，不能原样运行；仅在隔离副本重放经审查的补丁。
- **共同后处理：**旧脚本会依赖编译器输出的字符串替换并删除中间文件；必须改成可核验、保留失败产物的流程，对替换次数及初始化回调做断言，不静默跳过或删掉兼容分支。

**2026-11-03 进度：**
- **字体/压缩那一条：** 字体模块已完成。`wasm_jmp.cpp`、raster 包装和 Hyphen 包装都已恢复。共享 FreeType 副本按 9 个历史补丁准备，每个都断言只匹配一处。
- **配方适配：** 字体配方补了 `APP14cmyktorgb.cpp`，依据是官方 `DesktopEditor/graphics/pro/raster.pri:268`。
- **zlib 配方：** 比字体配方旧，引用的 `EmfClip.cpp`、`EmfPath.cpp`、`WmfClip.cpp` 在 `075052dd` 本身就不存在，编译时需要按官方构建文件适配。
- **后处理：** spell、zlib、hash 的历史后处理写的是旧版 Emscripten 的 `function getBinaryPromise(){`，而 3.1.48 生成的是 `getBinaryPromise(binaryFile)`。需要按新写法适配，并记录替换次数。
- **其余：** hash、spell、PDF 仍未编译。另有 `path-boolean-min.js` 等预生成资源边界，见审计；处理完这五项不等于完整前端依赖溯源全部结束。

## 7. 工具授权、执行连接与保护约束

**2026-11-03 更新：用户已批准并执行下面的最小方案。**
- 只下载了 `wasm-binaries.zip`，441,957,173 字节，SHA256 与固定 emsdk `revisions.bzl` 一致。
- 解压到 `dependencies/emscripten-3.1.48/install`，共 9,258 个文件。包里自带 node_modules，其中有 Closure 的 Windows 原生程序，所以不需要 Java，也没有另装 npm 依赖。
- 没有 clone emsdk，没有运行 `emsdk install/activate`，没有改系统 PATH。
- 环境变量（`EM_CONFIG`、`EM_CACHE`、`EMSDK_PYTHON`）只在子进程里设置，由 `scripts/wasm_toolchain.py` 统一提供。
- 最小测试 5 项全部通过，见 `evidence/wasm-source/toolchain-smoke-pass1.json`。

除此之外没有新增授权。以后若需要其他工具，仍须先列明再请用户批准。

初版记录的方案（已按此执行）：

1. 项目内部署 **Emscripten 3.1.48** 配套 LLVM/Binaryen/wasm2js。固定 emsdk 提交 `f677ef915645c09794f0ae88f21d3cba2886459f`，构建 ID `694434b6d47c5f6eff2c8fbd9eeb016c977ae9dc`。
2. 官方包：[Windows wasm-binaries.zip](https://storage.googleapis.com/webassembly/emscripten-releases-builds/win/694434b6d47c5f6eff2c8fbd9eeb016c977ae9dc/wasm-binaries.zip)。官方 SHA256：`9276435ea7c402c18572a4301d6a26426eac73414b0ed5cb3e721044a50f651d`；依据 [固定校验表](https://github.com/emscripten-core/emsdk/blob/f677ef915645c09794f0ae88f21d3cba2886459f/bazel/revisions.bzl)。这是此前官方只读核验结果，不是已经下载校验过本地包。
3. 缺失时补齐该版本官方 lockfile 的生产 npm 依赖，包括 **Closure Windows 原生版 20230802.0.0**；保留原有编译级别、ECMASCRIPT5_STRICT 与许可头处理。固定依据为 [Emscripten 3.1.48 lockfile](https://github.com/emscripten-core/emscripten/blob/3.1.48/package-lock.json)。
4. 计划复用已批准的 Python 3.11.8 和现有 Node 24；版本检查满足要求，但实际 smoke 未运行。历史默认 SDK 依赖 Node 16.20/Python 3.9.2/Java 8.152，不是已经证明都必须另装；历史 min.py 用 Java，计划改用同版 Closure 原生程序后可避免新增 Java，仍需实测。
5. 只用项目内工具、独立 EM_CONFIG 与缓存，不运行 install latest、不永久激活、不改系统 PATH。若出现不同工具、额外权限或协议要求，应列明后再处理。

这是此前会话中的后续研究方案；旧落盘审计仍保留默认依赖清单及 Closure 版本未定的历史结论，需按时间区分。以上没有替用户作出批准。

用户已明确禁止格式化；不得删除用户文件、清理失败目录、覆盖需求或已验收运行目录。保护 `requirements.md`、原 upstream、`build/full-source-run`、`build/native-engine-run` 和现有证据；不得擅改账号、网络或安全配置。普通源码获取与既有工程授权不等于付费、发布、管理员安装或新协议授权。

执行连接会间歇返回 `exec-server transport disconnected`；这不是 DNS 错误，也不是自动审批拒绝。遇到结果不确定先查目录/进程/日志，不重复启动或覆盖。当前正文编写前的小型 shell 连接测试成功，当前进程清点未完成。

此前 Library 传输未完成。本轮核查两个 `openoffice-library-transfer-*` 临时目录和 `.library-handoff-6gd98x4t` 均为空，未删除。本文由项目现有材料直接生成，不声称与 Library 文件 ID `libfile_e58b050db29c8191a6996541a41334c7` 的最新版内容一致，也不声称已经回写 Library。

## 8. 未完成清单与接手顺序

### 8.1 下一工程目标：补齐源码资产

1. **【已完成 2026-11-03】** 先读取本文、当前需求、最新验收 JSON、WASM 来源审计及补充复核。检查连接、当前进程、工具与工作树状态，复用已有成果，不重新下载八个完整仓库。
2. **【已完成 2026-11-03】** 在 `build/wasm-source` 隔离目录恢复官方历史包装、公共脚本和依赖，逐项对照当前 core 与 SDKJS ABI；保持已验收目录原样。
3. **【已完成 2026-11-03】** 在收到上述新工具明确授权后，按固定官方来源校验部署，先做编译器、Node、WASM 与 asm.js smoke。独立记录命令、版本、源提交、补丁、哈希和失败日志。
4. **【基本完成 2026-11-04：5 个模块都已重建；fonts、spell、hash、drawingfile 差分通过；zlib 仅 WMF→SVG 有源码演进差异和一处上游空指针缺陷待定】** 分模块编译并同步生成 JS/回退资源；不得用 stub、删功能、旧版不匹配产物或回填上游预编译文件掩盖失败。
5. **【已完成 2026-11-05：候选目录 DOCX 回归通过，经用户同意设为默认入口】** 模块测试通过后在新的候选运行目录装配，记录来源清单，再执行 DOCX 基本回归和受影响路径测试。完成后才更新整体状态与默认入口，不覆盖当前回退。

### 8.2 产品与扩展能力仍未完成

- 新建、另存为、同进程替换文档：DOCX 已完成并回归，已是默认入口（第 13 节第 26、27 条）。多文档会话（标签页、多窗口）未做；新建表格、新建演示未开放。
- 导出为其他格式（PDF、ODT 等）：编辑器自己菜单里的导出会被宿主取消，只支持同格式另存为。
- XLSX/PPTX/PDF 的宿主界面流程和真实打开—修改—保存—重开；底层库编译成功不代表这些交互已经验收。
- 打印、多媒体、图片/PDF 转换、OCR、离线依赖及质量逐项验证。
- 复杂 DOCX 及三类格式的广泛兼容、版式/分页/公式/图表/字体/多显示器验证；需保留输入输出与参考基线。
- 干净机器运行、许可/署名与对应源码交付清单、代码签名、正式发布。安装/卸载包已有（第 13 节第 28 条），只在本机测过。当前没有发布完成声明。
- 全操作命令通道、稳定 ID、语义角色、主题令牌、排版度量、headless 导出、撤销事务、来源与前后值实验。
- 第二阶段 AI：命令接口和悬浮球已有第一版（第 13 节第 29 条）；模型、MCP 服务、风格档案、记忆、后台学习与多智能体都还没做。

`docs/feasibility/editor-interfaces.md` 的 IF-01 至 IF-06 是实验计划/源码依据，不能因为 CLI 或头文件存在就记为通过。桌面插件、Docs Automation API、Document Builder 是不同产品边界，不得默认收费接口免费可用。

## 9. 后续验收与安全复现

下列是接手后的操作说明，本轮均未执行。

只读复核已有 DOCX 样本语义：

```powershell
Set-Location -LiteralPath 'D:\open office'
python -B scripts/verify_full_source_docx_sample.py
```

该脚本只输出结果，验证正文和表格；它不启动 CEF，不代表新的一次界面回归。实际新回归应使用原始样本的新副本、新证据标签和独立 profile，避免覆盖现有保存结果。

既有宿主重编入口是 `scripts/build_full_source_host.cmd <新的标签>`。既有 GUI 记录器为 `scripts/run_full_source_gui_session.py <新的标签> --document <测试副本完整路径>`。运行前阅读脚本、确认标签和目标均未占用，记录实际手动/受支持 GUI 自动化观察；启动器 false 标记的作用域见第 5 节。

`scripts/stage_full_source_runtime.py` 已完成当前运行目录装配，默认拒绝覆盖。不要对已有目录盲目重跑或删除后重来；接手新增 WASM 候选需要先审查新的装配目标方案。详见 `docs/feasibility/full-source-runtime-staging.md`。

| 验收层次 | 至少需要的证据 |
| --- | --- |
| 来源 | 新资产来自哪个固定源码、依赖、补丁、命令；实际产物哈希；无旧核心偷偷回填 |
| WASM 与 JS | 导出名称、参数/返回值、初始化与内存管理；加载错误处理；asm.js/JS 回退行为 |
| hash | 已知算法向量、盐/迭代、空值、中文和非 BMP 字符、Worker 消息 |
| spell | 实际词典加载、正确词/错词与建议、Unicode、移除与重复创建销毁 |
| fonts/zip/image | 实际字体与塑形、归档往返和字节比较、图像路径及异常输入 |
| PDF | 打开、渲染、生成/编辑、保存重开、字体/图像缓存生命周期、新 ABI；不执行文件内宏来证明读取接口 |
| DOCX 回归 | 新进程/新 profile 打开、输入中文、保存、退出码与关闭事件、独立 XML/ZIP 检查、再次重开观察 |
| 完整 Office | 三类文件的结构与视觉、复杂样本和真实参考输出；小样本不外推全面兼容 |

失败要保留输入、日志和未完成清单。测试替身、编译退出码、进程启动、截图、保存文件和来源核验分别证明不同事情，不能互相替代。

## 10. 文档时效、许可与冲突处理

- `TASK.md`、`docs/feasibility/STATUS.md` 保留 2026-11-01 早期阶段；2026-11-02 full-source 增补和实际验收是原生源码阶段依据。
- `sources.lock.json` 中旧的 built_locally 状态、状态 JSON 中 V8 中间进度都不能覆盖最终完成证据。本轮不修改这些历史文件。
- WASM 初始审计的“旧 PDF 配方缺六接口”仍属实，但后续已找到对应核心实现；不能升级成“必要源码未公开、无法完成”的结论。构建成功与兼容测试仍未发生。
- `licensing-and-features.md` 是 2026-11-01 的固定版本研究，指出需求原文中关于旧 logo 条款/争议的描述不能直接当作当前精确法律要求。保留来源、法律文本、修改记录；品牌方案、独立商标政策全文及全部依赖许可仍需核对。本文不新增法律结论，不把产品宣传当作本机功能或许可验收。
- 用户最新明确指令优先于历史建议；需求变动应明确记录，不能静默修改附录或把产品目标写成已交付。

## 11. 本文校验方式

本文仅新建根目录接手文档和独立验证记录。写入使用排他创建：若目标已存在即停止，避免覆盖。需求附录以 `read_bytes()` 读取后直接拼接，不经编码、换行或格式转换；写入后从边界标记提取，再与当时及再次读取的 `requirements.md` 做字节比较。

生成时的输入文件 SHA256、读取时间、附录偏移/长度、最终文件 SHA256、检查结果保存在同次 `evidence/ai-handoff-verification-*.json` 中。最后的校验记录才能证明落盘成功；正文中的历史工程通过项仍指向各自已有证据，不伪称本轮重跑。

**持续维护后的校验（2026-11-03 起）：**
- `evidence/ai-handoff-verification-20261103T045307436201Z.json` 只证明初版文件（SHA256 `67136086…e19`）的生成过程；本文改动后，其中记录的整份文件哈希不再适用。
- 此后每次修改，用 `python -I scripts/check_handoff_doc.py` 校验附录 A 与 `requirements.md` 逐字节一致。正文改动记录在第 13 节。


## 12. 本次输入文件指纹

以下是 2026-11-03T04:53Z 初版生成时的文件快照，用于识别后续变化，不代表重新执行了其中的构建或验收。之后改过的文件（如 WASM 来源审计）以第 13 节记录为准。

| 路径 | 字节 | SHA256 |
| --- | ---: | --- |
| `requirements.md` | 14275 | `1e7d43e339deb30766561fb315e87585ea3bb06c32cb11678f84059d379c19ff` |
| `README.md` | 4767 | `0d557e140330782aad10b8cfe1a36b2ac48dd7216bab93eda202b8ce64edeea0` |
| `TASK.md` | 8789 | `e846a975888f2adfd9255841adb345098d4298db75f74acf33045726a4f7af5c` |
| `sources.lock.json` | 3799 | `e2bddfeef79cf14bdd9b71ad467d537567c77f404dff5ba5ae80725489e2f069` |
| `docs/feasibility/STATUS.md` | 11897 | `d32c4119c0e30172a51f5d81f3bae02ad5593fedad42f7df1c41656fd1a74e23` |
| `docs/feasibility/full-source-progress.md` | 4319 | `7a6732dac8f2c473fe583490774f7eb8d788af19cd57f18ad7e568ba70195e17` |
| `docs/feasibility/full-source-runtime-staging.md` | 4275 | `c0243bdb805cf27353d8caa6c382930c73091c56dcee1f2d891f809374047fb0` |
| `docs/feasibility/full-source-wasm-provenance.md` | 14684 | `435fbb325aae8573ae0a978e2f7dfc8573325cdd660bd32b53600e3cab294779` |
| `docs/feasibility/licensing-and-features.md` | 6017 | `207d92e4e0373df1975c0bfd57ec147d03632a62f8878bf58f8ed9cd4905ffd4` |
| `docs/feasibility/editor-interfaces.md` | 11301 | `ad3822570df3b9d52b9831d9f269ee49a527da908f51e22a20ae8e8ee0146f57` |
| `evidence/full-source-build-status.json` | 3180 | `8a818db708adf3e89525b22c933124553b5d7c0103d3207b2958c8a456a50074` |
| `evidence/full-source-checkpoint-current.json` | 1224 | `77b5163711d67624bd7d49a730505c31fa3824d06101f8fac5122d21aeebc311` |
| `evidence/full-source-docx-roundtrip-result.json` | 7137 | `2accb6546e11b009d4fa32aa543c49932318d6345dd9ec932259576da70d909e` |
| `evidence/full-source-docx-saved-semantics.json` | 1484 | `0ebc548868ddf71928afe3ade3d0e55c418e329492006ee64c579dd8a2692b0c` |
| `evidence/full-source-runtime-probe-pass1.json` | 59429 | `51e63b3d71abaab50c933d7d1c0f2f938b0622f44099260db9275236f900f19f` |
| `evidence/full-source-wasm-provenance.json` | 150735 | `6a241d06deb5498997991a9ee24a4f55c1e47de8213a1de6b7d533a85f8f5067` |
| `evidence/full-source-wasm-upstream-recheck.json` | 2145 | `d8d01c9f962f1c06a26d4a6f872952e6814322e2227feb3191e86b30dc36b4c2` |
| `evidence/full-source-patches-final/README.md` | 3682 | `bf5e9354452245fa7e77c525d5e969f2e552191fe979eb49c2593e9810f19f39` |
| `evidence/full-source-patches-final/manifest.json` | 67691 | `a45b5db26c0535f19cbba727f09d90112aa9fab1083c15de974c4104f3e6e192` |
| `evidence/full-source-gui/default-home/result.json` | 643 | `06423c20808102c7556bd849f667f7dc1ce06fcd9afdc25cb8bb756038f62e32` |
| `evidence/full-source-gui/final-process-check.json` | 103 | `47aefd4ff9dfe1bc106b16026be4cf48f0bd2187f065371566e59adf8e3944be` |
| `app/engine/office_engine.h` | 3409 | `4b4b55a2d15760fff67147f68e0591431642afd5dcb1d43b42a716cb8df4478d` |
| `scripts/verify_full_source_docx_sample.py` | 1505 | `a2e919671149bfb31e2af7ce2a9793cb786e6b49cd4ce13f20065fb2fc73b0ea` |

## 13. 变更日志（只追加，最新在最后）

每条写明：时间、执行者、做了什么、改了哪些文件、证据在哪、结果。

**2026-11-02，Claude Code**
- 只读复核上游 WASM 配方：固定 core 就是上游 master 当前 HEAD；PDF 新接口中有 4 个在 `doctrenderer/drawingfile.h` 有实现。
- 新增 `evidence/full-source-wasm-upstream-recheck.json`，审计文档加“2026-11-02 上游复核补充”。

**2026-11-03 04:53Z，Codex**
- 生成本文初版和 `evidence/ai-handoff-verification-20261103T045307436201Z.json`。

**2026-11-03，Claude Code**
1. **恢复历史源码：** 用户要求“按照接手文档继续做”。用独立稀疏仓库恢复官方 core `075052dd` 的 383 个文件到 `build/wasm-source/history-075052dd`，没有动 upstream 的对象库。
2. **接口对照：** 新增 `scripts/check_wasm_history_abi.py`，生成 `evidence/wasm-source/history-abi-check-20261103.json` 和 `history-checkout-075052dd.json`。
   - 结论：fonts、zlib、spell、hash 的接口与历史包装一致。
   - PDF 的 6 个新接口在公开源码中都有对应；两个 Clear 接口可以由 `GetFontsStorage()`、`GetImagesStorage()` 加 `Clear()` 实现；`SetScanPageFonts` 改为 `(pFile, int* pages, int count)` 并返回指针。
   - 审计文档加“2026-11-03 历史包装恢复与接口对照”。
3. **下载工具链：** 用户批准下载 Emscripten 3.1.48，范围见第 2.4 节。下载、校验、安全解压到 `dependencies/emscripten-3.1.48`；记录在 `evidence/wasm-source/emscripten-3.1.48-download.json`。
4. **最小测试：** 新增 `scripts/wasm_toolchain.py`，5 项全部通过（`evidence/wasm-source/toolchain-smoke-pass1.json`）。
5. **准备字体源码：** 新增 `scripts/prepare_wasm_module_source.py`。字体模块：补回 83 个历史文件，复制 HarfBuzz、Brotli、hyphen，应用 9 个 FreeType 补丁，改 `pnglibconf.h` 一处（`evidence/wasm-source/fonts-prepare-pass1.json`）。
6. **编译字体模块：** 新增 `scripts/build_wasm_module.py`。
   - pass1 链接失败：缺 `ima_jpeg_cmyk_to_rgb`（`fonts-build-pass1.json`）。
   - 依据官方 `raster.pri:268` 加配方适配 `APP14cmyktorgb.cpp` 后，pass2 成功（`fonts-build-pass2.json`）。`fonts.wasm` 3,641,996 字节，50 个导出签名、83 个导入与官方一致。
7. **字体差分测试：** 新增 `scripts/wasm_fonts_harness.cjs`、`scripts/test_wasm_fonts.py`。
   - 第一次（标签 pass2）是校验脚本自身出错，没有生成证据；pass3、pass4 也是测试脚本的问题（`GetPaths` 长度含头、空文件期望），证据保留，结论为未通过。
   - pass5 全部通过（`evidence/wasm-source/fonts-functional-pass5.json`）：FreeType 3,628 次字形加载、HarfBuzz 5 组、4 张图片、3 种断词词典、zip 读写，WASM 与 asm.js 两版都与官方一致。
   - 顺带发现：官方版和重建版的 `Zlib_GetPaths` 都不列空文件、都不设 UTF-8 文件名标志。
   - 审计文档加“2026-11-03 字体 WASM 源码重建与差分测试”。
8. **准备其余模块：** 用户要求“继续其余模块”。准备脚本改为按模块配置并可在同一棵树上重复执行，新增 zlib、spell（验证原生树 Hunspell 的改动与历史 before.py 逐字相同）、hash（OpenSSL 1.1.1f + Perl Configure linux-generic32）配置。尚未运行。
9. **改造本文：** 用户要求维护一个 AI 接手文件。本文改为持续维护版：新增维护规则、第 0 节速览、第 2.4 节用户指令表、本节变更日志，并更新第 1、3、6、7、8、11、12 节中过时的说法。新增 `scripts/check_handoff_doc.py`。
**2026-11-03（新会话），Claude Code**
- 用户要求查看接手文档和文件夹，了解进度和目标。只读核对磁盘状态，没有编译或运行程序。
- 更正第 0 节：zlib 准备其实已在 10:45:33Z 运行过（`evidence/wasm-source/zlib-prepare-pass1.json`），上一条日志写成“尚未运行”是因为文档先于这次运行落盘。结果：60 个历史文件已存在、0 个新增、无第三方复制、无改动，已跟踪改动仍只有 `pnglibconf.h`。zlib 尚未编译；`build/wasm-source/out/` 下仍只有 fonts。

**2026-11-03 至 11-04，Claude Code（主会话，续第 9 条）**
10. **zlib 编译：** 准备无需新文件（`zlib-prepare-pass1.json`）。编译共 6 次，配方适配全部写在 `scripts/build_wasm_module.py` 的 `RECIPE_ADAPTATIONS["zlib"]`，每条都引用官方构建文件：
    - 删去 `EmfClip.cpp`、`EmfPath.cpp`、`WmfClip.cpp`（在 075052dd 时已不存在）。
    - 补 `Common/CPath.cpp`、`Common/CClip.cpp`、`EmfPlusObjects.cpp`（`metafile.pri:72-73,84`）。
    - 补 `APP14cmyktorgb.cpp`（`raster.pri:268`）、`PICFile.cpp`（`raster.pri:319`）。
    - 补 `Matrix.cpp`、`GraphicsPath.cpp`、`agg_trans_affine.cpp`、`agg_bezier_arc.cpp`（`graphics.pro:77,80,90,111`）。
    - 加定义 `IMAGE_CHECKER_DISABLE_XML`（XML 只用于按文件路径检测 SVG，WASM 用不到；同快照字体配方也这样定义）。
    - 第 5 次在拼接 JS 时停下：模板没有 `//polyfill` 占位符。按历史 make.py 改为“只要求 `//module` 必须存在”，并让失败时也写证据。第 6 次成功：`zlib.wasm` 1,917,249 字节（官方 2022 版 1,617,419）。
11. **spell、hash 准备：** `spell-prepare-pass1.json`，Hunspell 原生树的 3 处改动已证实与历史 before.py 逐字相同。hash 的 OpenSSL 准备失败 4 次，半成品都保留在 `build/wasm-source/failed/hash-openssl-prepare-pass1..4`：
    - Strawberry Perl 被 Configure 拒绝（不产生 Unix 路径），改用 Git for Windows 自带的 MSYS Perl（本机已装，未下载新东西）。
    - 该 Perl 缺 `Pod::Usage`：加只在被调用时报错的替身 `build/wasm-source/toolchain/perl-shim/Pod/Usage.pm`，并通过 `PERL5LIB` 传给子进程。
    - 头文件清单改为从生成的 Makefile 的 `GENERATED_MANDATORY` 读取。
    - 第 5 次成功（`hash-prepare-pass5.json`），目标 `linux-generic32 no-shared no-asm no-threads no-dso`。
12. **spell、hash 编译：** 都一次成功（`spell-build-pass1.json`、`engine-build-pass1.json`）。
    - spell 模板把 `return getBinaryPromise2();` 改为转发参数，依据是官方 `Common/js/desktop_fetch.js:48` 的同款写法。
    - 三个模块的后处理把旧版 `getBinaryPromise(){` 改为匹配 `getBinaryPromise(`，因为 3.1.48 生成的函数带参数。
    - hash 的 after.py 改写为逐步断言的 `post_process_hash`，插入的是固定 SDKJS 的 `stringserialize.js`。
13. **差分测试：** 新增通用测试 `scripts/wasm_engine_harness.cjs` 和 `scripts/test_wasm_engines.py`。
    - hash pass1 全部通过：两版与官方一致；36 个摘要与 hashlib 一致；20 组密码哈希与 Python 独立实现一致。
    - spell pass1 未通过：重建版 WASM 在 `Spellchecker_Load` 内存越界，asm.js 版虚函数指针无效；官方版正常。证据 `spell-functional-pass1.json`，排查中。
14. **2026-11-04：** 应用中途退出后继续。核对产物、证据都在，无残留编译进程。
15. **本文换行事故：** 用 Python `write_text` 回写本文时被改成 CRLF，附录校验失败；已改回 LF，并在维护规则里加了提醒。另一会话写的“2026-11-03（新会话）”条目原样保留，并把本会话第 10–14 条移到它之后，保持时间顺序。
16. **spell 崩溃定位与修复：**
    - 给 `build_wasm_module.py` 加了只用于诊断的 `--extra-flag`，并记入证据。用法要写成 `--extra-flag=--xxx`。
    - 诊断构建 `spell-build-diag-stackcheck.json`（`STACK_OVERFLOW_CHECK=2`）直接报栈溢出：上限 64KB。原因见 Emscripten `ChangeLog.md` 3.1.27：默认栈从 5MB 降到 64KB；官方 spell（2021）、zlib（2022）都是在旧默认值下构建的。
    - 新增配方适配类型 `add_flags`，spell 与 zlib 加 `-s STACK_SIZE=5MB`，重编为 `spell-build-pass2.json`、`zlib-build-pass7.json`。
    - spell 差分 pass2 全部通过（`spell-functional-pass2.json`）：en/de/ru/fr/es 五种词典，拼写判断与建议词两版都与官方一致。
17. **zlib 差分测试：**
    - pass1 中止：官方版和重建版把 GIF/JPEG 重新编码为 PNG 时都抛 C++ 异常。测试脚本改为逐步记录“抛出了异常”并重载新实例；环境变量 `HARNESS_STACK=1` 时打印堆栈。
    - pass2（`zlib-functional-pass2.json`）：zip 读写、4 个 Office 样本与 Python zipfile 一致；PNG/GIF/JPEG 结果和抛异常的行为都与官方相同。只有 4 个 WMF 转 SVG 不同。
    - 四方对照见 `zlib-wmf-svg-analysis.json`：3 个 WMF 的重建 zlib 与重建 fonts 输出逐字节相同，差异来自源码演进。
    - `image10.wmf` 在重建 zlib 中中止。`--profiling-funcs` 诊断构建（`zlib-build-diag-funcs.json`）定位到 `CInterpretatorSvgBase::WriteText`：`Raster_Encode` 用 `MetaFile::Create(NULL)` 创建对象，字体管理器为空，`CInterpretatorSvgBase.cpp:336` 和 `CEmfInterpretatorSvg.cpp:1255` 不判空就调用虚函数。这是 075052dd 已存在的上游缺陷，重建 fonts 对该文件的输出同样不可信。
    - 未打补丁，待用户决定。
18. **2026-11-04 再次退出后继续：** 开始准备 drawingfile。只读分析结论：
    - 当前 48 个导出中，42 个与历史 `drawingfile.cpp` 一一对应。
    - 需补写或改写 7 个：`BuildPdf`、`GetMacros`、`SetImageBinary`、`IsImageBinaryExist`、两个 Clear、`SetScanPageFonts`。参数含义按当前 SDKJS 加载器的调用方式确定。
    - 图片存储由 `PdfWriter.cpp:141` 在 WASM 下读取，导出层需要在 Open 时初始化（按字体存储的既有生命周期类推）。
19. **drawingfile 准备与编译设置：**
    - **准备结果（`drawingfile-prepare-pass2.json`）：**
      - ICU 74.2 的 `source/common` 从已按 SHA512 校验的 `icu4c-74_2-src.tgz` 解出 430 项，再按历史 before.py 改 `udata.cpp`。
      - libxml2 取自原生树 `Common/3dParty/libxml/libxml2`（`39b66eb`），3 处改动已证实与当前 `fetch.py` 逐字相同。075052dd 时它还内置在 `DesktopEditor/xml/libxml2`，现已移出。
      - OpenSSL 1.1.1f 另解压一份到 `Common/3dParty/openssl/openssl`，因为 `PdfFile/lib/xpdf/Decrypt.cc` 直接 include 了 `sha512.c`、`mem_clr.c`。用 linux-generic32 配置，`sha512.c` 的历史类型转换修改全部 3 处。
      - pass1 因“预期 1 处”断言失败，半成品保留在 `build/wasm-source/failed/drawingfile-prepare-pass1/`。
    - **导出层：** 由准备脚本从历史 `drawingfile.cpp` 生成 `drawingfile_reconstructed.cpp`，共 6 步有依据的修改（`DRAWINGFILE_RECONSTRUCTION` 表），历史文件本身不动。
    - **配方适配（`RECIPE_ADAPTATIONS["drawingfile"]`）：** 换成重建的导出层；xml 改为原位编译；libxml 按 `libxml.pri` 的 39 个源文件和包含目录；补 6 个导出名。
    - **加载器 JS 模板的边界：**
      - 第一方模板 `wasm/js/*.js` 已于 2026-05 从公开 core 删除。075052dd 版缺 BuildPdf、GetMacros、图片存储和新版 SetScanPageFonts，而当前 PDF 编辑器要用到这些。
      - 因此 drawingfile 的 `drawingfile.js`、`drawingfile_ie.js` 用官方生成加载器中的模板部分：先剥去许可证头，再把 Emscripten 胶水代码（从 `var Module=` 到 preInit 后的 `run();`）换成重建的胶水代码。
      - `drawingfile_native.js` 不含胶水代码，沿用官方文件，不重新生成。
      - 结论应表述为“drawingfile.wasm 与胶水代码从源码重建，第一方模板 JS 取自官方生成文件（无公开源码）”。
20. **drawingfile 编译 pass1–5：**
    - pass1：配方里 cryptopp `eprecomp.cpp` 列了两次，脚本改为“完全相同的条目去重并记录，不同文件同名才报错”。
    - pass2：只有历史 `metafile.cpp` 编译失败——当前 `IMetaFile` 多了纯虚的缓冲区版 `ConvertToRaster`。准备脚本据此生成 `metafile_reconstructed.cpp`（同样的空实现风格，记入 `drawingfile-prepare-pass3.json`）。
    - pass3、pass4：链接缺符号，按官方构建清单补源文件：`APP14cmyktorgb.cpp`（`PdfFile.pro:99`）、`OverlapOutputDev.cpp`（`PdfFile.pro:234`）、WASM 专用 `FontsWasm.cpp`、OFDFile 全部 32 个源文件（`OFDFile.pro`）、`ProcessEnv.cpp`（`kernel.pro:126`）。
    - pass5：867 个源文件全部编译，WASM 链接成功（`drawingfile.wasm` 10,611,404 字节，官方 10,868,120），但拼接加载器时自检失败。
    - 原因：官方压缩时 Closure 把 IIFE 内所有函数声明提到了顶部（保留相对顺序），胶水代码因此分成两段：函数区（`locateFile` 到第一个后段模板函数 `CBinaryReader` 之前）和语句区（`var Module=` 到 preInit 后的 `run();`）。
    - 切分改为两段式，并断言边界唯一、顺序正确，且函数区内没有任何 075052dd 模板函数名。WASM 版去掉 137 个胶水函数，asm.js 版去掉 140 个；asm.js 版前段那些短名字函数（`pb`、`qb`…）已核实来自 `Common/js/polyfill.js`，属于模板，予以保留。
21. **drawingfile pass6 与差分测试：**
    - **pass6 成功（`drawingfile-build-pass6.json`）：** 两版各 867 个源文件，后处理计数与历史 after.py 一致。产物：`drawingfile.js` 115,294 字节（官方 114,976），`drawingfile.wasm` 10,611,404 字节，`drawingfile_ie.js` 18,989,147 字节。46 个导出签名与官方完全一致。
    - **测试样本：** 新增 `scripts/make_test_pdfs.py`，只用标准库生成确定的样本到 `build/wasm-source/test/pdf-inputs/`，不使用用户个人文件。
      - `document.pdf`：2 页，嵌入 OpenSans TrueType，含矢量图形、JPEG、URI 链接、书签目录。
      - `form.pdf`：文本框、复选框、便签注释。
    - **测试脚本：** `wasm_engine_harness.cjs` 加入 PDF 用例，`test_wasm_engines.py` 加入 drawingfile。拆分和另存为生成的 PDF 只比较重新打开后的渲染，原始字节以 `info_` 字段仅作记录。
    - **pass1：** 只有表单外观不同。原因是 `CAnnotAP::ToWASM`（`PdfAnnot.cpp:3591`）把外观位图的堆地址直接写进了结果。
    - **pass2（`drawingfile-functional-pass2.json`）：** 按格式解析后，用指针所指的 `4×宽×高` 像素哈希代替地址，WASM 与 asm.js 两版全部与官方一致；两边的独立检查（打开无误、生成的 PDF 可重开、存储写入与清空）也都通过。
    - **复核：** 需求文件、两个 `OpenOffice.exe`、upstream、官方 SDKJS 的 5 个 WASM 与 PDF 加载器哈希均未变。
    - **临时补丁程序：** 留在 `build/wasm-source/tmp/`，是一次性修改脚本用的，不参与构建。
22. **用户选择“修复后装配”（2026-11-04）：**
    - **更正上一轮判断：** 那两处字体查找代码其实包在 `#ifndef BUILDING_WASM_MODULE` 里。字体和 PDF 配方都定义了这个宏，所以字体引擎的 `image10.wmf` 输出是可信的；只有 2022 年那一代的 zlib 配方漏了这个宏。
    - **修复方式：** 给 zlib 配方补 `BUILDING_WASM_MODULE`（配方适配，不改上游源码）。在 zlib 范围内它还关闭了 CxImage 的异常处理，与字体引擎一致。
    - **结果（`zlib-build-pass8.json`、`zlib-functional-pass3.json`、`zlib-wmf-svg-fix.json`）：**
      - `image10.wmf` 不再中止；4 个 WMF 转 SVG 都与重建 fonts 逐字节相同。
      - GIF/JPEG 重新编码为 PNG 改为正常返回“无数据”，与官方和重建的 fonts 引擎一致，只有官方 2022 版 zlib 会抛异常。
      - zip 与其余图像结果仍与官方一致。fonts 无需重编。
23. **候选运行目录：**
    - 新增 `scripts/stage_wasm_candidate_runtime.py`，装配出 `build/full-source-run-wasm-candidate`（`candidate-runtime-full-source-run-wasm-candidate.json`，目录内标记 `openoffice-wasm-candidate.json`）。
    - 从已验收的 `build/full-source-run` 复制，不含其 `openoffice-profile`。替换前先确认 18 个原引擎文件与官方 SDKJS 逐字节相同。
    - 替换 5 个模块共 15 个文件；删除不再被引用的 `spell.js.mem`；保留官方 `fonts_native.js`、`drawingfile_native.js`。其余 20,956 个文件逐一核对与原运行目录相同。
    - 第一次装配的复核计数漏算了删除项，暂存目录移至 `build/wasm-source/failed/candidate-runtime-staging-pass1`。
    - DOCX 回归见第 24 条。
24. **候选运行目录的真实编辑器 DOCX 回归（2026-11-05）：通过（`evidence/wasm-source/candidate-docx-roundtrip-result.json`）。**
    - **准备：**
      - `run_full_source_gui_session.py` 加了可选 `--runtime`，非默认目录必须带候选标记；`verify_full_source_docx_sample.py` 加了可选 `--after`、`--marker`。不传参数时两者行为与原来相同，已验收结果复查仍通过。
      - 测试文档是 `fixtures/results/wasm-candidate-docx-roundtrip.docx`，从 `fixtures/generated/text-table.docx` 新复制。
    - **中断的一次：** 第一次会话 `wasm-candidate-docx-first` 启动后应用退出，进程以退出码 1 结束，没有正常关闭事件。文档已经加载完成但没有编辑，文件未变。保留为中断记录，换新标签重做。
    - **`wasm-candidate-docx-first-r2`：**
      - 用桌面操作工具点进正文，按 Ctrl+Home，输入标记“源码重建WASM编辑保存验证 2026-11-05”并回车。
      - 点程序的“保存”，界面确认保存完成；用标题栏关闭，关闭事件与已验收版一致，退出码 0。
    - **OOXML 独立检查（`candidate-docx-saved-semantics.json`）：** 4 项全过——包有效、只加一段标记、原段落和 2×2 表格保留、文件已改写。
    - **`wasm-candidate-docx-reopen`：**
      - 新进程加全新配置重开，截图 `reopen-window.jpg` 可见标记、原文、粗斜体和表格。
      - 由测试计时器正常关闭，退出码 0，无残留进程。
    - **副作用：** 桌面工具输入中文时经过剪贴板，用户剪贴板被替换成了标记文字。桌面操作授权只针对候选目录的 `OpenOffice.exe`（Windows 下要用 `OpenOffice.exe` 这个名字申请）。
    - **范围：** 只验证了一个 DOCX 在一个进程内的中文编辑、保存和重开。PDF、拼写、哈希、WMF/EMF 在界面中的路径没有测，复杂文档也没有测。
    - **默认入口：** 仍是已验收的 `build/full-source-run`。是否把候选目录设为默认入口，需要用户决定。
25. **设为默认入口（2026-11-05，用户要求“设为默认入口”）：**
    - 在候选目录做默认首页启动测试（`evidence/full-source-gui/wasm-candidate-default-home`）：退出码 0，引擎已连接，可选择本地文件。
    - README 的启动说明、验证命令和证据链接改为指向 `build/full-source-run-wasm-candidate`；构建边界改为“5 个 WASM 已重建，PDF 模板 JS 与两个原生桥 JS 取自官方”。`build/full-source-run` 与 `build/native-engine-run` 列为回退，原样保留。
    - 候选目录内的 `OPEN_OFFICE_THIRD_PARTY_NOTICES.md` 写的是“WASM 等预生成资产并非全部重编”，依然准确，未改，以免与验收记录的文件内容不一致。
    - `docs/feasibility/STATUS.md`、`TASK.md` 加了日期说明。脚本默认值（如 GUI 会话脚本默认运行 `build/full-source-run`）未改，需用 `--runtime` 指定默认入口。
26. **新建、另存为、同进程换文档（2026-11-06，用户要求“新建与另存为”）：**
    - **查源码定方案：**
      - 新建用 `CCefViewEditor::CreateLocalFile`，空白样本是运行目录的 `converter/empty/zh-CN/new.docx`。语言由 `InitAdditionalEditorParams(L"lang=zh-CN")` 决定，上游宿主也这样设。
      - 另存为走上游菜单命令同一路径：`ASC_MENU_EVENT_TYPE_CEF_EXECUTE_COMMAND_JS` 发送 `file:saveas`，到 web-apps `Desktop.js` 后调用 `asc_DownloadAs()`。编辑器随后发 `LOCALFILE_SAVE`，宿主回 `LOCALFILE_SAVE_PATH`；空路径表示取消。
      - 关闭最后一个浏览器会结束原生消息循环（`OnDestroyWindow` 计数到 0）。所以换文档时先在新视图里加载，新文档确认就绪后才 `DestroyCefView` 旧视图。
    - **顺带修复的缺陷：** 没有修改时点“保存”，编辑器直接忽略（`asc_isDocumentCanSave` 为假），不发任何事件。旧宿主会一直等到 180 秒超时，然后停止接受操作。现在同路径、无修改时直接报告“文档没有新的修改，无需再次写入。”，GUI 回归已验证。
    - **改动：**
      - `app/engine/office_engine.h/.cpp`：接口和契约注释。
        - `EngineCapabilities.saveAs`、虚函数 `canCreate(kind)`、`cancelSaveDestination()`、信号 `saveDestinationRequested`、`classifyFileName()`（只按扩展名）。
        - `requestOpen` 加 `discardCurrentChanges`；`requestCreate` 加 `title`、`discardCurrentChanges`。
      - `app/engine/onlyoffice_engine_adapter_native.cpp`：多视图模型。
        - 一个容器（`QStackedLayout`）里有当前视图、加载中的新视图和待销毁视图，事件按视图 ID 分流。
        - 新视图加载失败、超时或崩溃时只销毁它，原文档保留可用，不把会话标为故障；第一份文档的失败仍按原规则处理。
        - 旧视图销毁前先隐藏浏览器窗口，因为上游 `DoClose` 会把它挂到桌面。
        - 同一文件重复打开会被拒绝（避免文件锁冲突）；只允许保存为与文档相同的格式；目标文件夹必须已存在。
        - 完成事件带上真实的文档类型（原来固定报 Text）。启动检查加 `converter/empty/zh-CN/new.docx`。
        - 只开放新建文字文档。
      - `app/engine/onlyoffice_engine_adapter.h/.cpp`：不可用基线跟随新接口。
      - `app/ui/main_window.h/.cpp`：
        - 新增“另存为…”（Ctrl+Shift+S）、“编辑器”（从首页回到当前文档）。新文档命名为“文档1.docx”“文档2.docx”（按类型分别计数），首次保存弹出保存对话框（默认“文档”文件夹，自动补扩展名，覆盖时确认）。
        - 换文档前如有修改，提示“保存 / 不保存 / 取消”。选“保存”时，等保存完成且编辑器报告文档已干净后再继续，沿用关闭流程的等待和 5 秒超时。
        - 编辑器自己发起的保存（编辑器内 Ctrl+S、编辑器菜单另存为）也弹出同一对话框，取消时回告编辑器。
        - 提示框按钮改为中文（程序没有附带 Qt 翻译，原来显示 Save/Discard/Cancel；关闭提示同样受益）。
      - 测试：`tests/native/engine_contract_test.cpp`（21 项）、`tests/native/native_ui_test.cpp`（24 项，新增新建与首次保存、取消保存、另存为、换文档提示与失败保留、先保存再换、编辑器发起保存、未开放类型、中文按钮、首页状态）。
      - 新脚本 `scripts/stage_host_candidate_runtime.py`、`scripts/verify_host_saveas_session.py`；`scripts/run_full_source_gui_session.py` 支持宿主候选标记，按标记里的宿主哈希校验，并在 `session.json` 记录标记哈希。一次性补丁脚本在 `build/wasm-source/tmp/`。
    - **构建：** `build_full_source_host.cmd` 标签 `new-saveas-1`：编译通过，测试有 3 处失败，都是新代码的问题（未初始化时 `requestSave` 返回了线程错误；新建完成时先刷新标题、后记录名称），日志保留。`new-saveas-2`、`new-saveas-3` 全部通过。
    - **装配：** 两个候选目录都从默认入口复制（不含 profile），只替换 `OpenOffice.exe`。20,972 个文件中其余 20,971 个逐字节一致。装配记录在 `evidence/host-candidate/candidate-runtime-*.json`。
    - **真实编辑器回归（`host-saveas-session1`，宿主 `9119d202…`）：** 全部通过，见 `evidence/host-candidate/host-saveas-session1-result.json`，8 项检查。用桌面操作工具在同一个进程里依次：
      1. 新建 → 文档1.docx，输入第一段。
      2. 点“保存” → 系统保存对话框 → `fixtures/results/host-saveas-new.docx`。
      3. 输入第二段 → 另存为 → `host-saveas-copy.docx`，标题随之切换。截图 `evidence/full-source-gui/host-saveas-session1/saveas-window.jpg`。
      4. 回首页打开 `host-saveas-open-source.docx`（`text-table.docx` 的新副本）：同进程换文档，进程没有退出。
      5. 在它末尾输入“放弃的修改” → 新建 → 提示里选“不保存” → 文档2.docx。
      6. 编辑器内 Ctrl+S → 取消 → 再 Ctrl+S → `host-saveas-editor.docx`。
      7. 未修改时点“保存”，立即提示无需写入。
      8. 关闭窗口，退出码 0。
    - **核对结果：**
      - new 只含第一段；copy 含两段；editor 只含它自己那段。
      - 被“不保存”丢弃的源文档与原样本逐字节相同。
      - 事件日志：旧视图 1、2 各自退役并收到销毁事件；编辑器发起的保存位置请求 2 次、取消 1 次；8 个请求全部完成，无失败、无超时。
    - **回归中发现的问题：** 已在 r2 修正，CTest 覆盖。
      - 提示按钮是英文。
      - 新建完成时，首页选择卡片在没选文件时也显示“文件已选择 · 尚未打开”。
    - **r2 复查：** 用户拒绝了第二次屏幕授权，没有再申请。r2 只验证了启动和正常关闭（`host-saveas-r2-prompts`）：测试窗口用不带 `/F` 的 `taskkill` 发送关闭消息，没有打开过文档，退出码 0。
    - **副作用：** 中文和路径通过剪贴板输入，用户剪贴板被替换。
    - **未改动：** 默认入口、两个回退、需求原文的哈希均未变。
    - **未做：**
      - 新建表格、新建演示，以及 XLSX/PPTX 的打开。
      - 导出为其他格式（编辑器菜单里的 PDF 等导出会被取消）、多文档标签页。
      - 把候选设为默认入口（待用户决定）。
27. **r2 复查并设为默认入口（2026-11-06，用户选择“复查后设为默认”）：**
    - **复查授权：** 用户批准了对 `build/full-source-run-host-saveas-r2/OpenOffice.exe` 的屏幕控制。
    - **校验脚本：** `scripts/verify_host_saveas_session.py` 加了 `--prefix`、`--same-path-marker`、`--close-discarded` 三个参数。不加参数时行为不变。
    - **真实编辑器复查（`host-saveas-r2-recheck`）：** 8 项全过，见 `evidence/host-candidate/host-saveas-r2-recheck-result.json`。在同一进程里依次：
      1. 新建并首次保存，得到 `host-saveas-r2-new.docx`。
      2. 另存为 `host-saveas-r2-copy.docx`。
      3. 首页确认没选文件时显示“等待选择”（上一轮发现的问题已修正）。
      4. 打开 `host-saveas-r2-open-source.docx`（`text-table.docx` 的新副本）。
      5. **新增：** 另起一段输入“原路径保存验证”，点“保存”写回原路径。
      6. 再改动后新建，提示按钮为“保存 / 不保存 / 取消”，选“不保存”。截图 `evidence/full-source-gui/host-saveas-r2-recheck/replace-prompt-chinese.jpg`。
      7. 编辑器内 Ctrl+S：先取消，再保存为 `host-saveas-r2-editor.docx`。
      8. 无修改时保存，立即提示无需写入。
      9. **新增：** 输入“关闭时放弃”后关闭窗口，关闭提示也是中文按钮，选“不保存”，退出码 0。
    - **文件核对：**
      - 原路径保存的文档只多了一段标记，原段落和表格单元格都在；之后被“不保存”丢弃的修改没有写入。
      - 关闭时放弃的文字不在 editor 文件里。
      - 两个旧视图都退役并销毁，没有失败或超时。
    - **首页启动测试（`host-saveas-r2-default-home`）：** 9 秒后由计时器正常关闭，退出码 0；引擎已连接，可选择本地文件。
    - **设为默认：**
      - README 的简介、启动步骤、PowerShell 命令、目录来历、未开放功能、证据与回退列表都改为以 `build/full-source-run-host-saveas-r2` 为默认入口，并写清新建、另存为、换文档的用法。
      - `build/full-source-run-wasm-candidate` 改列为回退，原样保留。
      - `scripts/stage_host_candidate_runtime.py` 的说明补了“源目录是 WASM 候选”。
      - 默认值没改：GUI 会话脚本的默认运行目录仍是 `build/full-source-run`，需用 `--runtime` 指定。
    - **副作用：** 中文和路径通过剪贴板输入，用户剪贴板被替换。复查期间桌面上另有一个资源管理器窗口，未操作它。
    - **未改动：** 两个回退目录与需求原文。
28. **便携压缩包与安装程序（2026-11-06）：**
    - **用户要求：** 先问怎么打开软件、在哪找安装包。我说明当前没有安装包，并提出打 zip，用户同意。随后用户明确：“我要的是已经编译过的软件安装包”。本机没有 Inno Setup、NSIS、WiX、Qt 安装器框架；Windows 自带的 IExpress 应付不了 1.6 GB、两万多个文件。所以提出三种做法，用户选“下载 Inno Setup 做”。
    - **便携 zip（`scripts/package_portable_zip.py`）：**
      - 产物 `dist/OpenOffice-portable-win64-20261106.zip`，757,500,827 字节，SHA256 `5874d211…2304`。
      - 内容：默认入口原样（不含 profile）放在 `OpenOffice/` 下，另加 `使用说明.txt`。
      - 校验：逐项 CRC 与大小比对通过。解压到 `build/portable-check/` 后，用最精简的 PATH、换一个工作目录启动：引擎已连接，在程序文件夹里自动建了 profile，退出码 0。
      - 记录在 `evidence/portable/OpenOffice-portable-win64-20261106.json`。
    - **依赖检查：** 读了全部 EXE/DLL 的导入表（临时脚本 `build/wasm-source/tmp/pe_imports.py`）。目录外只缺 Windows 10/11 系统 DLL 和 VC++ 运行库（`msvcp140*`、`vcruntime140*`，69 个文件依赖）；目录里不带运行库。最长相对路径 121 字符，没有非 ASCII 文件名。
    - **Inno Setup 6.7.4：**
      - 官网下载页链接到 GitHub 不可修改的发布 `is-6_7_4`。官网同时推荐 7.1.1，因用户批准的是 6，只用 6.7.4。
      - 下载到 `dependencies/innosetup-6.7.4/download/`，SHA256 `a5499293…0e8c` 与 GitHub 公布值一致；Authenticode 有效，发布者 Pyrsys B.V.，与官网说明一致。
      - 用其官方脚本里的便携模式安装到 `install/`（`/PORTABLE=1 /CURRENTUSER /VERYSILENT /NOICONS`）：无卸载程序、无卸载登记、无快捷方式。日志 `install.log`。
    - **中文向导：** Inno Setup 6.7.4 官方语言里没有简体中文。我把 `Default.isl` 的 296 条全部翻译为 `installer/ChineseSimplified.isl`，核对过键名一一对应、`%1`/`[name]` 占位符一致，没有下载第三方译文。
    - **VC++ 运行库：**
      - 本机 VS 带 14.44 与 14.51 两版。选 14.51.36231 目录下的文件（版本 14.51.36247），与本机系统装的完全相同。
      - 依据微软 VS 2026 Distributable Code 清单：`VC\Redist` 下文件（`debug_nonredist` 除外）可不加修改地随程序分发。
      - 只打包被导入的 5 个文件，放在 `{app}` 和 `{app}\converter` 各一份；签名均有效。
    - **安装脚本 `installer/OpenOffice.iss`：**
      - 按当前用户安装（`PrivilegesRequired=lowest`，默认 `%LOCALAPPDATA%\Programs\OpenOffice`），因为程序把 profile 写在自己文件夹里，安装目录必须对用户可写。
      - 只允许 x64、Windows 10 及以上；安装前显示 `installer/安装前说明.txt`。
      - 开始菜单建“Open Office”和卸载项，桌面快捷方式可选；装完可直接运行。
      - 文件用 lzma2/max 固实压缩；卸载时保留 `openoffice-profile`。
      - `.iss`、`.isl`、说明文本由构建脚本补 UTF-8 BOM（否则 Inno Setup 按 ANSI 读取）。
    - **构建（`scripts/build_installer.py`）：**
      - 先检查编译器签名、默认入口宿主哈希、运行库签名与版本。
      - 编译用时 216 秒。产物 `dist/OpenOffice-Setup-0.1.0-dev-20261106-win64.exe`，594,438,784 字节，SHA256 `9c6b4d02587407a8f9aac6f917e2c980fedd6d25e66a4f5f145baa3839242763`，没有签名。
      - 编译日志与记录在 `evidence/installer/`。
    - **第一次测试未通过（保留记录）：** 安装、逐文件比对、启动、卸载都正常，但有两项检查没过，都是测试脚本自身的问题：
      - 查询进程加载了哪个运行库的 PowerShell 命令，被字符串拼接多带了一个 `}`，返回空。
      - 安装后立即查卸载登记项没查到。安装日志显示已创建，原因未能查明。
      - 修正后加了 `--retest`（复用同一安装程序，按记录核对哈希，不重新编译），查询登记项改为最多等 60 秒。
    - **重测 `retest-1`：通过（`evidence/installer/OpenOffice-Setup-0.1.0-dev-20261106-win64-retest-1.json`）。**
      - 静默安装到 `build/installer-check/…-retest-1/app`，不建快捷方式，用时 50 秒，安装结束时已能查到登记项。
      - 20,985 个文件与源文件逐个 SHA256 一致，多出的只有 `unins000.exe/.dat`。
      - 用最精简的环境启动：引擎已连接，退出码 0；5 个运行库都从安装目录加载。
      - 静默卸载后登记项已移除，只剩程序生成的 `openoffice-profile`；开始菜单没有残留。
    - **未删除：** 两个测试安装目录（`build/installer-check/` 下，只剩 profile）和 `build/portable-check/` 的解压副本都保留了，没删。
    - **未做：** 在其他电脑或干净虚拟机上安装；代码签名；SmartScreen 信誉。

29. **AI 助手：接口与悬浮球（2026-11-06～07）：**
    - **用户要求的演变：** 详见第 2.4 节，按顺序为：
      1. 先做接口，并做一个高级的艺术图案（定为助手界面的视觉设计，先不接模型）。
      2. 模仿苹果的样式和动效；后来澄清只借鉴细节和设计、不模仿美术。
      3. 助手是一个球；历史对话要有背景；阴影要柔和、背景用连续曲率。
      4. 要像苹果一样高级；加强与背景的对比；暗色模式下文档页面太刺眼。
      5. 可随意摆放、随文档页面显示、限制在窗口内。
    - **命令接口：**
      - `app/assistant/assistant_commands.*`：8 条命令。
        - 宿主侧：`assistant.listCommands`、`document.status`、`document.create`、`document.save`。
        - 编辑器侧：`text.insert`、`selection.getText`、`edit.undo`、`edit.redo`。
      - 严格的参数校验：多余、缺失、类型错误、超长都拒绝。来源标记为 user/assistant/external/unknown，不猜。`commandCatalogJson()` 生成 MCP `tools/list` 形式的清单，但还没有 MCP 服务。斜杠指令支持中英文别名和全角斜杠。
      - 宿主侧命令复用窗口原有流程，保存、不保存的提示仍由用户决定。编辑器侧命令走新增的 `OfficeEngine::requestEditorCommand` / `editorCommandFinished`。
      - 原生实现：用 `ExecuteEditorScript` 执行编辑器自己导出的 API（`asc_PasteData`、`asc_GetSelectedText`、`Undo`/`Redo`）。脚本参数全部转成纯 ASCII 的 JS 字面量。编辑器通过 `AscDesktopEditor.execCommand("openoffice:command")` 回报，宿主按请求编号匹配；10 秒没回报按超时处理，不把会话标为故障。
      - 命令日志 `assistant-commands.jsonl` 写在 profile 目录，只记时间、来源、命令名和结果码，不记正文；`verify_assistant_session.py` 会核对这一点。
    - **悬浮球（`app/ui/assistant_ball.*`）：**
      - 是主窗口拥有的独立透明小窗口，所以能浮在嵌入的 CEF 编辑器上面。
      - 用独立窗口还有一个原因：最早的侧边面板版里，点了 Qt 输入框后 Win32 键盘焦点仍留在 CEF 窗口，打的字进了文档。侧边面板源码移到 `build/wasm-source/tmp/superseded-assistant-panel/` 保留。
      - 只在打开文档的编辑页面显示。在文档区域内可任意摆放、不吸边；拖出区域会弹回边缘内；位置按比例记，窗口缩放后保持相对位置。
      - 点击展开输入胶囊，按位置自动选择向左或向右、向上或向下；点别处或按 Esc 收起。对话历史在一张跟随主题的连续曲率卡片上。
    - **视觉（`app/ui/assistant_art.*`，全部自绘、原创图案）：**
      - 玻璃球：菲涅尔边缘、镜面高光、底部焦散光斑；内有三片弧形玻璃叶瓣（蓝、薄荷绿、琥珀，对应文档、表格、演示）和发光球心。
      - 白底上加一圈均匀的蓝色光晕和接触投影，提高辨识度；执行中有渐变弧线扫过。
      - 连续曲率圆角按 Figma 公开的 corner smoothing 构造实现，平滑度 0.6。阴影用三遍盒式模糊近似高斯，并缓存结果。
      - 弹簧缓动；系统关闭动画效果时（`SPI_GETCLIENTAREAANIMATION`）全部静止。
      - 预览图在 `docs/design/`，由测试用同一套绘制代码导出。
    - **深色外观同步到编辑器：**
      - 新增 `OfficeEngine::setEditorAppearance(bool)`。原生侧调用 web-apps 的 `Common.UI.Themes`：切深色时记住当前浅色主题、换成配套的深色主题（White 配 Night，其它配 Dark），并打开 `setContentTheme('dark')` 暗色文档；切回时恢复原主题。
      - 新文档加载完成后也会套用。只改显示，不改文件。样式库预览格仍是编辑器自己的白底渲染。
    - **过程中修掉的问题：**
      - 重叠命令被拒时清掉了正在执行的状态。
      - 原生侧在无可编辑文档时也报告支持编辑器命令。
      - 气泡颜色不随主题、气泡过窄。
      - 球靠上时向上展开、伸出窗口。
    - **候选目录：** 都从 `build/full-source-run-wasm-candidate` 复制，只替换宿主。
      - `-assistant`：侧边面板版，有焦点缺陷。
      - `-assistant-r2`：修了焦点的侧边面板版，已作废。
      - `-assistant-ball`、`-ball-r2`～`-r6`：悬浮球各轮迭代。
      - `-ball-r7`：当前推荐。
      - `-ball-r8`：可拖出窗口的版本，是对用户意思的误解，作废；源码已恢复到 r7。
      - 两个中途中断或停掉的暂存目录 `build/.full-source-run-assistant-r2.staging-*` 按规矩保留，没有删。
    - **验证：**
      - CTest：命令接口 15 项、契约 21 项、界面 31 项，全过。
      - 真实编辑器会话（`evidence/host-candidate/*-result.json`）：`assistant-ball-session1`、`-r2-session`、`-r3-session`、`-r5-session`、`-r6-session`（深色）、`-r7-session`，各 6 项全过。`-r4-session` 只完成了插入、撤销、重做、读取选中：桌面工具误操作把测试样本内容替换了，已选“不保存”关闭，样本哈希未变。
      - r7 截图：拖到侧边栏后弹回、放在页面中间停住、回首页隐藏。
    - **测试工具的注意事项：**
      - 中文输入法开着时，桌面工具逐字敲中文或英文都可能乱码；改用剪贴板粘贴，用户剪贴板会被替换。
      - 每次工具调用后 Claude 窗口会抢回焦点，所以点击和输入要放在同一批操作里。
    - **未改动：** 默认入口、各回退目录、需求原文的哈希均未变。

30. **设为默认入口（2026-11-07，用户要求“设为默认入口”）：**
    - **首页启动测试（`assistant-ball-r7-default-home`）：** 9 秒后由计时器正常关闭，退出码 0；引擎已连接，可选择本地文件，宿主 `039299d6…`。
    - **README：** 简介、默认入口说明、启动步骤（加了"AI 助手"一步）、PowerShell 命令、AI 助手一节、回退列表、脚本表都改为以 `build/full-source-run-assistant-ball-r7` 为默认入口。
    - **回退：** `build/full-source-run-host-saveas-r2` 改列为回退，原样保留。
    - **安装包：** 另一个会话生成的安装包和便携包装的仍是 r2，没有重新生成，README 已写明。
    - **未改动：** 各回退目录、安装包与需求原文。

31. **用默认入口 r7 重新生成安装包（2026-11-07，属于用户“做完后……”要求的一部分）：**
    - **命令：** `python -I scripts/build_installer.py --test`。`build_installer.py` 的默认来源改成 `build/full-source-run-assistant-ball-r7`，并新增 `--source` 参数。
    - **产物：** `dist/OpenOffice-Setup-0.1.0-dev-20261107-win64.exe`，594,548,035 字节，SHA256 `219a8ae236d7ef9fd2a487e5c8cb0144f2f43a3bf92c7722dd6cf2afc8c00ccf`，编译 196 秒，未签名。记录在 `evidence/installer/OpenOffice-Setup-0.1.0-dev-20261107-win64.json`。
    - **本机测试通过：**
      - 静默安装到 `build/installer-check/OpenOffice-Setup-0.1.0-dev-20261107-win64`，用时 45 秒，退出码 0，能查到卸载登记项。
      - 20,985 个文件与来源逐个一致，多出的只有 `unins000.exe/.dat`。
      - 启动：引擎已连接，退出码 0。
      - 静默卸载后登记项移除，profile 以外没有残留。
    - **未删除：** 测试安装目录保留。
    - **未做：** 便携包没有重新生成（仍是 r2）；没在别的电脑上装过；没签名。

32. **本地 AI：接入 Ollama（2026-11-07，用户要求“尝试先接入本地 ollama……做好通用性适配，让什么也不会的小白也能快速使用”）：**
    - **新代码：**
      - `app/assistant/ollama_client.*`：Ollama REST 客户端。
        - 用到 `/api/version`、`/api/tags`、`/api/chat`（流式 NDJSON，含 `tool_calls`）和 `/api/pull`（流式进度）。
        - 只连本机回环地址，`QNetworkProxy::NoProxy`。地址取 `OPEN_OFFICE_OLLAMA_URL`，再看 `OLLAMA_HOST`，不是本机地址一律忽略，默认 `http://127.0.0.1:11434`；`0.0.0.0` 按本机处理。
        - 带 `remote_host`/`remote_model` 或名字以 `:cloud` 结尾的云端模型直接丢掉。
        - 内存用 `GlobalMemoryStatusEx` 读取。
        - 选模型 `pickInstalledModel`：能调用工具 +100，qwen +30、glm +25、deepseek +20、gpt-oss +18、llama/gemma/mistral +10，大小（到 9 GB 为止）最多 +20；超过内存 55% 和嵌入模型不选。
        - 推荐下载 `recommendDownload`：内存不到 7.5 GB 用 `qwen3:1.7b`，不到 15 GB 用 `qwen3:4b`，否则 `qwen3:8b`。
      - `app/assistant/local_assistant.*`：状态机，状态为 未检测 / 检测中 / 未安装 / 未运行 / 无模型 / 下载中 / 就绪 / 思考中。
        - 找 Ollama 程序：`%LOCALAPPDATA%\Programs\Ollama\ollama app.exe`、`%ProgramFiles%\Ollama`、PATH。“启动 Ollama”用 `QProcess::startDetached`。
        - 选中的模型记在 `QSettings`（`Open Office Project/Open Office` 的 `ai/model`）。
        - 中文系统提示里带文档名、类型和是否有未保存修改；历史最多 16 条；一次只执行一个工具，最多 4 轮。
        - gpt-oss 用 `think: "low"`，其他推理模型关掉推理以加快回答。
        - 命令目录转成 Ollama 工具（`.` 换成 `_`，不含 `assistant.listCommands`）。执行结果以 `role: tool` 消息回给模型。
      - 悬浮球（`assistant_ball.*`）：
        - 新增带按钮的卡片 `showCard`，每张卡的按钮只能点一次。
        - 流式回复 `beginReply/appendReply/replaceReply/endReply`。
        - 欢迎区状态行 `setStatus`。
        - 原“重做”快捷按钮换成“本地 AI”。
        - 不以 / 开头的话交给本地 AI，原来“没有发送到任何地方”的回复去掉。
      - 主窗口（`main_window.*`）：
        - 球第一次展开时才检测 Ollama，不在启动时检测。
        - 按状态给出卡片：未安装 → 打开 `https://ollama.com/download/windows` + 重新检测；未运行 → 启动 Ollama + 重新检测；无模型 → 下载推荐模型（写明大小）+ 重新检测；就绪 → 可改用本机其他模型。
        - 检测中提的问题，检测完自动回答。
        - 模型提出的 `text.insert`、`document.save`、`document.create` 先出确认卡片（执行 / 不要）。读取类命令直接执行；不认识的命令交给校验拒绝并回告模型。
        - 命令结果回给模型；“执行中”状态同时考虑命令和模型。
      - CMake：`office_assistant` 加入两个新文件并链接 `Qt6::Network`。运行目录本来就有 `Qt6Network.dll`。
    - **测试：**
      - `tests/native/fake_ollama.h`：用 `QTcpServer` 模拟 Ollama，记录每个请求体。
      - `assistant_test` 新增 7 项：
        - 地址只认本机；
        - 按内存选模型和推荐下载；
        - 工具清单与命令目录一致；
        - 连不上时进入“未安装/未运行”；
        - 完整对话：云端模型被过滤；首个请求带文档上下文、工具、`think:false`；工具调用变成 `text.insert` 提议；结果以 tool 消息回传后流式收尾；
        - 拒绝执行会告诉模型，gpt-oss 用 `think:"low"`；
        - 只有嵌入模型时进入“无模型”，且不会自动下载。
      - `native_ui_test`：
        - 新增“模型改文档必须先确认”：执行后引擎才收到命令；点“不要”引擎收不到。
        - 原自由文本用例改为期望出现 Ollama 配置卡片。
        - 测试启动时把 `OPEN_OFFICE_OLLAMA_URL` 指向一个没有监听的本机端口。
      - 构建 `local-ai-1` 编译失败：`QPointer<QNetworkReply>` 只有前置声明。失败记录保留，改为包含头文件后，`local-ai-2` 通过：契约 21、界面 32、助手 22，共 75 项。
    - **候选目录：** `build/full-source-run-local-ai`，由 `stage_host_candidate_runtime.py` 从 `build/full-source-run-wasm-candidate` 复制，只换宿主（`3eb9a92a…`），记录在 `evidence/host-candidate/candidate-runtime-full-source-run-local-ai.json`。
    - **真实编辑器验证（这台电脑：内存 31.5 GB，Ollama 0.40.15，本机有 `gpt-oss:20b`、`granite4.1:3b` 和云端 `deepseek-v4-pro:cloud`）：**
      - `local-ai-session1`：自动选中 `gpt-oss:20b`，云端模型没出现。
        - 提问“请在光标处插入这句话，原样插入，不要加标点：你好，本地 AI 助手”后，出现确认卡片，点“执行”插入，模型随后说明结果；再用 `/保存` 保存。
        - `verify_assistant_session.py` 6 项全过（`evidence/host-candidate/local-ai-session1-result.json`）：日志里 `text.insert` 来源为 `assistant`、`document.save` 来源为 `user`；日志里不含正文；保存的文档在原内容后多了这一段。
        - 对话里还有一条不是本次操作发出的“你好”及回答，推测是用户在两次操作之间自己试用的。
      - `local-ai-session2`：把地址指向没有监听的端口启动。欢迎区显示“Ollama 已安装，但现在没有运行”，提问后出现“启动本地 AI 引擎”卡片，不编造回答。没点“启动 Ollama”；样本未改动，哈希与原始样本相同。
      - `verify_assistant_session.py` 新增 `--editor`、`--host`、`--from-assistant` 参数，默认行为不变。
    - **测试工具的注意事项：**
      - `open_application("Openoffice")` 会另开一个 `build/full-source-run-wasm-candidate` 的实例；已用 WM_CLOSE 正常关闭，它没有打开文档。
      - 把窗口提到前台用 PowerShell `AppActivate(进程号)`；之后球会收起，要在同一批操作里重新展开。
    - **未做：**
      - 没在没装 Ollama 的电脑上走过新手流程；“打开下载页”“下载模型”“启动 Ollama”按钮没在真实界面点过（只有单元测试）。
      - 没有设为默认入口，安装包里没有这个版本。
      - 还没有 MCP 服务、记忆和风格学习。
    - **未改动：** 默认入口 r7、各回退目录、两个安装包、需求原文的哈希均未变。

33. **把本地 AI 版设为默认入口（2026-11-07，用户要求“设为默认入口”）：**
    - **首页启动测试（`local-ai-default-home`）：** 9 秒后由计时器正常关闭，退出码 0；引擎已连接，可选择本地文件，宿主 `3eb9a92a…`。
    - **README：** 简介、默认入口说明（加了本地 AI 的验证记录）、启动步骤、PowerShell 命令、目录来历、AI 助手一节、回退列表、脚本表都改为以 `build/full-source-run-local-ai` 为默认入口。
    - **脚本：** `scripts/build_installer.py` 的默认来源改为 `build/full-source-run-local-ai`。
    - **回退：** `build/full-source-run-assistant-ball-r7` 改列为回退，原样保留。
    - **安装包：** 没有重新生成。现有 `…-20261107-win64.exe` 装的是 r7（不含本地 AI），README 已写明。
    - **未改动：** 各运行目录、两个安装包、便携包与需求原文。

34. **用本地 AI 版重新生成安装包（2026-11-07，用户要求“重新生成”）：**
    - **命名：** 当天已有 `…-20261107-win64.exe`（r7），脚本按规矩拒绝覆盖。给 `scripts/build_installer.py` 加了 `--same-day N`（N ≥ 2），日期后加 `-N`，安装程序里的版本说明也随之带编号。
    - **命令：** `python -I scripts/build_installer.py --test --same-day 2`，来源是默认入口 `build/full-source-run-local-ai`（宿主 `3eb9a92a…`）。
    - **产物：** `dist/OpenOffice-Setup-0.1.0-dev-20261107-2-win64.exe`，594,635,602 字节，SHA256 `aa91dbeba5dbd7eda1796a8669c5d3005a15e6b798b45b81dc3f7ea18afc2656`，编译 183 秒，未签名。记录在 `evidence/installer/OpenOffice-Setup-0.1.0-dev-20261107-2-win64.json`。
    - **本机测试通过：** 静默安装 44 秒、退出码 0；20,985 个文件逐个一致，多出的只有 `unins000.exe/.dat`；启动时引擎已连接、退出码 0；静默卸载后登记项移除，profile 以外没有残留。
    - **未做：** 便携包没有重新生成；没在别的电脑上装过；没签名。
    - **未改动：** 两个旧安装包、各运行目录、需求原文。

35. **代码放进 git、准备 GitHub 仓库（2026-11-07，用户要求“把代码放入 git 和在 GitHub 建立仓库”）：**
    - **用户的选择（问答）：** 公开并加 AGPL-3.0；仓库名 `open-office-desktop`（避开 Apache 的 OpenOffice 商标）；放代码、文档和 5 MB 以下的证据；提交邮箱用 GitHub 隐私邮箱（全局 git 邮箱像手机号，不公开）。
    - **本机工具：** Git 2.55 已装，全局身份 `rzk` 保持不动；没有 GitHub CLI（`gh`），也没有安装。仓库在内置浏览器里建。
    - **新文件：**
      - `LICENSE`：标准 AGPL-3.0 原文。取自本地 ONLYOFFICE 源码的 LICENSE 前 661 行（7 个子仓库的 LICENSE 完全相同），换成 LF 后 34,523 字节，SHA256 以 `0d96a4ff` 开头，与 FSF 的 `agpl-3.0.txt` 一致。ONLYOFFICE 在后面附加的版权和附加条款没有放进本项目的 LICENSE。
      - `licenses/ONLYOFFICE-LICENSE.txt`：ONLYOFFICE 的完整 LICENSE（AGPL-3.0 加 Ascensio 附加条款），逐字节复制，因为安装包分发了 ONLYOFFICE 组件。
      - `.gitignore`：排除 `build/`、`dist/`、`upstream/`、`dependencies/`、`.library-handoff-*/`，以及 evidence 里的两个下载残包目录和 5 个超过 5 MB 的文件。
      - `.gitattributes`：`* -text`，所有文件按字节原样存，不转换换行（证据里记了脚本和样本的 SHA256，本文也必须保持 LF）。
    - **README：** 安装一节改为第二次生成的安装包，并写明安装包不进 git；新增“许可证”一节，按 ONLYOFFICE 附加条款列出署名、修改说明、界面声明和商标要求。
    - **本地仓库：** `git init -b main`，暂存 763 个文件，约 6.8 MB（压缩后），最大单个文件 4.2 MB。扫描暂存内容：没有 GitHub/OpenAI/AWS 密钥或私钥；“password”“token”只出现在 ONLYOFFICE 文件名、构建日志和哈希测试用例里；没有用户的邮箱或手机号。约 23 个证据文件里的本机路径含 Windows 用户名 `18268`，用户已知情并选择公开。
    - **未完成：** 内置浏览器里 GitHub 还没登录（需要用户自己登录），所以还没查到隐私邮箱，也还没提交、建仓库、推送。

36. **GitHub 仓库建好并推送（2026-11-07，接第 35 条；用户登录 GitHub 后说“继续”）：**
    - **账号：** 内置浏览器里登录的是 GitHub 账号 `renzhekai`。隐私邮箱 `279273193+renzhekai@users.noreply.github.com` 只写进本仓库的 `git config --local user.email`；全局 git 配置和 GitHub 账号的邮箱隐私设置都没改。
    - **首次提交：** `ac630cf`（“Open Office：首次提交”），763 个文件，作者 `rzk <隐私邮箱>`，带 Claude 共同作者行。
    - **建仓库：** 在 github.com/new 建了 `renzhekai/open-office-desktop`：公开，无描述，不让 GitHub 生成 README、.gitignore、许可证。第一次输入仓库名时页面元素没初始化，文字没进输入框，页面也被切到了仓库列表（可能是用户在浏览器里点了）；重新打开后用表单填写，没有建出别的仓库。
    - **推送：** `git remote add origin https://github.com/renzhekai/open-office-desktop.git`，`git push -u origin main` 成功（Git Credential Manager 已有凭据，没弹窗）。远端 `main` 与本地同为 `ac630cf…`。
    - **网页核对：** 仓库显示为 Public，GitHub 识别出 AGPL-3.0 许可证，README 正常显示。
    - **之后的规矩：** 每次更新都提交并推送（已写进“维护规则”）。

37. **办公助手：常用活一键完成（2026-11-08，用户要求“结合实际来制作，在用户的角度看问题”）：**
    - **原来的问题（从用户角度）：** 模型看不到文档内容；只会在光标处插纯文本，模型写的 `#`、`**` 会原样进文档；不能改选中的文字；什么都要自己写提示词；每次操作要“模型 → 工具 → 模型”两轮。
    - **先查清编辑器能做什么（调试通道，只用于开发）：**
      - 一次性的实验 profile 里写 `data/settings.xml` 的 `ascdesktop-support-debug-info-keep=1`，ONLYOFFICE 自己就会打开 127.0.0.1:8080 调试端口；`build/wasm-source/tmp/devtools_probe_launch.py` 启动这种实例，`cdp_eval.mjs`（Node 自带 WebSocket）在 `frameEditor` 里执行脚本。正式运行目录和用户 profile 都没改。
      - 实测结论：`AscBuilder.Word.Api.GetDocument()` 能读全文；`ToMarkdown` 在有选区时只转选区，读全文要逐段读；`_pluginMethod_PasteHtml` 能把 `<h1>`、列表、表格贴成真正的样式；`pluginMethod_ReplaceTextSmart` 替换选区时保留粗斜体，新段落比选中段落少时会留空段（这时改用 HTML 粘贴）；纯文本粘贴会丢格式；`logic.Search` + `ReplaceSearchElement` 能查找替换；每种操作都只占一步撤销。
    - **新代码：**
      - `app/assistant/markdown_html.*`：模型写的 Markdown 转成编辑器能粘贴的 HTML（标题、段落、粗斜体、列表、表格、代码），文字全部转义，去掉 `<think>`；`cleanRewrite` 去掉改写结果里的“以下是……：”开头、整段引号和 Markdown 符号。
      - `app/assistant/assistant_tasks.*`：15 个任务——选区 6 个（润色、纠错、精简、扩写、正式、翻译，翻译自动判断方向）、全文 3 个（总结、待办表格、校对）、起草 6 种（通知、会议纪要、工作总结、邮件、请假条、活动方案，没给的信息用【】标出）。校对要求结构化输出，`parseCorrections` 只保留文档里真找得到、确实改了、长度合理、不重复的条目，最多 30 条。文档超过 12000 字只发前面部分并告诉模型；上下文长度按内容估算（4096 到 32768）。
      - 命令目录：新增 `selection.replace`（可带 `expected` 核对原文）、`document.getText`、`document.replaceText`；`text.insert` 增加 `format`（plain / markdown）和 `position`（cursor / after），上限 20000 字；参数类型新增“选项”。
      - 引擎侧：每条编辑器命令有自己的参数表并再校验一次；新脚本见上；超时改为 15 秒。`build/wasm-source/tmp/check_editor_scripts.py` 用 Node 检查脚本语法，`run_editor_scripts.py` 把宿主源码里的脚本原样拿到真实编辑器执行。
      - `LocalAssistant`：`runTask` 一次生成、不带工具、流式；`warmUp` 在打开球时预先装载模型（同一模型 10 分钟内不重复）；请求都带 `keep_alive: 30m` 和 `num_ctx`；系统提示改为“附有文档内容和选区”，并说明每个工具什么时候用。
      - 窗口：为 AI 做的读取不弹气泡（仍记日志）；读取和用户命令撞上时排队，用户自己的两条命令仍然互斥；打开球时读选区决定按钮；直接提问前先读全文和选区一起发给模型；结果卡片按任务给出替换、插入、复制、重写；校对结果逐条可改，也可全部改正。
      - 悬浮球：尺寸 500×580；输入框上方常驻一排随情况变化的按钮和说明；起草模式（Esc 退出）；回复里的 Markdown 显示成格式；校对列表卡片。
    - **测试：** 助手 28 项、界面 33 项、契约 21 项。新增端到端测试：选中润色 → 替换原文（带 expected）、全文校对 → 只给文档里有的错 → 改一条、起草通知 → 带格式插入（HTML 里有 `<h1>`）。
    - **候选：** `build/full-source-run-office-ai`（宿主 `0def1f43…`，构建 `office-ai-3`）。
    - **真实会话 `office-ai-session1`：** 打开球后按钮正确（起草…、总结全文、提取待办、全文校对、本地 AI）；点全文校对后 gpt-oss:20b 6 分钟没出结果，关闭会话（样本未保存，哈希未变）。原因见第 38 条。
    - **未改动：** 默认入口、各回退目录、安装包、需求原文。

38. **慢的原因和改进（2026-11-08，用户：“好慢啊”）：**
    - **查明原因：**
      - 这台电脑：内存 31.5 GB，显卡 RTX 5070 Ti Laptop 12 GB。原来只按内存选模型，选中了 gpt-oss:20b（13 GB）。Ollama 只能把其中 9.6 GB 放进显卡，其余在处理器上算，实测每秒约 0.8 个词，两句话的校对要 150 秒。
      - gpt-oss 不能关掉推理，加上结构化输出（`format`）时整个请求卡住，6 分钟没有结果，还挡住了后面的请求。
      - 另一个模型 granite4.1:3b 很快（4.6 秒），但中文弱，校对找不出错。
    - **用户的决定：** 同意下载 qwen3:8b；随后要求删除中文很差的模型、新模型放 D 盘；之后说不要关闭 TUN、跳过下载这步。
      - 已通过 Ollama 接口删除 granite4.1:3b（D:\models 从 15 GB 降到 13 GB）。
      - Ollama 本来就设置了用户环境变量 `OLLAMA_MODELS=D:\models`，C 盘的模型目录是空的，所以新模型本来就会下到 D 盘；没有改任何设置。
      - 下载 qwen3:8b 失败：`redirect target not allowed: …r2.cloudflarestorage.com resolves to non-public 198.18.1.56`。Clash Verge 的 Fake-IP 模式把模型库的下载地址解析成 198.18.x.x，Ollama 出于安全拒绝下载。没有改代理和网络设置；用户说稍后自己处理。
    - **改进（候选 `-r2` 宿主 `4dcf47f8…`，`-r3` 宿主 `76ab8d1a…`）：**
      - `totalVideoMemory()` 用 DXGI 读最大独立显卡的显存（小于 3 GB 视为没有）；`runsSlowly()`：放不进 85% 显存的模型（没有显卡时大于 3.5 GB 的）算慢。
      - `pickInstalledModel` 加入显存：放得进显卡 +60、放不进 -60，gpt-oss -25，大小加分到 40。这台电脑上装了 qwen3:8b 就选它，只有 gpt-oss 和 granite 时选 granite，24 GB 显卡上选 gpt-oss。
      - `recommendDownload` 按显存：7.5 GB 以上 qwen3:8b，4.5 GB 以上 qwen3:4b，没有显卡时按内存 qwen3:4b 或 1.7b。
      - 慢模型：状态行加“较慢”；回答（或任务）结束后每次会话弹一次“回答有点慢？”，说明原因并给“下载 X”或“改用 X”；“本地 AI”卡片里也有。实测速度低于每秒 5 个词也算慢（来自 Ollama 的 `eval_count / eval_duration`）。
      - 等待：“正在思考… N 秒”，超过 15 秒附说明；本地 AI 工作时发送键变成 ■，点一下停止，显示“已停止。”和“重试”。
      - gpt-oss 这类停不了推理的模型不再使用结构化输出。
      - 下载失败用中文说明：Fake-IP / 内网地址、磁盘满、找不到模型、网络不通各给一句可以照做的话。下载成功后自动改用新模型。
    - **测试：** 助手 30 项（新增：按显存选模型和推荐、下载失败说明、停止、gpt-oss 不用结构化输出）、界面 33 项、契约 21 项，共 84 项全过。
    - **真实会话 `office-ai-session2`（候选 r2，样本 `office-ai-sample-2.docx`）：**
      - 选中一句话后打开球：状态“本地模型 gpt-oss:20b · 只在本机运行 · 较慢”，按钮变成润色、纠错、精简、扩写、正式、翻译，说明行显示选中的字数和开头。
      - 点润色：发送键变 ■；点 ■ 后显示“润色 · 没有完成 / 已停止。”和“重试”，发送键恢复。
      - 回复刚出现时，按钮那一排有约一秒被裁掉一半，之后自己恢复正常（入场动画时的重绘），没有改。
      - 之后用户自己在这个窗口里拖动悬浮球、改选区试用；窗口留给用户，没有关闭。启动它的脚本在上一次运行结束时被终止，所以 `session.json` 没有退出记录。
      - r3 只比 r2 多一行：任务结束后也会弹“回答有点慢？”，由界面测试覆盖，没有再做真实会话。
    - **新增脚本：** `scripts/verify_office_ai_session.py`：等下载好合适的模型后，用来核对完整的办公助手会话（两个错别字已改、润色已替换、起草带标题样式、日志不含正文）。
    - **未做：** 完整的校对、润色、起草真实会话；没设为默认入口，没打安装包。

39. **内置 AI 引擎：llama.cpp 取代 Ollama（2026-11-08）：**
    - **用户的决定（侧边会话“btw: 有没有办法让一个轻量ai也能快速达到我的目前…”里做的，交接消息没送到本会话，后来用户转告）：** 改用内置引擎，Ollama 只作可选；本地模型是可选项，做一个设置页：列出支持的模型，能下载、删除、选存放位置；安装包不自带模型；默认不放 C 盘；按电脑实测分档；能用程序做的不交给 AI，交给 AI 的活要短。用户选择保留第 37、38 条的办公功能，只换引擎。
    - **侧边会话做的准备：** `scripts/fetch_local_ai_dependencies.py`（逐项批准的下载：llama.cpp b12202 官方 Windows 版、魔搭上 Qwen 官方 GGUF；校对 SHA256）、`evidence/local-ai-engine/`；引擎在 `dependencies/llama.cpp-b12202/{win-vulkan-x64,win-cpu-x64}`。它提醒：官方压缩包里没有 llama.cpp 的 MIT 许可证原文，随软件分发前要补上。
    - **先查清楚的事实：**
      - Ollama 存的 gpt-oss 是它自己的变体格式（架构名 `gptoss`），官方 llama.cpp 读不了。
      - `llama-server` b12202 提供 OpenAI 兼容接口；`--device` 选设备，`--fit on` 自动适配显存，`--offline` 不联网，`--api-key` 防止本机别的程序调用，`--reasoning-budget` 限制推理长度。本机看到两个设备：Vulkan0 是 NVIDIA 12 GB，Vulkan1 是 Intel 核显（显示 18 GB 共享内存）；只用独显。
      - 1.7B 模型实测（关推理）：普通回答 0.1 秒、每秒约 173 词；工具调用正确；带 JSON 结构的校对 0.5 秒。但关推理时润色会原样照抄、校对漏字；开推理（约 900 字的推理，2.5–6 秒）后润色合格、两个错别字都找到。
    - **新代码：**
      - `app/assistant/chat_client.h`：两种引擎共用的对话接口（`LocalModel`、`ChatOptions` 也移到这里）。
      - `app/assistant/engine_client.*`：内置引擎客户端，流式回答、工具调用（参数分片拼接）、JSON 结构回答、`timings.predicted_per_second` 记速度；消息格式在助手格式和 OpenAI 格式之间转换（工具结果对上 `tool_call_id`）。
      - `app/assistant/local_engine.*`：启动和停止 `llama-server`。程序目录的 `ai-engine/vulkan`、`ai-engine/cpu`（开发时可用 `OPEN_OFFICE_AI_ENGINE` 指到 `dependencies/llama.cpp-b12202`）；有独显时先 `--list-devices`，按 Windows 报的显卡名选设备，不用核显；没有独显用 CPU 版（`-c 8192`，一半线程）；只听 127.0.0.1 随机端口、一次性密钥、`--offline`、`--reasoning-budget 1024`、不弹控制台窗口；每 0.5 秒查 `/health`，4 分钟没好算失败；失败原因翻成中文；程序退出时结束引擎。测试用 `OPEN_OFFICE_AI_ENGINE_URL` 直接连已有的服务。
      - `app/assistant/model_catalog.*`：支持的模型 Qwen3 1.7B（Q8_0，1.8 GB）、4B（Q4_K_M，2.5 GB）、8B（Q4_K_M，5.0 GB），来自魔搭 `Qwen/Qwen3-*-GGUF`；推荐沿用第 38 条按显存的分档；存放位置：`OPEN_OFFICE_MODEL_DIR` > 设置 `ai/modelFolder` > 默认（系统盘以外、可用空间最大且超过 12 GB 的盘上的 `OpenOffice-AI/models`，没有才放用户数据目录）；`ai/builtinModel` 记住选的模型；`ai/useOllama`（或 `OPEN_OFFICE_AI_BACKEND=ollama|builtin`）切换引擎。
      - `app/assistant/model_downloader.*`：先查魔搭文件信息（大小、SHA256），写 `.part`、断点续传（Range）、下载前查磁盘空间、下完在后台线程算 SHA256，对上才改名；出错用中文说明。
      - `LocalAssistant` 重写：默认内置引擎；第一次用时启动引擎（状态显示“正在启动内置引擎…”），请求等引擎好了再发；打开球时预先启动；下载、删除、改用模型；停止键在等引擎时也能停。推理开关：小模型（< 3.5 GB）在快的引擎上（有独显，或实测每秒 40 词以上）开推理，否则关；gpt-oss 仍是 Ollama 那套。
      - `app/ui/ai_settings_dialog.*`：“本地 AI 设置”：引擎和显卡、启动状态、上次速度；存放位置（更改、打开，放在 C 盘时提示）；三个模型的大小、用途、“适合这台电脑”标记、下载/暂停/使用/删除（删除先确认）；高级里的“改用本机已安装的 Ollama”。悬浮球的“本地 AI”按钮和卡片里的“打开设置”都打开它。
      - 卡片：内置引擎有自己的“找不到内置引擎 / 下载一个本地模型（推荐、大小、存放位置）/ 本地 AI 已就绪”。
      - `scripts/stage_host_candidate_runtime.py` 加 `--ai-engine`：把两个版本复制成 `ai-engine/vulkan`、`ai-engine/cpu`，逐个核对 SHA256，记进标记文件。
      - `tests/manual/local_ai_smoke.cpp`（目标 `local_ai_smoke`，不进 CTest）：用真实引擎和真实模型跑一个任务，打印原始回答和程序的理解。
    - **真实会话发现并修掉的问题：**
      - `builtin-ai-session1`（候选 `-builtin-ai`，宿主 `6c5caf26…`）：设置页显示正确（独显、已启动、1.7B 正在使用、8B 标“适合这台电脑”）；校对给出“按排人员 → 按排”这种只删字的错改，润色原样照抄；起草菜单换两行按钮时按钮那一排被压扁。修法：只删字的改正只在删掉重复字（“的的→的”）时才给；校对说明里加一个例子；小模型开推理；按钮行先排版再算高度。会话里一次失误把“/保存”粘进了文档，已撤销，样本哈希未变。
      - `builtin-ai-session2`（候选 `-builtin-ai-r2`，宿主 `18bed569…`）：校对说“没有发现”。用 `local_ai_smoke` 查到原因：Qt 的 `QJsonObject` 会把键按字母排序，结构里 `corrected` 排在 `original` 前，引擎按结构顺序生成，小模型先写“改正”再写“原文”，就把错字原样抄进了改正。已把字段改名为 `error`、`fix`、`reason`（排序后顺序正确），解析时兼容旧名，并加了测试锁住顺序。这个修复在构建 `builtin-ai-4` 里，**还没装配、没在真实会话里验证**。
      - 另外：两个引擎同时占用显卡时会慢到 90 秒以上（只是测试时的情况）。
    - **测试：** 助手 36 项、界面 34 项、契约 21 项，共 91 项全过（构建 `builtin-ai-4`）。新增：消息格式转换、真实格式的流式回答和工具调用、设备选择、模型目录与存放位置、内置引擎首次使用即启动并回答、下载续传与 SHA256、设置页、校对字段顺序。原有 Ollama 测试改为显式 `OPEN_OFFICE_AI_BACKEND=ollama`。
    - **接手先做：**
      1. 复查四个任务（在项目根目录、Git Bash 里）：
         `export PATH="/d/Qt/6.11.1/msvc2022_64/bin:$PATH"; export OPEN_OFFICE_AI_ENGINE="D:/open office/dependencies/llama.cpp-b12202" OPEN_OFFICE_MODEL_DIR="D:/open office/dependencies/local-models/download"; ./build/full-source-host/bin/local_ai_smoke.exe proofread build/wasm-source/tmp/smoke-document.txt`（再换 `polish`、`formal`、`summary`）。期望校对给出“按排→安排”“笔计本→笔记本”。交接时校对已复查通过：原始回答字段顺序正确，程序得到这两条改正；但用了 52 秒、每秒约 10 个词，因为当时 Ollama 还占着 9.5 GB 显存（较早的 Ollama 版候选窗口把 gpt-oss:20b 载入了），显卡只剩约 0.6 GB，引擎大部分只能用处理器算。测速前先确认显存空闲（`nvidia-smi`；Ollama 的模型可用 `curl --noproxy '*' http://127.0.0.1:11434/api/generate -d '{"model":"gpt-oss:20b","keep_alive":0}'` 卸载，先问用户是否还在用）。润色、正式、总结三项交接时还在跑，结果在 `build/wasm-source/tmp/smoke-*.out`。以后可以考虑：显存被别的程序占满时，在界面上说明原因。
      2. `python -I scripts/stage_host_candidate_runtime.py --target build/full-source-run-builtin-ai-r3 --build-label builtin-ai-4 --ai-engine dependencies/llama.cpp-b12202`，再用 `run_full_source_gui_session.py`（加环境变量 `OPEN_OFFICE_MODEL_DIR`，或在设置页里选文件夹）走完整流程，样本复制 `fixtures/results/office-ai-sample.docx` 成新文件；用 `scripts/verify_office_ai_session.py` 核对。
      3. 往 `ai-engine` 补 llama.cpp 的 MIT 许可证原文（需要用户批准下载或给出来源），并写进第三方说明。
      4. 1.7B 模型的 SHA256 还没和魔搭公布值核对（魔搭接口连接被重置）。
      5. 问用户：是否把内置引擎版设为默认入口、重新生成安装包（安装包会带 `ai-engine`，约多 60 MB，不带模型）。
    - **桌面操作注意：** 每次工具调用之间 Claude 窗口会抢焦点，悬浮球随之收起；点球和往输入框粘贴要放在同一批操作里，否则会粘进文档。设置页第一次点击只会把它提到前面。
    - **没提交的文件：** 交接时 `打开最新版.cmd`、`打开稳定版.cmd`、`版本一览.cmd`、`scripts/open_runtime.py` 来源不明，没有提交；后来查明是侧边会话按用户要求建的，见第 40 条。
    - **未改动：** 默认入口 `build/full-source-run-local-ai`、各回退目录、安装包、需求原文。

40. **一键找到版本的启动文件（2026-11-08，侧边会话按用户要求新建，本会话核对后提交）：**
    - **用户的问题：** build 里版本太多，找不到最新版。
    - **新文件（都在项目根目录或 scripts，不改任何共用文件）：**
      - `打开最新版.cmd`：启动 build 里最近构建的 `OpenOffice.exe`，可能是还没验证完的候选版。
      - `打开稳定版.cmd`：启动默认入口，也就是 `scripts/build_installer.py` 里 `SOURCE` 指的目录（现在是 `build/full-source-run-local-ai`）。
      - `版本一览.cmd`：列出所有可运行的版本，新的在上，标出“最新”和“稳定版·默认入口”。
      - 背后是 `scripts/open_runtime.py`（`--list`、`--newest`、`--default`）：每次现算，只列出和启动，不移动、不改名、不删除任何目录。启动方式和双击 `OpenOffice.exe` 一样（用运行目录里自己的 profile）。
    - **以后换默认入口：** 照旧改 `build_installer.py` 的 `SOURCE`，“稳定版”会跟着变。
    - **注意：** 用这些文件打开内置引擎版时，模型默认找 `D:\OpenOffice-AI\models`（这台电脑的默认位置），而用户下载的 1.7B 在 `dependencies\local-models\download`。第一次要在“本地 AI 设置 → 更改…”里选那个文件夹，或把模型文件移过去。
    - **侧边会话同时告知：** `Qwen3-1.7B-Q8_0.gguf` 已下载完并核对 SHA256 无误（补充第 39 条“接手先做”第 4 点：已核对）；`Qwen3-4B-Q4_K_M.gguf` 下载时连接被重置、还没开始，重新运行 `python -I scripts/fetch_local_ai_dependencies.py models` 可续上（下载前确认仍在用户批准范围内）。

41. **交接时的真实引擎复查结果（2026-11-08，`local_ai_smoke`，构建 `builtin-ai-4`，Qwen3 1.7B，当时显存被 Ollama 占着，每秒约 8–10 词）：**
    - 校对：52 秒，给出“按排→安排”“笔计本→笔记本”，字段顺序正确（第 39 条的修复有效）。
    - 润色：68 秒，两个错别字都改了，语句更正式，段落保留。
    - 正式：47 秒，整体合格，但把“笔记本”写成了“笔记本电脑”（小模型编造）；可考虑在这类任务后用程序比对，提醒“改动了原文的实词”。
    - 总结：56 秒，主旨和要点（时间、地点、要求、截止）都对；`markdownToHtml` 不支持缩进的子列表，子项被摊平成同一层，可改进。
    - 结果原文在 `build/wasm-source/tmp/smoke-*.out`（不在 git 里）。显存空闲时同样的任务应在几秒内完成（见第 39 条）。

42. **新会话只读核对，改正速览里过时的说法（2026-11-09T04:20Z，本机时间 2026-11-08 20:20，Claude Code）：**
    - **用户要求：** 查看接手文档。
    - **核对结果：** git 工作区干净，与 `origin/main` 同步（最新提交 `8be528b`）；`build/full-source-run-builtin-ai-r3` 还不存在；`build/full-source-host/bin` 是 20:13 的构建（`builtin-ai-4`）；`smoke-*.out` 四份都在；模型目录只有 1.7B；`build_installer.py` 的 `SOURCE` 仍是 `build/full-source-run-local-ai`。
    - **显存：** Ollama（`ollama serve`，20:06 启动）载着 gpt-oss:20b，约 10 GB 显存，到 20:43 自动卸载；显卡共用 11.8/12.2 GB。进程列表里的 `llama-server.exe` 是 Ollama 自己的（路径在 Ollama 安装目录、父进程是 `ollama serve`），不是本软件的内置引擎。没有卸载、没有结束任何进程。
    - **改动：** 只改本文。第 0 节“正在做”和“下一步”第 0 项：复查已完成（第 41 条），并加了显存提醒；第 0 节模型说明：1.7B 的 SHA256 已核对（第 40 条）；顶部“最后更新”时间。没有编译、运行或装配任何东西。

43. **显存空闲时重跑复查，装配 r3（2026-11-08 本机时间 20:22–20:30，Claude Code）：**
    - **用户要求：** 回答“可以”，同意接着装配 r3，并同意把 gpt-oss:20b 从显存卸载。
    - **卸载：** `curl --noproxy '*' http://127.0.0.1:11434/api/generate -d '{"model":"gpt-oss:20b","keep_alive":0}'`，回 `done_reason: unload`；`/api/ps` 为空；显存 11.8 GB → 2.6 GB。Ollama 进程没动。
    - **重跑 `local_ai_smoke`（构建 `builtin-ai-4`，Qwen3 1.7B，`OPEN_OFFICE_AI_ENGINE` 指向 `dependencies/llama.cpp-b12202`）：** 输出在 `build/wasm-source/tmp/smoke-vram-free/*.out`（不在 git 里）。
      - 校对：8 秒（含引擎启动），每秒 153 词，得到“按排→安排”“笔计本→笔记本”，原始回答字段顺序为 `error`、`fix`、`reason`。
      - 润色：7 秒，每秒 154 词，两个错别字都改了，**但漏掉了“大家要带好笔计本，认真听讲”这句和落款“行政部”**。第 41 条那次没漏，说明是小模型抽样不稳定。
      - 正式：7 秒，每秒 158 词，结构完整；加了原文没有的“笔和”（携带笔和笔记本）和“负责人”（各部门负责人）。
      - 总结：6 秒，每秒 154 词，要点全对；“参加要求”里照抄了原文的错字“笔计本”；这次是平的列表。
      - 结论：显存空闲时速度没问题，比第 41 条快 7–9 倍。质量问题主要是漏句和添词，建议加程序检查（对比原文句子和实词，结果缺句或改实词时提示），而不是只靠提示词。
    - **装配：** `python -I scripts/stage_host_candidate_runtime.py --target build/full-source-run-builtin-ai-r3 --build-label builtin-ai-4 --ai-engine dependencies/llama.cpp-b12202`。复制 20,972 个文件，20,971 个逐个与 `build/full-source-run-wasm-candidate` 一致（另一个是被替换的 `OpenOffice.exe`），宿主 SHA256 `7f5cbb10659da14fd1fbcbd84aaa192b371d1a2ee7a0efe6af8214e7f49d8e1c`；记录 `evidence/host-candidate/candidate-runtime-full-source-run-builtin-ai-r3.json`。
    - **真实编辑器会话：** 已启动 `builtin-ai-session3`（进程 35580），样本 `fixtures/results/office-ai-sample-5.docx`（从 `office-ai-sample.docx` 新复制，SHA256 `ff48031c…7d7f` 与原样本相同）。没有屏幕控制工具，由用户手动操作；结果见下一条。
    - **未改动：** 默认入口 `build/full-source-run-local-ai`、各回退和旧候选目录、安装包、需求原文。

44. **r3 会话里发现两个问题；修好按钮被压扁，装配 r4（2026-11-08 本机时间 20:32–20:55，Claude Code）：**
    - **问题 1：打开悬浮球时 Ollama 又载入了 gpt-oss:20b（显存又满，r3 的内置引擎只能用处理器）。** Ollama 日志 `%LOCALAPPDATA%\Ollama\server.log` 显示：20:32:38 r3 依次请求 `/api/version`、`/api/tags`、`/api/generate`（预热），20:32:54 又查了一次，20:32:59 r3 才启动内置引擎。说明 r3 启动时共用设置 `HKCU\Software\Open Office Project\Open Office\ai\useOllama` 是开着的，几秒后又被关掉（多半是用户在设置页取消了“改用 Ollama”，已问用户确认）。代码里只有设置页的勾选会写这个值；测试进程在 `initTestCase` 里用环境变量指定引擎，不读写它。所以不是程序缺陷，是之前某次真实会话留下的设置。现在是 `false`。同一处还存着 `ai/modelFolder = D:/models`（Ollama 的模型目录，里面没有 GGUF），用启动文件直接打开内置引擎版时会显示“没有模型”，要在设置页改位置或设环境变量。已再次卸载 gpt-oss:20b（仍在用户同意的范围内）。
    - **问题 2（用户截图）：悬浮球里“已选中 114 字：……”那行和下面一排快捷按钮被压扁，只露出上半截。** 根因：新按钮是在已显示的按钮区里创建的，Qt 要到下一轮事件循环才真正显示它们；排版不算隐藏的部件，所以按钮区的高度只算了说明文字那一行，按钮显示出来后只能挤进过矮的区域。第 39 条“按钮行先排版再算高度”的修复没覆盖这种情况。
      - 新测试 `quickActionBarFitsChipsThatArriveWhileOpen`（`tests/native/native_ui_test.cpp`）：打开悬浮球后换三次按钮，检查说明文字和每个按钮都不比自己需要的高度矮、也不超出按钮区。修复前构建 `ui-bar-red-1` 失败：说明文字被压到 3 像素（需要 12），按钮全部超出按钮区（`build/full-source/host-evidence/ui-bar-red-1`）。
      - 修复（`app/ui/assistant_ball.cpp`）：新按钮量尺寸前先套用样式（`ensurePolished`），放进行后立刻 `show()`；换下的旧按钮先隐藏再删除；按钮区装事件过滤器，内容变化（`LayoutRequest`）时重新摆放。
      - 构建 `builtin-ai-5`：CTest 3 组全过，共 92 项（助手 36、契约 21、界面 35）。
    - **装配 r4：** `python -I scripts/stage_host_candidate_runtime.py --target build/full-source-run-builtin-ai-r4 --build-label builtin-ai-5 --ai-engine dependencies/llama.cpp-b12202`：复制 20,972 个文件，20,971 个逐个一致，宿主 `05919c83931b3e150c539fbaa65b154d5d5fa9b9a4c048f2fe91f219972b11d2`；记录 `evidence/host-candidate/candidate-runtime-full-source-run-builtin-ai-r4.json`。新样本 `fixtures/results/office-ai-sample-6.docx`（SHA256 与原样本相同）。
    - **`builtin-ai-session3`：** 作废，不用于验收；用户正在关窗口，关后证据留在 `evidence/full-source-gui/builtin-ai-session3/`。
    - **未改动：** 默认入口、各回退和旧候选目录（含 r3）、安装包、需求原文。

45. **r3 会话结果；修掉改写结果的行尾空格；装配 r5（2026-11-08 21:11 至 2026-11-09，Claude Code）：**
    - **`builtin-ai-session3` 结果（r3，退出码 0，`evidence/host-candidate/builtin-ai-session3-result.json`，9 项检查 7 项过）：** 用户选中全文点“纠错”再“替换原文”（`selection.replace` 1 次），没有用“全文校对”；保存是在助手里让 AI 执行的（日志里 `document.save` 来源是 `assistant`），不是 `/保存`。所以“AI 改动与用户保存”一项不过，这是操作路径不同，不是程序缺陷。两个错别字都改对了，38 条编辑器命令全部有回报，日志不含正文。
    - **发现的缺陷：** 另一项不过是因为保存后的前四段末尾都多了两个空格（如“关于开展安全培训的通知␣␣”）。这是模型按 Markdown 习惯在行尾加的强制换行，`cleanRewrite` 没去掉就写进了文档。修法（`app/assistant/markdown_html.cpp`）：清理改写结果时去掉每行末尾的空格、制表符和不换行空格。`tests/native/assistant_test.cpp` 的清理测试加了这个例子（没有先在旧代码上单独跑一次失败，按代码逻辑旧版会保留这些空格）。另外注意：`cleanRewrite` 最后的 `trimmed()` 会去掉开头的全角空格缩进，这次没改。
    - **显存：** 关窗后显存仍是 11.6 GB，占用者是用户另一个 Claude 会话在 `D:\claude视频` 跑的 Qwen3-TTS 批处理（`qwen_tts_batch.py`），没有动它；2026-11-09 再查时已跑完，显存 1.7 GB。
    - **构建 `builtin-ai-6`：** CTest 3 组全过，共 92 项。
    - **装配 r5：** 第一次运行被用户中途打断，留下没复制完的 `build/.full-source-run-builtin-ai-r5.staging-c4129cb8f17345599b7b2d51498ec7e5`（约 1.6 GB），按规则不删除。随后重新运行同一命令（`--target build/full-source-run-builtin-ai-r5 --build-label builtin-ai-6 --ai-engine dependencies/llama.cpp-b12202`），因为用户另一个项目同时在渲染、硬盘忙，超过 10 分钟，转后台继续；结果见下一条或 `evidence/host-candidate/candidate-runtime-full-source-run-builtin-ai-r5.json`。
    - **未改动：** 默认入口、各回退和旧候选目录（含 r3、r4）、安装包、需求原文。

46. **r5 装好；用户问“下载的模型怎么没了”（2026-11-09，Claude Code）：**
    - **r5：** 后台重跑的装配成功，`build/full-source-run-builtin-ai-r5`，记录 `evidence/host-candidate/candidate-runtime-full-source-run-builtin-ai-r5.json`。这次的临时目录 `.staging-a76a56fe…` 已正常改名成 r5；第一次被打断留下的 `.staging-c4129cb8…` 仍原样保留。
    - **模型没丢：** `dependencies/local-models/download/` 里有 `Qwen3-1.7B-Q8_0.gguf`（1,834,426,016 字节）和 `Qwen3-4B-Q4_K_M.gguf`（2,497,280,256 字节，2026-11-08 21:04）。4B 是用户在 r3 会话窗口的设置页里下载的：那个窗口带着 `OPEN_OFFICE_MODEL_DIR` 指向这个目录，所以下到了这里；下载器核对 SHA256 后才把 `.part` 改名，文件完整。用户还选了它（注册表 `ai/builtinModel = qwen3-4b`）。
    - **为什么“没了”：** 用户随后用启动文件直接打开 r4（01:56，没有环境变量），程序按注册表 `ai/modelFolder = D:/models` 找模型。那是 Ollama 的模型目录，没有 GGUF，所以显示没有模型。
    - **产品问题（还没改）：** 测试用的环境变量会悄悄改变下载位置，设置页存的目录又是另一个地方，用户看不出模型去了哪里。可以考虑：所选目录里找不到模型时，顺便查一下其他已知位置（上次下载的目录、默认目录），找到就提示“在 X 找到 N 个模型，要改用这个位置吗”。（第 47 条按用户的决定改成了固定位置，这条建议不再需要。）

47. **模型固定放在软件文件夹，加“迁移”功能（2026-11-09，Claude Code）：**
    - **用户要求：** “以后下载模型默认下载在软件本体所在的文件夹中，无法更改文件夹，但是可以去设置中迁移数据文件夹，将整个 openoffice 移动”。
    - **模型位置（`app/assistant/model_catalog.*`）：** `modelFolder()` 改为 `程序目录/models`；`OPEN_OFFICE_MODEL_DIR` 仍可覆盖（开发和测试用）。删掉 `defaultModelFolder()`、`setModelFolder()`，注册表 `ai/modelFolder` 不再读。
    - **设置页（`app/ui/ai_settings_dialog.*`）：** “模型存放位置 / 更改…”换成“软件位置”：显示程序目录，按钮“打开”“迁移到其他位置…”；说明里写模型在 `models`、现有多少 GB，在 C 盘时提示可以迁移到其他盘，有开发环境变量时直说。悬浮球“下载一个本地模型”卡片的文字同步改了。
    - **迁移流程（`app/ui/main_window.*`、`app/main.cpp`、`app/ui/relocation.*`）：** 正在下载、文档操作没结束、文档没保存时先拒绝并说明原因；选文件夹（目标是“所选文件夹/程序文件夹名”）；检查：不能是网络位置、不能在自己里面、目标不能已存在、要能写；跨盘时检查空间（程序加模型再多 512 MB）；确认框写明大小、来源和去向。确认后正常关闭窗口；关闭失败或在保存提示里取消时作废。程序完全退出后，`main()` 才启动搬家脚本（`powershell.exe -EncodedCommand`，不受脚本执行策略限制，无窗口，尝试脱离作业对象）。
    - **搬家脚本（内嵌在 `relocation.cpp`，只用 ASCII）：** 最多等 2 分钟，等程序及其目录里启动的所有进程（编辑器辅助进程、AI 引擎）退出；同盘用改名，跨盘用 robocopy 复制，再核对文件数和总字节，不一致就删掉这次复制出来的东西、原文件夹不动；然后删旧文件夹（删不干净报 `moved-leftover`）；把开始菜单程序和桌面里指向旧位置的快捷方式改到新位置；把卸载信息里 `Inno Setup: App Path` 等于旧位置的那一项的路径值改掉；最后在新位置（失败时在原位置）用 `--relocation-log` 重新打开，程序据此弹出中文结果。
    - **安装脚本（`installer/OpenOffice.iss`）：** 加 `[UninstallDelete]`，卸载时删 `openoffice-profile` 和 `models`（以前卸载会留下配置）；加 `[Code]`：卸载开始时读卸载信息里的 `Inno Setup: App Path`，卸载结束后如果那里还有 `OpenOffice.exe`（说明搬过家、安装记录里是旧路径），就整个删掉那个文件夹。
    - **开发启动文件（`scripts/open_runtime.py`）：** 启动时若 `dependencies/local-models/download` 存在且没设环境变量，就设 `OPEN_OFFICE_MODEL_DIR` 指向它，所以 `打开最新版.cmd` 等打开的开发版本都能用已下载的 1.7B 和 4B。
    - **测试：** 构建 `relocate-2`，CTest 3 组共 95 项全过（助手 36、契约 21、界面 38）。新增：模型位置默认在程序目录；设置页没有“更改…”、有“迁移…”、说明写出开发环境变量；`relocationPlanChecksTheChosenFolder`（目标、同盘、各种拒绝）；`relocationHelperMovesTheFolderShortcutsAndUninstallEntry`（用真实脚本搬一个带中文名的替身文件夹：同盘一次、跨盘一次（临时目录在 C 盘，构建目录在 D 盘），核对文件、空文件夹、快捷方式三项、卸载信息四项、无关的卸载项不动，测试注册表项清理掉）；`relocateAsksConfirmsAndClosesWithThePlan`（取消选文件夹、取消确认、确认后窗口关闭并带着计划，且还没移动任何东西）。第一次构建 `relocate-1` 因一个字符串里混入了真换行而编译失败。
    - **真实安装包检查：** `python -I scripts/check_installer_relocation.py inno-1` 通过（`evidence/installer/relocation-check-inno-1.json`）：替身程序的安装包用真实 `.iss` 和真实 AppId 编译（这台电脑没装安装版，先核对过），静默安装、放入假模型和配置、用脚本迁移（同盘改名）、卸载信息四项都指向新位置、从新位置静默卸载后新旧位置都没有残留、卸载信息删除；不搬的那一组卸载后模型和配置也删掉了。跨盘复制在安装版上没有单独测，CTest 里测过。
    - **还没做：** 在真实编辑器里点一次“迁移”（要用户操作）；用大文件夹（约 2 万个文件、带模型）实测跨盘复制的时间；README 还写着 Ollama 版，等内置引擎版设为默认入口时一起改。
    - **装配 r6：** `python -I scripts/stage_host_candidate_runtime.py --target build/full-source-run-builtin-ai-r6 --build-label relocate-2 --ai-engine dependencies/llama.cpp-b12202`：复制 20,972 个文件，20,971 个逐个一致，宿主 `b37743e348aa645e56147be1371f0ecf7fe8690e08f1ec2930e25269a8d909c8`；记录 `evidence/host-candidate/candidate-runtime-full-source-run-builtin-ai-r6.json`。
    - **迁移的真实界面测试打算：** 不在 r6 本身上点迁移（会把候选目录搬出 build）；另复制一份 r6 当一次性副本，让用户在副本里点“迁移”。

48. **对话：新对话、历史对话、工作文件、`/` 指令列表（2026-11-09，Claude Code）：**
    - **用户要求：** “开新文件后上下文不会重置，我需要你新增一个开启新对话的指令，关闭软件后重新打开会自动创建一个新对话，也可以选择历史对话返回原来的对话，也可以看见对话所在的工作文件，也可以管理对话，增加打/后显示指令列表”。
    - **我做的选择（已告知用户，可推翻）：** 打开或新建另一份文档时也自动开新对话（旧的进历史）；另存为只改工作文件名，不换对话；打开的文档就是对话的工作文件时不换。对话内容存本机（含用户和 AI 说的话，可能有文档片段），与只记元数据的命令日志分开。“新对话”“历史”是悬浮球自己的指令，不进 AI 的工具清单。
    - **存储（`app/assistant/conversation_store.*`）：** `程序配置文件夹/conversations/<编号>.json`（`main.cpp` 用 `openoffice-profile` 下的 `conversations`；没有文件夹时只在内存里，测试用）。内容：标题（用户改过的不再自动替换）、工作文件路径和名称、创建/更新时间、气泡（角色、标题、文字、成败、是否 Markdown）、AI 的上下文 `modelHistory`。`QSaveFile` 写入；编号必须是 `yyyyMMdd-HHmmss-6位十六进制`，`../` 之类直接拒绝。空对话不保存。
    - **AI 记忆（`LocalAssistant::conversation()` / `setConversation()`）：** 新对话传空，打开旧对话传它保存的上下文；只在没有回答进行时切换（按钮在忙时禁用）。
    - **悬浮球（`app/ui/assistant_ball.*`）：** 快捷按钮那一行的说明文字右边加“新对话”“历史”两个文字按钮（显示历史时“历史”变“返回”）。每个要保留的气泡通过 `messageRecorded` 报给主窗口：用户的话、AI 最终回答（带标题和 Markdown 标记）、指令结果（选中文字的引文最多 600 字）、校对清单（逐条写成文字）、无法识别的指令；设置卡片和“正在思考”不记。历史列表临时替换滚动区里的气泡（`takeWidget`/`setWidget`，原气泡原样保留），每条是一张卡：标题、“当前对话 · 工作文件：… · 今天 14:30 · N 条”、打开/重命名/删除；重命名就地编辑（回车保存，Esc 取消且不收起悬浮球）；删除和“删除其他所有对话”都要点两次（第一次变“确认删除”，3 秒后复原）。恢复旧对话时不再重复记录。新气泡到来时自动回到对话。
    - **`/` 列表：** 输入框里以 `/`（或全角 `／`）开头、还没有空格时，在胶囊上方弹出列表：“/新对话”“/历史”在前，然后是 `/命令 /状态 /新建 文档 /保存 /插入 文字 /选中 /全文 /撤销 /重做`，每条带说明；继续打字按开头筛选；↑↓ 选择，回车：要跟文字的（插入、新建）补全成“/插入 ”等着打字，不要的直接执行；Tab 只补全；Esc 只关列表；点选同回车。说明来自新加的 `slashHints()`（`app/assistant/assistant_commands.*`），`/命令` 的列表也加上了这两条。
    - **主窗口（`app/ui/main_window.*`）：** 启动时建一个空对话；打开/新建/保存完成后按上面的规则跟随文档；打开旧对话时，如果它的工作文件不是当前文档，显示一张“工作文件”卡片说明，文件还在就给“打开 文件名”按钮（打开后因为文件一致，不会再换对话）；删除当前对话会清空并开新对话。
    - **测试：** 构建 `conv-4`，CTest 3 组共 98 项全过（助手 37、契约 21、界面 40）。新增：`conversationsAreKeptOnePerFile`（存取、排序、Markdown 和 AI 上下文往返、标题截短、重命名、非法编号、删除其他、内存模式）；`conversationsStartOverKeepHistoryAndFollowTheDocument`（用假的 Ollama：新对话后 AI 请求里没有上一个对话的话；历史列表顺序、当前标记、工作文件；打开旧对话后 AI 请求里有旧对话的问答、没有新对话的；重命名；两次点击删除且文件被删；换文档自动开新对话；新窗口是新对话、旧对话仍在）；`slashListOffersCommandsAndCompletesThem`（全部列出、筛选、回车补全、↓ 回车执行“历史”、Esc 只关列表、未知词和不在开头时不弹）。中间几次失败都是测试写法：Qt 的模拟按键不支持中文字符（`conv-1` 整个界面测试因此中断）；新对话后快捷按钮会马上读一次选区，测试要等空闲；测试引擎异步完成打开，第二次打开要等完成消息。
    - **装配 r7：** `python -I scripts/stage_host_candidate_runtime.py --target build/full-source-run-builtin-ai-r7 --build-label conv-4 --ai-engine dependencies/llama.cpp-b12202`：复制 20,972 个文件，20,971 个逐个一致，宿主 `e8ca75428506a60324f316ed7eaaab4ce4414ab3647eff94f10c2021e1293cd2`；记录 `evidence/host-candidate/candidate-runtime-full-source-run-builtin-ai-r7.json`。
    - **r6 会话 `builtin-ai-session4` 的结果（`evidence/host-candidate/builtin-ai-session4-result.json`）：** 退出码 0，5 条编辑器命令（4 次读选区、1 次读全文）全部回报，日志不含正文；用户只是试用（提问），没有改正、润色或保存，样本 `office-ai-sample-6.docx` 哈希不变，所以 9 项检查有 3 项不过。用户在这次试用中提出了本条的要求。不算 AI 改文档流程的验收。
    - **还没做：** 真实编辑器里看对话列表和 `/` 列表的样子；对话数量和单个对话长度没设上限（悬浮球只显示最近 40 个气泡）。
    - **r7 会话 `builtin-ai-session5`（`evidence/host-candidate/builtin-ai-session5-result.json`）：** 2026-11-09 11:14:14–11:16:41 UTC，退出码 0；没有任何助手命令、没有保存的对话、样本 `office-ai-sample-7.docx` 哈希不变。不算验收。
    - **更正：** 起初以为用户没用悬浮球。实际上用户 11:14:48 UTC 自己又开了一个 r7 窗口（`build/full-source-run-builtin-ai-r7/OpenOffice.exe`，不经会话记录器，用 r7 目录里的默认配置 `openoffice-profile`），关掉的是我开的那个；11:17:35 内置引擎在用户的窗口里启动（1.7B）。用户关窗后可以从 `build/full-source-run-builtin-ai-r7/openoffice-profile` 的 `assistant-commands.jsonl` 和 `conversations/` 查看试用情况。

49. **打字提的要求由程序分流（2026-11-09，Claude Code）：**
    - **用户反馈：** “有点问题，我选中一段文字让他详细点，他却给了我一大段完整的md字，还要在光标处插入，这不符合我的需求”（在自己打开的 r7 窗口里，见第 48 条更正）。
    - **查到的原因（r7 的 `openoffice-profile/assistant-commands.jsonl` 和 `conversations/`）：** “详细点”被当成普通对话交给带工具的模型；1.7B 没用 `selection_replace` 而用了 `text_insert`（默认插在光标处）；其中两次插入没带 `format`，`###`、`**` 原样进了文档；插完后回答又把整篇重复一遍（系统提示里“工具执行后用一句话告诉用户结果”没被遵守）。之前“写一篇文章”也被拆成三次插入、位置不一。
    - **改法（用程序判定，不交给小模型）：**
      - `classifyRequest()`（`app/assistant/assistant_tasks.*`）：以 `/` 开头→对话；问题（以？/?/吗/呢/么结尾，或含 什么/为什么/意思/解释/是否/有没有/哪些/多少 等）→对话；“好的/谢谢/收到”等→对话；有选中文字且不超过 60 字→`ReworkSelection`；以（请/帮我/给我）写/起草/拟/撰写/编写/生成 开头→`Write`；其余→对话。
      - 两个不在快捷按钮里出现的任务（`TaskSpec::offered = false`）：`rework`“按要求改写”（选中文字＋用户原话，沿用改写任务“不用 Markdown、按原文分段、不编造事实”的约束，结果走“替换原文/插到后面/复制/重写”）；`write`“按要求起草”（Markdown，用 `##` 小标题分节，只有必须由用户补充的具体信息才用【】，结果走“插入文档/复制/重写”）。`MainWindow::askLocalAi` 读完全文和选区后按分类启动任务或对话。
      - 模型提议的 `text.insert` 没带 `format` 但内容像 Markdown（`looksLikeMarkdown`：标题、两条以上列表、**粗体**、表格分隔线）时，按 Markdown 转成真格式插入；只对模型的插入这样做，用户自己 `/插入` 的不变。
      - `LocalAssistant` 记下这一轮成功插入/替换的文字；这一轮最后的回答若重复了其中两行以上（去掉 Markdown 标记后比较），用 `withoutEchoedText` 去掉重复部分和前面“内容如下：”一类的引导行，什么都不剩时改成“已经写进文档了，可以在文档里查看。需要调整的话告诉我。”；保存进 AI 记忆的也是删减后的。
    - **测试：** 构建 `route-4`，CTest 3 组共 101 项全过（助手 39、契约 21、界面 41）。新增：`typedRequestsGoWhereThePersonMeant`（17 种说法的分类，两个任务不在快捷按钮里、提示里带上用户原话和原文）；`modelMarkdownAndEchoesAreTidied`（Markdown 识别；用真实会话里的重复回答做例子）；`typedRequestsUseTasksAndModelInsertsAreTidy`（假 Ollama：选中后“详细点”的请求不带工具、“替换原文”发 `selection.replace` 并核对原文；“写一篇…”不带工具、“插入文档”带 `<h1>`；普通对话里模型不带格式的 Markdown 插入变成 `<h2>`，重复的回答变成一句话）。`tests/manual/local_ai_smoke.cpp` 加了第三个参数（用户原话）。
    - **真实模型（1.7B，显卡，`build/wasm-source/tmp/smoke-route/`，不在 git 里）：** “详细点”5.3 秒，结果是更具体的普通文字、没有 Markdown；“写一篇关于高效学习的短文”第一版提示下 6.4 秒，内容好但把【】当小标题用；改提示后再跑两次（6.4、7.3 秒）：都用 `##` 分节，一次完全干净，一次仍有几处把普通词放进【】并在末尾加了【需补充】——小模型不稳定，结果先给用户看、可“重写”。
    - **装配 r8：** `python -I scripts/stage_host_candidate_runtime.py --target build/full-source-run-builtin-ai-r8 --build-label route-4 --ai-engine dependencies/llama.cpp-b12202`，后台进行中，结果见 `evidence/host-candidate/candidate-runtime-full-source-run-builtin-ai-r8.json`。
    - **还没做：** 真实编辑器里复查；没有选中文字时的“详细点”仍走对话（不知道指哪段）；分类规则是关键词，遇到没覆盖的说法会走对话。

## 附录 A：requirements.md 完整原文

以下代码块内容按原始字节嵌入，不加行号，不修改标点、编码或换行。

- 原文字节：14275
- 行数：212
- SHA256：`1e7d43e339deb30766561fb315e87585ea3bb06c32cb11678f84059d379c19ff`

<!-- BEGIN REQUIREMENTS ORIGINAL BYTES -->
````markdown
# AI 办公软件项目：需求总结

整理自与 Claude 的对话 · 2026 年 11 月 1 日

> **阅读说明**
> - 标注【要求】的是用户明确提出的要求和决定。
> - 标注【建议】的是 Claude 在对话中提出、尚未经用户确认、需要实测验证的内容。
> - 给 Claude Code 的用户：当前阶段只做 `TASK.md` 里的可行性实验。本文是背景资料，第二阶段（AI）的内容不要在本阶段实现。

---

## 一、项目定位与目标

- 【要求】做一个模仿 Microsoft Office 的办公软件。**第一版是不含 AI 的完整 Office 程序，“完整”的标准是接近微软 Office。** 之后再加入 AI 助手，并设置 MCP 来提升办公效率。
- 【要求】目标：
  - 打破办公软件的订阅和会员模式；
  - 所有功能免费（包括 Word、图片、PDF 转换等原本收费的功能）；
  - 文件兼容性高；
  - AI 在本地部署（第二阶段）。
- 【要求】项目完全开源。
- 【要求】对市面现状的判断：很多 Agent 做出的 Word、表格、PPT 都是同一个模板，不接入办公软件很难做得美观；希望发挥 AI 自身的审美（第二阶段）。
- 背景参考：对话开头研究了 GitHub 高星项目的爆火特征，用于参考产品和传播思路，不是需求。

## 二、产品与界面要求

- 【要求】目标平台：Windows。采用桌面版，不做网页版。
- 【要求】非常喜欢微软 Office 的操作手感，希望保留。这份手感包括四类：
  1. 打字时的光标和文字响应；
  2. 快捷键、鼠标选择、拖拽等操作习惯；
  3. 功能区和菜单的布局；
  4. 撤销、格式刷、自动填充等细节。
- 【要求】界面追求下面 10 条视觉打磨规范所描述的效果。**这 10 条只是参考，效果相同即可，实现方式不限。**
  1. 用 1% 到 3% 的明度阶梯区分层级，做无边框分层，不使用纯黑；
  2. 输入框聚焦时使用双层环形阴影，不用 outline；
  3. 深色模式下字重降低 40；
  4. 流式文字采用模糊渐入，每字 90ms，错开 30ms；
  5. 图标垂直对齐 -0.125em；
  6. 数字使用等宽（tabular-nums）；
  7. 折叠动画使用 grid 的 0fr 到 1fr；
  8. 使用 text-wrap: pretty；
  9. 弹簧动效：拖拽 1200/80（过阻尼），点赞和勾选 380/18（欠阻尼）；
  10. 复制成功的勾选图标 2.5 秒后自动淡出。
- 【建议，用户交给 Claude 决定】视觉方向：**手感遵循经典 Office，外观采用现代无边框风格。** 功能区、菜单、快捷键、编辑行为保持 Office 习惯；颜色、层次、动效和字体渲染按上面的规范处理。用户可随时推翻。
- “接近微软 Office”以可核对的差距清单来衡量（见 7.1）。

## 三、开发条件

- 【要求】主要用 vibecoding 开发，用户不能手动修改代码，依赖 AI 写代码。
- 【要求】使用 Claude Code 来编写项目代码。
- 用户之前用过 ONLYOFFICE。

## 四、技术路线与决定

- 【要求】**不基于微软 Office 做插件**，而是接入自己的编辑器；AI 层（审美和设计引擎、MCP）在第二阶段插在自己的编辑器上。
- 【要求】**底座必须开源，并在保证文件兼容性的前提下修改**，所有功能免费。
- 【要求】**底座已定：ONLYOFFICE，并保留其标识和署名**（用户已接受）。
  - 原因：ONLYOFFICE 在 AGPL 之外附加了“保留原标识、不授予商标权”的条款，并已因此与 Euro-Office 分叉发生法律争议。保留标识是规避风险的做法。
  - 以后是否改名或去标识，待争议明朗后再议。
  - 保留标识的具体位置和方式（比如关于页面、启动页），需要核对其许可证原文。
  - 本文不构成法律意见。
- 备选（仅在可行性实验失败时启用）：
  - Univer：核心为 Apache-2.0；但导入导出等在其 README 中列为 Pro 付费，幻灯片仍在开发。
  - LibreOffice：C++ 代码库巨大，修改难度高。
- 【建议】修改原则：由外到内。先改界面和外围（品牌、视觉、插件），最后才碰文档解析和渲染的核心；建立 docx、xlsx、pptx 的往返回归测试集，每次改动都要通过。

### 4.1 第一版不含 AI，但架构要预留的接口

【要求】预留 AI 的接口，为 AI 做准备。【建议】具体清单如下，后补成本很高，所以第一版就要做进架构：

1. 所有编辑操作走统一的命令通道，记录来源（人工或 AI）、修改前后的值，并支持撤销。
2. 元素有稳定的 ID 和语义角色（标题、正文、强调、图表等）。
3. 样式引用主题令牌，不写死数值。
4. 能读取排版度量（溢出、重叠、行数），能无界面渲染成 PDF 或图片。
5. 本地事件日志，只存样式值，不存文字内容。
6. 这些能力底座能否提供，要放进可行性实验里验证。

## 五、AI 设计要求（第二阶段）

以下为第二阶段（第一版完成之后）的要求。

### 5.1 AI 的工作方式

- 【要求】不是让 AI 直接生成文件，而是通过**专用的 MCP**，让 AI 输出特定指令来修改文档。
- 【建议】指令分层：原子操作、关系约束、设计意图、参数化生成；一次提交一批指令，带事务和撤销。
- 【建议，待决定】MCP 的定位：对外供其他 AI 驱动编辑器；内置助手是否直接走同一套命令层，待定。

### 5.2 对小模型的要求

- 【要求】尽可能降低对模型能力的要求，让本地小模型也能有很强的能力。
- 【要求】降低本地模型的部署成本和资源占用。

### 5.3 风格与审美

- 【要求】不希望所有文件都套同一个模板。
- 【要求】希望先了解用户常用的风格，再按文档主题偏向这种风格来制作，包括用户平时喜欢的颜色、喜欢的图表和形状。
- 【要求】想法：为小模型预先规定常用的格式、颜色、形状、图表等，并附上权重，让不常见的选项也能在需要时被选中。

### 5.4 记忆与多智能体

- 【要求】加入记忆功能。
- 【要求】多个智能体协同，分工合作。

### 5.5 后台静默学习

- 【要求】用户让 AI 助手修改之后，在后台静默分析：用户对同一处的修改次数、用户手动修改的位置；如果某处长时间未被修改，就记住用户修改后的风格。

## 六、学习信号表（【建议】，待用户确认）

这是对 5.5 的细化。原则：采集可以静默，但学到的内容必须在“我的风格档案”中可见、可编辑、可删除、可暂停；全部在本地用代码统计，不占用模型资源。

### 6.1 信号强弱与方向

| 用户行为 | 方向 | 强度 | 记录内容 | 排除条件和注意事项 |
|---|---|---|---|---|
| 用户从零新建元素并设置样式 | 正向 | 强 | 元素角色、属性、值、场景 | 排除从外部粘贴来的格式 |
| 手动修改 AI 输出的风格属性（颜色、字体、形状、图表类型），并且稳定 | 对用户终值为正，对 AI 原值为负 | 强 | AI 原值到用户终值 | 稳定的判定见 6.2 |
| 撤销 AI 的修改 | 负向 | 很强 | AI 的值 | 如果 AI 的输出违反规则（溢出、对比度不足），记为 AI 错误，不计入偏好 |
| 同一处反复修改多次 | 对 AI 的选择为负 | 中 | 修改次数、最终状态 | 中间状态忽略，终态稳定之前不写入偏好 |
| 保留 AI 输出未改动 | 正向 | 弱 | 该元素的风格 | 没改不等于喜欢，需要和导出、重复出现结合 |
| 文档被导出、分享或打印 | 确认 | 强 | 该文档当时的整体风格 | 作为“已接受”的确认信号 |
| 使用“应用到全部” | 正向 | 强 | 属性、范围 | 表示用户希望统一 |
| 手动切换整套风格 | 正向 | 强 | 所选风格、场景 | 无 |
| 修改文字或数据内容 | 无 | 不计 | 不记录 | 属于内容，不是风格 |
| 为塞下内容而缩字号、改间距 | 无 | 不计 | 不记录 | 属于一次性适配 |
| 修正 AI 的排版错误 | 记为 AI 错误 | 不计入偏好 | 错误类型 | 只用来改进规则检查 |
| 用户在档案中编辑、锁定、删除条目 | 直接生效 | 最高 | 用户设置 | 优先于一切自动学习 |

### 6.2 “稳定”的判定

不用单纯的时间长短，改用事件判定，满足任一条即可：

- 修改后又经过多次保存，或跨越多个会话，该处仍未再被改动；
- 文档被导出、分享或打印。

具体阈值（几次保存、几个会话）没有现成答案，需要用评测集或真实使用数据来调。

### 6.3 升级为长期偏好的规则

1. 先写入临时记录；同一模式在至少 3 处、至少 2 份文档或会话中重复出现，才升级为长期偏好（阈值待调）。
2. 按角色聚合，比如标题、正文、强调、图表，而不是按页面位置。
3. 偏好按主题场景条件化，例如财务汇报、活动方案；某场景样本少时，向全局偏好靠拢。
4. 加入时间衰减，偏好会随时间变化。
5. 偏好是软约束，可读性和排版质量规则是硬约束，偏好不能覆盖硬约束。
6. 品牌或团队规范的优先级高于个人偏好，档案支持按项目或品牌切换。

### 6.4 观察记录的数据结构

每条记录包含：元素角色、被修改的属性、AI 原值、用户值、动作（修改、撤销、保留）、所属场景、会话编号、是否已稳定。

### 6.5 冷启动与数据最小化

- 冷启动：从用户已有的 docx、xlsx、pptx 中用代码提取颜色、字体、图表类型、形状和间距习惯；没有历史文档时，让用户从几组样例里挑选。
- 数据最小化：观察记录只存样式值，不存文字内容；全部本地存储，可查看、编辑、删除、暂停。

## 七、已讨论的建议与风险（【建议】，待验证）

### 7.1 范围与衡量

- “接近微软 Office”仍然是开放的标准，需要做成可核对的差距清单（常用功能、快捷键、交互细节）并排优先级，否则永远做不完。
- 验收标准：用户选择暂缓；可行性实验的清单可以先当作最低门槛。

### 7.2 打分与候选

- 打分思路：得分 = 通用先验 + 用户在该主题下的偏好 + 场景匹配 + 少量探索（加权求和并设下限；连乘会让任何一项为 0 的候选永远选不中，冷门选项就没有机会）。
- 候选太多时分两步：先由代码或向量检索筛出 5 到 8 个，再由模型挑选。
- 权重挂在“整套搭配”上，避免孤立挑选导致搭配出问题；颜色用色彩算法生成并保证对比度，不枚举；图表类型主要由数据形态用规则决定。
- 目录用 JSON 等数据文件管理，加格式校验，可做成“风格包”开放给社区。

### 7.3 AI 与小模型分工

- 模型只做“小而明确的决策”：判断主题场景、在候选中选择；提取、统计、打分、补色、检查由代码完成。
- 用受约束解码保证输出格式合法，并用规则校验兜底。
- 视觉评审模型较重，建议做成可选的高配模式，默认用规则检查。
- 一次提交一批指令，带事务和撤销。

### 7.4 多智能体

- 对本地小模型，多智能体有额外开销，建议先做“单智能体加固定流水线”的基线，用评测集验证后再拆分。
- 角色可以是同一个模型配不同的系统提示和工具，分时复用，协调由代码编排而不是智能体自由对话。
- 真正需要模型的角色大致是：规划、内容、设计选择；执行、检查、记忆读写交给代码。

### 7.5 记忆

- 存结构化数据，不存聊天记录；由代码写入和召回；全部本地存储；用户可查看、编辑、删除、关闭。
- 稳定层（品牌色、常用字体、明确禁忌）可固化，可变层（版式、图形风格）保留探索空间。

### 7.6 主要风险

- 范围：见 7.1。
- 底座：ONLYOFFICE 已选定，需保留其标识；其构建说明被 Euro-Office 团队评价为过时甚至失效，Windows 下从源码构建桌面版可能很复杂，需实测；社区版和付费版的功能差别需核对，以确认“全部免费”可行。
- 兼容性和修改深度冲突：改得越深越难保证兼容，必须靠回归测试集守住。
- 本地小模型在原创深度内容和长文写作上仍然偏弱，系统设计无法完全补偿。
- 只能观察用户在自己编辑器内的行为；底座是否能提供带来源标记的变更事件需要验证。
- vibecoding 下复杂度累积很快，每一步都需要可运行、可演示、有测试。

## 八、建议的推进顺序

1. **可行性实验**（时间盒 1 到 2 周，见 `TASK.md`）：在 Windows 上从源码构建并运行底座的桌面版；确认需要的功能都在开源版里；核对许可证的附加条款；docx、xlsx、pptx 打开与保存的保真度；能否拿到元素 ID、位置、尺寸；能否无界面渲染；能否从外部用命令修改指定元素；能否区分人工和 AI 修改并拿到前后值。
2. 建立兼容性回归测试集（docx、xlsx、pptx 样本）。
3. 打通 Windows 桌面打包。
4. 由外到内定制：品牌和视觉规范，其次交互手感，最后才动核心。
5. 把 4.1 的接口预留做进架构。
6. 对照微软 Office 做差距清单，按优先级补齐功能和手感，通过回归测试，作为第一版发布。
7. 第二阶段（AI）：指令接口和 MCP，评测集，风格目录和权重，风格档案和后台学习，渲染评审闭环，最后才考虑多角色拆分。

## 九、待用户决定的问题

- 第一版“接近微软 Office”的具体衡量：建议做差距清单，由用户确认优先级。
- 第二阶段的学习信号阈值和排除规则是否采纳。
- MCP 的定位：只对外，还是内置助手也走同一套命令层。
- 视觉方向：已按“经典 Office 手感加现代无边框外观”决定，可随时推翻。
- 保留 ONLYOFFICE 标识的具体位置和方式，需要核对其许可证原文。
````
<!-- END REQUIREMENTS ORIGINAL BYTES -->
