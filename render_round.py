#!/usr/bin/env python3
# Render this round's deliverables: PRODUCT-PLAN.md -> PDF, architecture.svg -> PNG.
import markdown, pathlib
from playwright.sync_api import sync_playwright

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
  table { border-collapse:collapse; width:100%; margin:10px 0 16px; font-size:11px; }
  th { background:#3d88c4; color:#fff; text-align:left; padding:7px 9px; font-weight:600; vertical-align:top; }
  td { padding:6px 9px; border-bottom:1px solid #e3ecf4; vertical-align:top; }
  tr:nth-child(even) td { background:#f4f8fb; }
  strong { color:#20456b; }
  hr { border:none; border-top:1px solid #dbe5ef; margin:20px 0; }
  .footer { margin-top:24px; padding-top:10px; border-top:1px solid #dbe5ef; color:#7a8aa0; font-size:10px; }
</style>
"""

base = pathlib.Path(".")
with sync_playwright() as p:
    browser = p.chromium.launch()

    # 1) PRODUCT-PLAN.md -> PDF
    page = browser.new_page(viewport={"width": 1000, "height": 1400})
    md_text = (base / "PRODUCT-PLAN.md").read_text(encoding="utf-8")
    body = markdown.markdown(md_text, extensions=["tables", "fenced_code", "toc", "sane_lists"])
    title = "6GGW / NetSwitch — Product Set & Aug-Sep-Oct Plan"
    html = f"<!doctype html><meta charset='utf-8'><title>{title}</title>{CSS}<body>{body}" \
           f"<div class='footer'>{title}. Anirudha Talmale.</div></body>"
    (base / "PRODUCT-PLAN.html").write_text(html, encoding="utf-8")
    page.set_content(html, wait_until="load")
    page.pdf(path="PRODUCT-PLAN.pdf", format="A4", print_background=True,
             margin={"top": "0", "bottom": "0", "left": "0", "right": "0"})
    print("rendered PRODUCT-PLAN.pdf")

    # 2) architecture.svg -> PNG (viewport-bounded, under 2000px both dims)
    svg = (base / "docs" / "architecture.svg").read_text(encoding="utf-8")
    page2 = browser.new_page(viewport={"width": 1480, "height": 1020})
    page2.set_content(f"<!doctype html><meta charset='utf-8'>"
                      f"<body style='margin:0'>{svg}</body>", wait_until="load")
    page2.locator("svg").screenshot(path="docs/architecture.png")
    print("rendered docs/architecture.png")

    browser.close()
print("DONE")
