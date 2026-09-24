"""Writes a preview page for a set of icons: each at 16, 20, 32 and 64 px on dark and light."""

import html


def write_gallery(path, sections, title="UniVex editor icons"):
    rows = []
    for section, icons in sections:
        cells = []
        for name, svg in icons:
            sizes = "".join(f'<img src="data:image/svg+xml;utf8,{_uri(svg)}" width="{s}" height="{s}">'
                            for s in (16, 20, 32, 64))
            cells.append(f'<div class="cell"><div class="row dark">{sizes}</div>'
                         f'<div class="row light">{sizes}</div><div class="name">{html.escape(name)}</div></div>')
        rows.append(f'<h2>{html.escape(section)}</h2><div class="grid">{"".join(cells)}</div>')
    page = f"""<!doctype html><html><head><meta charset="utf-8"><title>{html.escape(title)}</title>
<style>
body {{ margin: 0; padding: 20px 24px 28px; background: #15181d; color: #cfd6df;
       font: 13px/1.4 -apple-system, "Segoe UI", Roboto, Arial, sans-serif; }}
h1 {{ font-size: 18px; margin: 0 0 4px; color: #eef2f6; font-weight: 600; }}
.sub {{ color: #8b95a3; margin-bottom: 8px; }}
h2 {{ font-size: 13px; text-transform: uppercase; letter-spacing: .08em; color: #8fa3bb;
      margin: 22px 0 10px; font-weight: 600; }}
.grid {{ display: grid; grid-template-columns: repeat(auto-fill, minmax(176px, 1fr)); gap: 10px; }}
.cell {{ background: #1d2127; border: 1px solid #2a3038; border-radius: 6px; overflow: hidden; }}
.row {{ display: flex; align-items: flex-end; gap: 10px; padding: 10px 10px 8px; }}
.dark {{ background: #1d2127; }}
.light {{ background: #e9ecf0; }}
.name {{ padding: 6px 10px 8px; color: #dfe5ec; border-top: 1px solid #2a3038; font-size: 12px; }}
</style></head><body><h1>{html.escape(title)}</h1>
<div class="sub">Each icon at 16, 20, 32 and 64 px, on a dark and a light panel.</div>
{"".join(rows)}</body></html>"""
    with open(path, "w", encoding="utf-8") as f:
        f.write(page)


def _uri(svg):
    return (svg.replace("%", "%25").replace("#", "%23").replace('"', "'").replace("<", "%3C")
            .replace(">", "%3E").replace("\n", " "))
