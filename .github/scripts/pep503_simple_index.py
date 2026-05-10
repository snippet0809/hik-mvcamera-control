"""
PEP 503：生成 simple/<项目名>/index.html（仅 wheel 链接列表，供 pip --index-url 使用）。
与站点主页（landing）无关。
"""

from __future__ import annotations

import html
import re
from pathlib import Path


def extract_hrefs(body: str) -> list[str]:
    return re.findall(r'href="([^"]+)"', body, flags=re.I)


def build_simple_index_html(
    repo: str,
    tag: str,
    wheel: str,
    project: str,
    prev_index_path: str,
) -> str:
    """返回 PEP 503 所需的极简 HTML（多行 wheel 链接）。"""
    url = f"https://github.com/{repo}/releases/download/{tag}/{wheel}"
    new_line = f'<a href="{html.escape(url)}">{html.escape(wheel)}</a>'

    links: list[str] = []
    if prev_index_path and Path(prev_index_path).is_file():
        text = Path(prev_index_path).read_text(encoding="utf-8", errors="replace")
        for href in extract_hrefs(text):
            if href == url or href.endswith("/" + wheel):
                continue
            desc = href.rsplit("/", 1)[-1]
            links.append(f'<a href="{html.escape(href)}">{html.escape(desc)}</a>')

    links.append(new_line)
    return "<!DOCTYPE html>\n<html><body>\n" + "\n<br/>\n".join(links) + "\n</body></html>\n"


def write_simple_index(site_dir: Path, project: str, html_out: str) -> Path:
    out_dir = site_dir / "simple" / project
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / "index.html"
    path.write_text(html_out, encoding="utf-8")
    return path
