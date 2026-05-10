#!/usr/bin/env python3
"""
编排 GitHub Pages 站点生成：
- PEP 503：pep503_simple_index.py → site/simple/<项目>/
- 主页：pages_landing.py（依赖 readme_render.py）→ site/index.html

环境变量与旧入口 gen_pep503_simple.py 一致；发版与 pages-readme 工作流调用本脚本。
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

from pages_landing import build_pages_landing_html
from pep503_simple_index import build_simple_index_html, write_simple_index


def main_readme_landing_only() -> int:
    repo = os.environ.get("GITHUB_REPOSITORY", "").strip()
    project = os.environ.get("PEP503_PROJECT", "hik-code-reader").strip()
    docs_ref = os.environ.get("DOCS_REF", "").strip() or "main"
    if not repo:
        print("Need GITHUB_REPOSITORY", file=sys.stderr)
        return 1

    Path("site").mkdir(parents=True, exist_ok=True)
    landing = build_pages_landing_html(
        Path("README.md"),
        repo,
        project,
        badge=docs_ref,
        header_sub="README 变更后自动刷新；pip 索引在发版时更新。",
    )
    Path("site/index.html").write_text(landing, encoding="utf-8")
    print("Wrote site/index.html (README-only, simple/ unchanged on server)")
    return 0


def main() -> int:
    if os.environ.get("PAGES_README_ONLY", "").strip() in ("1", "true", "yes"):
        return main_readme_landing_only()

    repo = os.environ.get("GITHUB_REPOSITORY", "").strip()
    tag = os.environ.get("RELEASE_TAG", "").strip()
    wheel = os.environ.get("WHEEL_FILENAME", "").strip()
    project = os.environ.get("PEP503_PROJECT", "hik-code-reader").strip()
    prev_index = os.environ.get("PREVIOUS_INDEX_HTML", "").strip()

    if not repo or not tag or not wheel:
        print("Need GITHUB_REPOSITORY, RELEASE_TAG, WHEEL_FILENAME", file=sys.stderr)
        return 1

    simple_html = build_simple_index_html(repo, tag, wheel, project, prev_index)
    site = Path("site")
    out = write_simple_index(site, project, simple_html)
    print(f"Wrote {out}")

    landing = build_pages_landing_html(
        Path("README.md"),
        repo,
        project,
        badge=f"Release {tag}",
        header_sub=f"与发版 {tag} 同步；Python 包见 pip 索引（PEP 503）。文档区随 README 更新。",
    )
    Path("site/index.html").write_text(landing, encoding="utf-8")
    print("Wrote site/index.html (landing + README.md)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
