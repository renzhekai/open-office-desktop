#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate small deterministic PDF samples for testing the PDF (drawingfile) WASM engine.

Builds the PDFs from scratch with the standard library only: an embedded TrueType font
(OpenSans from the project runtime fonts), vector paths and fills, an embedded JPEG, a URI
link annotation, document outlines, and an AcroForm text field and checkbox with appearance
streams. Output goes to the directory given on the command line; existing files are not
overwritten.

Usage:
    python -I scripts/make_test_pdfs.py <output dir>
"""

import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / "build" / "full-source-run" / "fonts" / "OpenSans-Regular.ttf"
JPEG = Path("C:/Windows/Web/4K/Wallpaper/Windows/img0_1920x1200.jpg")


def ttf_metrics(data):
    """unitsPerEm, ascender, descender, bbox and WinAnsi (32..126) advance widths in 1/1000 em."""
    num_tables = struct.unpack(">H", data[4:6])[0]
    tables = {}
    for i in range(num_tables):
        tag, _, offset, length = struct.unpack(">4sIII", data[12 + 16 * i:28 + 16 * i])
        tables[tag.decode("latin-1")] = (offset, length)
    head = tables["head"][0]
    units = struct.unpack(">H", data[head + 18:head + 20])[0]
    bbox = struct.unpack(">hhhh", data[head + 36:head + 44])
    hhea = tables["hhea"][0]
    ascender, descender = struct.unpack(">hh", data[hhea + 4:hhea + 8])
    num_hmetrics = struct.unpack(">H", data[hhea + 34:hhea + 36])[0]
    hmtx = tables["hmtx"][0]
    advances = [struct.unpack(">H", data[hmtx + 4 * i:hmtx + 4 * i + 2])[0] for i in range(num_hmetrics)]
    cmap = tables["cmap"][0]
    gid_for = {}
    for i in range(struct.unpack(">H", data[cmap + 2:cmap + 4])[0]):
        platform, encoding, offset = struct.unpack(">HHI", data[cmap + 4 + 8 * i:cmap + 12 + 8 * i])
        sub = cmap + offset
        if platform == 3 and encoding == 1 and struct.unpack(">H", data[sub:sub + 2])[0] == 4:
            seg2 = struct.unpack(">H", data[sub + 6:sub + 8])[0]
            ends = struct.unpack(f">{seg2 // 2}H", data[sub + 14:sub + 14 + seg2])
            starts = struct.unpack(f">{seg2 // 2}H", data[sub + 16 + seg2:sub + 16 + 2 * seg2])
            deltas = struct.unpack(f">{seg2 // 2}h", data[sub + 16 + 2 * seg2:sub + 16 + 3 * seg2])
            range_base = sub + 16 + 3 * seg2
            offsets = struct.unpack(f">{seg2 // 2}H", data[range_base:range_base + seg2])
            for code in range(32, 127):
                for k, end in enumerate(ends):
                    if code <= end:
                        if code >= starts[k]:
                            if offsets[k] == 0:
                                gid = (code + deltas[k]) & 0xFFFF
                            else:
                                addr = range_base + 2 * k + offsets[k] + 2 * (code - starts[k])
                                gid = struct.unpack(">H", data[addr:addr + 2])[0]
                                gid = (gid + deltas[k]) & 0xFFFF if gid else 0
                            gid_for[code] = gid
                        break
            break
    widths = [round(advances[min(gid_for.get(c, 0), num_hmetrics - 1)] * 1000 / units) for c in range(32, 127)]
    scale = lambda v: round(v * 1000 / units)
    return {"ascent": scale(ascender), "descent": scale(descender), "bbox": [scale(v) for v in bbox], "widths": widths}


class PdfWriter:
    def __init__(self):
        self.objects = []

    def add(self, body):
        self.objects.append(body if isinstance(body, bytes) else body.encode("latin-1"))
        return len(self.objects)

    def stream(self, dictionary, data, compress=True):
        if compress:
            data = zlib.compress(data, 9)
            dictionary += " /Filter /FlateDecode"
        return self.add(f"<< {dictionary} /Length {len(data)} >>\nstream\n".encode("latin-1") + data + b"\nendstream")

    def reserve(self):
        self.objects.append(None)
        return len(self.objects)

    def set(self, number, body):
        self.objects[number - 1] = body.encode("latin-1") if isinstance(body, str) else body

    def write(self, path, root, info):
        out = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
        offsets = []
        for number, body in enumerate(self.objects, 1):
            offsets.append(len(out))
            out += f"{number} 0 obj\n".encode("latin-1") + body + b"\nendobj\n"
        xref = len(out)
        out += f"xref\n0 {len(self.objects) + 1}\n0000000000 65535 f \n".encode("latin-1")
        for offset in offsets:
            out += f"{offset:010d} 00000 n \n".encode("latin-1")
        out += (f"trailer\n<< /Size {len(self.objects) + 1} /Root {root} 0 R /Info {info} 0 R "
                f"/ID [<4f50454e4f4646494345303030303031> <4f50454e4f4646494345303030303031>] >>\n"
                f"startxref\n{xref}\n%%EOF\n").encode("latin-1")
        path.write_bytes(bytes(out))


def pdf_text(text):
    return "(" + text.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)") + ")"


def font_objects(pdf):
    data = FONT.read_bytes()
    m = ttf_metrics(data)
    file_obj = pdf.stream(f"/Length1 {len(data)}", data)
    descriptor = pdf.add(f"<< /Type /FontDescriptor /FontName /OpenSans-Regular /Flags 32 "
                         f"/FontBBox [{' '.join(map(str, m['bbox']))}] /ItalicAngle 0 /Ascent {m['ascent']} "
                         f"/Descent {m['descent']} /CapHeight {m['ascent']} /StemV 80 /FontFile2 {file_obj} 0 R >>")
    return pdf.add(f"<< /Type /Font /Subtype /TrueType /BaseFont /OpenSans-Regular /FirstChar 32 /LastChar 126 "
                   f"/Widths [{' '.join(map(str, m['widths']))}] /Encoding /WinAnsiEncoding "
                   f"/FontDescriptor {descriptor} 0 R >>")


def make_document(path):
    pdf = PdfWriter()
    font = font_objects(pdf)
    jpeg = JPEG.read_bytes()
    width, height = 1920, 1200
    image = pdf.stream(f"/Type /XObject /Subtype /Image /Width {width} /Height {height} /ColorSpace /DeviceRGB "
                       f"/BitsPerComponent 8 /Filter /DCTDecode", jpeg, compress=False)
    pages_id = pdf.reserve()
    page_ids = []
    contents = [
        "\n".join([
            "BT /F1 28 Tf 72 760 Td " + pdf_text("Open Office PDF engine test") + " Tj ET",
            "BT /F1 12 Tf 72 730 Td 16 TL " + pdf_text("Embedded TrueType text, kerning-free WinAnsi glyphs.") + " Tj T* "
            + pdf_text("The quick brown fox jumps over the lazy dog 0123456789.") + " Tj ET",
            "0.85 0.2 0.2 rg 72 560 200 120 re f",
            "0.1 0.4 0.8 RG 4 w 300 560 m 520 680 l 520 560 l h S",
            "0 0.6 0.3 rg 0 0 0 RG 2 w 160 470 m 220 530 300 410 360 470 c 300 360 220 520 160 470 c b",
            "q 288 0 0 180 72 220 cm /Im1 Do Q",
        ]),
        "\n".join([
            "BT /F1 20 Tf 72 760 Td " + pdf_text("Second page with a link and outlines") + " Tj ET",
            "BT /F1 12 Tf 72 700 Td 0 0 1 rg " + pdf_text("https://www.onlyoffice.com") + " Tj ET",
            "0.5 g 72 600 468 2 re f",
        ]),
    ]
    resources = f"<< /Font << /F1 {font} 0 R >> /XObject << /Im1 {image} 0 R >> >>"
    for index, text in enumerate(contents):
        content = pdf.stream("", text.encode("latin-1"))
        annots = ""
        if index == 1:
            link = pdf.add("<< /Type /Annot /Subtype /Link /Rect [72 695 260 712] /Border [0 0 1] "
                           "/A << /S /URI /URI (https://www.onlyoffice.com) >> >>")
            annots = f" /Annots [{link} 0 R]"
        page_ids.append(pdf.add(f"<< /Type /Page /Parent {pages_id} 0 R /MediaBox [0 0 612 792] "
                                f"/Resources {resources} /Contents {content} 0 R{annots} >>"))
    pdf.set(pages_id, f"<< /Type /Pages /Kids [{' '.join(f'{p} 0 R' for p in page_ids)}] /Count {len(page_ids)} >>")
    outlines = pdf.reserve()
    first = pdf.reserve()
    second = pdf.reserve()
    pdf.set(first, f"<< /Title (Page one) /Parent {outlines} 0 R /Next {second} 0 R /Dest [{page_ids[0]} 0 R /XYZ 0 792 0] >>")
    pdf.set(second, f"<< /Title (Page two) /Parent {outlines} 0 R /Prev {first} 0 R /Dest [{page_ids[1]} 0 R /XYZ 0 792 0] >>")
    pdf.set(outlines, f"<< /Type /Outlines /First {first} 0 R /Last {second} 0 R /Count 2 >>")
    catalog = pdf.add(f"<< /Type /Catalog /Pages {pages_id} 0 R /Outlines {outlines} 0 R /PageMode /UseOutlines >>")
    info = pdf.add("<< /Title (Open Office WASM test) /Producer (scripts/make_test_pdfs.py) "
                   "/CreationDate (D:20261104000000Z) >>")
    pdf.write(path, catalog, info)


def make_form(path):
    pdf = PdfWriter()
    font = font_objects(pdf)
    pages_id = pdf.reserve()
    page_id = pdf.reserve()
    text_ap = pdf.stream(f"/Type /XObject /Subtype /Form /BBox [0 0 200 24] /Resources << /Font << /F1 {font} 0 R >> >>",
                         b"/Tx BMC BT /F1 12 Tf 2 7 Td (Prefilled value) Tj ET EMC")
    on_ap = pdf.stream("/Type /XObject /Subtype /Form /BBox [0 0 16 16]", b"0 g 3 3 10 10 re f")
    off_ap = pdf.stream("/Type /XObject /Subtype /Form /BBox [0 0 16 16]", b"0 G 1 w 0.5 0.5 15 15 re S")
    text_field = pdf.add(f"<< /Type /Annot /Subtype /Widget /FT /Tx /T (name) /V (Prefilled value) "
                         f"/DA (/F1 12 Tf 0 g) /Rect [72 700 272 724] /P {page_id} 0 R /F 4 "
                         f"/AP << /N {text_ap} 0 R >> >>")
    checkbox = pdf.add(f"<< /Type /Annot /Subtype /Widget /FT /Btn /T (agree) /V /Yes /AS /Yes "
                       f"/Rect [72 660 88 676] /P {page_id} 0 R /F 4 "
                       f"/AP << /N << /Yes {on_ap} 0 R /Off {off_ap} 0 R >> >> >>")
    note = pdf.add(f"<< /Type /Annot /Subtype /Text /Rect [300 700 320 720] /Contents (A sticky note) "
                   f"/P {page_id} 0 R /Open false /Name /Comment >>")
    content = pdf.stream("", ("BT /F1 14 Tf 72 760 Td " + pdf_text("Form fields and a note") + " Tj ET").encode("latin-1"))
    pdf.set(page_id, f"<< /Type /Page /Parent {pages_id} 0 R /MediaBox [0 0 612 792] "
                     f"/Resources << /Font << /F1 {font} 0 R >> >> /Contents {content} 0 R "
                     f"/Annots [{text_field} 0 R {checkbox} 0 R {note} 0 R] >>")
    pdf.set(pages_id, f"<< /Type /Pages /Kids [{page_id} 0 R] /Count 1 >>")
    catalog = pdf.add(f"<< /Type /Catalog /Pages {pages_id} 0 R "
                      f"/AcroForm << /Fields [{text_field} 0 R {checkbox} 0 R] /DA (/F1 12 Tf 0 g) "
                      f"/DR << /Font << /F1 {font} 0 R >> >> >> >>")
    info = pdf.add("<< /Title (Open Office WASM form test) /Producer (scripts/make_test_pdfs.py) >>")
    pdf.write(path, catalog, info)


def main():
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for name, maker in (("document.pdf", make_document), ("form.pdf", make_form)):
        target = out / name
        if target.exists():
            sys.exit(f"{target} exists")
        maker(target)
        print(target, target.stat().st_size)


if __name__ == "__main__":
    main()
