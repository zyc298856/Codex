from __future__ import annotations

import argparse
import html
import re
import struct
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


ROOT = Path(__file__).resolve().parents[2]


def esc(text: str) -> str:
    return html.escape(text, quote=True)


def normalize_text(text: str) -> str:
    text = text.replace("`", "")
    text = re.sub(r"(?<=[\u4e00-\u9fff])\s+(?=[A-Za-z0-9])", "", text)
    text = re.sub(r"(?<=[A-Za-z0-9])\s+(?=[\u4e00-\u9fff])", "", text)
    return text


def rpr(size_half_points: int = 24, east_asia: str = "宋体", ascii_font: str = "Times New Roman", bold: bool = False) -> str:
    b = "<w:b/>" if bold else ""
    return (
        "<w:rPr>"
        f"<w:rFonts w:ascii=\"{ascii_font}\" w:hAnsi=\"{ascii_font}\" w:eastAsia=\"{east_asia}\"/>"
        f"{b}<w:sz w:val=\"{size_half_points}\"/><w:szCs w:val=\"{size_half_points}\"/>"
        "</w:rPr>"
    )


def paragraph(
    text: str = "",
    *,
    align: str = "both",
    first_line: bool = True,
    size_half_points: int = 24,
    east_asia: str = "宋体",
    ascii_font: str = "Times New Roman",
    bold: bool = False,
    before: int = 0,
    after: int = 0,
    line: int = 360,
    style_id: str | None = None,
) -> str:
    style = f"<w:pStyle w:val=\"{style_id}\"/>" if style_id else ""
    indent = "<w:ind w:firstLine=\"480\"/>" if first_line else ""
    jc = f"<w:jc w:val=\"{align}\"/>" if align else ""
    ppr = (
        "<w:pPr>"
        f"{style}<w:spacing w:before=\"{before}\" w:after=\"{after}\" w:line=\"{line}\" w:lineRule=\"auto\"/>"
        f"{indent}{jc}</w:pPr>"
    )
    run = f"<w:r>{rpr(size_half_points, east_asia, ascii_font, bold)}<w:t xml:space=\"preserve\">{esc(normalize_text(text))}</w:t></w:r>"
    return f"<w:p>{ppr}{run}</w:p>"


def heading(text: str, level: int) -> str:
    if level == 1:
        return paragraph(text, align="center", first_line=False, size_half_points=36, east_asia="黑体", bold=True, before=240, after=240)
    if level == 2:
        return paragraph(text, align="left", first_line=False, size_half_points=28, east_asia="黑体", bold=True, before=240, after=120)
    return paragraph(text, align="left", first_line=False, size_half_points=24, east_asia="黑体", bold=True, before=180, after=80)


def caption_paragraph(text: str) -> str:
    ppr = (
        "<w:pPr>"
        '<w:spacing w:before="40" w:after="140" w:line="300" w:lineRule="auto"/>'
        '<w:jc w:val="center"/></w:pPr>'
    )
    run = f"<w:r>{rpr(21)}<w:t xml:space=\"preserve\">{esc(text)}</w:t></w:r>"
    return f"<w:p>{ppr}{run}</w:p>"


def code_block(text: str) -> str:
    rows = []
    for line_text in text.splitlines():
        rows.append(
            f"<w:p><w:pPr><w:spacing w:before=\"0\" w:after=\"0\" w:line=\"260\" w:lineRule=\"auto\"/>"
            f"<w:jc w:val=\"left\"/></w:pPr><w:r>{rpr(20, '宋体', 'Consolas')}"
            f"<w:t xml:space=\"preserve\">{esc(line_text)}</w:t></w:r></w:p>"
        )
    inner = "".join(rows) or paragraph("", first_line=False)
    return (
        "<w:tbl>"
        "<w:tblPr><w:tblW w:w=\"9000\" w:type=\"dxa\"/>"
        "<w:tblBorders>"
        "<w:top w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"D9EAF7\"/>"
        "<w:left w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"D9EAF7\"/>"
        "<w:bottom w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"D9EAF7\"/>"
        "<w:right w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"D9EAF7\"/>"
        "</w:tblBorders></w:tblPr>"
        "<w:tr><w:tc><w:tcPr><w:tcW w:w=\"9000\" w:type=\"dxa\"/>"
        "<w:shd w:fill=\"F6FAFC\"/><w:tcMar><w:top w:w=\"120\" w:type=\"dxa\"/>"
        "<w:left w:w=\"160\" w:type=\"dxa\"/><w:bottom w:w=\"120\" w:type=\"dxa\"/>"
        "<w:right w:w=\"160\" w:type=\"dxa\"/></w:tcMar></w:tcPr>"
        f"{inner}</w:tc></w:tr></w:tbl>"
        + paragraph("", first_line=False, after=80)
    )


