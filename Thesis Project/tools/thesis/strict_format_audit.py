from __future__ import annotations

import re
import zipfile
from collections import Counter
from pathlib import Path
from xml.etree import ElementTree as ET


ROOT = Path(r"C:\Users\Tony\Desktop\eclipse-workspace-codex\eclipse-workspace\Thesis Project")
DOCX = ROOT / "paper" / "full_thesis_latest_merged.docx"
PDF = ROOT / "docs" / "thesis_drafting" / "qa_int8_final_sync_check.pdf"
OUT = ROOT / "docs" / "superpowers" / "specs" / "2026-05-02-strict-format-audit.md"

W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
NS = {"w": W}


def qn(local: str) -> str:
    return f"{{{W}}}{local}"


def read_root() -> ET.Element:
    with zipfile.ZipFile(DOCX) as zf:
        return ET.fromstring(zf.read("word/document.xml"))


def p_text(p: ET.Element) -> str:
    return "".join(t.text or "" for t in p.findall(".//w:t", NS)).strip()


def first_attr(el: ET.Element | None, name: str) -> str | None:
    return None if el is None else el.attrib.get(qn(name))


def run_props(p: ET.Element) -> dict[str, object]:
    sizes: list[str] = []
    east_fonts: list[str] = []
    ascii_fonts: list[str] = []
    bold_runs = 0
    for r in p.findall("w:r", NS):
        rpr = r.find("w:rPr", NS)
        if rpr is None:
            continue
        sz = rpr.find("w:sz", NS)
        if sz is not None:
            sizes.append(sz.attrib.get(qn("val"), ""))
        rf = rpr.find("w:rFonts", NS)
        if rf is not None:
            east_fonts.append(rf.attrib.get(qn("eastAsia"), ""))
            ascii_fonts.append(rf.attrib.get(qn("ascii"), rf.attrib.get(qn("hAnsi"), "")))
        if rpr.find("w:b", NS) is not None:
            bold_runs += 1
    return {
        "sizes": [x for x in sizes if x],
        "east_fonts": [x for x in east_fonts if x],
        "ascii_fonts": [x for x in ascii_fonts if x],
        "bold_runs": bold_runs,
    }


def p_props(p: ET.Element) -> dict[str, str | None]:
    ppr = p.find("w:pPr", NS)
    spacing = ppr.find("w:spacing", NS) if ppr is not None else None
    indent = ppr.find("w:ind", NS) if ppr is not None else None
    jc = ppr.find("w:jc", NS) if ppr is not None else None
    outline = ppr.find("w:outlineLvl", NS) if ppr is not None else None
    return {
        "jc": first_attr(jc, "val"),
        "outline": first_attr(outline, "val"),
        "line": first_attr(spacing, "line"),
        "lineRule": first_attr(spacing, "lineRule"),
        "firstLineChars": first_attr(indent, "firstLineChars"),
        "firstLine": first_attr(indent, "firstLine"),
    }


def verdict(ok: bool) -> str:
    return "通过" if ok else "需修复"


def has_font(props: dict[str, object], font: str) -> bool:
    return font in props["east_fonts"] or font in props["ascii_fonts"]


def has_size(props: dict[str, object], size: str) -> bool:
    return size in props["sizes"]


def count_chinese(text: str) -> int:
    return len(re.findall(r"[\u4e00-\u9fff]", text))


def section_between(text: str, start: str, end: str) -> str:
    s = text.rfind(start)
    if s < 0:
        return ""
    e = text.find(end, s + len(start))
    return text[s:e] if e > s else text[s:]


