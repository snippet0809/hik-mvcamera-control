"""
GitHub Pages 站点主页：Hero、要点、安装示例 + README 正文（与 PEP 503 simple/ 无关）。
"""

from __future__ import annotations

import html
from pathlib import Path

from readme_render import (
    default_readme_missing,
    markdown_to_html_fragment,
    parse_readme_for_landing,
)
from site_common import repo_short_name


def build_pages_landing_html(
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
        raw = default_readme_missing(repo)

    fb_title = repo_short_name(repo)
    hero_title, hero_lead, remainder_md = parse_readme_for_landing(raw, fb_title)
    readme_html = markdown_to_html_fragment(remainder_md)

    owner = repo.split("/")[0] if "/" in repo else repo
    repo_short = repo_short_name(repo)
    pages_root = f"https://{owner}.github.io/{repo_short}/"
    simple_url = f"{pages_root}simple/{project}/"
    repo_url = f"https://github.com/{repo}"
    releases_url = f"{repo_url}/releases"

    pip_example = (
        f'pip install "hik-code-reader==&lt;版本&gt;" \\\n'
        f'  --index-url "{pages_root}simple/" \\\n'
        f'  --trusted-host "{owner}.github.io"'
    )

    go_example = f'go get github.com/{repo}/ffi/go@&lt;tag 或 main&gt;'

    e = html.escape
    hero_title_e = e(hero_title)
    hero_lead_e = e(hero_lead)
    badge_e = e(badge)
    sub_e = e(header_sub)
    owner_e = e(owner)
    repo_e = e(repo)
    proj_e = e(project)
    pip_e = e(pip_example)
    go_e = e(go_example)
    repo_short_e = e(repo_short)

    doc_prefix = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <meta name="description" content="{hero_title_e} — 海康机器视觉读码封装，Python / Go 客户端集成" />
  <title>{hero_title_e}</title>
  <style>
    :root {{
      --ink: #0c1222;
      --muted: #5c6578;
      --line: #e2e6ef;
      --surface: #ffffff;
      --wash: #f4f6fb;
      --hero-a: #0b1220;
      --hero-b: #153e5c;
      --accent: #0ea5e9;
      --accent-dim: #0284c7;
      --card-shadow: 0 8px 30px rgba(15, 23, 42, 0.08);
    }}
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      color: var(--ink);
      background: var(--wash);
      font-family: ui-sans-serif, system-ui, "Segoe UI", "PingFang SC", "Microsoft YaHei", sans-serif;
      font-size: 16px;
      line-height: 1.65;
    }}
    a {{ color: var(--accent-dim); text-decoration: none; }}
    a:hover {{ text-decoration: underline; }}

    .hero {{
      background: linear-gradient(145deg, var(--hero-a) 0%, var(--hero-b) 55%, #0f2840 100%);
      color: #e8f4ff;
      padding: 3rem 1.5rem 3.25rem;
      position: relative;
      overflow: hidden;
    }}
    .hero::after {{
      content: "";
      position: absolute;
      inset: 0;
      background: radial-gradient(900px 400px at 80% -10%, rgba(14, 165, 233, 0.35), transparent 55%);
      pointer-events: none;
    }}
    .hero-inner {{
      max-width: 1040px;
      margin: 0 auto;
      position: relative;
      z-index: 1;
    }}
    .hero h1 {{
      margin: 0 0 0.75rem;
      font-size: clamp(1.75rem, 4vw, 2.35rem);
      font-weight: 700;
      letter-spacing: -0.03em;
      line-height: 1.2;
    }}
    .hero-lead {{
      margin: 0 0 1.25rem;
      max-width: 52rem;
      font-size: 1.05rem;
      color: rgba(232, 244, 255, 0.88);
      line-height: 1.7;
    }}
    .meta {{
      display: flex;
      flex-wrap: wrap;
      align-items: center;
      gap: 0.5rem 1rem;
      font-size: 0.88rem;
      color: rgba(232, 244, 255, 0.72);
    }}
    .pill {{
      display: inline-flex;
      align-items: center;
      padding: 0.2rem 0.65rem;
      border-radius: 999px;
      background: rgba(255, 255, 255, 0.12);
      border: 1px solid rgba(255, 255, 255, 0.2);
      font-weight: 600;
      color: #fff;
    }}
    .cta-row {{
      margin-top: 1.75rem;
      display: flex;
      flex-wrap: wrap;
      gap: 0.65rem;
    }}
    .btn {{
      display: inline-flex;
      align-items: center;
      gap: 0.35rem;
      padding: 0.55rem 1.1rem;
      border-radius: 8px;
      font-weight: 600;
      font-size: 0.92rem;
      border: 1px solid transparent;
    }}
    .btn-primary {{
      background: var(--accent);
      color: #042f4a;
    }}
    .btn-primary:hover {{ filter: brightness(1.06); text-decoration: none; }}
    .btn-ghost {{
      background: rgba(255, 255, 255, 0.1);
      color: #fff;
      border-color: rgba(255, 255, 255, 0.25);
    }}
    .btn-ghost:hover {{ background: rgba(255, 255, 255, 0.16); text-decoration: none; }}

    .wrap {{
      max-width: 1040px;
      margin: 0 auto;
      padding: 2.25rem 1.5rem 3rem;
    }}

    .features {{
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
      gap: 1rem;
      margin-bottom: 2.25rem;
    }}
    .feature {{
      background: var(--surface);
      border: 1px solid var(--line);
      border-radius: 12px;
      padding: 1.15rem 1.25rem;
      box-shadow: var(--card-shadow);
    }}
    .feature h3 {{
      margin: 0 0 0.4rem;
      font-size: 1rem;
      color: var(--ink);
    }}
    .feature p {{
      margin: 0;
      font-size: 0.9rem;
      color: var(--muted);
      line-height: 1.55;
    }}

    .panel {{
      background: var(--surface);
      border: 1px solid var(--line);
      border-radius: 12px;
      padding: 1.35rem 1.5rem;
      margin-bottom: 1.5rem;
      box-shadow: var(--card-shadow);
    }}
    .panel h2 {{
      margin: 0 0 1rem;
      font-size: 1.15rem;
      border-bottom: 1px solid var(--line);
      padding-bottom: 0.5rem;
    }}
    .panel pre {{
      margin: 0;
      padding: 1rem;
      background: #0f172a;
      color: #e2e8f0;
      border-radius: 8px;
      overflow: auto;
      font-size: 0.82rem;
      line-height: 1.5;
    }}

    .markdown-body h1 {{ font-size: 1.55rem; margin-top: 0; }}
    .markdown-body h2 {{ font-size: 1.22rem; margin-top: 1.75rem; border-bottom: 1px solid var(--line); padding-bottom: 0.3em; }}
    .markdown-body h3 {{ font-size: 1.05rem; margin-top: 1.25rem; }}
    .markdown-body code {{
      font-family: ui-monospace, SFMono-Regular, "SF Mono", Menlo, Consolas, monospace;
      font-size: 0.88em;
      background: #eef2f8;
      padding: 0.12em 0.35em;
      border-radius: 4px;
    }}
    .markdown-body pre {{
      background: #0f172a;
      color: #e2e8f0;
      border-radius: 8px;
      padding: 1rem;
      overflow: auto;
      font-size: 0.86rem;
    }}
    .markdown-body pre code {{ background: none; padding: 0; color: inherit; }}
    .markdown-body table {{
      border-collapse: collapse;
      width: 100%;
      font-size: 0.9rem;
      margin: 1rem 0;
    }}
    .markdown-body th, .markdown-body td {{
      border: 1px solid var(--line);
      padding: 0.45rem 0.6rem;
    }}
    .markdown-body th {{ background: var(--wash); text-align: left; }}

    .site-footer {{
      text-align: center;
      padding: 2rem 1rem 2.5rem;
      font-size: 0.85rem;
      color: var(--muted);
    }}
  </style>
</head>
<body>
  <header class="hero">
    <div class="hero-inner">
      <h1>{hero_title_e}</h1>
      <p class="hero-lead">{hero_lead_e}</p>
      <div class="meta">
        <span class="pill">{badge_e}</span>
        <span>{sub_e}</span>
      </div>
      <div class="cta-row">
        <a class="btn btn-primary" href="{e(simple_url)}">pip 索引（PEP 503）· {proj_e}</a>
        <a class="btn btn-ghost" href="{e(releases_url)}">Releases 下载</a>
        <a class="btn btn-ghost" href="{e(repo_url)}">GitHub 仓库</a>
      </div>
    </div>
  </header>

  <main class="wrap">
    <section class="features" aria-label="项目要点">
      <div class="feature">
        <h3>读码器 C++ 封装</h3>
        <p>设备枚举、开停流、GigE 改 IP、GenICam 写参与读码回调；面向客户端进程内集成。</p>
      </div>
      <div class="feature">
        <h3>稳定 C ABI</h3>
        <p><code>hik_cr_*</code> 与线程局部错误信息，便于 ctypes / cgo 等 FFI 安全调用。</p>
      </div>
      <div class="feature">
        <h3>Python &amp; Go</h3>
        <p>正式包 <strong>hik-code-reader</strong>（wheel 内嵌 DLL）；Go 子模块在 <code>ffi/go</code>。</p>
      </div>
      <div class="feature">
        <h3>分发方式</h3>
        <p>Release 附件 + 本站的 PEP 503 索引；Go 使用 <code>go get</code> 与 <code>ffi/go/v*</code> 标签。</p>
      </div>
    </section>

    <section class="panel" aria-label="快速安装">
      <h2>快速安装（示例）</h2>
      <p style="margin:0 0 0.75rem;color:var(--muted);font-size:0.92rem;">
        以下为典型写法；版本号与 tag 对应，请以 <a href="{e(releases_url)}">Releases</a> 与 README 为准。当前平台主要为 <strong>Windows x64</strong>。
      </p>
      <p style="margin:0 0 0.35rem;font-weight:600;font-size:0.88rem;">Python（PEP 503）</p>
      <pre>{pip_e}</pre>
      <p style="margin:1rem 0 0.35rem;font-weight:600;font-size:0.88rem;">Go</p>
      <pre>{go_e}</pre>
    </section>

    <section class="panel markdown-body" aria-label="README">
      <h2 style="border:none;margin-bottom:0.75rem;padding:0;">完整说明（README）</h2>
      <p style="margin:0 0 1.25rem;color:var(--muted);font-size:0.9rem;">
        以下内容由仓库根目录 <code>README.md</code> 自动生成；更新 README 并推送到默认分支后，由 <strong>Pages (README)</strong> 工作流刷新本页。
      </p>
"""
    doc_suffix = f"""
    </section>
  </main>

  <footer class="site-footer">
    <p>
      仓库 <a href="{e(repo_url)}">{repo_e}</a> ·
      站点由 GitHub Actions 部署 ·
      托管用户页：<a href="{e(pages_root)}">{owner_e}.github.io/{repo_short_e}</a>
    </p>
  </footer>
</body>
</html>
"""
    return doc_prefix + readme_html + doc_suffix