def page_break() -> str:
    return '<w:p><w:r><w:br w:type="page"/></w:r></w:p>'


def formula_block(text: str) -> str:
    if "|" in text:
        formula_text, number = [part.strip() for part in text.rsplit("|", 1)]
    else:
        formula_text, number = text.strip(), ""
    return (
        '<w:tbl><w:tblPr><w:tblW w:w="8200" w:type="dxa"/>'
        '<w:tblLayout w:type="fixed"/><w:jc w:val="center"/>'
        '<w:tblBorders>'
        '<w:top w:val="nil"/><w:left w:val="nil"/><w:bottom w:val="nil"/><w:right w:val="nil"/>'
        '<w:insideH w:val="nil"/><w:insideV w:val="nil"/>'
        '</w:tblBorders></w:tblPr>'
        '<w:tblGrid><w:gridCol w:w="6800"/><w:gridCol w:w="1400"/></w:tblGrid>'
        '<w:tr>'
        '<w:tc><w:tcPr><w:tcW w:w="6800" w:type="dxa"/><w:vAlign w:val="center"/></w:tcPr>'
        + paragraph(formula_text, align="center", first_line=False, size_half_points=24, ascii_font="Times New Roman", line=300)
        + '</w:tc>'
        '<w:tc><w:tcPr><w:tcW w:w="1400" w:type="dxa"/><w:vAlign w:val="center"/></w:tcPr>'
        + paragraph(number, align="right", first_line=False, size_half_points=24, ascii_font="Times New Roman", line=300)
        + '</w:tc>'
        '</w:tr></w:tbl>'
        + paragraph("", first_line=False, after=80)
    )


def table(rows: list[list[str]]) -> str:
    if not rows:
        return ""
    col_count = max(len(r) for r in rows)
    if col_count == 3:
        widths = [1700, 2200, 4300]
    elif col_count == 4:
        widths = [1700, 2100, 2100, 2300]
    elif col_count == 5:
        widths = [1700, 1400, 1600, 1600, 1900]
    elif col_count == 6:
        widths = [1300, 1100, 1100, 1500, 1400, 1800]
    elif col_count == 7:
        widths = [1300, 950, 950, 1300, 1300, 1000, 1400]
    elif col_count == 8:
        widths = [1100, 900, 900, 1050, 1050, 1100, 1000, 1100]
    elif col_count == 9:
        widths = [800, 750, 750, 950, 950, 1150, 850, 950, 1050]
    else:
        widths = [int(8200 / col_count)] * col_count

    cell_font_size = 20 if col_count <= 5 else 18
    cell_line = 250 if col_count <= 5 else 220
    cell_margin = 90 if col_count <= 5 else 60
    table_width = sum(widths)

    xml = [
        f"<w:tbl><w:tblPr><w:tblW w:w=\"{table_width}\" w:type=\"dxa\"/>"
        "<w:tblLayout w:type=\"fixed\"/><w:jc w:val=\"center\"/>",
        "<w:tblBorders>",
        "<w:top w:val=\"single\" w:sz=\"8\" w:space=\"0\" w:color=\"000000\"/>",
        "<w:left w:val=\"nil\"/>",
        "<w:bottom w:val=\"single\" w:sz=\"8\" w:space=\"0\" w:color=\"000000\"/>",
        "<w:right w:val=\"nil\"/>",
        "<w:insideH w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"000000\"/>",
        "<w:insideV w:val=\"nil\"/>",
        "</w:tblBorders></w:tblPr><w:tblGrid>",
    ]
    for width in widths:
        xml.append(f"<w:gridCol w:w=\"{width}\"/>")
    xml.append("</w:tblGrid>")

    for row_index, row in enumerate(rows):
        xml.append("<w:tr>")
        for col_index in range(col_count):
            value = row[col_index] if col_index < len(row) else ""
            fill = "<w:shd w:fill=\"DCEBF4\"/>" if row_index == 0 else ""
            header_border = '<w:tcBorders><w:bottom w:val="single" w:sz="6" w:space="0" w:color="000000"/></w:tcBorders>' if row_index == 0 else ""
            bold = row_index == 0
            east = "黑体" if bold else "宋体"
            align = "center" if row_index == 0 or len(value) <= 18 else "left"
            xml.append(
                "<w:tc><w:tcPr>"
                f"<w:tcW w:w=\"{widths[col_index]}\" w:type=\"dxa\"/>"
                f"{fill}{header_border}<w:vAlign w:val=\"center\"/>"
                f"<w:tcMar><w:top w:w=\"90\" w:type=\"dxa\"/><w:left w:w=\"{cell_margin}\" w:type=\"dxa\"/>"
                f"<w:bottom w:w=\"90\" w:type=\"dxa\"/><w:right w:w=\"{cell_margin}\" w:type=\"dxa\"/></w:tcMar>"
                "</w:tcPr>"
                + paragraph(value, align=align, first_line=False, size_half_points=cell_font_size, east_asia=east, bold=bold, line=cell_line)
                + "</w:tc>"
            )
        xml.append("</w:tr>")
    xml.append("</w:tbl>")
    xml.append(paragraph("", first_line=False, after=120))
    return "".join(xml)


