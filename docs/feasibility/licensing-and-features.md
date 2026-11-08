# ONLYOFFICE 桌面版许可证与免费功能核验

核验日期：2026-11-01。本文记录官方资料中的事实与工程待验证项；没有安装软件、接受协议或修改上游代码。

## 固定研究版本

- 官方桌面最新发布为 **v10.0.0**，发布日期 **2026-10-13**。[发布页](https://github.com/ONLYOFFICE/DesktopEditors/releases/tag/v10.0.0)、[官方桌面更新日志](https://helpcenter.onlyoffice.com/desktop/desktop-changelog.aspx)
- 发布关联提交：`daf8c6948148827201b2907ac1134d222fbfa7b4`。[提交](https://github.com/ONLYOFFICE/DesktopEditors/commit/daf8c6948148827201b2907ac1134d222fbfa7b4)
- 本文的代码许可结论以该提交的 [LICENSE](https://raw.githubusercontent.com/ONLYOFFICE/DesktopEditors/daf8c6948148827201b2907ac1134d222fbfa7b4/LICENSE) 为准。官网与帮助中心为核验当日的动态页面，不能替代固定版本实测。选作研究对象不等于已经确定项目构建基线。

## 已核实的许可与署名要求

固定提交的 LICENSE 包含 AGPLv3 正文和 Ascensio System SIA 附加条款，不能只读 GitHub 的许可证标签。附加条款要求保留版权、许可、免责声明和来源信息；修改版需突出标明修改及日期、原软件来源；交互界面应提供明显、易访问的法律信息入口。部分非代码素材另按 CC BY-SA 4.0 许可。该许可没有授予商标权，而是指向独立商标政策。分发源码或二进制时还要满足 AGPL 对修改声明、对应源码等的条件。[固定版本 LICENSE](https://raw.githubusercontent.com/ONLYOFFICE/DesktopEditors/daf8c6948148827201b2907ac1134d222fbfa7b4/LICENSE)

该版本根 LICENSE 未出现旧式“必须将原 logo 保留在左上角”的表述。因此，不能把需求文档中的旧条款描述直接当作 v10.0.0 的精确要求。也不能由此推导出可以任意移除署名或使用 ONLYOFFICE 品牌。

官方 2026-05-20 指南说明：修改版应有自己的品牌身份，ONLYOFFICE 可以用于准确说明来源，不能成为修改产品的主名称或暗示官方认可；在允许的商标使用场景下，应显示“ONLYOFFICE is a trademark of Ascensio System SIA.”。[官方许可及商标指南](https://www.onlyoffice.com/blog/2026/05/onlyoffice-license-and-trademark-policy)

工程上的临时处理：完整保留上游法律文本与来源署名，记录本项目修改；暂不调整品牌位置或制作对外分发包。这是保守实施安排，不是对商标政策效力或适用范围的法律判断。

## 免费桌面功能与边界

| 项目 | 官方资料确认的范围 | 本项目仍需验证 |
|---|---|---|
| 使用价格 | 桌面版对个人和专业用途免费 | 自行构建所得功能、组件和分发内容 |
| 本地办公 | 文档、表格、演示、PDF 编辑；支持离线文件工作 | 中文排版、字体、复杂公式、图表及往返保真 |
| 桌面版本差别 | Community 与 Enterprise 的编辑、格式、协作工具相同；AI Tools 的随包方式不同 | 不能套用 Docs 服务器版的付费功能对比来判断桌面版 |
| 文档与 PDF 转换 | 官方格式表列出 DOCX 导出 PDF/JPG/PNG，PDF 转 DOCX/JPG/PNG 等路径 | 逐项验证桌面菜单、离线运行和结果质量 |
| 图片转文字 | OCR 插件有 PNG/JPG 提取文字并插入文档的流程 | 中文识别、版式重建、实际随包情况及离线依赖 |
| 协作与邮件合并 | 协作需要连接云或服务器；邮件合并属于在线功能 | 不能承诺全部功能均本地离线，也不能承诺外部服务免费 |

价格和桌面基础功能见[官方桌面页](https://www.onlyoffice.com/desktop)及[固定版本 README](https://github.com/ONLYOFFICE/DesktopEditors/blob/daf8c6948148827201b2907ac1134d222fbfa7b4/README.md)。版本差别和联网边界见[桌面入门文档](https://helpcenter.onlyoffice.com/desktop/getting-started.aspx)。转换路径见[文档格式表](https://helpcenter.onlyoffice.com/docs/userguides/document_editor/supportedformats.aspx)和[PDF 格式表](https://helpcenter.onlyoffice.com/docs/userguides/pdf_editor/SupportedFormats.aspx)；这些是官方编辑器文档，仍须在固定桌面构建上验证。图片识别见[OCR 文档](https://helpcenter.onlyoffice.com/docs/userguides/plugins/ocr.aspx)。

“支持转换”不能等同于逐像素兼容或扫描件可完整恢复为可编辑版式。当前研究没有运行样本转换，也没有证明需求中的全部转换场景均已覆盖。

## 第一版不含 AI 的直接影响

官方桌面说明写明：Community 的 AI Tools 不随安装包提供；首次启动可能出现安装邀请，其中 `Accept & Install` 会接受许可并安装附加组件。文档也提供 `Don't show me ONLYOFFICE AI Tools` 选项，用来关闭提示及 AI 功能。当前任务不应接受或安装该附加组件；构建时仍需检查自带 AI 入口、插件和默认开关是否满足第一版范围。[官方桌面入门文档](https://helpcenter.onlyoffice.com/desktop/getting-started.aspx)

## 尚未完成的核验

1. 取得独立商标政策全文并核对最终品牌方案。已读官方说明，但政策页面链接的 DocSpace 文档未能通过本次网页文本工具提取全文；因此没有宣称完成该法律文档审阅。[政策入口](https://www.onlyoffice.com/trademark-policy)、[官方共享文档](https://docspace.onlyoffice.com/s/R5Nm5zx84nDSptG)
2. 对实际构建中所有子模块、字体、图标、插件及第三方依赖逐项建立许可证清单；本次只核验了固定提交的顶层 LICENSE，不能作为完整依赖许可审计。
3. 在 Windows 固定版本上验证需求所列功能；官方价格与产品介绍不能证明自行构建已经成功或达到 Microsoft Office 的功能和兼容程度。

未把需求文档中关于其他分叉的争议、侵权结论或构建质量评价当作已核实事实。本次没有外部发布、注册账号、调用付费服务或接受协议。
