#!/usr/bin/env python3
"""docs_check: the mechanical half of docs-parity.

Reads `.agents/docs-parity.json` at the project root and checks the user docs it
describes against the rules a script can check: every image a page embeds exists
and has its dark twin, no image sits unreferenced, internal links resolve, a public
root never links into a private one, every page is in the nav manifest and every
manifest entry exists, pages carry their frontmatter and opening summary and stay
under budget, and a root's deny patterns stay out of its pages.

Two git-backed modes look at the code too:

  --changed BASE   files changed since BASE (a ref or an A..B range); a change to a
                   surface's code with no change to its pages or shots is
                   PARITY_MISSING, unless a `Docs: none ...` line in the range's
                   commit messages or in --note says why
  --stale          a shot whose capture commit (the shots manifest, else the image's
                   own last commit) predates a change to its surface's code is
                   SHOT_STALE

Exit 0 clean (info only), 1 warnings, 2 critical. --json prints one object:
{"config", "mode", "summary", "accepted", "findings": [{code, severity, path, line, message}]}.
Stdlib only; never writes a file.
"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True

CONFIG = Path(".agents") / "docs-parity.json"
PAGE_EXT = (".md", ".mdx")
IMAGE_EXT = (".png", ".jpg", ".jpeg", ".webp", ".gif", ".svg", ".avif")
TWIN_EXT = (".png", ".jpg", ".jpeg", ".webp", ".avif")  # raster only: an SVG can be theme-neutral
NAV_FILES = ("_Sidebar.md", "_sidebar.md", "_Footer.md", "_footer.md", "SUMMARY.md")
SEVERITY = {"critical": 2, "warning": 1, "info": 0}

DEFAULTS = {
    "target": "docs-folder",
    "docsRoot": "docs",
    "nav": "auto",
    "frontmatter": [],
    "tags": None,
    "summary": True,
    "pageBudget": 120,
    "linkBase": "/",
    "roots": [],
    "images": {},
    "surfaces": [],
    "ignore": ["**/AGENTS.md", "**/CLAUDE.md", "**/*.test.*", "**/*.spec.*", "**/test/**", "**/tests/**", "**/__tests__/**"],
    "accept": [],
}

IMG_MD = re.compile(r"!\[[^\]]*\]\(\s*<?([^)\s>]+)>?(?:\s+[\"'][^)]*[\"'])?\s*\)")
IMG_ATTR = re.compile(r"\b(?:src|srcSet|srcset)\s*=\s*[{]?[\"'`]([^\"'`]+)[\"'`]")
LINK_MD = re.compile(r"(?<!!)\[[^\]]*\]\(\s*<?([^)\s>]+)>?(?:\s+[\"'][^)]*[\"'])?\s*\)")
LINK_ATTR = re.compile(r"\bhref\s*=\s*[{]?[\"'`]([^\"'`]+)[\"'`]")
LINK_WIKI = re.compile(r"\[\[([^\]|]+)(?:\|([^\]]+))?\]\]")
INLINE_CODE = re.compile(r"`[^`]*`")
EXTERNAL = re.compile(r"^(?:[a-z][a-z0-9+.-]*:|//|#)", re.I)
DOCS_NONE = re.compile(r"^\s*Docs:\s*(none|n/?a|no\b)", re.I | re.M)


class Finding:
    def __init__(self, code, severity, path, line, message):
        self.code, self.severity, self.path, self.line, self.message = code, severity, path, line, message

    def as_dict(self):
        return {"code": self.code, "severity": self.severity, "path": self.path, "line": self.line, "message": self.message}


def glob_re(pattern):
    """A repo-relative glob as a regex: `**/` any depth (including none), `*` within a segment."""
    out, i = "", 0
    while i < len(pattern):
        c = pattern[i]
        if pattern.startswith("**/", i):
            out, i = out + "(?:.*/)?", i + 3
        elif pattern.startswith("**", i):
            out, i = out + ".*", i + 2
        elif c == "*":
            out, i = out + "[^/]*", i + 1
        elif c == "?":
            out, i = out + "[^/]", i + 1
        else:
            out, i = out + re.escape(c), i + 1
    return re.compile(out + r"\Z")


def matches(path, globs):
    return any(glob_re(g).match(path) for g in globs)


def rel(p, root):
    try:
        return p.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return p.as_posix()


def load_config(root, config=None):
    path = Path(config) if config else root / CONFIG
    if not path.is_file():
        return None
    cfg = json.loads(path.read_text(encoding="utf-8-sig"))
    merged = dict(DEFAULTS)
    merged.update(cfg)
    return merged


# ---- pages -------------------------------------------------------------------

def split_frontmatter(text):
    """(frontmatter text or None, body, line number the body starts on)."""
    if text.startswith("---\n") or text.startswith("---\r\n"):
        end = re.search(r"^---[ \t]*\r?$", text[4:], re.M)
        if end:
            fm = text[4:4 + end.start()]
            body = text[4 + end.end():]
            return fm, body, fm.count("\n") + 2  # body[0] is the rest of the closing --- line
    return None, text, 1


def frontmatter_keys(fm):
    return set(re.findall(r"^([A-Za-z_][\w-]*)\s*:", fm or "", re.M))


def tag_count(fm):
    m = re.search(r"^tags\s*:\s*\[([^\]]*)\]", fm or "", re.M)
    if m:
        return len([t for t in m.group(1).split(",") if t.strip()])
    m = re.search(r"^tags\s*:\s*\n((?:\s+-\s+.*\n?)+)", fm or "", re.M)
    return len(m.group(1).strip().splitlines()) if m else 0


def strip_code(body):
    """The body with fenced code blocks blanked (line count kept), so examples are not checked as links."""
    out, fenced = [], False
    for line in body.split("\n"):
        if re.match(r"^\s*(```|~~~)", line):
            fenced = not fenced
            out.append("")
        else:
            out.append("" if fenced else line)
    return "\n".join(out)


def has_summary(body):
    """True when the first block after imports and an optional H1 is a prose paragraph."""
    for block in re.split(r"\n\s*\n", body.strip()):
        block = block.strip()
        if not block or block.startswith("import ") or block.startswith("export "):
            continue
        if re.match(r"^#\s", block):
            rest = block.split("\n", 1)
            if len(rest) == 1:
                continue
            block = rest[1].strip()
        return not re.match(r"^(#|[-*+]\s|\d+\.\s|<|```|~~~|\||!\[|>)", block)
    return False


def page_files(docs_root, root=None, include=None):
    """The pages: markdown under docsRoot, narrowed to the config's `include` globs when it has them."""
    if not docs_root.is_dir():
        return []
    pages = sorted(p for p in docs_root.rglob("*") if p.suffix in PAGE_EXT and p.is_file()
                   and p.name not in NAV_FILES and "node_modules" not in p.parts and ".git" not in p.parts)
    if include:
        pages = [p for p in pages if matches(rel(p, root), include)]
    return pages


