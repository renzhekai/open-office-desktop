# ONLYOFFICE 往返兼容性样本

本目录是第一阶段的工程测试材料，不是办公软件实现，也不是用户文档成品。

## 已提供的最小样本

| 文件 | 内容 | 自动检查范围 |
| --- | --- | --- |
| `generated/text-table.docx` | 中文、英文、数字、粗斜体、颜色、2×2 表格 | 段落文字与表格单元格文字 |
| `generated/values-formula.xlsx` | 中文表头、整数、小数、布尔值、`SUM(A2:B2)` 公式 | 工作表名、单元格地址、值、公式是否保留 |
| `generated/text-shape.pptx` | 单页 16:9 幻灯片、中文标题、矩形与圆角矩形 | 幻灯片顺序、文字、预设形状类型及数量 |

生成器只依赖 Python 标准库。样本通过 ZIP 完整性、XML 可解析性和内部关系目标检查；这不等于完整 OOXML XSD 验证，也不证明 ONLYOFFICE 能正常打开。

## 运行工具

在项目根目录执行：

```powershell
python scripts/roundtrip.py generate
python scripts/roundtrip.py inspect fixtures/generated/text-table.docx
python -B -m unittest discover -s tests -p test_roundtrip.py -v
python scripts/roundtrip.py compare
```

`generate` 遇到任何同名样本即拒绝覆盖，保护已生成或人工修改过的文件。需要另一组样本时使用 `--directory fixtures/another-run`。

`compare` 默认读取 `fixtures/generated` 和 `fixtures/roundtrip`。缺少编辑器保存的输出时，JSON 状态为 `not_executed`，退出码 **2**。它不会自己启动编辑器、安装插件、连接网络或修改输入。

## 真实编辑器验证步骤（尚待执行）

1. 记录实际 ONLYOFFICE Desktop Editors 版本、来源、执行时间和运行命令；截图保留窗口及样本文档。官方预编译版本只能证明该二进制可运行，不能证明本机源码构建成功。
2. 在真实编辑器中逐个打开 `generated` 下的三个文件，检查是否出现修复、兼容或错误提示。不要修改原始输入。
3. 用“另存为”保存到独立的 `fixtures/roundtrip` 目录，保持文件名及原格式。关闭输出，再次打开输出并检查。
4. 表格中检查 `C2` 仍含公式且结果为 5；另保存一份副本，将 `A2` 改为 7，验证结果变成 10。自动比较不检查公式引擎重算，也不将缓存值视作计算通过。
5. 截图或导出 PDF，对比文档分页、字体、粗斜体、表格边框、列宽、幻灯片颜色及形状位置。自动检查不覆盖字体替代、样式、坐标、分页、图像、图表、宏、修订、批注、复杂公式等。
6. 只有上述打开—保存—重开真实发生后，才创建操作记录 JSON，并运行比较。以下是**模板**，不得填入未发生的事实：

```json
{
  "engine": "ONLYOFFICE Desktop Editors",
  "version": "填写实际版本",
  "operation": "open-save-reopen",
  "saved_files": ["text-table.docx", "values-formula.xlsx", "text-shape.pptx"],
  "run_note": "填写执行时间、打开/保存/重开结果及截图路径"
}
```

```powershell
python scripts/roundtrip.py compare --evidence path/to/actual-run-evidence.json
```

操作记录只是人工声明，工具无法认证文件由哪个程序生成。报告始终显示 `engine_execution_verified: false`，需结合真实运行证据审阅。简单复制原文件只能得到语义相同、来源未验证的结果，不应报告往返通过。

## 状态与限制

| 退出码 | 含义 |
| --- | --- |
| 0 | 生成/检查工具执行成功，或有操作声明时限定范围的语义检查通过；查看 JSON `status` 区分。**不代表完整兼容性通过。** |
| 1 | 无效输入/输出、损坏包、丢失文字或公式等语义差异，或工具错误 |
| 2 | 没有编辑器保存输出，或已有语义相同输出但缺少有效操作声明 |

比较使用 XML 语义而不是整个 ZIP 的哈希。允许 ZIP 元数据、命名空间前缀、文字运行拆分、数值序列化及公式缓存发生无害变化；共享字符串和内联字符串以文本处理。当前不支持共享公式展开等复杂情况，遇到时明确失败而非猜测。

这些样本仅用于首轮冒烟测试。发布前仍需独立来源的真实复杂文档语料、Microsoft Office 对照、字体环境记录和视觉/功能回归。单测里复制和损坏的文件只用于验证检查工具自身，不能计作 ONLYOFFICE 测试结果。
