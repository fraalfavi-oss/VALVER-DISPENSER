"""Generate the Spanish project handoff PDF using only the Python standard library."""

from __future__ import annotations

import re
import textwrap
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "docs" / "PROJECT_HANDOFF_AI.md"
OUTPUT = ROOT / "docs" / "PROJECT_HANDOFF_AI.pdf"
PAGE_W, PAGE_H = 595.28, 841.89
LEFT, RIGHT, TOP, BOTTOM = 52.0, 52.0, 54.0, 52.0
CONTENT_W = PAGE_W - LEFT - RIGHT

NAVY = (0.09, 0.20, 0.30)
TEAL = (0.08, 0.54, 0.54)
TEXT = (0.15, 0.19, 0.23)
MUTED = (0.39, 0.45, 0.50)
PALE = (0.92, 0.95, 0.97)
WHITE = (1.0, 1.0, 1.0)


def pdf_string(value: str) -> str:
    """Encode a string as a PDF literal with WinAnsi-compatible octal escapes."""
    encoded = value.encode("cp1252", errors="replace")
    out = []
    for byte in encoded:
        if byte in (ord("("), ord(")"), ord("\\")):
            out.append("\\" + chr(byte))
        elif 32 <= byte <= 126:
            out.append(chr(byte))
        else:
            out.append(f"\\{byte:03o}")
    return "(" + "".join(out) + ")"


def color(rgb: tuple[float, float, float], operator: str) -> str:
    return f"{rgb[0]:.3f} {rgb[1]:.3f} {rgb[2]:.3f} {operator}"


def clean_inline(text: str) -> str:
    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r"\1 [\2]", text)
    text = text.replace("**", "").replace("*", "").replace("`", "")
    return text.replace("&", "&")


class PdfLayout:
    def __init__(self) -> None:
        self.pages: list[list[str]] = [[]]
        self.y = PAGE_H - TOP

    @property
    def stream(self) -> list[str]:
        return self.pages[-1]

    def new_page(self) -> None:
        self.pages.append([])
        self.y = PAGE_H - TOP

    def ensure(self, height: float) -> None:
        if self.y - height < BOTTOM + 15:
            self.new_page()

    def text(self, value: str, *, size: float = 9.2, font: str = "F1",
             x: float = LEFT, fill=TEXT, leading: float | None = None) -> None:
        leading = leading or size * 1.38
        self.ensure(leading)
        self.stream.extend([
            color(fill, "rg"), "BT", f"/{font} {size:.2f} Tf",
            f"1 0 0 1 {x:.2f} {self.y:.2f} Tm", f"{pdf_string(value)} Tj", "ET",
        ])
        self.y -= leading

    def paragraph(self, text: str, *, size: float = 9.2, font: str = "F1",
                  fill=TEXT, indent: float = 0, chars: int = 102,
                  leading: float | None = None) -> None:
        text = clean_inline(text)
        lines = textwrap.wrap(text, width=max(30, chars - int(indent / 4.5)),
                              break_long_words=True, break_on_hyphens=True) or [""]
        line_height = leading or size * 1.42
        self.ensure(len(lines) * line_height + 2)
        for line in lines:
            self.text(line, size=size, font=font, x=LEFT + indent, fill=fill,
                      leading=line_height)
        self.y -= 2

    def heading(self, text: str, level: int) -> None:
        size = {1: 17.0, 2: 12.5, 3: 10.4}[level]
        leading = {1: 22.0, 2: 17.0, 3: 14.0}[level]
        self.ensure(leading + 10)
        self.y -= 5
        if level == 1:
            self.stream.extend([
                color(PALE, "rg"), f"{LEFT - 5:.2f} {self.y - 5:.2f} {CONTENT_W + 10:.2f} {leading + 3:.2f} re f",
            ])
        self.text(clean_inline(text), size=size, font="F2", fill=NAVY if level != 2 else TEAL,
                  leading=leading)
        if level == 1:
            self.y -= 4

    def bullet(self, text: str, number: str | None = None, depth: int = 0) -> None:
        marker = f"{number}." if number else "•"
        prefix = f"{marker}  "
        available = max(34, 96 - depth * 3)
        lines = textwrap.wrap(clean_inline(text), width=available,
                              break_long_words=True, break_on_hyphens=True) or [""]
        self.ensure(len(lines) * 12.5 + 2)
        x = LEFT + 10 + depth * 12
        self.text(prefix + lines[0], size=9.0, x=x, leading=12.5)
        for line in lines[1:]:
            self.text("   " + line, size=9.0, x=x + 8, leading=12.5)
        self.y -= 1

    def code_block(self, lines: list[str]) -> None:
        wrapped: list[str] = []
        for line in lines:
            wrapped.extend(textwrap.wrap(line, width=88, replace_whitespace=False,
                                         break_long_words=True, break_on_hyphens=False) or [""])
        line_height = 10.6
        for offset in range(0, len(wrapped), 45):
            chunk = wrapped[offset:offset + 45]
            height = len(chunk) * line_height + 12
            self.ensure(height)
            self.stream.extend([
                color(PALE, "rg"), f"{LEFT - 3:.2f} {self.y - height + 5:.2f} {CONTENT_W + 6:.2f} {height:.2f} re f",
            ])
            self.y -= 9
            for line in chunk:
                self.text(line, size=7.6, font="F3", x=LEFT + 4, fill=TEXT, leading=line_height)
            self.y -= 5


