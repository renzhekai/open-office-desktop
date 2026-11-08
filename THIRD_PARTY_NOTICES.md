# 第三方组件与当前接入状态

本机开发版本：0.1.0。程序使用 Qt 6.11.1 动态库；保留 MinGW 原型构建，并增加 MSVC x64 原生引擎构建。相关 Qt、编译器运行库及第三方组件保留原有版权和许可证。准确条款以实际组件附带的许可文件为准。此工程用于本地开发验证，尚未对外发布。

ONLYOFFICE 是本项目选定的办公编辑底座。开启 `OPEN_OFFICE_WITH_ONLYOFFICE` 的构建链接官方 v10.0.0 原生引擎 DLL，并使用本地从固定源码编译的 Qt 6 包装层。窗口与适配层属于本工程；原生引擎、CEF helper、转换器和编辑器资源来自经过固定 SHA256 校验的官方发行包。完整引擎源码构建尚未通过；实际文档编辑验证结果以 `docs/feasibility/STATUS.md` 及对应证据为准。未开启此选项的基线构建仍没有办公编辑能力。

保留 ONLYOFFICE 的法律信息、来源署名、原有编辑资源和修改说明。Copyright (C) Ascensio System SIA, 2009–2026. 固定源码的程序许可为 GNU Affero General Public License v3（连同其 LICENSE 中的附加条款）；素材使用其各自许可。完整原文见 `upstream/DesktopEditors/LICENSE`、`upstream/DesktopEditors/desktop-sdk/LICENSE` 和 `upstream/DesktopEditors/core/LICENSE`。ONLYOFFICE is a trademark of Ascensio System SIA. 本项目是独立开发构建，并非 ONLYOFFICE 官方发行版，未声称获得官方认可。

官方依据与未完成的许可核验见 [许可与免费范围](docs/feasibility/licensing-and-features.md)。发布前需补齐实际构建依赖的许可证、源码提供方式及署名清单。
