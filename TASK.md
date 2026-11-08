# 第一阶段：ONLYOFFICE Windows 底座可行性验证

## 当前执行重点（2026-11-01 10:09 UTC 状态快照）

用户后续已授权实际工程，并明确最低目标是自己的 Windows 程序能启动、编辑文件和保存。本轮 **ONLYOFFICE DOCX 打开—修改—保存—正常关闭—新进程重开** 最小闭环已通过；此前的可行性清单服务于这一目标。第一阶段仍不做 AI，不修改需求原文。

- [x] 自有 C++17 / Qt Widgets 入口，原基线已实际运行；本轮 MSVC 原生引擎版本也已编译、链接和部署，编辑运行验收另列。
- [x] 明确请求受理/异步完成边界，准备保存失败和未保存修改保护；本机 CTest 2/2 通过，启动 smoke 退出码 0，均不代表真实引擎接入。
- [x] 按用户后续授权，由助手下载并核对 DesktopEditors v10.0.0、固定 build_tools 和全部六个子模块；会话 55054 退出码 0。八个仓库的提交对象、实际 tracked 文件和工作区状态已核验，见 [checkout 证据](evidence/onlyoffice-source-checkout-verified.json)。
- [x] 完成用户已批准的 Qt 6.11.1 MSVC2022 x64 与对应 Multimedia 到位和运行验证。9 份官方归档逐一校验后手动部署，未注册到 MaintenanceTool，原 MinGW 保留；SDK 探针、MSVC 宿主构建均通过。[SDK 证据](evidence/qt-msvc-manual-sdk.json)
- [x] 校验并部署官方 v10.0.0 原生运行依赖；本地源码构建两份 Qt 6 包装 DLL，自有 Native Adapter 与主程序实际链接，最终 CTest 2/2 通过。[构建证据](evidence/native-engine-build-final.log)
- [ ] 完成全部引擎及依赖的 Windows 源码构建；六个子模块 checkout、官方原生二进制和本地 Qt 6 包装层构建不等于 CEF、V8、转换核心等全链自行构建成功。
- [x] 应用 Windows qmake 工具链适配补丁，8 项隔离替身测试通过；不等于真实上游构建通过。
- [x] 验证 CEF 初始化和真实 DOCX 视图：收到 `doc:onready`，修改正文并通过自有保存按钮落盘；正常关闭收到 `DESTROYWINDOW`、消息循环返回及 shutdown 完成，进程退出码 0。[启动报告](evidence/native-docx-launch-templatefix.json)、[原生事件日志](build/native-docx-profile/native-events.log)
- [x] 独立检查真实保存产物 [native-docx-roundtrip.docx](fixtures/results/native-docx-roundtrip.docx)：新增正文标记、原表格 4 个单元格文字和 ZIP CRC 通过；此检查不代表复杂排版保真。
- [x] 新进程 PID 19736 以全新 profile 重开保存文件，实际界面确认新增标记、原中文/English、粗斜体及 2×2 表格；本机 150% 缩放下默认/最大化窗口中 CEF 填满编辑区域，关闭退出码 0。最小 DOCX 闭环通过。[闭环结果](evidence/native-docx-roundtrip-result.json)、[重开事件](evidence/native-docx-reopen-events.log)
- [ ] 扩展并验证新建、另存为、同进程替换文档、XLSX/PPTX、多显示器及复杂文件保真；这些不计入本轮小样本通过范围。

不把文件选择、接口代码、测试替身或外部成品启动计为编辑保存完成。本轮有真实引擎界面、保存文件、独立 XML 检查及全新 profile 重开证据；原生引擎仍来自官方预编译包，完整源码构建单独列为未完成。现状与证据见 [STATUS.md](docs/feasibility/STATUS.md)。

需求依据：[requirements.md](requirements.md)。本文件把已授权的可行性工作落实为可检查任务；需求原文保持不变。当前只验证底座及第一版所需扩展能力，AI、MCP 服务、模型、风格学习属于后续阶段。

## 首个目标与证据规则

在 Windows 上建立真实 ONLYOFFICE 桌面运行基线，验证源码构建路线、免费能力、许可证、三种办公格式的打开/保存及扩展接口。官方二进制运行成功和自行源码构建成功是两个独立门槛。

- `passed`：有本机命令、产物或运行截图证明，并注明版本与测试输入。
- `documented`：有固定版本源码或官方说明；尚未代表本机通过。
- `blocked`：有具体失败命令或缺失前置条件。
- `not_run`：尚未执行，不记为通过。
- 工具单测仅证明测试工具行为；生成样本、原样复制文件或比较自己不能证明 ONLYOFFICE 往返保真。

## 按顺序执行