def root_of(page_rel, roots, docs_rel):
    inner = page_rel[len(docs_rel) + 1:] if docs_rel and page_rel.startswith(docs_rel + "/") else page_rel
    for r in roots:
        p = r.get("path", "").strip("/")
        if p and (inner == p or inner.startswith(p + "/")):
            return r
    return None


# ---- resolution --------------------------------------------------------------

def resolve_image(ref, page, ctx):
    ref = ref.split("?")[0].split("#")[0].split()[0]
    images = ctx["images"]
    prefix = images.get("urlPrefix")
    if prefix and ref.startswith(prefix) and images.get("dir"):
        return ctx["root"] / images["dir"] / ref[len(prefix):]
    if ref.startswith("/"):
        return ctx["site_root"] / ref.lstrip("/")
    return page.parent / ref


def resolve_link(href, page, ctx):
    """The page file a link points at, or None when nothing on disk matches."""
    href = href.split("#")[0].split("?")[0]
    if not href:
        return page
    base = ctx["link_base"]
    if href.startswith("/"):
        target = ctx["docs_root"] / href[len(base):] if href.startswith(base) else ctx["docs_root"] / href.lstrip("/")
    else:
        target = page.parent / href
    candidates = [target]
    if target.suffix not in PAGE_EXT:
        candidates += [target.with_name(target.name + e) for e in PAGE_EXT]
        candidates += [target / ("index" + e) for e in PAGE_EXT] + [target / ("README" + e) for e in PAGE_EXT]
    for c in candidates:
        if c.is_file():
            return c
    if ctx.get("wiki") and "/" not in href.strip("/"):
        return resolve_wiki(Path(href).stem, ctx)  # a wiki links pages by name, from anywhere
    if target.is_dir() or (target.suffix not in PAGE_EXT and target.exists()):
        return target  # a folder or an asset (a download, an image link)
    return None


def resolve_wiki(name, ctx):
    stem = name.strip().replace(" ", "-")
    for p in ctx["pages"]:
        if p.stem.lower() == stem.lower():
            return p
    return None


