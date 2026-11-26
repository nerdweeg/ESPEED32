#!/usr/bin/env python3
"""Build generated docs pages from shared sources."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[1]
DOCS_SRC_DIR = ROOT_DIR / "source/ESPEED32" / "docs_src"
SHARED_DIR = DOCS_SRC_DIR / "shared"
DATA_DOCS_DIR = ROOT_DIR / "source/ESPEED32" / "data" / "docs"

TEMPLATE_PATH = SHARED_DIR / "template.html"
STYLE_PATH = SHARED_DIR / "style.css"
SCRIPT_PATH = SHARED_DIR / "script.js"
DEFAULT_LANGS = ("en", "no", "es", "de", "it", "nl", "pt", "sv")

DEFAULT_TEMPLATE = """<!DOCTYPE html>
<html lang="{{lang}}">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{{title}}</title>
<link rel="icon" href="../../favicon.svg" type="image/svg+xml">
<style>
{{style}}</style>
</head>
<body>
{{main}}
<script>
{{script}}</script>
<script src="/ui/chat-widget.js"></script>
</body>
</html>
"""


class DocsBuildError(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build generated ESPEED32 docs.")
    parser.add_argument(
        "--check",
        action="store_true",
        help="Fail if generated output differs from checked-in files.",
    )
    parser.add_argument(
        "--lang",
        action="append",
        dest="langs",
        help="Only build/check the given language code. Repeatable.",
    )
    parser.add_argument(
        "--bootstrap-en",
        action="store_true",
        help="Bootstrap the English source files from the current generated page.",
    )
    parser.add_argument(
        "--bootstrap-all",
        action="store_true",
        help="Bootstrap all supported language sources from current generated pages.",
    )
    parser.add_argument(
        "--bootstrap-lang",
        action="append",
        dest="bootstrap_langs",
        help="Bootstrap the given language source from the current generated page. Repeatable.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Allow bootstrap to overwrite existing pilot source files.",
    )
    return parser.parse_args()


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def write_text(path: Path, text: str, *, overwrite: bool) -> None:
    if path.exists():
        current = path.read_text(encoding="utf-8")
        if current == text:
            return
        if not overwrite:
            raise DocsBuildError(f"Refusing to overwrite existing file: {path}")
    ensure_dir(path.parent)
    path.write_text(text, encoding="utf-8")


def read_text(path: Path) -> str:
    if not path.is_file():
        raise DocsBuildError(f"Missing required file: {path}")
    return path.read_text(encoding="utf-8")


def extract_tag_content(text: str, open_tag: str, close_tag: str) -> str:
    start = text.find(open_tag)
    if start == -1:
        raise DocsBuildError(f"Could not find opening tag {open_tag!r}")
    start += len(open_tag)
    end = text.find(close_tag, start)
    if end == -1:
        raise DocsBuildError(f"Could not find closing tag {close_tag!r}")
    content = text[start:end]
    if content.startswith("\r\n"):
        return content[2:]
    if content.startswith("\n"):
        return content[1:]
    return content


def extract_tag_block(text: str, open_tag: str, close_tag: str) -> str:
    start = text.find(open_tag)
    if start == -1:
        raise DocsBuildError(f"Could not find opening tag {open_tag!r}")
    end = text.find(close_tag, start)
    if end == -1:
        raise DocsBuildError(f"Could not find closing tag {close_tag!r}")
    return text[start:end + len(close_tag)]


def extract_single_value(text: str, prefix: str, suffix: str) -> str:
    start = text.find(prefix)
    if start == -1:
        raise DocsBuildError(f"Could not find prefix {prefix!r}")
    start += len(prefix)
    end = text.find(suffix, start)
    if end == -1:
        raise DocsBuildError(f"Could not find suffix {suffix!r}")
    return text[start:end]


def bootstrap_page(lang: str, *, force: bool) -> None:
    source_path = DATA_DOCS_DIR / lang / "index.html"
    html = read_text(source_path)

    html_lang = extract_single_value(html, '<html lang="', '">')
    title = extract_single_value(html, "<title>", "</title>")
    main = extract_tag_block(html, "<main>", "</main>")

    page_dir = DOCS_SRC_DIR / lang
    write_text(
        page_dir / "page.json",
        json.dumps({"lang": html_lang, "title": title}, indent=2) + "\n",
        overwrite=force,
    )
    write_text(page_dir / "main.html", main + "\n", overwrite=force)

    print(f"Bootstrapped: {source_path}")


def bootstrap_shared(force: bool, *, source_lang: str = "en") -> None:
    source_path = DATA_DOCS_DIR / source_lang / "index.html"
    html = read_text(source_path)
    style = extract_tag_content(html, "<style>", "</style>")
    script = extract_tag_content(html, "<script>", "</script>")

    write_text(TEMPLATE_PATH, DEFAULT_TEMPLATE, overwrite=force)
    write_text(STYLE_PATH, style, overwrite=force)
    write_text(SCRIPT_PATH, script, overwrite=force)


def bootstrap_pages(langs: list[str], *, force: bool) -> None:
    bootstrap_shared(force=force)
    for lang in langs:
        bootstrap_page(lang, force=force)


def iter_pages(selected_langs: set[str] | None) -> list[dict[str, object]]:
    if not DOCS_SRC_DIR.is_dir():
        raise DocsBuildError(f"Docs source directory not found: {DOCS_SRC_DIR}")

    pages = []
    for page_dir in sorted(path for path in DOCS_SRC_DIR.iterdir() if path.is_dir()):
        if page_dir.name == "shared" or page_dir.name.startswith("."):
            continue

        page_json = page_dir / "page.json"
        if not page_json.is_file():
            continue

        page = json.loads(read_text(page_json))
        lang = str(page.get("lang", "")).strip()
        title = str(page.get("title", "")).strip()
        if not lang or not title:
            raise DocsBuildError(f"Invalid page metadata in {page_json}")
        if selected_langs and lang not in selected_langs:
            continue

        pages.append(
            {
                "lang": lang,
                "title": title,
                "main_path": page_dir / "main.html",
                "output_path": DATA_DOCS_DIR / lang / "index.html",
            }
        )

    if selected_langs:
        found_langs = {str(page["lang"]) for page in pages}
        missing = sorted(selected_langs - found_langs)
        if missing:
            raise DocsBuildError(
                "No docs source found for language(s): " + ", ".join(missing)
            )

    if not pages:
        raise DocsBuildError("No docs pages found to build.")

    return pages


def render_page(page: dict[str, object]) -> str:
    template = read_text(TEMPLATE_PATH)
    replacements = {
        "{{lang}}": str(page["lang"]),
        "{{title}}": str(page["title"]),
        "{{style}}": read_text(STYLE_PATH).rstrip("\n"),
        "{{main}}": read_text(Path(page["main_path"])).rstrip("\n"),
        "{{script}}": read_text(SCRIPT_PATH).rstrip("\n"),
    }

    rendered = template
    for token, value in replacements.items():
        rendered = rendered.replace(token, value)
    return rendered


def build_pages(pages: list[dict[str, object]], check: bool) -> int:
    exit_code = 0
    for page in pages:
        output_path = Path(page["output_path"])
        rendered = render_page(page)
        current = output_path.read_text(encoding="utf-8") if output_path.is_file() else None

        if check:
            if current != rendered:
                print(f"Out of date: {output_path}")
                exit_code = 1
            else:
                print(f"OK: {output_path}")
            continue

        if current != rendered:
            ensure_dir(output_path.parent)
            output_path.write_text(rendered, encoding="utf-8")
            print(f"Built: {output_path}")
        else:
            print(f"Unchanged: {output_path}")

    return exit_code


def main() -> int:
    args = parse_args()

    try:
        bootstrap_langs: list[str] = []
        if args.bootstrap_all:
            bootstrap_langs.extend(DEFAULT_LANGS)
        if args.bootstrap_en:
            bootstrap_langs.append("en")
        if args.bootstrap_langs:
            bootstrap_langs.extend(args.bootstrap_langs)

        if bootstrap_langs:
            seen = set()
            ordered_bootstrap_langs = []
            for lang in bootstrap_langs:
                if lang in seen:
                    continue
                seen.add(lang)
                ordered_bootstrap_langs.append(lang)
            bootstrap_pages(ordered_bootstrap_langs, force=args.force)
            if args.check:
                return 0

        selected_langs = set(args.langs or [])
        pages = iter_pages(selected_langs if selected_langs else None)
        return build_pages(pages, check=args.check)
    except DocsBuildError as exc:
        print(f"[docs] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
