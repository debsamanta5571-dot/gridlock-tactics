#!/usr/bin/env python3
"""Render docs/HOW_TO_PLAY.md as a print rulebook PDF."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_JUSTIFY, TA_LEFT
from reportlab.lib.pagesizes import LETTER
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import inch
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.pdfmetrics import registerFontFamily
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    CondPageBreak,
    KeepTogether,
    ListFlowable,
    ListItem,
    PageBreak,
    Paragraph,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
    HRFlowable,
)

INK = colors.HexColor("#2C241C")
RUST = colors.HexColor("#8F3D2A")
RUST_DARK = colors.HexColor("#6E2E20")
RULE = colors.HexColor("#C4A574")
CREAM = colors.HexColor("#F6F1E8")
MUTED = colors.HexColor("#6A5E52")
PAPER = colors.HexColor("#EDE6D6")


def register_fonts() -> tuple[str, str, str]:
    fonts = Path(r"C:\Windows\Fonts")
    families = [
        ("Georgia", "georgia.ttf", "georgiab.ttf", "georgiai.ttf", "georgiaz.ttf"),
        ("Palatino", "pala.ttf", "palab.ttf", "palai.ttf", "palabi.ttf"),
        ("Calibri", "calibri.ttf", "calibrib.ttf", "calibrii.ttf", "calibriz.ttf"),
    ]
    for family, r, b, it, bi in families:
        paths = [fonts / n for n in (r, b, it, bi)]
        if not all(p.exists() for p in paths):
            continue
        names = ("PlayR", "PlayB", "PlayI", "PlayBI")
        pdfmetrics.registerFont(TTFont(names[0], str(paths[0])))
        pdfmetrics.registerFont(TTFont(names[1], str(paths[1])))
        pdfmetrics.registerFont(TTFont(names[2], str(paths[2])))
        pdfmetrics.registerFont(TTFont(names[3], str(paths[3])))
        registerFontFamily("PlayR", normal="PlayR", bold="PlayB", italic="PlayI", boldItalic="PlayBI")
        return names[0], names[1], names[2]
    return "Times-Roman", "Times-Bold", "Times-Italic"


def styles(body: str, bold: str, italic: str) -> dict[str, ParagraphStyle]:
    def ps(name: str, **kw) -> ParagraphStyle:
        base = dict(
            fontName=body,
            fontSize=11,
            leading=16.5,
            textColor=INK,
            alignment=TA_JUSTIFY,
            spaceBefore=0,
            spaceAfter=8,
        )
        base.update(kw)
        return ParagraphStyle(name, **base)

    return {
        "cover_kicker": ps(
            "cover_kicker",
            fontName=bold,
            fontSize=10,
            leading=13,
            textColor=RUST,
            alignment=TA_CENTER,
            spaceAfter=14,
            tracking=1.4,
        ),
        "cover_title": ps(
            "cover_title",
            fontName=bold,
            fontSize=28,
            leading=34,
            textColor=RUST_DARK,
            alignment=TA_CENTER,
            spaceAfter=10,
        ),
        "cover_sub": ps(
            "cover_sub",
            fontName=italic,
            fontSize=13,
            leading=18,
            textColor=MUTED,
            alignment=TA_CENTER,
            spaceAfter=0,
        ),
        "h1": ps(
            "h1",
            fontName=bold,
            fontSize=16,
            leading=21,
            textColor=RUST_DARK,
            alignment=TA_LEFT,
            spaceBefore=16,
            spaceAfter=8,
        ),
        "body": ps("body"),
        "li": ps("li", leftIndent=16, firstLineIndent=0, spaceAfter=4, alignment=TA_LEFT, leading=15.5),
        "footer": ps(
            "footer",
            fontName=italic,
            fontSize=8,
            leading=10,
            textColor=MUTED,
            alignment=TA_CENTER,
        ),
    }


def esc(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def inline(text: str, italic: str, bold: str) -> str:
    text = esc(text)
    text = re.sub(r"\*\*(.+?)\*\*", rf'<font name="{bold}">\1</font>', text)
    text = re.sub(r"\*(.+?)\*", rf'<font name="{italic}">\1</font>', text)
    return text


def on_page(canvas, doc) -> None:
    canvas.saveState()
    canvas.setFillColor(CREAM)
    canvas.rect(0, 0, LETTER[0], LETTER[1], fill=1, stroke=0)
    canvas.setStrokeColor(RULE)
    canvas.setLineWidth(0.6)
    canvas.line(0.85 * inch, LETTER[1] - 0.55 * inch, LETTER[0] - 0.85 * inch, LETTER[1] - 0.55 * inch)
    canvas.line(0.85 * inch, 0.55 * inch, LETTER[0] - 0.85 * inch, 0.55 * inch)
    canvas.setFillColor(MUTED)
    canvas.setFont("PlayI" if "PlayI" in pdfmetrics.getRegisteredFontNames() else "Times-Italic", 8)
    canvas.drawCentredString(LETTER[0] / 2, 0.36 * inch, f"Gridlock Tactics  ·  Rulebook  ·  {doc.page}")
    canvas.restoreState()


def on_cover(canvas, doc) -> None:
    canvas.saveState()
    canvas.setFillColor(CREAM)
    canvas.rect(0, 0, LETTER[0], LETTER[1], fill=1, stroke=0)
    canvas.setFillColor(PAPER)
    canvas.rect(0.7 * inch, 0.7 * inch, LETTER[0] - 1.4 * inch, LETTER[1] - 1.4 * inch, fill=1, stroke=0)
    canvas.setStrokeColor(RUST)
    canvas.setLineWidth(1.2)
    canvas.rect(0.78 * inch, 0.78 * inch, LETTER[0] - 1.56 * inch, LETTER[1] - 1.56 * inch, fill=0, stroke=1)
    canvas.restoreState()


def parse_md(md: str, st: dict, italic: str, bold: str) -> list:
    story: list = []
    lines = md.splitlines()
    i = 0
    # skip title/subtitle; cover uses them
    while i < len(lines) and not lines[i].startswith("## "):
        i += 1

    bullets: list[str] = []
    seen_heading = False

    def flush_bullets() -> None:
        nonlocal bullets
        if not bullets:
            return
        items = [
            ListItem(Paragraph(inline(b, italic, bold), st["li"]), leftIndent=8, value="•")
            for b in bullets
        ]
        story.append(
            ListFlowable(
                items,
                bulletType="bullet",
                start="•",
                leftIndent=12,
                bulletFontName=bold,
                bulletFontSize=10,
                spaceBefore=2,
                spaceAfter=10,
            )
        )
        bullets = []

    while i < len(lines):
        raw = lines[i].rstrip()
        stripped = raw.strip()
        if not stripped:
            flush_bullets()
            i += 1
            continue
        if stripped.startswith("## "):
            flush_bullets()
            title = stripped[3:].strip()
            block = [Spacer(1, 4)]
            if seen_heading:
                block.append(HRFlowable(width="100%", thickness=0.4, color=RULE, spaceBefore=2, spaceAfter=8))
            else:
                block.append(Spacer(1, 8))
            seen_heading = True
            block.append(Paragraph(inline(title, italic, bold), st["h1"]))
            story.append(KeepTogether(block))
            i += 1
            continue
        if stripped.startswith("- "):
            bullets.append(stripped[2:].strip())
            i += 1
            continue
        if re.match(r"^\d+\.\s", stripped):
            flush_bullets()
            story.append(Paragraph(inline(stripped, italic, bold), st["li"]))
            i += 1
            continue
        flush_bullets()
        story.append(Paragraph(inline(stripped, italic, bold), st["body"]))
        i += 1
    flush_bullets()
    return story


def build(md_path: Path, pdf_path: Path) -> None:
    body, bold, italic = register_fonts()
    st = styles(body, bold, italic)
    md = md_path.read_text(encoding="utf-8")

    doc = SimpleDocTemplate(
        str(pdf_path),
        pagesize=LETTER,
        leftMargin=0.95 * inch,
        rightMargin=0.95 * inch,
        topMargin=0.8 * inch,
        bottomMargin=0.75 * inch,
        title="Gridlock Tactics Rulebook",
        author="Gridlock Tactics",
    )

    cover = [
        Spacer(1, 2.35 * inch),
        Paragraph("GRIDLOCK TACTICS", st["cover_kicker"]),
        Paragraph("Rulebook", st["cover_title"]),
        HRFlowable(width="42%", thickness=0.8, color=RUST, spaceBefore=6, spaceAfter=14),
        PageBreak(),
    ]
    story = cover + parse_md(md, st, italic, bold)
    doc.build(story, onFirstPage=on_cover, onLaterPages=on_page)


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    md = repo / "docs" / "HOW_TO_PLAY.md"
    pdf = repo / "docs" / "HOW_TO_PLAY.pdf"
    if len(sys.argv) >= 2:
        md = Path(sys.argv[1])
    if len(sys.argv) >= 3:
        pdf = Path(sys.argv[2])
    build(md, pdf)
    print(f"Wrote {pdf}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