# ---- tree checks -------------------------------------------------------------

def check_tree(root, cfg):
    findings = []
    docs_rel = cfg["docsRoot"].strip("/")
    docs_root = root / docs_rel
    images = cfg.get("images") or {}
    ctx = {
        "root": root,
        "docs_root": docs_root,
        "site_root": root / (cfg.get("siteRoot") or docs_rel),
        "link_base": cfg.get("linkBase") or "/",
        "images": images,
        "pages": page_files(docs_root, root, cfg.get("include")),
        "wiki": str(cfg.get("target", "")).startswith("wiki"),
    }
    if not docs_root.is_dir():
        return [Finding("DOCS_ROOT_MISSING", "critical", docs_rel, 0, "the configured docsRoot does not exist")]
    roots = cfg.get("roots") or []
    themes = images.get("themes") or ["default"]
    dark_suffix = images.get("darkSuffix", ".dark")
    referenced = set()

    for page in ctx["pages"]:
        prel = rel(page, root)
        text = page.read_text(encoding="utf-8-sig", errors="replace")
        fm, body, body_line = split_frontmatter(text)
        root_cfg = root_of(prel, roots, docs_rel)

        missing = [k for k in cfg.get("frontmatter") or [] if k not in frontmatter_keys(fm)]
        if missing:
            findings.append(Finding("FRONTMATTER_MISSING", "warning", prel, 1, "frontmatter lacks " + ", ".join(missing)))
        tags = cfg.get("tags")
        if tags and "tags" in frontmatter_keys(fm):
            n = tag_count(fm)
            if not tags[0] <= n <= tags[1]:
                findings.append(Finding("FRONTMATTER_TAGS", "warning", prel, 1, f"{n} tags, want {tags[0]} to {tags[1]}"))
        if cfg.get("summary") and not has_summary(body):
            findings.append(Finding("SUMMARY_MISSING", "warning", prel, body_line, "the page does not open with a summary paragraph"))
        lines = text.count("\n") + 1
        if cfg.get("pageBudget") and lines > cfg["pageBudget"]:
            findings.append(Finding("PAGE_BUDGET", "warning", prel, lines, f"{lines} lines, budget {cfg['pageBudget']}: split it by topic"))

        scan = strip_code(body)
        for n, line in enumerate(scan.split("\n"), start=body_line):
            for pat in (root_cfg or {}).get("deny") or []:
                if re.search(pat, line):
                    findings.append(Finding("ROOT_DENY", "warning", prel, n,
                                            f"the {root_cfg.get('name', 'root')} root does not allow /{pat}/"))
            line = INLINE_CODE.sub("", line)  # deny patterns may target code spans; links and images never sit in one
            for ref in IMG_MD.findall(line) + IMG_ATTR.findall(line):
                ref = ref.split(",")[0].strip()
                if EXTERNAL.match(ref) or not ref.split("#")[0].split("?")[0].split()[0].lower().endswith(IMAGE_EXT):
                    continue
                target = resolve_image(ref, page, ctx)
                referenced.add(target.resolve())
                if not target.is_file():
                    findings.append(Finding("IMAGE_MISSING", "warning", prel, n, f"{ref} does not exist"))
                    continue
                if "dark" in themes and target.suffix.lower() in TWIN_EXT and not target.stem.endswith(dark_suffix):
                    twin = target.with_name(target.stem + dark_suffix + target.suffix)
                    referenced.add(twin.resolve())
                    if not twin.is_file():
                        findings.append(Finding("IMAGE_NO_DARK", "warning", prel, n, f"{ref} has no {twin.name} beside it"))
            for href in LINK_MD.findall(line) + LINK_ATTR.findall(line):
                if EXTERNAL.match(href):
                    continue
                target = resolve_link(href, page, ctx)
                check_link(findings, prel, n, href, target, root_cfg, roots, docs_rel, root)
            for name, label in LINK_WIKI.findall(line):
                # GitHub writes [[text|Page]], MediaWiki [[Page|text]]: try both halves
                target = resolve_wiki(label or name, ctx) or resolve_wiki(name, ctx)
                check_link(findings, prel, n, f"[[{label or name}]]", target, root_cfg, roots, docs_rel, root)

    img_dir = images.get("dir")
    if img_dir and (root / img_dir).is_dir():
        for f in sorted((root / img_dir).rglob("*")):
            if f.is_file() and f.suffix.lower() in IMAGE_EXT and f.resolve() not in referenced:
                findings.append(Finding("IMAGE_ORPHAN", "warning", rel(f, root), 0, "no page embeds this image"))

    findings += check_nav(root, cfg, docs_root, ctx["pages"])
    return findings


