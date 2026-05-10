#!/usr/bin/env python3
"""
为 GitHub Pages 生成 PEP 503 兼容的 simple/<项目名>/index.html，指向 GitHub Release 上的 wheel。
可合并 gh-pages 上已有 index 中的链接（同一 wheel 文件名不重复添加）。
"""
from __future__ import annotations

import html
import os
import re
import sys
from pathlib import Path


def extract_hrefs(body: str) -> list[str]:
    return re.findall(r'href="([^"]+)"', body, flags=re.I)


def main() -> int:
    repo = os.environ.get("GITHUB_REPOSITORY", "").strip()
    tag = os.environ.get("RELEASE_TAG", "").strip()
    wheel = os.environ.get("WHEEL_FILENAME", "").strip()
    project = os.environ.get("PEP503_PROJECT", "hik-code-reader").strip()
    prev_index = os.environ.get("PREVIOUS_INDEX_HTML", "").strip()

    if not repo or not tag or not wheel:
        print("Need GITHUB_REPOSITORY, RELEASE_TAG, WHEEL_FILENAME", file=sys.stderr)
        return 1

    url = f"https://github.com/{repo}/releases/download/{tag}/{wheel}"
    new_line = f'<a href="{html.escape(url)}">{html.escape(wheel)}</a>'

    links: list[str] = []
    if prev_index and Path(prev_index).is_file():
        text = Path(prev_index).read_text(encoding="utf-8", errors="replace")
        for href in extract_hrefs(text):
            if href == url or href.endswith("/" + wheel):
                continue
            # 保留历史 wheel 链接（指向旧版 Release）
            desc = href.rsplit("/", 1)[-1]
            links.append(f'<a href="{html.escape(href)}">{html.escape(desc)}</a>')

    links.append(new_line)
    # 新版本放最后一行；pip 会挑选匹配版本
    body = "<!DOCTYPE html>\n<html><body>\n" + "\n<br/>\n".join(links) + "\n</body></html>\n"

    out_dir = Path("site/simple") / project
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "index.html").write_text(body, encoding="utf-8")

    # 便于人工浏览
    safe_proj = html.escape(project, quote=True)
    Path("site/index.html").write_text(
        "<!DOCTYPE html><html><body><p>PEP 503 simple index: "
        f'<a href="simple/{safe_proj}/">{safe_proj}</a></p></body></html>\n',
        encoding="utf-8",
    )
    print(f"Wrote {out_dir / 'index.html'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
