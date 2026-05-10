#!/usr/bin/env python3
"""
为 GitHub Pages 生成 PEP 503 兼容的 simple/<项目名>/index.html，指向 GitHub Release 上的 wheel。
根路径 index.html 由 README.md 渲染为正式文档首页（与 pip 用的 simple/ 分离）。
"""
from __future__ import annotations

import html
import os
import re
import sys
from pathlib import Path

try:
    import markdown
except ImportError as e:
    print("Install: pip install markdown", file=sys.stderr)
    raise SystemExit(1) from e


def extract_hrefs(body: str) -> list[str]:
    return re.findall(r'href="([^"]+)"', body, flags=re.I)


def repo_short_name(repo: str) -> str:
    return repo.split("/")[-1] if "/" in repo else repo


def render_readme_to_html(
    readme_path: Path,
    repo: str,
    project: str,
    *,
    badge: str,
    header_sub: str,
) -> str:
    if readme_path.is_file():
        raw = readme_path.read_text(encoding="utf-8", errors="replace")
    else:
        raw = f"# {repo_short_name(repo)}\n\n（未找到 README.md）\n"

    body_html = markdown.markdown(
        raw,
        extensions=[
            "markdown.extensions.fenced_code",
            "markdown.extensions.tables",
            "markdown.extensions.nl2br",
        ],
    )

    owner_repo = html.escape(repo, quote=True)
    repo_url = html.escape(f"https://github.com/{repo}", quote=True)
    releases_url = html.escape(f"https://github.com/{repo}/releases", quote=True)
    simple_href = html.escape(f"simple/{project}/", quote=True)
    safe_proj = html.escape(project, quote=True)
    title = html.escape(repo_short_name(repo), quote=True)
    safe_badge = html.escape(badge, quote=True)
    safe_sub = html.escape(header_sub, quote=True)

    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <meta name="description" content="{title} — 项目说明与 pip 安装索引" />
  <title>{title} · 项目文档</title>
  <style>
    :root {{
      --bg: #f6f8fa;
      --card: #ffffff;
      --text: #1f2328;
      --muted: #59636e;
      --border: #d1d9e0;
      --accent: #0969da;
      --code-bg: #f0f3f6;
    }}
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", "Noto Sans", Helvetica, Arial, sans-serif;
      font-size: 16px;
      line-height: 1.6;
      color: var(--text);
      background: var(--bg);
    }}
    .site-header {{
      background: var(--card);
      border-bottom: 1px solid var(--border);
      padding: 1rem 1.25rem;
    }}
    .site-header-inner {{
      max-width: 980px;
      margin: 0 auto;
    }}
    .site-header h1 {{
      margin: 0 0 0.35rem 0;
      font-size: 1.35rem;
      font-weight: 600;
      letter-spacing: -0.02em;
    }}
    .site-header p {{
      margin: 0;
      color: var(--muted);
      font-size: 0.9rem;
    }}
    .nav {{
      margin-top: 0.75rem;
      display: flex;
      flex-wrap: wrap;
      gap: 0.5rem 1rem;
    }}
    .nav a {{
      color: var(--accent);
      text-decoration: none;
      font-size: 0.9rem;
      font-weight: 500;
    }}
    .nav a:hover {{ text-decoration: underline; }}
    .badge {{
      display: inline-block;
      margin-left: 0.5rem;
      padding: 0.1rem 0.45rem;
      font-size: 0.75rem;
      font-weight: 600;
      color: var(--muted);
      background: var(--code-bg);
      border: 1px solid var(--border);
      border-radius: 999px;
      vertical-align: middle;
    }}
    main {{
      max-width: 980px;
      margin: 0 auto;
      padding: 1.75rem 1.25rem 3rem;
    }}
    .markdown-body {{
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 1.75rem 2rem;
      box-shadow: 0 1px 2px rgba(0,0,0,0.04);
    }}
    .markdown-body h1 {{ font-size: 1.75rem; border-bottom: 1px solid var(--border); padding-bottom: 0.3em; }}
    .markdown-body h2 {{ font-size: 1.35rem; margin-top: 1.75rem; border-bottom: 1px solid var(--border); padding-bottom: 0.25em; }}
    .markdown-body h3 {{ font-size: 1.12rem; margin-top: 1.25rem; }}
    .markdown-body a {{ color: var(--accent); text-decoration: none; }}
    .markdown-body a:hover {{ text-decoration: underline; }}
    .markdown-body code {{
      font-family: ui-monospace, SFMono-Regular, "SF Mono", Menlo, Consolas, monospace;
      font-size: 0.88em;
      background: var(--code-bg);
      padding: 0.15em 0.4em;
      border-radius: 4px;
    }}
    .markdown-body pre {{
      background: var(--code-bg);
      border: 1px solid var(--border);
      border-radius: 6px;
      padding: 1rem;
      overflow: auto;
      font-size: 0.88rem;
      line-height: 1.45;
    }}
    .markdown-body pre code {{ background: none; padding: 0; font-size: inherit; }}
    .markdown-body table {{
      border-collapse: collapse;
      width: 100%;
      margin: 1rem 0;
      font-size: 0.92rem;
    }}
    .markdown-body th, .markdown-body td {{
      border: 1px solid var(--border);
      padding: 0.5rem 0.65rem;
    }}
    .markdown-body th {{ background: var(--code-bg); text-align: left; }}
    .markdown-body ul, .markdown-body ol {{ padding-left: 1.5rem; }}
    .markdown-body blockquote {{
      margin: 1rem 0;
      padding: 0 1rem;
      color: var(--muted);
      border-left: 4px solid var(--border);
    }}
    .site-footer {{
      max-width: 980px;
      margin: 0 auto;
      padding: 0 1.25rem 2rem;
      font-size: 0.85rem;
      color: var(--muted);
      text-align: center;
    }}
    .site-footer a {{ color: var(--accent); }}
  </style>