def png_size(path: Path) -> tuple[int, int]:
    with path.open("rb") as f:
        header = f.read(24)
    if header[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"Unsupported image format, expected PNG: {path}")
    return struct.unpack(">II", header[16:24])


def image_paragraph(rel_id: str, docpr_id: int, image_path: Path, max_width_emu: int = 5_300_000) -> str:
    width_px, height_px = png_size(image_path)
    width_emu = max_width_emu
    height_emu = int(max_width_emu * height_px / width_px)
    name = esc(image_path.name)
    return (
        '<w:p><w:pPr><w:spacing w:before="120" w:after="80"/><w:jc w:val="center"/></w:pPr><w:r><w:drawing>'
        '<wp:inline distT="0" distB="0" distL="0" distR="0">'
        f'<wp:extent cx="{width_emu}" cy="{height_emu}"/>'
        '<wp:effectExtent l="0" t="0" r="0" b="0"/>'
        f'<wp:docPr id="{docpr_id}" name="{name}"/>'
        '<wp:cNvGraphicFramePr><a:graphicFrameLocks xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" noChangeAspect="1"/></wp:cNvGraphicFramePr>'
        '<a:graphic xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"><a:graphicData uri="http://schemas.openxmlformats.org/drawingml/2006/picture">'
        '<pic:pic xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture">'
        '<pic:nvPicPr>'
        f'<pic:cNvPr id="{docpr_id}" name="{name}"/>'
        '<pic:cNvPicPr/>'
        '</pic:nvPicPr>'
        '<pic:blipFill>'
        f'<a:blip r:embed="{rel_id}"/>'
        '<a:stretch><a:fillRect/></a:stretch>'
        '</pic:blipFill>'
        '<pic:spPr>'
        '<a:xfrm><a:off x="0" y="0"/>'
        f'<a:ext cx="{width_emu}" cy="{height_emu}"/></a:xfrm>'
        '<a:prstGeom prst="rect"><a:avLst/></a:prstGeom>'
        '</pic:spPr>'
        '</pic:pic>'
        '</a:graphicData></a:graphic>'
        '</wp:inline>'
        '</w:drawing></w:r></w:p>'
    )


def figure_image_for_caption(caption: str, source_dir: Path) -> Path | None:
    normalized_caption = caption.replace(" ", "")
    figure_maps = {
        "图3.1": source_dir / "figures" / "fig_3_1_system_architecture.png",
        "图3.2": source_dir / "figures" / "fig_3_2_model_migration.png",
        "图3.3": source_dir / "figures" / "fig_3_3_realtime_pipeline.png",
        "图3.4": source_dir / "figures" / "fig_3_4_multi_context.png",
    }
    for prefix, path in figure_maps.items():
        if normalized_caption.startswith(prefix) and path.exists():
            return path
    return None


