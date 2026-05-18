from __future__ import annotations

import json
import sys
import zipfile
from pathlib import Path
from xml.etree import ElementTree as ET


NS = {
    "p": "http://schemas.openxmlformats.org/presentationml/2006/main",
    "a": "http://schemas.openxmlformats.org/drawingml/2006/main",
    "r": "http://schemas.openxmlformats.org/officeDocument/2006/relationships",
}


def text_of_slide(zf: zipfile.ZipFile, name: str) -> str:
    root = ET.fromstring(zf.read(name))
    chunks = []
    for t in root.findall(".//a:t", NS):
        if t.text:
            chunks.append(t.text)
    return "\n".join(chunks)


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: pptx_package_check.py <deck.pptx> <report.json>")
        return 2
    pptx = Path(sys.argv[1])
    report = Path(sys.argv[2])
    issues: list[str] = []
    warnings: list[str] = []
    with zipfile.ZipFile(pptx) as zf:
        names = zf.namelist()
        slides = sorted(
            [n for n in names if n.startswith("ppt/slides/slide") and n.endswith(".xml")],
            key=lambda p: int(Path(p).stem.replace("slide", "")),
        )
        media = [n for n in names if n.startswith("ppt/media/")]
        rels = [n for n in names if n.startswith("ppt/slides/_rels/") and n.endswith(".rels")]
        zero_media = [n for n in media if zf.getinfo(n).file_size == 0]
        if len(slides) != 9:
            issues.append(f"expected 9 slides, found {len(slides)}")
        if zero_media:
            issues.append(f"zero-byte media: {zero_media}")
        placeholder_hits = []
        for slide in slides:
            txt = text_of_slide(zf, slide)
            if any(token in txt for token in ["Click to add", "TODO", "TBD", "Lorem ipsum"]):
                placeholder_hits.append(slide)
        if placeholder_hits:
            issues.append(f"placeholder-like text in {placeholder_hits}")
        # Basic relationship sanity: every slide relationship target under ../media should exist.
        broken_media_refs = []
        for rel_name in rels:
            root = ET.fromstring(zf.read(rel_name))
            for rel in root:
                target = rel.attrib.get("Target", "")
                if "../media/" in target:
                    media_name = "ppt/media/" + target.split("../media/", 1)[1]
                    if media_name not in names:
                        broken_media_refs.append((rel_name, media_name))
        if broken_media_refs:
            issues.append(f"broken media relationships: {broken_media_refs}")
        slide_summaries = []
        for slide in slides:
            txt = text_of_slide(zf, slide)
            slide_summaries.append(
                {
                    "slide": Path(slide).stem,
                    "chars": len(txt),
                    "first_text": txt.splitlines()[:3],
                }
            )
    payload = {
        "pptx": str(pptx),
        "size_bytes": pptx.stat().st_size,
        "slide_count": len(slides),
        "media_count": len(media),
        "relationship_count": len(rels),
        "issues": issues,
        "warnings": warnings,
        "slide_summaries": slide_summaries,
    }
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({k: payload[k] for k in ["slide_count", "media_count", "issues", "warnings"]}, ensure_ascii=False))
    return 1 if issues else 0


if __name__ == "__main__":
    raise SystemExit(main())