def check_link(findings, prel, n, href, target, root_cfg, roots, docs_rel, root):
    if target is None:
        findings.append(Finding("LINK_BROKEN", "warning", prel, n, f"{href} resolves to no page"))
        return
    to_root = root_of(rel(target, root), roots, docs_rel)
    if to_root and to_root.get("private") and not (root_cfg or {}).get("private"):
        findings.append(Finding("ROOT_LEAK", "critical", prel, n,
                                f"a public page links into the private {to_root.get('name', 'root')} root: {href}"))


# ---- nav manifests -----------------------------------------------------------

def detect_nav(root, cfg, docs_root):
    nav = cfg.get("nav") or "auto"
    if nav != "auto":
        return nav
    if any(docs_root.rglob("meta.json")):
        return "meta.json"
    if (root / "mkdocs.yml").is_file():
        return "mkdocs"
    if (docs_root / "SUMMARY.md").is_file():
        return "summary"
    if (docs_root / "_Sidebar.md").is_file() or (docs_root / "_sidebar.md").is_file():
        return "sidebar"
    return "none"


def check_nav(root, cfg, docs_root, pages):
    kind = detect_nav(root, cfg, docs_root)
    if kind == "meta.json":
        return nav_meta_json(root, docs_root)
    if kind == "mkdocs":
        return nav_listed(root, docs_root, pages, root / "mkdocs.yml", mkdocs_entries(root / "mkdocs.yml"))
    if kind == "summary":
        f = docs_root / "SUMMARY.md"
        return nav_listed(root, docs_root, pages, f, md_link_entries(f))
    if kind == "sidebar":
        f = docs_root / "_Sidebar.md" if (docs_root / "_Sidebar.md").is_file() else docs_root / "_sidebar.md"
        return nav_sidebar(root, docs_root, pages, f)
    return []


def nav_meta_json(root, docs_root):
    """Fumadocs: every folder with a meta.json lists its pages and subfolders in reading order."""
    out = []
    for meta in sorted(docs_root.rglob("meta.json")):
        folder = meta.parent
        try:
            entries = json.loads(meta.read_text(encoding="utf-8-sig")).get("pages")
        except ValueError:
            out.append(Finding("NAV_INVALID", "warning", rel(meta, root), 0, "meta.json does not parse"))
            continue
        if entries is None:
            continue
        names = []
        for e in entries:
            if not isinstance(e, str) or e.startswith("---") or e.startswith("[") or e.startswith("!"):
                continue
            names.append(e[3:] if e.startswith("...") and len(e) > 3 else e)
        rest = "..." in entries or "z...a" in entries
        for name in names:
            if name in ("...", "z...a"):
                continue
            if not any((folder / (name + e)).is_file() for e in PAGE_EXT) and not (folder / name).is_dir():
                out.append(Finding("NAV_DEAD", "warning", rel(meta, root), 0, f"lists {name}, which is not a page or folder"))
        if rest:
            continue
        excluded = {e[1:] for e in entries if isinstance(e, str) and e.startswith("!")}
        for child in sorted(folder.iterdir()):
            name = child.stem if child.is_file() else child.name
            if (child.is_file() and child.suffix in PAGE_EXT) or (child.is_dir() and any(child.rglob("*.md*"))):
                if name not in names and name not in excluded and name != "index":  # index is the folder's own page
                    out.append(Finding("NAV_MISSING", "warning", rel(child, root), 0, f"not listed in {rel(meta, root)}"))
    return out


def mkdocs_entries(path):
    text = path.read_text(encoding="utf-8-sig")
    m = re.search(r"^nav\s*:\s*\n((?:[ \t-].*\n?|\s*\n)*)", text, re.M)
    if not m:
        return None  # no nav key: mkdocs builds the nav from the file system
    return re.findall(r"([\w./-]+\.md)\b", m.group(1))


def md_link_entries(path):
    return [h for h in LINK_MD.findall(strip_code(path.read_text(encoding="utf-8-sig"))) if not EXTERNAL.match(h)]


def nav_listed(root, docs_root, pages, manifest, entries):
    if entries is None:
        return []
    out, listed = [], set()
    for e in entries:
        target = (docs_root / e.split("#")[0])
        if not target.is_file():
            out.append(Finding("NAV_DEAD", "warning", rel(manifest, root), 0, f"lists {e}, which does not exist"))
        listed.add(target.resolve())
    for p in pages:
        if p.resolve() not in listed:
            out.append(Finding("NAV_MISSING", "warning", rel(p, root), 0, f"not listed in {rel(manifest, root)}"))
    return out


