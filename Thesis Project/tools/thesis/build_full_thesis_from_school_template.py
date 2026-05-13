from __future__ import annotations

import copy
import importlib.util
import re
import shutil
import xml.etree.ElementTree as ET
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


ROOT = Path(__file__).resolve().parents[2]
THESIS_DIR = ROOT / "docs" / "thesis_drafting"
TEMPLATE = Path(r"C:\Users\Tony\Desktop\毕业设计\资料\上海大学通信学院本科毕业论文（设计）撰写格式模板2026年6月.docx")
SIMPLE_BUILD_SCRIPT = ROOT / "tools" / "thesis" / "build_full_thesis_initial_draft.py"
CONTENT_DOCX = THESIS_DIR / "full_thesis_initial_draft.docx"
OUTPUT_DOCX = THESIS_DIR / "full_thesis_template_based_initial_draft_repaired.docx"

NS = {
    "w": "http://schemas.openxmlformats.org/wordprocessingml/2006/main",
    "r": "http://schemas.openxmlformats.org/officeDocument/2006/relationships",
    "rel": "http://schemas.openxmlformats.org/package/2006/relationships",
    "ct": "http://schemas.openxmlformats.org/package/2006/content-types",
    "mc": "http://schemas.openxmlformats.org/markup-compatibility/2006",
}
WP_NS = "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing"
PIC_NS = "http://schemas.openxmlformats.org/drawingml/2006/picture"
WORD_COMPAT_NAMESPACES = {
    "w14": "http://schemas.microsoft.com/office/word/2010/wordml",
    "w15": "http://schemas.microsoft.com/office/word/2012/wordml",
    "w16cid": "http://schemas.microsoft.com/office/word/2016/wordml/cid",
    "w16se": "http://schemas.microsoft.com/office/word/2015/wordml/symex",
    "wp14": "http://schemas.microsoft.com/office/word/2010/wordprocessingDrawing",
}

for prefix, uri in {
    "w": NS["w"],
    "r": NS["r"],
    "wp": "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing",
    "a": "http://schemas.openxmlformats.org/drawingml/2006/main",
    "pic": "http://schemas.openxmlformats.org/drawingml/2006/picture",
    "mc": "http://schemas.openxmlformats.org/markup-compatibility/2006",
    "w14": "http://schemas.microsoft.com/office/word/2010/wordml",
    "w15": "http://schemas.microsoft.com/office/word/2012/wordml",
    "w16cid": "http://schemas.microsoft.com/office/word/2016/wordml/cid",
    "w16se": "http://schemas.microsoft.com/office/word/2015/wordml/symex",
    "wp14": "http://schemas.microsoft.com/office/word/2010/wordprocessingDrawing",
}.items():
    ET.register_namespace(prefix, uri)
ET.register_namespace("", NS["rel"])