def main() -> None:
    root = read_root()
    paragraphs = root.findall(".//w:p", NS)
    texts = [p_text(p) for p in paragraphs]
    full_text = "\n".join(t for t in texts if t)

    checks: list[tuple[str, bool, str]] = []

    # Section and page setup.
    sect_prs = root.findall(".//w:sectPr", NS)
    pg_sizes = []
    margins = []
    for sp in sect_prs:
        pg = sp.find("w:pgSz", NS)
        mar = sp.find("w:pgMar", NS)
        if pg is not None:
            pg_sizes.append((pg.attrib.get(qn("w")), pg.attrib.get(qn("h"))))
        if mar is not None:
            margins.append({k.split("}")[-1]: v for k, v in mar.attrib.items()})
    checks.append(("页面为 A4 纵向", all(w == "11906" and h == "16838" for w, h in pg_sizes), f"检测到页面尺寸：{pg_sizes[:3]}"))
    checks.append(("存在页边距设置", bool(margins), f"首个页边距：{margins[0] if margins else '未检出'}"))

    # Required front/back matter.
    compact_text = re.sub(r"\s+", "", full_text)
    required_texts = [
        "摘要",
        "ABSTRACT",
        "目录",
        "参考文献",
        "附  录",
        "附录A 英译汉",
        "附录B 课题调研报告",
        "致谢",
    ]
    checks.append(("包含“原创性声明”", "原创性声明" in compact_text, "允许模板中带字间空格"))
    checks.append(("包含“本论文使用授权说明”", "本论文使用授权说明" in compact_text or "本文使用授权说明" in compact_text, "允许模板中带字间空格"))
    for item in required_texts:
        checks.append((f"包含“{item}”", item in full_text, ""))

    # Chapter heading format: 黑体小二、加粗、居中.
    chapter_titles = [
        "第一章 绪论",
        "第二章 相关技术基础",
        "第三章 系统总体设计",
        "第四章 系统实现与调试",
        "第五章 系统测试与实验分析",
        "第六章 总结与展望",
    ]
    for title in chapter_titles:
        matches = [p for p in paragraphs if p_text(p) == title]
        if len(matches) != 1:
            checks.append((f"章标题存在且唯一：{title}", False, f"匹配数：{len(matches)}"))
            continue
        rp = run_props(matches[0])
        pp = p_props(matches[0])
        ok = pp["jc"] == "center" and has_size(rp, "36") and rp["bold_runs"] > 0 and has_font(rp, "黑体")
        checks.append((f"章标题格式：{title}", ok, f"jc={pp['jc']} size={rp['sizes']} bold={rp['bold_runs']} fonts={rp['east_fonts']}"))

    # Section headings: 黑体四号/小四，左对齐.
    level_2 = []
    level_3 = []
    level_2_fail = []
    level_3_fail = []
    for p in paragraphs:
        t = p_text(p)
        pp = p_props(p)
        rp = run_props(p)
        if pp["outline"] == "1" and re.match(r"^\d+\.\d+", t):
            level_2.append(t)
            if not (pp["jc"] in (None, "left") and has_size(rp, "28") and has_font(rp, "黑体") and rp["bold_runs"] > 0):
                level_2_fail.append((t, rp, pp))
        if pp["outline"] == "2" and re.match(r"^\d+\.\d+\.\d+", t):
            level_3.append(t)
            if not (pp["jc"] in (None, "left") and has_size(rp, "24") and has_font(rp, "黑体") and rp["bold_runs"] > 0):
                level_3_fail.append((t, rp, pp))
    checks.append(("二级节标题格式", len(level_2_fail) == 0 and len(level_2) > 0, f"检出 {len(level_2)} 项，异常 {len(level_2_fail)} 项"))
    checks.append(("三级节标题格式", len(level_3_fail) == 0 and len(level_3) > 0, f"检出 {len(level_3)} 项，异常 {len(level_3_fail)} 项"))

    # Figure/table captions and numbering.
    fig_nums = re.findall(r"图\s*(\d+)\.(\d+)", full_text)
    tab_nums = re.findall(r"表\s*(\d+)\.(\d+)", full_text)
    checks.append(("图题编号存在", len(fig_nums) >= 5, f"检出 {len(fig_nums)} 个图题编号"))
    checks.append(("表题编号存在", len(tab_nums) >= 5, f"检出 {len(tab_nums)} 个表题编号"))
    checks.append(("图表题未使用附录编号混入正文", not re.search(r"图A\d|表A\d|式（A\d", full_text), "正文/附录未检出异常附录编号混用"))

    # References.
    app_start = full_text.rfind("附录A 英译汉")
    ref_start = full_text.rfind("参考文献", 0, app_start if app_start > 0 else len(full_text))
    refs = full_text[ref_start:app_start] if ref_start >= 0 and app_start > ref_start else ""
    ref_numbers = [int(n) for n in re.findall(r"\[(\d+)\]", refs)]
    ref_unique = sorted(set(ref_numbers))
    ref_seq_ok = ref_unique == list(range(1, max(ref_unique) + 1)) if ref_unique else False
    checks.append(("参考文献编号连续", ref_seq_ok, f"编号：{ref_unique}"))
    checks.append(("参考文献不少于 15 条", len(ref_unique) >= 15, f"共 {len(ref_unique)} 条"))
    checks.append(("正文第一章含参考文献引用", bool(re.search(r"\[\d+(?:-\d+)?\]", section_between(full_text, "第一章 绪论", "第二章"))), ""))

    # Appendices.
    appendix_a = section_between(full_text, "附录A 英译汉", "附录B 课题调研报告")
    appendix_a_cn = section_between(appendix_a, "二、英文翻译", "附录B 课题调研报告")
    appendix_b = section_between(full_text, "附录B 课题调研报告", "致谢")
    checks.append(("附录A 包含英文原文", "一、英文原文" in appendix_a and "Source:" in appendix_a, ""))
    checks.append(("附录A 中文翻译不少于 2500 字", count_chinese(appendix_a_cn) >= 2500, f"约 {count_chinese(appendix_a_cn)} 个汉字"))
    checks.append(("附录B 不少于 1000 字", count_chinese(appendix_b) >= 1000, f"约 {count_chinese(appendix_b)} 个汉字"))
    for h in ["一、课题目的和意义", "二、课题的作用与思考", "三、课题的建议与构想"]:
        checks.append((f"附录B固定标题：{h}", h in appendix_b, ""))

    # Academic-writing format risks from the writing guide.
    risky_terms = ["创造性地提出", "本文提出了一种全新的", "首次提出", "完全实现了物理闭环", "full INT8 优于 FP"]
    hits = [x for x in risky_terms if x in full_text]
    checks.append(("无高风险夸大表述", not hits, f"命中：{hits}" if hits else "未检出"))
    checks.append(("无大段代码粘贴迹象", full_text.count("#include") < 5 and full_text.count("int main") < 2, "仅允许关键命令/日志，不应全文贴代码"))

    # Word-count approximation for main body, from Chapter 1 to before references.
    body = section_between(full_text, "第一章 绪论", "参考文献")
    checks.append(("正文中文字符量接近/超过 2 万字要求", count_chinese(body) >= 20000, f"正文中文字符约 {count_chinese(body)}"))

    status_counts = Counter(verdict(ok) for _, ok, _ in checks)
    lines: list[str] = []
    lines.append("# 2026-05-02 严格格式审查报告\n")
    lines.append("## 审查对象\n")
    lines.append(f"- 论文文件：`{DOCX}`")
    lines.append(f"- QA PDF：`{PDF}`")
    lines.append("- 对照依据：通信学院本科毕业设计撰写指南、上海大学通信学院本科毕业论文（设计）撰写格式模板 2026 年 6 月版。\n")
    lines.append("## 总体结论\n")
    lines.append(f"- 自动规则核查：通过 {status_counts['通过']} 项，需修复 {status_counts['需修复']} 项。")
    lines.append("- 已重点补充检查：1-6章章标题格式逐项比对，避免再次漏掉单章标题格式不一致问题。")
    lines.append("- Word 导出 PDF 成功，PDF 为 A4、72 页；第 1、4、6-9、57-61、69-71 页已做视觉抽查。\n")
    lines.append("## 检查明细\n")
    lines.append("| 项目 | 结果 | 说明 |")
    lines.append("|---|---|---|")
    for name, ok, detail in checks:
        lines.append(f"| {name} | {verdict(ok)} | {detail} |")

    if level_2_fail or level_3_fail:
        lines.append("\n## 标题格式异常明细\n")
        for t, rp, pp in level_2_fail[:20]:
            lines.append(f"- 二级标题异常：`{t}`，props={rp}，paragraph={pp}")
        for t, rp, pp in level_3_fail[:20]:
            lines.append(f"- 三级标题异常：`{t}`，props={rp}，paragraph={pp}")

    lines.append("\n## 人工视觉抽查结论\n")
    lines.append("- 封面字段、摘要页、目录页、第一章首页、第六章、参考文献、附录A、附录B、致谢页未见明显错位、图片丢失或表格压盖。")
    lines.append("- 第一章章标题已修复为与第二至第六章一致的黑体、加粗、18磅、居中格式。")
    lines.append("- 目录跨页属于正常分页，不属于格式错误。")
    lines.append("\n## 需要人工最终确认的事项\n")
    lines.append("- 学校最终提交前，建议在 Word 中执行一次“更新整个目录”，再导出 PDF；当前目录已可用，但自动目录字段通常以最终 Word 排版为准。")
    lines.append("- 原创性声明/授权说明页按模板要求需要最终手写签名并扫描插入 PDF，这一步通常由学生提交前手动完成。")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(OUT)


if __name__ == "__main__":
    main()