def nav_sidebar(root, docs_root, pages, sidebar):
    """A GitHub or GitLab wiki: the sidebar links pages by wiki link or markdown link; Home is implicit."""
    text = strip_code(sidebar.read_text(encoding="utf-8-sig"))
    listed, out = set(), []
    ctx = {"pages": pages}
    for name, label in LINK_WIKI.findall(text):
        target = resolve_wiki(label or name, ctx) or resolve_wiki(name, ctx)
        if target is None:
            out.append(Finding("NAV_DEAD", "warning", rel(sidebar, root), 0, f"links [[{name}]], which is not a page"))
        else:
            listed.add(target.resolve())
    for href in LINK_MD.findall(text):
        if EXTERNAL.match(href):
            continue
        target = resolve_wiki(Path(href.split("#")[0]).stem, ctx)
        if target is None:
            out.append(Finding("NAV_DEAD", "warning", rel(sidebar, root), 0, f"links {href}, which is not a page"))
        else:
            listed.add(target.resolve())
    for p in pages:
        if p.stem.lower() != "home" and p.resolve() not in listed:
            out.append(Finding("NAV_MISSING", "warning", rel(p, root), 0, f"not linked from {rel(sidebar, root)}"))
    return out


# ---- git-backed checks -------------------------------------------------------

def git(root, *args):
    r = subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True, encoding="utf-8")
    if r.returncode != 0:
        raise RuntimeError(r.stderr.strip() or f"git {' '.join(args)} failed")
    return r.stdout


def changed_files(root, base):
    if ".." in base:
        files = git(root, "diff", "--name-only", base).split("\n")
        messages = git(root, "log", "--format=%B", base)
    else:
        files = git(root, "diff", "--name-only", base).split("\n")
        files += git(root, "ls-files", "--others", "--exclude-standard").split("\n")
        messages = ""
    return sorted({f for f in files if f}), messages


def check_changed(root, cfg, base, note):
    files, messages = changed_files(root, base)
    exempt = bool(DOCS_NONE.search(messages + "\n" + (note or "")))
    out = []
    for s in cfg.get("surfaces") or []:
        name = s.get("name") or ", ".join(s.get("code", []))
        skip = (cfg.get("ignore") or []) + (s.get("exclude") or [])
        code = [f for f in files if matches(f, s.get("code") or []) and not matches(f, skip)]
        if not code:
            continue
        pages = [f for f in files if matches(f, s.get("pages") or [])]
        shots = [f for f in files if matches(f, s.get("shots") or [])]
        group = f" (capture group {s['group']})" if s.get("group") else ""
        if not pages and not shots:
            sev = "info" if exempt else "warning"
            why = "; a Docs: none line says why" if exempt else ""
            out.append(Finding("PARITY_MISSING", sev, code[0], 0,
                               f"surface {name} changed ({len(code)} file(s)) but none of its pages "
                               f"({', '.join(s.get('pages') or ['none mapped'])}) or shots changed{group}{why}"))
        elif s.get("shots") and not shots:
            out.append(Finding("SHOTS_UNCHANGED", "info", code[0], 0,
                               f"surface {name}: pages changed, shots did not; retake{group} if the change shows on screen"))
    return out


def shot_commits(root, cfg):
    """{image repo path: the commit it was captured from}, from the shots manifest when the harness writes one."""
    images = cfg.get("images") or {}
    manifest = images.get("manifest") or (images.get("dir") and f"{images['dir'].rstrip('/')}/shots.json")
    out = {}
    if manifest and (root / manifest).is_file():
        data = json.loads((root / manifest).read_text(encoding="utf-8-sig")).get("shots") or {}
        base = Path(manifest).parent
        for name, entry in data.items():
            if isinstance(entry, dict) and entry.get("commit"):
                out[(base / name).as_posix()] = entry["commit"]
    return out


