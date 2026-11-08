# ONLYOFFICE v10.0.0 原生源码构建进展

当前原生核心、13 个格式静态库、转换器、桌面核心及 Qt 6 包装层已经编译并链接成功。V8 完整编译、中文头依赖修复重编、增量无任务检查及 JS / Intl / Wasm 运行测试均通过。新运行目录已经装配，真实 DOCX 中文编辑、保存、正常关闭、全新进程与 profile 重开回归已通过。完整结果见 `evidence/full-source-docx-roundtrip-result.json`；第一方 WASM 重编缺口仍须保留，不能声称全部资产源码构建完成。

## 已验证的构建与测试

| 范围 | 结果与证据 |
| --- | --- |
| 固定源码 | 8 个隔离工作树；原 upstream 与现有回退运行目录保留 |
| SDKJS / 网页编辑器 | 419 / 19,861 文件；evidence/full-source-js-sdk-result.json 与 full-source-web-build-result.json |
| Boost、CryptoPP、CompoundFile、ICU、OpenSSL | 源码产物通过；ICU 三种编码转换、OpenSSL SHA256/TLS 测试通过 |
| kernel / Unicode / network | 编译通过；中文 XML 实体解析、实际 DLL 加载检查通过 |
| graphics | 编译通过；自产 allfontsgen 对官方包字体解析和索引生成通过：build/full-source/core-evidence/allfonts-smoke/result.json |
| HEIF | RGB 无损样本 12,288 项精确一致；原 YCbCr 假设错误已记录，生产库未修改：graphics-dependency-evidence/rgb-lossless-validation/validation-result.json |
| 格式静态库 | 13 库完成；PPTX/Xlsb 的 verify-complete 日志及 module-runs/formats-pass1/result.json |
| PDF、DjVu、XPS、OFD | module-runs/drawing-pass1/result.json 全部退出 0 |
| HTML、Fb2、Epub、HWP、DocxRenderer、allfontsgen | module-runs/document-modules-pass1/result.json 全部退出 0 |
| IWork、Hunspell、pluginsmanager | remaining-dependency-evidence 下模块和功能测试证据 |
| V8 8.9.255.25 | 最终 monolith 218,458,654 字节，SHA256 5a481cbed294ebfd4c9c378edc978c0c1f652dbd1ac61be9cf01ea59ab2ed0c4；v8-evidence/monolith-smoke.json |
| doctrenderer、x2t、allthemesgen、docbuilder | module-runs/final-v8-linked/result.json；已补 V8 PRE_TARGETDEPS，日志确认真正重链最终库 |
| 桌面核心、helper、Qt 6 包装 | desktop-build-evidence 中 final-v8 结果；最终复核通过，helper 的外部 manifest 一并部署 |

表中的简写构建证据路径均位于 build/full-source 下。

## 构建调整与来源边界

原生编译使用现有 MSVC 14.44、Qt 6.11.1。主核心经 vcvars 选择 SDK 28000，V8 显式使用已安装 SDK 26100；均为 x64 Release /MD。AGG 旧 register 声明已在隔离源码中移除。V8 的中文 MSVC include 前缀已实测并配置到 Ninja，重编后抽查头依赖为 19、168、747 项，后续增量构建没有遗漏任务。

CEF 109、Qt 为明确的第三方预编译依赖；字体、空白模板等非代码资源有来源白名单。SDKJS/网页资源通过官方脚本重新生成，但 **5 个 WASM 及相邻预生成 JS 沿用固定 SDKJS 源码树资产，未重新编译，而且其中含 ONLYOFFICE 第一方代码**。不能将它们全部称为第三方依赖，或把本轮结果称为所有引擎资产均从源码重建。对应来源与历史构建配方已核查，见 [WASM 来源审计](full-source-wasm-provenance.md)；当前匹配配方尚缺，旧 PDF 配方与新接口不一致。

ICU 74 数据来自官方源码包的预生成数据输入再封装；V8 单独使用固定 DEPS 中 ICU 68.1 的数据，不能与根目录 CEF 的 icudtl.dat 混用。SDKJS 未做 JS 压缩，网页栅格图片保持原字节，forms addon 不在本次六个子模块中。

## 当前执行与授权

Perl 5.40.0.1 portable 与项目内官方 depot_tools / CIPD / GN / Python 3.11.8 均已分别获得用户批准并使用；未安装 Python 2.7，未修改系统 PATH、网络或安全配置。无待审批操作。实际最新阶段见 evidence/full-source-build-status.json 与 full-source-checkpoint-current.json。

本轮装配 20,970 文件，76 DLL 来源校验、21目标实际加载通过；自有入口两组 CTest 通过（20+17 QtTest条目含初始化/清理）。两个 DOCX 进程和默认首页均退出0，DOCX新增段落和原2×2表格核对通过。原 build/native-engine-run 作为回退保留。