def parse_markdown(text: str, layout: PdfLayout) -> None:
    lines = text.splitlines()
    paragraph: list[str] = []
    code: list[str] = []
    in_code = False

    def flush_paragraph() -> None:
        if paragraph:
            layout.paragraph(" ".join(part.strip() for part in paragraph))
            paragraph.clear()

    def flush_code() -> None:
        if code:
            layout.code_block(code)
            code.clear()

    for raw in lines:
        line = raw.rstrip()
        if line.startswith("```"):
            flush_paragraph()
            if in_code:
                flush_code()
            in_code = not in_code
            continue
        if in_code:
            code.append(line)
            continue
        if not line.strip():
            flush_paragraph()
            continue
        if line.strip() == "---":
            flush_paragraph()
            layout.y -= 5
            layout.ensure(5)
            layout.stream.extend(["0.82 0.87 0.90 RG", f"{LEFT} {layout.y:.2f} m {PAGE_W - RIGHT} {layout.y:.2f} l S"])
            layout.y -= 7
            continue
        if line.startswith("# "):
            flush_paragraph()
            layout.heading(line[2:], 1)
            continue
        if line.startswith("## "):
            flush_paragraph()
            layout.heading(line[3:], 1)
            continue
        if line.startswith("### "):
            flush_paragraph()
            layout.heading(line[4:], 2)
            continue
        if line.startswith("> "):
            flush_paragraph()
            layout.paragraph(line[2:], indent=11, chars=98, fill=NAVY, size=9.0)
            continue
        bullet = re.match(r"^(\s*)-\s+(.*)$", line)
        if bullet:
            flush_paragraph()
            layout.bullet(bullet.group(2), depth=min(len(bullet.group(1)) // 2, 3))
            continue
        numbered = re.match(r"^\s*(\d+)\.\s+(.*)$", line)
        if numbered:
            flush_paragraph()
            layout.bullet(numbered.group(2), number=numbered.group(1))
            continue
        paragraph.append(line)
    flush_paragraph()
    if in_code:
        flush_code()


def make_page_stream(content: list[str], page_num: int, total_pages: int) -> bytes:
    commands = ["q"]
    if page_num == 1:
        # Cover uses a dark banner and a contrasting accent line.
        commands.extend([
            color(NAVY, "rg"), f"0 {PAGE_H - 230:.2f} {PAGE_W:.2f} 230 re f",
            color(TEAL, "rg"), f"0 {PAGE_H - 236:.2f} {PAGE_W:.2f} 6 re f",
            color(WHITE, "rg"), "BT", "/F2 27 Tf", f"1 0 0 1 52 {PAGE_H - 104:.2f} Tm",
            f"{pdf_string('VALVER SMART DISPENSER')} Tj", "ET",
            color(WHITE, "rg"), "BT", "/F1 14 Tf", f"1 0 0 1 52 {PAGE_H - 137:.2f} Tm",
            f"{pdf_string('Documento de traspaso técnico para continuar con otra IA')} Tj", "ET",
        ])
    else:
        commands.extend([
            "0.82 0.87 0.90 RG", f"{LEFT} {PAGE_H - 35} m {PAGE_W - RIGHT} {PAGE_H - 35} l S",
            color(MUTED, "rg"), "BT", "/F1 8 Tf", f"1 0 0 1 {LEFT} {PAGE_H - 27} Tm",
            f"{pdf_string('VALVER SMART DISPENSER · TRASPASO TÉCNICO')} Tj", "ET",
        ])
    commands.extend(content)
    commands.extend([
        "0.82 0.87 0.90 RG", f"{LEFT} 34 m {PAGE_W - RIGHT} 34 l S",
        color(MUTED, "rg"), "BT", "/F1 8 Tf", f"1 0 0 1 {LEFT} 21 Tm",
        f"{pdf_string('Documento de trabajo · verificar hardware antes de cablear')} Tj",
        f"1 0 0 1 {PAGE_W - RIGHT - 55} 21 Tm", f"{pdf_string(f'Página {page_num} de {total_pages}')} Tj", "ET",
        "Q",
    ])
    return "\n".join(commands).encode("cp1252", errors="replace")


def build_pdf(streams: list[bytes]) -> bytes:
    # Objects 1-5 are the catalog, page tree and standard WinAnsi fonts.
    objects: list[bytes] = [b""] * 5
    page_ids = [6 + index * 2 for index in range(len(streams))]
    objects[0] = b"<< /Type /Catalog /Pages 2 0 R >>"
    kids = " ".join(f"{page_id} 0 R" for page_id in page_ids)
    objects[1] = f"<< /Type /Pages /Kids [{kids}] /Count {len(streams)} >>".encode("ascii")
    objects[2] = b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"
    objects[3] = b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>"
    objects[4] = b"<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding >>"

    for index, stream in enumerate(streams):
        page_id = page_ids[index]
        content_id = page_id + 1
        page = (
            f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {PAGE_W:.2f} {PAGE_H:.2f}] "
            f"/Resources << /Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R >> >> "
            f"/Contents {content_id} 0 R >>"
        ).encode("ascii")
        content = f"<< /Length {len(stream)} >>\nstream\n".encode("ascii") + stream + b"\nendstream"
        objects.extend([page, content])

    result = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offsets = [0]
    for index, obj in enumerate(objects, start=1):
        offsets.append(len(result))
        result.extend(f"{index} 0 obj\n".encode("ascii"))
        result.extend(obj)
        result.extend(b"\nendobj\n")
    xref_offset = len(result)
    result.extend(f"xref\n0 {len(objects) + 1}\n".encode("ascii"))
    result.extend(b"0000000000 65535 f \n")
    for offset in offsets[1:]:
        result.extend(f"{offset:010d} 00000 n \n".encode("ascii"))
    result.extend(
        f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\n"
        f"startxref\n{xref_offset}\n%%EOF\n".encode("ascii")
    )
    return bytes(result)


def main() -> None:
    if not SOURCE.exists():
        raise FileNotFoundError(f"No se encontró el documento fuente: {SOURCE}")
    source = SOURCE.read_text(encoding="utf-8")
    first_title = next((line[2:] for line in source.splitlines() if line.startswith("# ")), "VALVER SMART DISPENSER")
    body = []
    skipped_title = False
    skip_cover_metadata = True
    for line in source.splitlines():
        if line.startswith("# ") and not skipped_title:
            skipped_title = True
            continue
        if skip_cover_metadata:
            if line.startswith("> "):
                skip_cover_metadata = False
            continue
        body.append(line)

    layout = PdfLayout()
    # Cover title and summary.
    layout.y = PAGE_H - 295
    layout.text("ESTADO DEL PROYECTO", size=10, font="F2", x=LEFT, fill=TEAL, leading=16)
    layout.paragraph("Snapshot del repositorio · 30 de octubre de 2026", size=10.2,
                     font="F2", fill=NAVY, chars=95, leading=15)
    layout.paragraph("Estado: prototipo temprano · firmware en modo seguro", size=9.5, chars=100)
    layout.paragraph("Objetivo configurado: ESP32-S3 DevKitC-1 · movimiento y salidas inhibidos",
                     size=9.5, chars=100)
    layout.y -= 16
    layout.paragraph(
        "Incluye inventario técnico, estado real del código, comandos de compilación, "
        "advertencias de hardware y una guía de continuidad para otra IA.",
        size=10, fill=MUTED, chars=95, leading=15,
    )
    layout.new_page()
    parse_markdown("\n".join(body), layout)

    streams = [make_page_stream(page, number, len(layout.pages))
               for number, page in enumerate(layout.pages, start=1)]
    pdf_bytes = build_pdf(streams)
    OUTPUT.write_bytes(pdf_bytes)
    if not pdf_bytes.startswith(b"%PDF-") or len(pdf_bytes) < 5000:
        raise RuntimeError("La salida PDF no pasó la validación básica de firma/tamaño.")
    print(f"PDF generado: {OUTPUT}")
    print(f"Páginas: {len(streams)}")
    print(f"Tamaño: {len(pdf_bytes):,} bytes")
    print(f"Título de portada: {first_title}")


if __name__ == "__main__":
    main()