def check_stale(root, cfg):
    recorded = shot_commits(root, cfg)
    dark_suffix = (cfg.get("images") or {}).get("darkSuffix", ".dark")
    tracked = git(root, "ls-files").split("\n")
    out, cache = [], {}
    for s in cfg.get("surfaces") or []:
        if not s.get("shots") or not s.get("code"):
            continue
        skip = (cfg.get("ignore") or []) + (s.get("exclude") or [])
        spec = ["--"] + [f":(glob){g}" for g in s["code"]] + [f":(glob,exclude){g}" for g in skip]
        for img in [f for f in tracked if matches(f, s["shots"]) and f.lower().endswith(IMAGE_EXT)]:
            p = Path(img)
            key = p.with_name(p.stem[: -len(dark_suffix)] + p.suffix).as_posix() if p.stem.endswith(dark_suffix) else img
            sha = recorded.get(key) or recorded.get(Path(key).with_suffix("").as_posix())
            if not sha:
                sha = git(root, "log", "-1", "--format=%H", "--", img).strip()
            if not sha:
                continue
            if (sha, s.get("name")) not in cache:
                try:
                    cache[(sha, s.get("name"))] = [f for f in git(root, "diff", "--name-only", sha, *spec).split("\n") if f]
                except RuntimeError:
                    cache[(sha, s.get("name"))] = []
            moved = cache[(sha, s.get("name"))]
            if moved:
                group = f"; retake capture group {s['group']}" if s.get("group") else ""
                out.append(Finding("SHOT_STALE", "warning", img, 0,
                                   f"captured at {sha[:8]}, and {len(moved)} file(s) of surface {s.get('name', '?')} "
                                   f"changed since (first: {moved[0]}){group}"))
    return out


# ---- run ---------------------------------------------------------------------

def accepted(f, accept):
    return any(a.get("code") == f.code and (not a.get("path") or a["path"] == f.path) for a in accept)


def run(root, changed=None, stale=False, tree=True, note=None, config=None):
    """The report as a dict; the CLI and the hooks both call this."""
    root = Path(root)
    cfg = load_config(root, config)
    if cfg is None:
        return {"config": None, "mode": [], "summary": {"critical": 0, "warning": 0, "info": 0}, "accepted": 0, "findings": []}
    findings, mode = [], []
    if tree:
        mode.append("tree")
        findings += check_tree(root, cfg)
    if changed:
        mode.append("changed")
        findings += check_changed(root, cfg, changed, note)
    if stale:
        mode.append("stale")
        findings += check_stale(root, cfg)
    accept = cfg.get("accept") or []
    kept = [f for f in findings if not accepted(f, accept)]
    kept.sort(key=lambda f: (-SEVERITY[f.severity], f.path, f.line, f.code))
    summary = {k: sum(1 for f in kept if f.severity == k) for k in SEVERITY}
    return {"config": Path(config).as_posix() if config else CONFIG.as_posix(), "mode": mode, "summary": summary,
            "accepted": len(findings) - len(kept), "findings": [f.as_dict() for f in kept]}


def exit_code(report):
    s = report["summary"]
    return 2 if s["critical"] else 1 if s["warning"] else 0


def main(argv=None):
    ap = argparse.ArgumentParser(prog="docs_check", description=__doc__.split("\n\n")[0])
    ap.add_argument("--root", default=".", help="the project root (default: the current directory)")
    ap.add_argument("--config", help=f"the config file (default: <root>/{CONFIG.as_posix()})")
    ap.add_argument("--changed", metavar="BASE", help="check parity of files changed since BASE (a ref, or A..B)")
    ap.add_argument("--stale", action="store_true", help="find shots captured before their surface's code last changed")
    ap.add_argument("--no-tree", action="store_true", help="skip the tree checks (use with --changed or --stale)")
    ap.add_argument("--note", help="text to search for a 'Docs: none ...' line, such as a pull request body")
    ap.add_argument("--json", action="store_true", help="print the report as one JSON object")
    args = ap.parse_args(argv)
    try:
        report = run(args.root, changed=args.changed, stale=args.stale, tree=not args.no_tree, note=args.note,
                     config=args.config)
    except (RuntimeError, ValueError, OSError) as e:
        print(f"docs_check: {e}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(report, indent=1))
    elif report["config"] is None:
        print(f"docs_check: no {CONFIG.as_posix()} here; run /docs-parity:docs-init")
    else:
        for f in report["findings"]:
            where = f["path"] + (f":{f['line']}" if f["line"] else "")
            print(f"{f['severity']:<8} {f['code']:<20} {where}  {f['message']}")
        s = report["summary"]
        extra = f", {report['accepted']} accepted" if report["accepted"] else ""
        print(f"docs_check: {s['critical']} critical, {s['warning']} warning, {s['info']} info{extra}")
    return exit_code(report)


if __name__ == "__main__":
    sys.exit(main())
