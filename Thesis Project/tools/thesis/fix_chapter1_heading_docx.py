from __future__ import annotations

import shutil
import sys
from pathlib import Path

sys.path.insert(0, r"C:\Users\Tony\Desktop\eclipse-workspace-codex\.vendor")

from docx import Document
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml.ns import qn
from docx.shared import Pt


DOCX = Path(r"C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\paper\full_thesis_latest_merged.docx")
BACKUP = Path(r"C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\docs\thesis_drafting\full_thesis_latest_merged_before_chapter1_heading_fix_docx.docx")


def main() -> None:
    if not BACKUP.exists():
        BACKUP.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(DOCX, BACKUP)

    doc = Document(str(DOCX))
    fixed = 0
    for paragraph in doc.paragraphs:
        if paragraph.text.strip() == "第一章 绪论":
            paragraph.alignment = WD_ALIGN_PARAGRAPH.CENTER
            paragraph.paragraph_format.space_before = Pt(12)
            paragraph.paragraph_format.space_after = Pt(12)
            paragraph.paragraph_format.line_spacing = 1.5

            for run in paragraph.runs:
                run.font.name = "Times New Roman"
                run._element.rPr.rFonts.set(qn("w:eastAsia"), "黑体")
                run.font.size = Pt(18)
                run.font.bold = True
            fixed += 1

    if fixed != 1:
        raise RuntimeError(f"Expected to fix exactly one body heading, fixed {fixed}")

    doc.save(str(DOCX))
    print(f"Fixed chapter heading count: {fixed}")
    print(f"Backup: {BACKUP}")


if __name__ == "__main__":
    main()
