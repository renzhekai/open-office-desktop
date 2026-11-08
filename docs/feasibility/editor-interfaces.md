# ONLYOFFICE 桌面编辑接口与文件往返验证

核验日期：2026-11-01。范围：第一阶段底座可行性研究，不实现 AI、MCP 服务或偏好学习。

本报告完成了官方文档和官方源码检查，**没有安装或运行 ONLYOFFICE，没有运行插件、转换或文件往返测试**。下述“存在接口”不能当作本机实验通过。

## 版本与证据

工程候选版本为 DesktopEditors `v10.0.0`，固定提交 `daf8c6948148827201b2907ac1134d222fbfa7b4`。该提交的官方补丁将 `sdkjs` 子模块指向 `9d2138866d929be32f1d107e8b5bfae36156e097`；本报告的源码结论使用这一精确提交。[DesktopEditors 提交补丁](https://github.com/ONLYOFFICE/DesktopEditors/commit/daf8c6948148827201b2907ac1134d222fbfa7b4.patch)

`api.onlyoffice.com` 为滚动文档，目前包含 10.0 接口。将来运行时必须记录实际桌面版本及 `sdkjs` 提交，不能只凭当前网页判断旧版本可用。

## 文档或源码已证实

| 能力 | 已证实内容 | 实际边界 |
|---|---|---|
| 桌面插件 | Desktop Editors 官方支持自制插件；可以离线将 ZIP 改为 `.plugin`，通过插件管理器手动安装。 | 两份官方安装页分别列用户目录和 Program Files 路径；优先使用管理器并记录实际安装位置，不修改系统目录来规避权限。[桌面添加插件](https://api.onlyoffice.com/docs/desktop-editors/usage-api/adding-plugins)、[插件安装页](https://api.onlyoffice.com/docs/plugins/development-workflow/installing-and-testing/desktop-editors-installation) |
| 文档、表格、演示编辑 | 插件通过 `Asc.plugin` 与编辑器交互；10.0 增加 `Editor` 和 `Editor.RunMacro`，此前使用 `executeMethod`、`callCommand`。 | 插件位于已打开的编辑器内；这是可扩展入口，不自动成为外部进程可调用的桌面 RPC。[交互概览](https://api.onlyoffice.com/docs/plugins/interacting-with-editors/overview) |
| 命令数据与错误 | `RunMacro` 在隔离上下文执行，参数通过 JSON 传递，返回普通 JS 数据；文档说明抛出异常或编辑器回滚时 Promise 可拒绝。 | 不返回活的编辑器对象；不能从这一说明推导多次命令具有完整事务或崩溃恢复保证。[调用命令](https://api.onlyoffice.com/docs/plugins/interacting-with-editors/overview/how-to-call-commands) |
| 方法调用结果 | `Editor` 方法返回 Promise，调用会排队；文档指出未知方法可能解析为 `undefined`，方法失败需检查返回值。 | Promise 完成不等于修改成功；探针需要读取后置状态并设超时。[调用方法](https://api.onlyoffice.com/docs/plugins/interacting-with-editors/overview/how-to-call-methods) |
| DOCX 段落标识 | 有 `GetInternalId`、`GetParaId`、`SetParaId`。固定源码中 `GetInternalId` 返回内部 `GetId()`；`GetParaId` 未指定时返回 `0`。 | 不能无条件认为所有元素都有持久唯一 ID；保存重开、复制、拆分、合并和外部编辑后的稳定性都需实验。[固定 word/apiBuilder.js](https://github.com/ONLYOFFICE/sdkjs/blob/9d2138866d929be32f1d107e8b5bfae36156e097/word/apiBuilder.js#L10445-L10488) |
| 内容控件定位 | 固定源码区分内容控件 `Tag`、`Id`、`InternalId`，其中 `Tag` 可重复，`InternalId` 用于控件操作。 | 不把 Tag 当唯一主键；内容控件只能覆盖受控元素，不能据此声称全篇元素均已稳定寻址。[固定 word/api_plugins.js](https://github.com/ONLYOFFICE/sdkjs/blob/9d2138866d929be32f1d107e8b5bfae36156e097/word/api_plugins.js#L37-L51) |
| 表格与幻灯片定位 | 表格范围有 `GetAddress`；幻灯片形状有 `GetInternalId`、坐标、宽高等接口。 | 单元格地址是位置，插行、移动后可能改变；形状内部 ID 跨保存的稳定性未证实。[GetAddress](https://api.onlyoffice.com/docs/office-api/usage-api/spreadsheet-api/ApiRange/Methods/GetAddress)、[ApiShape](https://api.onlyoffice.com/docs/office-api/usage-api/presentation-api/ApiShape) |
| 部分排版信息 | 幻灯片形状可读 `GetPosX/GetPosY/GetWidth/GetHeight`；文档 `GetStatistics` 提供页数、字数、段落数等。 | 不等于所有文字的最终边界、溢出、重叠或行数检测；`GetStatistics` 的字段列表没有行数。[形状 API](https://api.onlyoffice.com/docs/office-api/usage-api/presentation-api/ApiShape)、[GetStatistics](https://api.onlyoffice.com/docs/office-api/usage-api/document-api/ApiDocument/Methods/GetStatistics) |
| 撤销与分组入口 | 文档编辑器公开 `Undo/Redo/CanUndo`；固定源码的 `StartAction/EndAction` 有 `GroupActions` 分支，分别调用分组方法，结束时非空 status 会设置 cancel。 | 方法文档仍只列 Information/Block；只能确认源码分组入口存在，尚未确认三种编辑器的一步撤销、错误回滚、异步边界或断电恢复。[Undo](https://api.onlyoffice.com/docs/plugins/interacting-with-editors/document-api/Methods/Undo)、[固定分组入口](https://github.com/ONLYOFFICE/sdkjs/blob/9d2138866d929be32f1d107e8b5bfae36156e097/common/apiBase_plugins.js#L557-L601) |
| 变更事件与来源线索 | 文档有 `onParagraphText`、`onChangeContentControl`；DOCX 的 `SetAssistantTrackRevisions` 可以设置助手署名的修订。 | 段落事件示例含文本和重算 ID，不提供通用 old/new 样式或人工/插件来源字段；助手修订不是覆盖所有格式的审计日志。研究接口不代表本阶段启用 AI。[段落事件](https://api.onlyoffice.com/docs/plugins/interacting-with-editors/document-api/Events/onParagraphText)、[控件事件](https://api.onlyoffice.com/docs/plugins/interacting-with-editors/document-api/Events/onChangeContentControl)、[助手修订接口](https://api.onlyoffice.com/docs/office-api/usage-api/document-api/ApiDocument/Methods/SetAssistantTrackRevisions) |
| 导出入口 | 插件有 `GetFileToDownload(format)`，官方示例使用 PDF；固定源码调用现有 `downloadAs`。 | 这是打开编辑器后的导出入口，不证明桌面无界面批转换或本地返回路径可用。[导出接口](https://api.onlyoffice.com/docs/plugins/interacting-with-editors/document-api/Methods/GetFileToDownload)、[固定导出入口](https://github.com/ONLYOFFICE/sdkjs/blob/9d2138866d929be32f1d107e8b5bfae36156e097/common/apiBase_plugins.js#L1330-L1357) |

## 产品边界：不能计入免费桌面能力

- **Automation API**：官方明确仅面向 ONLYOFFICE Docs Developer，且额外收费；`docEditor.createConnector()` 不能被列为免费 Desktop Editors 的现成外部控制接口。[官方 Automation API](https://api.onlyoffice.com/docs/docs-api/usage-api/automation-api)
- **Document Builder**：属于独立文档生成、编辑、转换组件；Windows 有 `docbuilder.exe` CLI。当前官方安装页说明免费版本生成文档带水印，去除限制需要商业许可证。不能直接承诺它满足本项目“全部免费”的无水印转换要求；也不能把它当作控制当前桌面会话的接口。[安装与限制](https://api.onlyoffice.com/docs/document-builder/get-started/installing)、[独立 CLI](https://api.onlyoffice.com/docs/document-builder/using-cli/overview)
- **桌面无界面转换**：当前官方命令行文档列启动、打开、只读、新建等标志，未提供可作为本项目契约的 headless 转换命令。桌面源码确实包含转换核心，但是否可独立构建并调用、其资源依赖与输出保真仍待验证。不能据“未列出”断言绝对不支持，也不以带水印 Builder 代替本项通过。[桌面 CLI](https://api.onlyoffice.com/docs/desktop-editors/usage-api/command-line-flags)、[官方仓库组件说明](https://github.com/ONLYOFFICE/DesktopEditors#components-)

## 当前不能证明的需求

1. 所有人工操作与插件操作统一经过可公开拦截的命令层。
2. 全部文档元素均有跨保存、跨 Office 往返稳定的 ID 与语义角色。
3. 全格式、全操作的来源标记和修改前后样式值可可靠取得。
4. 任意多命令、混合异步操作具有真正事务原子性或可靠回滚。
5. 免费桌面发行物可无界面批量输出无水印 PDF/图片，并与屏幕排版一致。
6. DOCX/XLSX/PPTX 在本机打开、编辑、保存及再打开后满足实际兼容标准。

这些是未验证项，不是已判定 ONLYOFFICE 无法实现的结论。关键门槛未通过时继续验证该底座，不自动切换替代产品。

## 待运行的最小实验

以下是实验设计，全部状态为 **未运行**。只使用合成样本和工作区副本，不覆盖用户原件。

| 编号 | 实验 | 通过证据 |
|---|---|---|
| IF-01 | 在固定版本的三个编辑器内加载无网络依赖的探针插件，读取实际版本、编辑器类型和目标内容。 | 三种编辑器的结构化结果与运行截图；插件加载失败、超时与空返回明确记录。 |
| IF-02 | DOCX 改一段的样式，XLSX 改一个指定范围的样式，PPTX 移动一个指定形状；每次均读取前后值。 | 只有目标属性发生预期变化；其他内容不变；保存后重开核验。 |
| IF-03 | 分别验证普通撤销、重做及 GroupActions；加入无效目标和故意失败步骤。 | 记录哪些变更撤回、哪些保留；不以单次成功推导所有命令都有事务保证。 |
| IF-04 | 记录元素 ID/地址，执行无修改保存、重开、前方插入、对象复制、段落拆分及表格插行。 | 映射结果和冲突清单；零 ID、重复 ID、消失或重分配均明确处理。 |
| IF-05 | 人工与探针分别改字形、颜色、大小和位置，观察事件。 | 测出事件覆盖率、是否有 old/new、是否可可靠区分来源；诊断输出不持久化文档正文。 |
| IF-06 | 从本地打开的三类文件导出 PDF；另行验证可构建转换核心的无界面路线。 | 输出文件、退出码/错误、字体环境、页数和视觉对比；GUI 导出与 headless 分开记结果。 |

三份最小往返样本分别覆盖：DOCX 中文与英文、标题/正文、表格、页眉页脚及图片；XLSX 中文表头、数值与日期、公式、合并单元格、图表和打印区域；PPTX 中文文本、形状、图片、表格或图表、主题字体与位置。复杂特性样本在最小链路跑通后扩充。

每份样本保留原始副本、另存副本、版本与字体清单、结构断言及必要的 PDF/截图。ZIP 字节或 XML 顺序变化不直接等于失真；需要比较正文、公式、关系、样式与视觉。若没有微软 Office 参考输出，只能证明 ONLYOFFICE 自身往返，不能声称已验证与微软完全兼容。

## 第一阶段接口预留建议

先记录能力探测、目标引用、执行结果、撤销能力及格式差异。命令适配层可保留调用来源和关联 ID，但只报告确实可取得的字段；缺少来源时标记 unknown，不猜测为人工。此时不实现模型、MCP、记忆或静默学习，也不为“统一接口”改写所有编辑核心。

本报告未新增代码、未安装插件、未接受协议、未调用收费服务。没有实验通过项；官方证据核验完成，实际运行与兼容性结果由后续工程实验补齐。