def run_simple_builder() -> None:
    spec = importlib.util.spec_from_file_location("full_builder", SIMPLE_BUILD_SCRIPT)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load builder: {SIMPLE_BUILD_SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.main()


def element_text(el: ET.Element) -> str:
    return "".join((t.text or "") for t in el.findall(".//w:t", NS)).strip()


def set_cover_value(paragraph: ET.Element, value: str) -> None:
    text_nodes = paragraph.findall(".//w:t", NS)
    if len(text_nodes) >= 2:
        text_nodes[1].text = value
        text_nodes[1].set("{http://www.w3.org/XML/1998/namespace}space", "preserve")
    elif text_nodes:
        text_nodes[0].text = (text_nodes[0].text or "") + value


def fill_cover_fields(prefix_children: list[ET.Element]) -> None:
    values = {
        "题   目：": "基于嵌入式平台的目标检测系统研究",
        "学    院：": "通信与信息工程学院",
        "专    业：": "电子信息工程",
        "学    号：": "22123739",
        "学生姓名：": "朱奕澄",
        "指导教师：": "滕国伟",
        "起讫日期：": "2025.12.29-2026.05.22",
    }
    for el in prefix_children:
        if not el.tag.endswith("}p"):
            continue
        text = element_text(el)
        for label, value in values.items():
            if text.startswith(label):
                set_cover_value(el, value)
                break


def first_body_index_with_text(children: list[ET.Element], text: str) -> int:
    for idx, el in enumerate(children):
        if el.tag.endswith("}p") and element_text(el) == text:
            return idx
    raise ValueError(f"Unable to find paragraph: {text}")


def max_relationship_number(rels_root: ET.Element) -> int:
    max_id = 0
    for rel in rels_root.findall("rel:Relationship", NS):
        rid = rel.attrib.get("Id", "")
        match = re.fullmatch(r"rId(\d+)", rid)
        if match:
            max_id = max(max_id, int(match.group(1)))
    return max_id


def max_drawing_id(elements: list[ET.Element]) -> int:
    max_id = 0
    drawing_tags = {f"{{{WP_NS}}}docPr", f"{{{PIC_NS}}}cNvPr"}
    for el in elements:
        for node in el.iter():
            if node.tag in drawing_tags:
                value = node.attrib.get("id", "")
                if value.isdigit():
                    max_id = max(max_id, int(value))
    return max_id


def remap_drawing_ids(elements: list[ET.Element], next_id: int) -> None:
    drawing_tags = {f"{{{WP_NS}}}docPr", f"{{{PIC_NS}}}cNvPr"}
    for el in elements:
        for node in el.iter():
            if node.tag in drawing_tags and "id" in node.attrib:
                node.attrib["id"] = str(next_id)
                next_id += 1


def used_namespace_uris(root: ET.Element) -> set[str]:
    used: set[str] = set()
    for el in root.iter():
        if el.tag.startswith("{"):
            used.add(el.tag[1:].split("}", 1)[0])
        for key in el.attrib:
            if key.startswith("{"):
                used.add(key[1:].split("}", 1)[0])
    return used


def normalize_mc_ignorable(root: ET.Element) -> None:
    # ElementTree drops unused namespace declarations. Keep mc:Ignorable in
    # sync so Word does not try to repair references to undeclared prefixes.
    attr = f"{{{NS['mc']}}}Ignorable"
    value = root.attrib.get(attr)
    if not value:
        return
    used_uris = used_namespace_uris(root)
    keep = [
        prefix
        for prefix in value.split()
        if WORD_COMPAT_NAMESPACES.get(prefix) in used_uris
    ]
    if keep:
        root.attrib[attr] = " ".join(keep)
    else:
        root.attrib.pop(attr, None)


def build() -> None:
    if not TEMPLATE.exists():
        raise FileNotFoundError(TEMPLATE)

    if not CONTENT_DOCX.exists():
        run_simple_builder()

    with ZipFile(TEMPLATE, "r") as template_zip, ZipFile(CONTENT_DOCX, "r") as content_zip:
        template_document = ET.fromstring(template_zip.read("word/document.xml"))
        template_body = template_document.find("w:body", NS)
        if template_body is None:
            raise RuntimeError("Template document body not found")
        template_children = list(template_body)

        # Keep the school template cover + originality/authorization pages.
        declaration_end = first_body_index_with_text(template_children, "(注：本页由学生和老师手写签名，然后以扫描页插入论文PDF版)")
        prefix_children = [copy.deepcopy(el) for el in template_children[: declaration_end + 2]]
        fill_cover_fields(prefix_children)

        content_document = ET.fromstring(content_zip.read("word/document.xml"))
        content_body = content_document.find("w:body", NS)
        if content_body is None:
            raise RuntimeError("Generated content body not found")
        content_children = list(content_body)
        content_start = first_body_index_with_text(content_children, "摘要")
        appended_children = [copy.deepcopy(el) for el in content_children[content_start:]]
        remap_drawing_ids(appended_children, max_drawing_id(prefix_children) + 1)

        template_rels = ET.fromstring(template_zip.read("word/_rels/document.xml.rels"))
        content_rels = ET.fromstring(content_zip.read("word/_rels/document.xml.rels"))
        next_rid = max_relationship_number(template_rels) + 1
        rid_map: dict[str, str] = {}
        media_to_copy: list[tuple[str, str]] = []

        for rel in content_rels.findall("rel:Relationship", NS):
            rel_type = rel.attrib.get("Type", "")
            target = rel.attrib.get("Target", "")
            if not rel_type.endswith("/image") or not target.startswith("media/"):
                continue
            old_rid = rel.attrib["Id"]
            new_rid = f"rId{next_rid}"
            next_rid += 1
            old_name = Path(target).name
            new_name = f"thesis_{old_name}"
            rid_map[old_rid] = new_rid
            media_to_copy.append((f"word/{target}", f"word/media/{new_name}"))
            ET.SubElement(
                template_rels,
                f"{{{NS['rel']}}}Relationship",
                {
                    "Id": new_rid,
                    "Type": rel_type,
                    "Target": f"media/{new_name}",
                },
            )

        for el in appended_children:
            for node in el.iter():
                embed_key = f"{{{NS['r']}}}embed"
                if embed_key in node.attrib and node.attrib[embed_key] in rid_map:
                    node.attrib[embed_key] = rid_map[node.attrib[embed_key]]

        for el in list(template_body):
            template_body.remove(el)
        for el in prefix_children:
            template_body.append(el)
        for el in appended_children:
            template_body.append(el)
        normalize_mc_ignorable(template_document)

        # Keep the template content-types file byte-for-byte. The school
        # template already declares PNG/JPEG/WMF defaults; reserializing this
        # part with namespace prefixes can make stricter renderers reject it.
        content_types_bytes = template_zip.read("[Content_Types].xml")

        OUTPUT_DOCX.parent.mkdir(parents=True, exist_ok=True)
        with ZipFile(OUTPUT_DOCX, "w", ZIP_DEFLATED) as output_zip:
            skip = {
                "word/document.xml",
                "word/_rels/document.xml.rels",
                "[Content_Types].xml",
            }
            for item in template_zip.infolist():
                if item.filename in skip:
                    continue
                output_zip.writestr(item, template_zip.read(item.filename))
            output_zip.writestr("word/document.xml", ET.tostring(template_document, encoding="utf-8", xml_declaration=True))
            output_zip.writestr("word/_rels/document.xml.rels", ET.tostring(template_rels, encoding="utf-8", xml_declaration=True))
            output_zip.writestr("[Content_Types].xml", content_types_bytes)
            for source_media, target_media in media_to_copy:
                output_zip.writestr(target_media, content_zip.read(source_media))

    print(OUTPUT_DOCX)


if __name__ == "__main__":
    build()
