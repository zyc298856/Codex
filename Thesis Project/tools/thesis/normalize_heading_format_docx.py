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
BACKUP = Path(r"C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project\docs\thesis_drafting\full_thesis_latest_merged_before_heading_normalize.docx")


CHAPTERS = {
    "第一章 绪论",
    "第二章 相关技术基础",
    "第三章 系统总体设计",
    "第四章 系统实现与调试",
    "第五章 系统测试与实验分析",
    "第六章 总结与展望",
    "参考文献",
    "附  录",
    "附录A 英译汉",
    "附录B 课题调研报告",
    "致谢",
}


def set_run_font(run, size_pt: int, bold: bool = True) -> None:
    run.font.name = "Times New Roman"
    run._element.rPr.rFonts.set(qn("w:eastAsia"), "黑体")
    run.font.size = Pt(size_pt)
    run.font.bold = bold


def main() -> None:
    if not BACKUP.exists():
        BACKUP.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(DOCX, BACKUP)

    doc = Document(str(DOCX))
    changed = {"chapter": 0, "level2": 0, "level3": 0}

    for paragraph in doc.paragraphs:
        text = paragraph.text.strip()
        if not text:
            continue

        if text in CHAPTERS:
            paragraph.alignment = WD_ALIGN_PARAGRAPH.CENTER
            paragraph.paragraph_format.line_spacing = 1.5
            for run in paragraph.runs:
                set_run_font(run, 18, True)
            changed["chapter"] += 1
            continue

        if paragraph._p.pPr is None or paragraph._p.pPr.outlineLvl is None:
            continue
        outline = paragraph._p.pPr.outlineLvl.val

        if outline == 1:
            paragraph.alignment = WD_ALIGN_PARAGRAPH.LEFT
            paragraph.paragraph_format.line_spacing = 1.5
            for run in paragraph.runs:
                set_run_font(run, 14, True)
            changed["level2"] += 1
        elif outline == 2:
            paragraph.alignment = WD_ALIGN_PARAGRAPH.LEFT
            paragraph.paragraph_format.line_spacing = 1.5
            for run in paragraph.runs:
                set_run_font(run, 12, True)
            changed["level3"] += 1

    doc.save(str(DOCX))
    print(changed)


if __name__ == "__main__":
    main()