1. **锁定依据及环境**：保护用户改动，记录需求 SHA256、官方版本/提交、编译工具与磁盘情况。完成条件：`evidence/environment.json` 与 `sources.lock.json` 可复查。
2. **读取上游构建脚本**：确认 Windows 分支实际依赖、各仓库对应版本、下载与系统修改行为。完成条件：形成可审核构建命令和前置条件清单。联网、安装、协议或额外权限的边界按当前用户授权处理。
3. **建立官方桌面运行基线**：校验官方 ZIP 哈希，解包至项目内独立目录，打开测试文件，记录版本/截图/输出；若首启要求接受协议，停在该步骤请求确认。不启用 AI 插件。完成条件：真实桌面进程与三类文件均有运行证据。
4. **验证 Windows 源码构建**：在隔离的上游副本运行已经审阅的命令，保留完整日志及产物。完成条件：从固定源码产生 Windows 桌面程序并启动；只下载源码或运行官方二进制不算完成。
5. **检查免费功能及署名**：分别记录编辑、转换、插件、额外组件的许可和费用边界。保留原署名；修改版品牌与标识位置依据实际版本许可确定。完成条件：官方依据矩阵；无法证实的能力继续标记待验证。
6. **文件往返测试**：用桌面程序打开并另存 DOCX/XLSX/PPTX 样本，执行语义对比，再检查渲染、分页、公式结果和版式。完成条件：保存后的真实产物、测试 JSON 和视觉证据。最小样本通过仍不等于复杂 Office 文件全面兼容。
7. **扩展能力实验**：逐项验证命令修改、撤销、元素标识、位置尺寸、排版度量、无界面导出、来源与前后值。先做不含 AI 的手动测试入口；免费桌面插件 API、Document Builder 与 Docs Automation API 分开判断。完成条件：每项均有通过/不支持/待验证结论及证据，不用模拟实现代替。

## 本轮交付及继续入口

- 运行环境：`python scripts/probe_environment.py --output evidence/environment.json`
- 环境记录保留历史；再次运行时省略 `--output` 或指定新的 JSON 文件名，工具不会覆盖已有文件。
- 文件测试：见 [fixtures/README.md](fixtures/README.md)。没有桌面保存输出时，往返检查必须退出非零。
- 原生宿主构建：`scripts\build-native-engine.cmd`；开发部署：`python scripts/stage_native_engine.py`，已有受管理目录只更新自有二进制时使用 `--update-own-binaries`。入口为 `build/native-engine-run/OpenOffice.exe`，最小 DOCX 打开、修改、保存、正常关闭及重开验收已通过。
- 官方 Windows 构建路线：[windows-build-route.md](docs/feasibility/windows-build-route.md)
- 免费范围及许可证：[licensing-and-features.md](docs/feasibility/licensing-and-features.md)
- 编辑接口：[editor-interfaces.md](docs/feasibility/editor-interfaces.md)
- 本机结论及剩余阻塞：[STATUS.md](docs/feasibility/STATUS.md)

只有运行、构建和兼容性关键门槛得到证据后，才进入第一版定制；失败时先诊断 ONLYOFFICE，不自动切换底座。第一版完整功能清单及正式验收优先级仍需在基线可运行后逐步细化。


## 原生源码构建与回归（2026-11-02）

Windows 原生核心、13 个格式库、V8、转换器、桌面核心、Qt 包装层及自有入口已经源码编译。新运行目录为 `build/full-source-run`；两组 CTest、76 DLL 校验/21目标加载和真实 DOCX 中文编辑、保存、关闭、独立进程重开均通过。两个 DOCX 进程及默认首页测试均退出 0，需求和原回退 EXE 哈希未变。

本轮结果见 [验收记录](evidence/full-source-docx-roundtrip-result.json)、[构建进展](docs/feasibility/full-source-progress.md)、[补丁重放](evidence/full-source-patches-final/README.md)。

尚未完成“全部第一方引擎资产均从源码重建”：5 个 WASM 及生成 JS/asm.js 等沿用固定官方 SDKJS 资产，当前匹配生成配方缺失，历史 PDF 配方缺少 6 个新接口。详见 [来源审计](docs/feasibility/full-source-wasm-provenance.md)。这不是连接或审批拒绝；未安装额外 Emscripten 工具，不以旧配方产物冒充当前版本。阶段一仍不加入 AI，也不扩展本轮 DOCX 之外的新功能。


## WASM 源码重建与默认入口（2026-11-05）

用户批准下载 Emscripten 3.1.48 后，字体、zip/图像、拼写、哈希、PDF 共 5 个 WASM 引擎已从固定源码重建，并与官方 SDKJS 资产逐项差分测试通过。用它们装配的 `build/full-source-run-wasm-candidate` 已在真实编辑器中通过 DOCX 中文编辑、保存与重开回归，并按用户要求设为默认入口。`build/full-source-run` 保留为回退。

仍保留的来源边界：PDF 加载器模板 JS 没有公开源码，取自官方生成文件；`fonts_native.js`、`drawingfile_native.js` 沿用官方文件；CEF、Qt 为第三方预编译。详见 [WASM 来源审计](docs/feasibility/full-source-wasm-provenance.md) 与 [AI 接手文档](Open_Office_AI接手文档.md)。第一版其余功能（新建、另存为、XLSX/PPTX/PDF 流程、复杂文档兼容）仍未开始。
