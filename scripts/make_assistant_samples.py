#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Write the sample documents the assistant is tested on (fixtures/assistant-samples).

Each is a small Word document with one or two tables from a different kind of office work
(purchases, a timetable, contacts, scores, project status, an English spec sheet, a sign-in
sheet), with its own problems: typos, sums that are wrong, mixed date/phone/status formats,
missing header formatting, empty cells, and one table that has nothing wrong. The files are
written byte for byte the same every time (fixed zip dates), so they can be kept in git.

Usage:
    python -I scripts/make_assistant_samples.py
"""

import zipfile
from pathlib import Path
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "fixtures" / "assistant-samples"

CONTENT_TYPES = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
</Types>"""
RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
</Relationships>"""
NS = 'xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"'
BORDERS = ("<w:tblBorders>" + "".join(f'<w:{side} w:val="single" w:sz="4" w:space="0" w:color="808080"/>'
                                      for side in ("top", "left", "bottom", "right", "insideH", "insideV"))
           + "</w:tblBorders>")


def run(text, bold=False):
    props = "<w:rPr><w:b/></w:rPr>" if bold else ""
    return f'<w:r>{props}<w:t xml:space="preserve">{escape(text)}</w:t></w:r>'


def paragraph(text, bold=False, size=None, align=None):
    ppr = f'<w:pPr><w:jc w:val="{align}"/></w:pPr>' if align else ""
    rpr = ""
    if bold or size:
        rpr = "<w:rPr>" + ("<w:b/>" if bold else "") + (f'<w:sz w:val="{size * 2}"/>' if size else "") + "</w:rPr>"
    return f'<w:p>{ppr}<w:r>{rpr}<w:t xml:space="preserve">{escape(text)}</w:t></w:r></w:p>'


def table(rows, header_bold=False, header_fill=None, widths=None):
    columns = max(len(row) for row in rows)
    widths = widths or [9000 // columns] * columns
    xml = [f"<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/>{BORDERS}</w:tblPr><w:tblGrid>"]
    xml += [f'<w:gridCol w:w="{w}"/>' for w in widths]
    xml.append("</w:tblGrid>")
    for r, row in enumerate(rows):
        xml.append("<w:tr>")
        for c, text in enumerate(row):
            shd = f'<w:shd w:val="clear" w:color="auto" w:fill="{header_fill}"/>' if r == 0 and header_fill else ""
            xml.append(f'<w:tc><w:tcPr><w:tcW w:w="{widths[c]}" w:type="dxa"/>{shd}</w:tcPr>'
                       f'<w:p>{run(text, bold=(r == 0 and header_bold)) if text else ""}</w:p></w:tc>')
        xml.append("</w:tr>")
    xml.append("</w:tbl>")
    return "".join(xml)


def document(*blocks):
    body = "".join(blocks) + paragraph("")
    return (f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?><w:document {NS}><w:body>{body}'
            '<w:sectPr><w:pgSz w:w="11906" w:h="16838"/><w:pgMar w:top="1440" w:right="1440" w:bottom="1440" '
            'w:left="1440" w:header="720" w:footer="720" w:gutter="0"/></w:sectPr></w:body></w:document>')


def write(name, xml):
    path = OUT / name
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        for part, data in (("[Content_Types].xml", CONTENT_TYPES), ("_rels/.rels", RELS), ("word/document.xml", xml)):
            info = zipfile.ZipInfo(part, date_time=(2026, 11, 9, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data.encode("utf-8"))
    return path


SAMPLES = {
    # Office supplies: a typo, one amount wrong (3 × 20 is not 80), prices written two ways, plain header.
    "purchase-list.docx": document(
        paragraph("十一月办公用品采购清单", bold=True, size=16, align="center"),
        paragraph("申请部门：行政部　　申请日期：2026年11月3日"),
        table([["序号", "品名", "单价（元）", "数量", "金额（元）"],
               ["1", "笔计本", "12", "10", "120"],
               ["2", "签字笔", "3", "20", "80"],
               ["3", "文件夹", "¥8.50", "15", "127.5"],
               ["4", "订书机", "25", "4", "100"],
               ["5", "便利贴", "6", "30", "180"]])),
    # A training timetable: the same kind of date written four ways.
    "course-schedule.docx": document(
        paragraph("新员工入职培训安排", bold=True, size=16, align="center"),
        table([["日期", "时间", "课程", "讲师", "地点"],
               ["11月3日", "09:00-11:30", "公司介绍与企业文化", "王敏", "三楼会议室"],
               ["2026-11-04", "14:00-16:00", "信息安全基础", "陈立", "三楼会议室"],
               ["11/5", "09:30-11:30", "财务报销流程", "刘洋", "二楼培训室"],
               ["2026年11月6日", "14:00-17:00", "办公软件实操", "赵婷", "机房"]], header_bold=True)),
    # Contacts: phone numbers in three styles, a comma in an e-mail address.
    "contacts.docx": document(
        paragraph("市场部通讯录", bold=True, size=16),
        table([["姓名", "职务", "手机", "邮箱"],
               ["张伟", "部门经理", "138 0013 8000", "zhangwei@example.com"],
               ["李娜", "市场专员", "13900139000", "lina@example,com"],
               ["王强", "市场专员", "137-0013-7000", "wangqiang@example.com"],
               ["刘芳", "设计师", "136 0013 6000", "liufang@example.com"]], header_bold=True, header_fill="D9D9D9")),
    # Scores: one total is wrong (88 + 92 + 85 = 265, not 255).
    "scores.docx": document(
        paragraph("第一学期期末成绩", bold=True, size=16, align="center"),
        table([["姓名", "语文", "数学", "英语", "总分"],
               ["陈晨", "88", "92", "85", "255"],
               ["林悦", "76", "81", "90", "247"],
               ["周航", "95", "89", "78", "262"],
               ["吴桐", "82", "77", "88", "247"]], header_bold=True)),
    # The same kind of table with nothing wrong: the assistant should not invent changes.
    "scores-correct.docx": document(
        paragraph("第二学期期末成绩", bold=True, size=16, align="center"),
        table([["姓名", "语文", "数学", "英语", "总分"],
               ["何静", "90", "85", "88", "263"],
               ["孙磊", "72", "95", "80", "247"],
               ["郑雨", "84", "79", "91", "254"]], header_bold=True)),
    # Project status: "done", "完成", "已完成" mean the same thing.
    "project-status.docx": document(
        paragraph("官网改版项目进度", bold=True, size=16),
        paragraph("更新时间：每周五下班前"),
        table([["任务", "负责人", "状态", "截止日期"],
               ["需求调研", "周明", "已完成", "10月20日"],
               ["视觉设计", "郑洁", "完成", "10月31日"],
               ["前端开发", "黄磊", "进行中", "11月15日"],
               ["后端接口", "高远", "done", "11月10日"],
               ["测试上线", "钱程", "未开始", "11月25日"]])),
    # An English spec sheet with a plain header.
    "product-specs.docx": document(
        paragraph("Wireless Mouse Line-up 2026", bold=True, size=16),
        table([["Model", "Weight", "Battery", "Price"],
               ["M100", "85 g", "AA x1", "$19.99"],
               ["M220", "92 g", "Rechargeable", "$29.99"],
               ["M350 Pro", "101 g", "Rechargeable", "$49.99"]])),
    # Two tables; requests name the second one.
    "two-tables.docx": document(
        paragraph("年会筹备", bold=True, size=16),
        paragraph("一、预算"),
        table([["项目", "预算（元）"], ["场地", "8000"], ["餐饮", "12000"], ["奖品", "6000"]]),
        paragraph("二、分工"),
        table([["事项", "负责人", "备注"], ["场地布置", "孙悦", "提前一天"], ["节目统筹", "马超", ""], ["抽奖环节", "林晓", "准备奖品清单"]])),
    # A sign-in sheet with empty cells to fill.
    "meeting-signin.docx": document(
        paragraph("项目周会签到表（11月7日）", bold=True, size=16, align="center"),
        table([["姓名", "部门", "签到"],
               ["唐宁", "研发部", "已到"],
               ["许诺", "研发部", ""],
               ["范晴", "产品部", "已到"],
               ["邓凯", "测试部", ""]], header_bold=True)),
}


# Held out: written after the rules for the samples above were made, and used only to see
# whether those rules carry over to other tables and other wording (never tuned on).
HOLDOUT = {
    # Expenses: one subtotal wrong (45 x 3 is 135), a typo (办工 for 办公).
    "expense.docx": document(
        paragraph("十一月差旅报销明细", bold=True, size=16, align="center"),
        table([["日期", "项目", "单价（元）", "数量", "小计（元）"],
               ["11月2日", "打车", "45", "3", "125"],
               ["11月3日", "办工用品", "18.5", "4", "74"],
               ["11月5日", "午餐", "30", "6", "180"],
               ["11月6日", "快递费", "12", "2", "24"]])),
    # A duty roster: dates written four ways, phones three ways.
    "duty.docx": document(
        paragraph("行政部值班安排", bold=True, size=16),
        table([["日期", "星期", "值班人", "联系电话"],
               ["2026/11/16", "星期一", "周岚", "139-1234-5678"],
               ["11月17日", "星期二", "吴迪", "135 8765 4321"],
               ["2026-11-18", "星期三", "郑好", "13612349876"],
               ["2026年11月19日", "星期四", "冯雪", "137 1111 2222"]], header_bold=True)),
    # Stock: closing = opening + in - out, a subtraction the program does not compute; one wrong (40 + 10 - 15 is 35).
    "inventory.docx": document(
        paragraph("办公用品库存（11月）", bold=True, size=16),
        table([["商品", "期初", "入库", "出库", "期末"],
               ["A4纸", "50", "30", "20", "60"],
               ["碳粉", "12", "6", "8", "10"],
               ["胶水", "40", "10", "15", "45"],
               ["文件袋", "100", "0", "25", "75"]], header_bold=True)),
    # Meeting rooms: two bookings cancelled; no typos.
    "booking.docx": document(
        paragraph("会议室预约（11月20日）", bold=True, size=16),
        table([["会议室", "时间", "预约人", "状态"],
               ["301", "09:00-10:00", "林涛", "已确认"],
               ["302", "10:30-11:30", "秦月", "已取消"],
               ["301", "14:00-15:00", "陆远", "待确认"],
               ["303", "15:30-16:30", "白露", "已取消"]], header_bold=True, header_fill="D9D9D9")),
    # English quarterly sales: one total wrong (98 + 102 + 110 is 310).
    "sales-en.docx": document(
        paragraph("Quarterly Sales by Region (units)", bold=True, size=16),
        table([["Region", "Q1", "Q2", "Q3", "Total"],
               ["North", "120", "135", "150", "405"],
               ["South", "98", "102", "110", "300"],
               ["East", "143", "128", "139", "410"],
               ["West", "87", "95", "101", "283"]], header_bold=True)),
}


# Documents of plain paragraphs for format requests (headings, words, whole paragraphs).
DOCUMENTS = {
    "notice.docx": document(*[paragraph(text) for text in [
        "关于开展年度消防安全检查的通知",
        "各部门：",
        "为切实做好消防安全工作，公司决定开展年度消防安全检查。现将有关事项通知如下：",
        "一、检查时间",
        "11月20日至11月24日，各部门自查截止日期为11月22日。",
        "二、检查内容",
        "消防通道是否畅通、灭火器是否在有效期内、电源线路是否老化。",
        "三、工作要求",
        "各部门负责人要高度重视，注意排查死角，自查报告请于截止日期前提交行政部。",
        "行政部",
        "2026年11月16日"]]),
    "minutes.docx": document(*[paragraph(text) for text in [
        "产品周会会议纪要",
        "时间：2026年11月12日 14:00",
        "地点：三楼会议室",
        "参会人：陈晨、林悦、周航、吴桐",
        "1. 上周进展",
        "官网改版完成视觉设计，负责人：郑洁。",
        "2. 本周计划",
        "完成前端开发，负责人：黄磊；接口联调，负责人：高远。",
        "3. 风险",
        "测试人手不足，可能影响上线时间。"]]),
    "report-en.docx": document(*[paragraph(text) for text in [
        "Quarterly Operations Report",
        "Summary",
        "Shipments grew 12% compared with the previous quarter. The warehouse deadline for inventory counts is December 5.",
        "Next Steps",
        "Hire two temporary packers. Confirm the deadline with the logistics partner.",
        "Prepared by the Operations Team"]]),
}

# Held out for paragraphs: written after the paragraph rules, never tuned on.
DOCUMENTS_HOLDOUT = {
    "training.docx": document(*[paragraph(text) for text in [
        "新员工入职培训安排",
        "培训对象：2026年11月入职的全体新员工",
        "（一）培训时间",
        "11月25日上午9:00开始，请提前十分钟签到。",
        "（二）培训地点",
        "公司二楼大会议室，注意携带工牌。",
        "（三）注意事项",
        "培训期间请将手机调至静音，注意遵守会场纪律。",
        "人力资源部"]]),
    "letter.docx": document(*[paragraph(text) for text in [
        "致客户的一封信",
        "尊敬的客户：",
        "感谢您一直以来对我们的支持。张三经理将于下月起负责贵司的对接工作。",
        "如有任何问题，请联系张三，或拨打客服电话。",
        "此致",
        "敬礼",
        "华东销售部"]]),
}


# Empty documents for drafting from scratch.
CREATE = {
    "blank.docx": document(),
}


def main():
    for name, xml in CREATE.items():
        print(write("create/" + name, xml).relative_to(ROOT))
    for name, xml in SAMPLES.items():
        print(write(name, xml).relative_to(ROOT))
    for name, xml in HOLDOUT.items():
        print(write("holdout/" + name, xml).relative_to(ROOT))
    for name, xml in DOCUMENTS.items():
        print(write("documents/" + name, xml).relative_to(ROOT))
    for name, xml in DOCUMENTS_HOLDOUT.items():
        print(write("documents-holdout/" + name, xml).relative_to(ROOT))


if __name__ == "__main__":
    main()
