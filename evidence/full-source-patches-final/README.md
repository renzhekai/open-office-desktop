# 最终源码补丁归档

本目录只记录既有隔离源码的状态。归档过程未修改源码、构建产物、Git 索引或系统配置，未重新编译、下载、安装或清理文件；全部 Git 查询均设置 `GIT_NO_LAZY_FETCH=1`、`GIT_OPTIONAL_LOCKS=0`。

`manifest.json` 收录 8 个主工作树和 37 个依赖克隆的固定 HEAD、原始仓库地址、补丁 SHA256、修改文件当前字节的 SHA256，以及采集前后稳定性检查。共有 45 份 `.patch`，其中 19 份非空，涉及 41 个 tracked 文件。每个非空补丁均通过当前工作树上的 `git apply --reverse --check` 只读检查；本次没有另建干净工作树进行重放或编译。

主工作树包括 DesktopEditors、core、desktop-apps、desktop-sdk、dictionaries、web-apps、sdkjs、build_tools。补丁对照固定 HEAD，合并记录 staged 和 unstaged 的 tracked 修改；嵌套仓库单独归档，避免把子仓库的 dirty 标记误作源码补丁。

特别确认：

- `patches/main-core.patch` 包含 `Common/3dParty/v8/v8.pri` 新增的 `PRE_TARGETDEPS += $$CORE_V8_PATH_LIBS/v8_monolith.lib`。
- V8 主仓库、Chromium `build` 和 Jinja2 的适配已独立归档。`depot_tools` 的 tracked 修改实际为空，因此保留空补丁和固定 HEAD；项目内 CIPD/GN/Python 来源、关闭自动更新的过程环境等配置保存在 `support/.../full_source_v8.py` 与对应 receipts 中。
- `support-index.json` 索引既有获取、解压、准备、构建脚本和来源/补丁 receipts 的快照。`supplemental/` 额外保存少量必要输入：V8 的 `wrappers.cc`、`gclient_args.gni` 和最终 OpenSSL `10-main.conf`。

## 重放顺序

1. 保留或重新准备 `manifest.json` 中每个仓库对应的固定原始 commit。先核对 HEAD，再选择属于该仓库的补丁；不要把主仓库补丁用于某个子依赖。依赖来源记录曾包含浮动分支名时，以本归档记录的已解析 commit 为准。
2. 使用既有获取脚本及已固定的 sources/归档清单补齐官方依赖和原始 source archives。这些脚本、完整上游源码、已有工具和对应权限仍是前提；本目录本身不是完整离线 SDK。
3. 对非空补丁先运行 `git -C <repo> apply --check <patch>`，通过后再应用。若准备脚本已经做了同一适配，应核对修改文件 hash，或用 `apply --reverse --check` 确认已应用状态并跳过；不要叠加执行旧补丁步骤。空补丁无需应用。
4. 参照保存的 preparation receipts 和驱动脚本恢复必要的 untracked/generated 输入，再按原有驱动构建。V8 的 generated Ninja 中文依赖前缀应由 `full_source_v8.py` 每次生成后配置；它属于构建输出，不在 tracked 源码补丁中。
5. 核对补丁和来源清单的 SHA256；修改文件 hash 是本机工作树原始字节 hash，跨平台或改变换行设置时可能不同，已另外记录 `core.autocrlf`/`core.eol`。最后仍须重新编译并运行验证，不能以补丁可应用代替编译通过。

## 未包含的内容

未穷举 untracked/ignored 文件，未打包所有 generated assets、SDKJS/web bundles、node_modules、GN/qmake/CMake/Ninja 输出、编译产物或 runtime stage。Boost、ICU、OpenSSL 的归档源码没有纳入 Git diff，使用既有来源、提取和配置 receipts 及脚本恢复。CEF、字体、词典、工具及其完整原始包仍依赖已有固定清单。项目自身宿主代码的 Git 改动不属于这 8 个上游工作树的范围。

`verification.json` 记录归档校验结果；`archive-index.json` 给出归档内文件的逐项 SHA256（不包含自身及其独立校验文件）。
