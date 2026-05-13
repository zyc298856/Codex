from __future__ import annotations

from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


ROOT = Path(__file__).resolve().parents[2]
DOCX = ROOT / "docs" / "thesis_drafting" / "chapter_3_system_design_paper_style.docx"
FIGURES_DIR = ROOT / "docs" / "thesis_drafting" / "figures_paper_style"
REPLACEMENTS = {
    "word/media/image1.png": FIGURES_DIR / "fig_3_1_system_architecture_paper.png",
    "word/media/image2.png": FIGURES_DIR / "fig_3_2_model_migration_paper.png",
    "word/media/image3.png": FIGURES_DIR / "fig_3_3_realtime_pipeline_paper.png",
    "word/media/image4.png": FIGURES_DIR / "fig_3_4_multi_context_paper.png",
}


def main() -> None:
    tmp = DOCX.with_suffix(".docx.tmp")
    with ZipFile(DOCX, "r") as zin, ZipFile(tmp, "w", ZIP_DEFLATED) as zout:
        for item in zin.infolist():
            if item.filename in REPLACEMENTS:
                zout.writestr(item, REPLACEMENTS[item.filename].read_bytes())
            else:
                zout.writestr(item, zin.read(item.filename))
    tmp.replace(DOCX)
    print(DOCX)


if __name__ == "__main__":
    main()
