"""README.md → 落地页 Hero 摘要 + Markdown 片段 HTML。"""

from __future__ import annotations

import re

try:
    import markdown
except ImportError as e:
    raise ImportError("pip install markdown") from e

from site_common import repo_short_name


def parse_readme_for_landing(raw: str, fallback_title: str) -> tuple[str, str, str]:
    """
    从 README 取：主标题、首段摘要（纯文本化）、供正文区渲染的 Markdown（去掉首个一级标题以免与 Hero 重复）。
    """
    text = raw.strip()
    if not text:
        return fallback_title, "", ""

    lines = text.splitlines()
    title = fallback_title
    i = 0
    if lines[0].startswith("# "):
        t = lines[0][2:].strip()
        if t:
            title = t
        i = 1

    while i < len(lines) and not lines[i].strip():
        i += 1

    para: list[str] = []
    while i < len(lines):
        line = lines[i]
        if not line.strip():
            break
        if line.lstrip().startswith("#"):
            break
        para.append(line.strip())
        i += 1

    lead = " ".join(para).strip()
    lead = re.sub(r"\*\*([^*]+)\*\*", r"\1", lead)
    lead = re.sub(r"`([^`]+)`", r"\1", lead)
    lead = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", lead)
    if len(lead) > 380:
        lead = lead[:377].rstrip() + "…"

    if not lead:
        lead = (
            "围绕海康机器人机器视觉 SDK 的本地封装：读码器 C++ 层、稳定 C ABI，"
            "便于 Python 与 Go 在 Windows 客户端侧集成。"
        )

    remainder = "\n".join(lines[1:]).lstrip("\n") if lines[0].startswith("# ") else text
    return title, lead, remainder


def markdown_to_html_fragment(md: str) -> str:
    if not md.strip():
        return "<p>（无更多正文）</p>"
    return markdown.markdown(
        md,
        extensions=[
            "markdown.extensions.fenced_code",
            "markdown.extensions.tables",
            "markdown.extensions.nl2br",
        ],
    )


def default_readme_missing(repo: str) -> str:
    return f"# {repo_short_name(repo)}\n\n（未找到 README.md）\n"