def parse_markdown(md: str, source_dir: Path) -> tuple[str, list[tuple[str, Path, str]]]:
    body: list[str] = []
    images: list[tuple[str, Path, str]] = []
    lines = md.splitlines()
    i = 0
    while i < len(lines):
        stripped = lines[i].strip()
        if not stripped:
            i += 1
            continue

        if stripped.startswith("# "):
            body.append(heading(stripped[2:].strip(), 1))
            i += 1
            continue
        if stripped.startswith("## "):
            body.append(heading(stripped[3:].strip(), 2))
            i += 1
            continue
        if stripped.startswith("### "):
            body.append(heading(stripped[4:].strip(), 3))
            i += 1
            continue

        if stripped.startswith("```"):
            code_lines: list[str] = []
            i += 1
            while i < len(lines) and not lines[i].strip().startswith("```"):
                code_lines.append(lines[i])
                i += 1
            i += 1
            body.append(code_block("\n".join(code_lines)))
            continue

        if stripped == "[[PAGEBREAK]]":
            body.append(page_break())
            i += 1
            continue

        if stripped.startswith("$$"):
            formula_lines: list[str] = []
            first = stripped[2:].strip()
            if first:
                formula_lines.append(first)
            i += 1
            while i < len(lines) and not lines[i].strip().endswith("$$"):
                formula_lines.append(lines[i].strip())
                i += 1
            if i < len(lines):
                last = lines[i].strip()[:-2].strip()
                if last:
                    formula_lines.append(last)
                i += 1
            body.append(formula_block(" ".join(formula_lines)))
            continue

        image_match = re.match(r"^!\[([^\]]*)\]\(([^)]+)\)$", stripped)
        if image_match:
            caption = image_match.group(1).strip()
            raw_path = image_match.group(2).strip()
            image_path = Path(raw_path)
            if not image_path.is_absolute():
                image_path = source_dir / image_path
            media_name = f"image{len(images) + 1}{image_path.suffix.lower()}"
            rel_id = f"rId{10 + len(images)}"
            images.append((rel_id, image_path, media_name))
            body.append(image_paragraph(rel_id, len(images) + 1, image_path))
            if caption:
                body.append(caption_paragraph(caption))
            i += 1
            continue

        if stripped.startswith("|"):
            rows: list[list[str]] = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                cells = [cell.strip() for cell in lines[i].strip().strip("|").split("|")]
                if not all(re.fullmatch(r":?-{3,}:?", cell) for cell in cells):
                    rows.append(cells)
                i += 1
            body.append(table(rows))
            continue

        if re.match(r"^图\s*\d+\.\d+", stripped):
            j = i + 1
            while j < len(lines) and not lines[j].strip():
                j += 1
            if j < len(lines) and lines[j].strip().startswith("```mermaid"):
                image_path = figure_image_for_caption(stripped, source_dir)
                if image_path:
                    media_name = f"image{len(images) + 1}{image_path.suffix.lower()}"
                    rel_id = f"rId{10 + len(images)}"
                    images.append((rel_id, image_path, media_name))
                    body.append(image_paragraph(rel_id, len(images) + 1, image_path))
                    body.append(caption_paragraph(stripped))
                    i = j + 1
                    while i < len(lines) and not lines[i].strip().startswith("```"):
                        i += 1
                    i += 1
                    continue
            body.append(caption_paragraph(stripped))
            i += 1
            continue

        if re.match(r"^表\s*\d+\.\d+", stripped):
            body.append(caption_paragraph(stripped))
            i += 1
            continue

        if re.match(r"^\d+\.\s+", stripped):
            body.append(paragraph(stripped, align="left", first_line=False, size_half_points=24, before=0, after=0, line=360))
            i += 1
            continue

        para_lines = [stripped]
        i += 1
        while i < len(lines):
            nxt = lines[i].strip()
            if (
                not nxt
                or nxt.startswith("#")
                or nxt.startswith("|")
                or nxt.startswith("![")
                or nxt.startswith("```")
                or nxt == "[[PAGEBREAK]]"
                or nxt.startswith("$$")
                or re.match(r"^(表|图)\s*\d+\.\d+", nxt)
                or re.match(r"^\d+\.\s+", nxt)
            ):
                break
            para_lines.append(nxt)
            i += 1
        body.append(paragraph("".join(para_lines)))
    return "".join(body), images


