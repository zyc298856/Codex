from __future__ import annotations

import copy
import shutil
import tempfile
import zipfile
from pathlib import Path
import xml.etree.ElementTree as ET


PROJECT = Path(r"C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project")
DOCX = PROJECT / "paper" / "full_thesis_latest_merged.docx"
OMML_DIR = PROJECT / "docs" / "thesis_drafting" / "appendix_a_formula_omml_20260507"

W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
M = "http://schemas.openxmlformats.org/officeDocument/2006/math"
A = "http://schemas.openxmlformats.org/drawingml/2006/main"
R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
REL = "http://schemas.openxmlformats.org/package/2006/relationships"

ET.register_namespace("w", W)
ET.register_namespace("m", M)
ET.register_namespace("a", A)
ET.register_namespace("r", R)


def qn(ns: str, tag: str) -> str:
    return f"{{{ns}}}{tag}"


def w_el(tag: str, attrs: dict[str, str] | None = None) -> ET.Element:
    el = ET.Element(qn(W, tag))
    for key, value in (attrs or {}).items():
        el.set(qn(W, key), value)
    return el


def m_el(tag: str, attrs: dict[str, str] | None = None) -> ET.Element:
    el = ET.Element(qn(M, tag))
    for key, value in (attrs or {}).items():
        el.set(qn(M, key), value)
    return el


def paragraph(text: str = "", jc: str | None = None) -> ET.Element:
    p = w_el("p")
    if jc:
        p_pr = w_el("pPr")
        p_pr.append(w_el("jc", {"val": jc}))
        p.append(p_pr)
    if text:
        r_el = w_el("r")
        t_el = w_el("t")
        t_el.text = text
        r_el.append(t_el)
        p.append(r_el)
    return p


def no_borders(tbl_pr: ET.Element) -> None:
    borders = w_el("tblBorders")
    for side in ("top", "left", "bottom", "right", "insideH", "insideV"):
        borders.append(w_el(side, {"val": "nil"}))
    tbl_pr.append(borders)


def cell(width: int, child: ET.Element | None = None) -> ET.Element:
    tc = w_el("tc")
    tc_pr = w_el("tcPr")
    tc_pr.append(w_el("tcW", {"w": str(width), "type": "dxa"}))
    tc_pr.append(w_el("vAlign", {"val": "center"}))
    tc.append(tc_pr)
    tc.append(child if child is not None else paragraph())
    return tc


def equation_table(omml_para: ET.Element, label: str) -> ET.Element:
    """Create a borderless 3-column equation layout: blank | formula | number."""
    tbl = w_el("tbl")
    tbl_pr = w_el("tblPr")
    tbl_pr.append(w_el("tblW", {"w": "9000", "type": "dxa"}))
    tbl_pr.append(w_el("jc", {"val": "center"}))
    no_borders(tbl_pr)
    tbl.append(tbl_pr)

    grid = w_el("tblGrid")
    for width in (550, 7900, 550):
        grid.append(w_el("gridCol", {"w": str(width)}))
    tbl.append(grid)

    tr = w_el("tr")

    formula_p = paragraph(jc="center")
    formula_p.append(copy.deepcopy(omml_para))

    label_p = paragraph(label, jc="right")

    tr.append(cell(550))
    tr.append(cell(7900, formula_p))
    tr.append(cell(550, label_p))
    tbl.append(tr)

    return tbl


def get_omml(formula_id: str) -> ET.Element:
    path = OMML_DIR / f"{formula_id}.omml.xml"
    root = ET.parse(path).getroot()
    if root.tag == qn(M, "oMathPara"):
        return root
    wrapper = m_el("oMathPara")
    wrapper.append(root)
    return wrapper


def rel_targets(rels_root: ET.Element) -> dict[str, str]:
    out: dict[str, str] = {}
    for rel in rels_root:
        rid = rel.attrib.get("Id")
        target = rel.attrib.get("Target")
        if rid and target:
            out[rid] = target
    return out


def paragraph_embed_targets(p: ET.Element, rid_to_target: dict[str, str]) -> list[str]:
    targets: list[str] = []
    for blip in p.findall(f".//{{{A}}}blip"):
        rid = blip.attrib.get(qn(R, "embed"))
        if rid and rid in rid_to_target:
            targets.append(rid_to_target[rid])
    return targets


def main() -> None:
    image_to_formula = {
        "media/image14.png": ("FD1-applsci-13-04402", "(1)"),
        "media/image15.png": ("FD2-applsci-13-04402", "(2)"),
        "media/image16.png": ("FD3-applsci-13-04402", "(3)"),
        "media/image25.png": ("FD3-applsci-13-04402", "(3)"),
        "media/image17.png": ("FD4-applsci-13-04402", "(4)"),
    }

    with tempfile.TemporaryDirectory(dir=PROJECT / "docs" / "thesis_drafting") as tmp_name:
        tmp = Path(tmp_name)
        unzip_dir = tmp / "doc"
        with zipfile.ZipFile(DOCX, "r") as zf:
            zf.extractall(unzip_dir)

        doc_xml = unzip_dir / "word" / "document.xml"
        rels_xml = unzip_dir / "word" / "_rels" / "document.xml.rels"

        doc_tree = ET.parse(doc_xml)
        doc_root = doc_tree.getroot()
        rels_root = ET.parse(rels_xml).getroot()
        rid_to_target = rel_targets(rels_root)

        body = doc_root.find(qn(W, "body"))
        if body is None:
            raise RuntimeError("document body not found")

        replacements = 0
        children = list(body)
        for idx, child in enumerate(children):
            if child.tag != qn(W, "p"):
                continue
            targets = paragraph_embed_targets(child, rid_to_target)
            match = next((t for t in targets if t in image_to_formula), None)
            if not match:
                continue
            formula_id, label = image_to_formula[match]
            tbl = equation_table(get_omml(formula_id), label)
            body.remove(child)
            body.insert(idx, tbl)
            replacements += 1

        if replacements != 8:
            raise RuntimeError(f"expected 8 formula replacements, got {replacements}")

        doc_tree.write(doc_xml, encoding="utf-8", xml_declaration=True)

        patched = tmp / "patched.docx"
        with zipfile.ZipFile(patched, "w", compression=zipfile.ZIP_DEFLATED) as zf:
            for file in unzip_dir.rglob("*"):
                if file.is_file():
                    zf.write(file, file.relative_to(unzip_dir).as_posix())

        shutil.copy2(patched, DOCX)
        print(f"Replaced {replacements} formula images with editable OMML equations.")


if __name__ == "__main__":
    main()
