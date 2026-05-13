from __future__ import annotations

import copy
import shutil
import zipfile
from pathlib import Path
from xml.etree import ElementTree as ET


DOCX = Path(r"C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\paper\full_thesis_latest_merged.docx")
BACKUP = Path(r"C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\docs\thesis_drafting\full_thesis_latest_merged_before_chapter1_heading_fix.docx")
TMP = DOCX.with_suffix(".chapter1fix.tmp.docx")

W_NS = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
NS = {"w": W_NS}
ET.register_namespace("w", W_NS)


def qn(local: str) -> str:
    return f"{{{W_NS}}}{local}"


def paragraph_text(p: ET.Element) -> str:
    return "".join((t.text or "") for t in p.findall(".//w:t", NS)).strip()


def make_spacing() -> ET.Element:
    spacing = ET.Element(qn("spacing"))
    spacing.set(qn("before"), "240")
    spacing.set(qn("after"), "240")
    spacing.set(qn("line"), "360")
    spacing.set(qn("lineRule"), "auto")
    return spacing


def make_rpr() -> ET.Element:
    rpr = ET.Element(qn("rPr"))
    rfonts = ET.SubElement(rpr, qn("rFonts"))
    rfonts.set(qn("eastAsia"), "黑体")
    ET.SubElement(rpr, qn("b"))
    sz = ET.SubElement(rpr, qn("sz"))
    sz.set(qn("val"), "36")
    szcs = ET.SubElement(rpr, qn("szCs"))
    szcs.set(qn("val"), "36")
    return rpr


def ensure_child(parent: ET.Element, tag: str) -> ET.Element:
    child = parent.find(f"w:{tag}", NS)
    if child is None:
        child = ET.SubElement(parent, qn(tag))
    return child


def fix_heading(p: ET.Element) -> None:
    ppr = p.find("w:pPr", NS)
    if ppr is None:
        ppr = ET.Element(qn("pPr"))
        p.insert(0, ppr)

    # Keep the chapter on a new page, but match the visible chapter-title spacing.
    if ppr.find("w:pageBreakBefore", NS) is None:
        ppr.insert(0, ET.Element(qn("pageBreakBefore")))

    old_spacing = ppr.find("w:spacing", NS)
    if old_spacing is not None:
        ppr.remove(old_spacing)
    ppr.append(make_spacing())

    jc = ensure_child(ppr, "jc")
    jc.set(qn("val"), "center")

    outline = ensure_child(ppr, "outlineLvl")
    outline.set(qn("val"), "0")

    old_para_rpr = ppr.find("w:rPr", NS)
    if old_para_rpr is not None:
        ppr.remove(old_para_rpr)

    for run in p.findall("w:r", NS):
        old = run.find("w:rPr", NS)
        if old is not None:
            run.remove(old)
        run.insert(0, copy.deepcopy(make_rpr()))


def main() -> None:
    if not BACKUP.exists():
        BACKUP.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(DOCX, BACKUP)

    with zipfile.ZipFile(DOCX, "r") as zin:
        document_xml = zin.read("word/document.xml")
        root = ET.fromstring(document_xml)

        matches = [p for p in root.findall(".//w:p", NS) if paragraph_text(p) == "第一章 绪论"]
        if len(matches) != 1:
            raise RuntimeError(f"Expected exactly one body heading match, found {len(matches)}")

        fix_heading(matches[0])
        new_xml = ET.tostring(root, encoding="utf-8", xml_declaration=True)

        with zipfile.ZipFile(TMP, "w", zipfile.ZIP_DEFLATED) as zout:
            for item in zin.infolist():
                data = new_xml if item.filename == "word/document.xml" else zin.read(item.filename)
                zout.writestr(item, data)

    TMP.replace(DOCX)
    print(f"Fixed: {DOCX}")
    print(f"Backup: {BACKUP}")


if __name__ == "__main__":
    main()
