#!/usr/bin/env python3
# Convert the project .md docs to phone-friendly HTML + A4 PDF (so they load anywhere).
import markdown, pathlib
from playwright.sync_api import sync_playwright

DOCS = [
    ("TECH-SPEC.md",    "6GGW / NetSwitch — Technical Specification"),
    ("AI-AND-MATHS.md", "6GGW / NetSwitch — AI & Implemented Maths"),
]

CSS = """
<style>
  @page { size: A4; margin: 15mm 14mm; }
  * { box-sizing: border-box; }
  body { font-family: -apple-system, Segoe UI, Roboto, Helvetica, Arial, sans-serif;
         color:#1f2733; line-height:1.5; font-size:12.5px; margin:0; }
  h1 { color:#2f6ea5; font-size:22px; border-bottom:3px solid #3d88c4; padding-bottom:6px; margin:0 0 14px; }
  h2 { color:#2f6ea5; font-size:16px; margin:22px 0 8px; border-left:4px solid #3d88c4; padding-left:8px; }
  h3 { color:#3d5063; font-size:13.5px; margin:16px 0 6px; }
  p, li { font-size:12.5px; }
  code { background:#eef3f8; color:#20456b; padding:1px 5px; border-radius:4px;
         font-family: SFMono-Regular, Consolas, monospace; font-size:11.5px; }
  pre { background:#f4f8fb; border:1px solid #d7e3ef; border-radius:6px; padding:10px 12px;
        overflow-x:auto; font-size:11px; line-height:1.45; }
  pre code { background:none; padding:0; color:#20456b; }
  table { border-collapse:collapse; width:100%; margin:10px 0 16px; font-size:11px; }
  th { background:#3d88c4; color:#fff; text-align:left; padding:7px 9px; font-weight:600; vertical-align:top; }
  td { padding:6px 9px; border-bottom:1px solid #e3ecf4; vertical-align:top; }
  tr:nth-child(even) td { background:#f4f8fb; }
  strong { color:#20456b; }
  hr { border:none; border-top:1px solid #dbe5ef; margin:20px 0; }
  a { color:#3d88c4; text-decoration:none; }
  .footer { margin-top:24px; padding-top:10px; border-top:1px solid #dbe5ef;
            color:#7a8aa0; font-size:10px; }
</style>
"""

base = pathlib.Path(".")
outputs = []
with sync_playwright() as p:
    browser = p.chromium.launch()
    page = browser.new_page(viewport={"width": 1000, "height": 1400})
    for md_name, title in DOCS:
        md_text = (base / md_name).read_text(encoding="utf-8")
        body = markdown.markdown(md_text, extensions=["tables", "fenced_code", "toc", "sane_lists"])
        html = f"<!doctype html><meta charset='utf-8'><title>{title}</title>{CSS}<body>{body}" \
               f"<div class='footer'>6GGW / NetSwitch — {title}. Anirudha Talmale.</div></body>"
        html_path = base / (md_name[:-3] + ".html")
        html_path.write_text(html, encoding="utf-8")
        pdf_path = base / (md_name[:-3] + ".pdf")
        page.set_content(html, wait_until="load")
        page.pdf(path=str(pdf_path), format="A4", print_background=True,
                 margin={"top": "0", "bottom": "0", "left": "0", "right": "0"})
        outputs.append((html_path.name, pdf_path.name))
        print("rendered", md_name, "->", html_path.name, pdf_path.name)
    browser.close()
print("DONE", outputs)
