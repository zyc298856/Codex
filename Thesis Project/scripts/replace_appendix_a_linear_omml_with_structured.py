from __future__ import annotations

import html
import re
import shutil
import tempfile
import zipfile
from pathlib import Path


ROOT = Path(r"C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project")
DOCX = ROOT / "paper" / "full_thesis_latest_merged.docx"
FRAG_DIR = ROOT / "docs" / "thesis_drafting" / "appendix_a_omml_fragments"


def load_fragment(name: str) -> str:
    return (FRAG_DIR / f"{name}.xml").read_text(encoding="utf-8-sig").strip()


FRAGMENTS = {
    "ciou1": load_fragment("ciou1"),
    "ciou2": load_fragment("ciou2"),
    "ciou3": load_fragment("ciou3"),
    "eiou": load_fragment("eiou"),
    "aiou": load_fragment("aiou"),
    "exiou": load_fragment("exiou"),
}


OMATH_RE = re.compile(r"<m:oMathPara\b[^>]*>.*?</m:oMathPara>", re.S)
TAG_RE = re.compile(r"<[^>]+>")


def omath_visible_text(fragment: str) -> str:
    text = TAG_RE.sub("", fragment)
    return html.unescape(text).replace("\xa0", " ")


def replacement_for(fragment: str) -> str:
    text = omath_visible_text(fragment)
    if "L_(CIoU)" in text:
        return FRAGMENTS["ciou1"]
    if text.startswith("α=") or text.startswith("α ="):
        return FRAGMENTS["ciou2"]
    if text.startswith("v=4/") or text.startswith("v = 4/"):
        return FRAGMENTS["ciou3"]
    if "L_(EIoU)" in text:
        return FRAGMENTS["eiou"]
    if "L_(α-IoU)" in text:
        return FRAGMENTS["aiou"]
    if "L_(EX-IoU)" in text or "LEX-IoU" in text:
        return FRAGMENTS["exiou"]
    return fragment


def main() -> None:
    with zipfile.ZipFile(DOCX, "r") as zin:
        original_xml = zin.read("word/document.xml").decode("utf-8")

    changed = 0

    def repl(match: re.Match[str]) -> str:
        nonlocal changed
        old = match.group(0)
        new = replacement_for(old)
        if new != old:
            changed += 1
        return new

    new_xml = OMATH_RE.sub(repl, original_xml)
    if changed != 12:
        raise RuntimeError(f"Expected to replace 12 appendix A equation lines, replaced {changed}.")

    tmp = Path(tempfile.mkstemp(suffix=".docx")[1])
    try:
        with zipfile.ZipFile(DOCX, "r") as zin, zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as zout:
            for item in zin.infolist():
                data = zin.read(item.filename)
                if item.filename == "word/document.xml":
                    data = new_xml.encode("utf-8")
                zout.writestr(item, data)
        shutil.copy2(tmp, DOCX)
    finally:
        try:
            tmp.unlink(missing_ok=True)
        except PermissionError:
            # Windows may keep the temporary zip handle alive briefly.
            pass

    print(f"Replaced {changed} appendix A equation objects with structured OMML equations.")


if __name__ == "__main__":
    main()