</head>
<body>
  <header class="site-header">
    <div class="site-header-inner">
      <h1>{title}<span class="badge">{safe_badge}</span></h1>
      <p>{safe_sub}</p>
      <nav class="nav" aria-label="站点导航">
        <a href="{simple_href}">pip 索引（PEP 503）· {safe_proj}</a>
        <a href="{repo_url}">GitHub 仓库</a>
        <a href="{releases_url}">Releases</a>
      </nav>
    </div>
  </header>
  <main>
    <article class="markdown-body">
{body_html}
    </article>
  </main>
  <footer class="site-footer">
    <p>仓库：<a href="{repo_url}">{owner_repo}</a> · 仅作说明展示，安装请以 Releases / pip 为准。</p>
  </footer>
</body>
</html>
"""


def main_readme_landing_only() -> int:
    """仅更新站点根 index.html（README），不碰 simple/；供默认分支推送时与发版解耦。"""
    repo = os.environ.get("GITHUB_REPOSITORY", "").strip()
    project = os.environ.get("PEP503_PROJECT", "hik-code-reader").strip()
    docs_ref = os.environ.get("DOCS_REF", "").strip() or "main"
    if not repo:
        print("Need GITHUB_REPOSITORY", file=sys.stderr)
        return 1

    Path("site").mkdir(parents=True, exist_ok=True)
    readme_path = Path("README.md")
    sub = (
        "本页随 README 更新；pip 索引（PEP 503）在每次发版时更新，与发版流程独立。"
    )
    landing = render_readme_to_html(
        readme_path,
        repo,
        project,
        badge=docs_ref,
        header_sub=sub,
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

    url = f"https://github.com/{repo}/releases/download/{tag}/{wheel}"
    new_line = f'<a href="{html.escape(url)}">{html.escape(wheel)}</a>'

    links: list[str] = []
    if prev_index and Path(prev_index).is_file():
        text = Path(prev_index).read_text(encoding="utf-8", errors="replace")
        for href in extract_hrefs(text):
            if href == url or href.endswith("/" + wheel):
                continue
            desc = href.rsplit("/", 1)[-1]
            links.append(f'<a href="{html.escape(href)}">{html.escape(desc)}</a>')

    links.append(new_line)
    body = "<!DOCTYPE html>\n<html><body>\n" + "\n<br/>\n".join(links) + "\n</body></html>\n"

    out_dir = Path("site/simple") / project
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "index.html").write_text(body, encoding="utf-8")

    readme_path = Path("README.md")
    landing = render_readme_to_html(
        readme_path,
        repo,
        project,
        badge=f"v{tag}",
        header_sub="本页由发版时的 README 生成；Python 包请使用下方「pip 索引」。",
    )
    Path("site/index.html").write_text(landing, encoding="utf-8")

    print(f"Wrote {out_dir / 'index.html'}")
    print("Wrote site/index.html (from README.md)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
