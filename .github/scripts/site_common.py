"""GitHub Pages 构建脚本共用小工具。"""


def repo_short_name(repo: str) -> str:
    return repo.split("/")[-1] if "/" in repo else repo