def document_xml(body_xml: str) -> str:
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<w:document xmlns:wpc="http://schemas.microsoft.com/office/word/2010/wordprocessingCanvas" '
        'xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006" '
        'xmlns:o="urn:schemas-microsoft-com:office:office" '
        'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" '
        'xmlns:m="http://schemas.openxmlformats.org/officeDocument/2006/math" '
        'xmlns:v="urn:schemas-microsoft-com:vml" '
        'xmlns:wp14="http://schemas.microsoft.com/office/word/2010/wordprocessingDrawing" '
        'xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing" '
        'xmlns:w10="urn:schemas-microsoft-com:office:word" '
        'xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" '
        'xmlns:w14="http://schemas.microsoft.com/office/word/2010/wordml" '
        'xmlns:wpg="http://schemas.microsoft.com/office/word/2010/wordprocessingGroup" '
        'xmlns:wpi="http://schemas.microsoft.com/office/word/2010/wordprocessingInk" '
        'xmlns:wne="http://schemas.microsoft.com/office/word/2006/wordml" '
        'xmlns:wps="http://schemas.microsoft.com/office/word/2010/wordprocessingShape" '
        'mc:Ignorable="w14 wp14"><w:body>'
        f"{body_xml}"
        '<w:sectPr><w:pgSz w:w="11906" w:h="16838"/><w:pgMar w:top="1440" w:right="1474" w:bottom="1440" w:left="1701" w:header="851" w:footer="992" w:gutter="0"/>'
        '<w:cols w:space="425"/><w:docGrid w:type="lines" w:linePitch="312"/></w:sectPr>'
        '</w:body></w:document>'
    )


def styles_xml() -> str:
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<w:styles xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">'
        '<w:style w:type="paragraph" w:default="1" w:styleId="Normal">'
        '<w:name w:val="Normal"/><w:qFormat/><w:rPr>'
        '<w:rFonts w:ascii="Times New Roman" w:hAnsi="Times New Roman" w:eastAsia="宋体"/>'
        '<w:sz w:val="24"/></w:rPr></w:style></w:styles>'
    )


def content_types_xml(has_png: bool = False) -> str:
    png_default = '<Default Extension="png" ContentType="image/png"/>' if has_png else ""
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
        '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
        '<Default Extension="xml" ContentType="application/xml"/>'
        f"{png_default}"
        '<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>'
        '<Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>'
        '<Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>'
        '<Override PartName="/docProps/app.xml" ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/>'
        '</Types>'
    )


def rels_xml() -> str:
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>'
        '<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>'
        '<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/app.xml"/>'
        '</Relationships>'
    )


def document_rels_xml(images: list[tuple[str, Path, str]] | None = None) -> str:
    images = images or []
    image_rels = "".join(
        f'<Relationship Id="{rel_id}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="media/{media_name}"/>'
        for rel_id, _path, media_name in images
    )
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>'
        f"{image_rels}"
        '</Relationships>'
    )


def core_xml() -> str:
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" '
        'xmlns:dc="http://purl.org/dc/elements/1.1/" '
        'xmlns:dcterms="http://purl.org/dc/terms/" '
        'xmlns:dcmitype="http://purl.org/dc/dcmitype/" '
        'xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">'
        '<dc:title>Chapter Draft</dc:title><dc:creator>Codex</dc:creator>'
        '</cp:coreProperties>'
    )


def app_xml() -> str:
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/extended-properties" '
        'xmlns:vt="http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes">'
        '<Application>Codex</Application></Properties>'
    )


def build_docx(source: Path, output: Path) -> None:
    md = source.read_text(encoding="utf-8")
    body, images = parse_markdown(md, source.parent)
    with ZipFile(output, "w", ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", content_types_xml(any(media_name.endswith(".png") for _, _, media_name in images)))
        z.writestr("_rels/.rels", rels_xml())
        z.writestr("word/_rels/document.xml.rels", document_rels_xml(images))
        z.writestr("word/document.xml", document_xml(body))
        z.writestr("word/styles.xml", styles_xml())
        z.writestr("docProps/core.xml", core_xml())
        z.writestr("docProps/app.xml", app_xml())
        for _rel_id, image_path, media_name in images:
            z.write(image_path, f"word/media/{media_name}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source")
    parser.add_argument("output")
    args = parser.parse_args()
    build_docx(Path(args.source), Path(args.output))
    print(args.output)


if __name__ == "__main__":
    main()
