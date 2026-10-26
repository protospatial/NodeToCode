"""docs_lint: fail when the agent docs drift from the MCP tool reference, or the reference from the tool sources.

Stdlib only; run with `python -I Scripts/docs_lint.py` from anywhere. Needs no Unreal Engine, so it runs on a
GitHub-hosted runner (.github/workflows/docs-check.yml, job docs-lint). The committed tool reference
(Context/mcp-server/tools.generated.json and .md, written by the N2CToolReference commandlet, card 0179) is the source
of truth for the docs; the C++ tool sources are the source of truth for the reference.

Checks (finding codes in brackets):

  1. reference against the sources [REF_STALE]: the tool names are the `FMcpToolDefinition` names of the files that
     use REGISTER_MCP_TOOL, one tool per use; each tool's toolset is the `N2CMcpToolsets::X` its definition names;
     the toolsets, their order and descriptions are the declarations in N2CMcpToolsets.h; a description written as
     plain string literals matches the reference; the counts add up; tools.generated.md has a section per tool and
     the same counts; Tests/smoke/expected.json `default` equals the tool count.
  2. tool names in the docs [TOOL_UNKNOWN]: a backticked kebab-case token that starts like a tool name (its first
     word is the first word of a registered tool, such as `get-` or `create-`) must be a registered tool.
  3. arguments [ARG_UNKNOWN]: in a `### <tool>` section, the arguments its **Parameters** list names must be in that
     tool's inputSchema.
  4. counts [COUNT_DRIFT]: "<N> tools" / "<N> MCP tools" must be a count the reference gives (all tools, the default
     tools, a toolset's size, the Unreal MCP overlap, ...). [TOOL_LIST_INCOMPLETE]: the per-tool list in
     Context/mcp-server/AGENTS-mcp-server.md names every registered tool, and every tool has a `### <tool>` section
     in some doc.
  5. links and paths [LINK_BROKEN, PATH_MISSING]: every relative Markdown link resolves, and every backticked repo
     path (a token whose first segment is a tracked top-level entry, such as `Source/...`) is a tracked file or
     folder.
  6. user pages [WIKI_LINK_BROKEN]: links between the wiki pages (decision 0006: wiki/ in this repository; --wiki
     points at another checkout, such as a clone of the published wiki).

What it cannot see without an editor: a changed input schema, a description built at run time, annotations or
long-running flags. `Scripts/generate_tool_reference.py --check` and the NodeToCode.MCP.ToolReference automation
test (`build.ps1 -Test`) compare the whole reference with what the registry builds.

Allowlist: Scripts/docs_lint_allow.txt, one entry per line, `<token>  # <reason>` (any file) or
`<path>::<token>  # <reason>` (one file); the reason is required. A token is a tool-like name, an argument as
`<tool>.<argument>`, a count as `<N> tools`, or a path or link target.

Exit 0 clean, 1 findings. --json prints {"findings": [{code, path, line, message}], "checked": {...}}.
"""
import argparse
import fnmatch
import importlib.util
import json
import os
import re
import subprocess
import sys

sys.dont_write_bytecode = True

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
REFERENCE_JSON = 'Context/mcp-server/tools.generated.json'
REFERENCE_MD = 'Context/mcp-server/tools.generated.md'
TOOLSETS_HEADER = 'Source/Public/MCP/Tools/N2CMcpToolsets.h'
TOOLS_DIR = 'Source/Private/MCP/Tools/Implementations'
SMOKE_EXPECTED = 'Tests/smoke/expected.json'
PLAY_SESSION_GUARD = 'Source/Private/MCP/Utils/N2CMcpPlaySessionGuard.cpp'
TOOL_LIST_DOC = 'Context/mcp-server/AGENTS-mcp-server.md'
ALLOWLIST = 'Scripts/docs_lint_allow.txt'
DOC_GLOBS = ('AGENTS.md', 'CLAUDE.md', 'README.md', 'Context/**/*.md', 'Source/**/AGENTS.md', 'wiki/developers/*.md')
GENERATED_DOCS = {REFERENCE_MD}

DEFINITION_RE = re.compile(r'FMcpToolDefinition\s+\w+\s*\(\s*TEXT\(\s*"([^"]+)"\s*\)\s*,\s*TEXT\(((?:\s*"(?:[^"\\]|\\.)*")+)\s*\)\s*([,)])')
DEFINITION_NAME_RE = re.compile(r'FMcpToolDefinition\s+\w+\s*\(\s*TEXT\(\s*"([^"]+)"\s*\)')
REGISTER_RE = re.compile(r'^\s*REGISTER_MCP_TOOL\(\w+\)', re.MULTILINE)
TOOLSET_REF_RE = re.compile(r'N2CMcpToolsets::(\w+)')
TOOLSET_DECL_RE = re.compile(r'FN2CMcpToolset\s+(\w+)\s*\{\s*TEXT\("([^"]+)"\)\s*,\s*TEXT\(((?:\s*"(?:[^"\\]|\\.)*")+)\s*\)')
TOOLSET_ALL_RE = re.compile(r'All\[\]\s*=\s*\{([^}]*)\}')
STRING_LITERAL_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')
BACKTICK_RE = re.compile(r'`([^`\n]+)`')
KEBAB_RE = re.compile(r'^[a-z][a-z0-9]*(?:-[a-z0-9]+)+$')
HEADING_RE = re.compile(r'^(#{2,6})\s+`?([a-z][a-z0-9]*(?:-[a-z0-9]+)+)`?(?:\s|$)')
ANY_HEADING_RE = re.compile(r'^(#{1,6})\s')
PARAMETERS_RE = re.compile(r'^(\s*)[-*]\s+\*\*Parameters\*\*')
INLINE_ARG_RE = re.compile(r'`([A-Za-z_]\w*)`\s*\((?:required|optional)')
BULLET_ARG_RE = re.compile(r'^(\s*)[-*]\s+`([A-Za-z_]\w*)`(?:\s*(?:,|and|or)\s*`[A-Za-z_]\w*`)*')
BULLET_RE = re.compile(r'^(\s*)[-*]\s')
COUNT_RE = re.compile(r'(?<![\w.-])(\d+)\s+(?:(?:MCP|NodeToCode|registered|Epic-duplicating|overlapping)\s+)?tools\b')
MD_LINK_RE = re.compile(r'(?<!!)\[[^\]]*\]\(\s*<?([^)\s>]+)>?(?:\s+"[^"]*")?\s*\)')
EXTERNAL_RE = re.compile(r'^(?:[a-z][a-z0-9+.-]*:|//|#)', re.I)
FENCE_RE = re.compile(r'^\s*(```|~~~)')
MCP_METHOD_RE = re.compile(r'^(?:tools|resources|prompts|notifications|completion|logging|sampling|roots)/[A-Za-z_]+$')
TABLE_TOOL_RE = re.compile(r'^\|\s*`([a-z][a-z0-9]*(?:-[a-z0-9]+)+)`\s*\|')
USER_PAGES = 'wiki/'
C_ESCAPES = {'n': '\n', 't': '\t', '"': '"', '\\': '\\', "'": "'", 'r': '\r', '0': '\0'}


class Finding:
    def __init__(self, code, path, line, message, token=None):
        self.code, self.path, self.line, self.message, self.token = code, path, line, message, token

    def as_dict(self):
        return {'code': self.code, 'path': self.path, 'line': self.line, 'message': self.message}

    def __str__(self):
        where = self.path + (':%d' % self.line if self.line else '')
        return '%s: %s %s' % (where, self.code, self.message)


def read(root, path):
    with open(os.path.join(root, path), encoding='utf-8-sig', errors='replace') as f:
        return f.read().replace('\r\n', '\n')


def tracked_files(root):
    """Files git tracks (what a CI checkout has); every file under root when git is not there."""
    try:
        out = subprocess.run(['git', '-C', root, 'ls-files', '-z'], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                             check=True).stdout.decode('utf-8', 'replace')
        files = [f for f in out.split('\0') if f]
        if files:
            return set(files)
    except (OSError, subprocess.CalledProcessError):
        pass
    found = set()
    for folder, dirs, names in os.walk(root):
        dirs[:] = [d for d in dirs if d not in ('.git', 'Binaries', 'Intermediate', 'BuildLogs', '__pycache__')]
        for name in names:
            found.add(os.path.relpath(os.path.join(folder, name), root).replace('\\', '/'))
    return found


def folders_of(files):
    folders = set()
    for f in files:
        parts = f.split('/')[:-1]
        for i in range(1, len(parts) + 1):
            folders.add('/'.join(parts[:i]))
    return folders


def glob_match(path, pattern):
    if '**/' in pattern:
        head, tail = pattern.split('**/', 1)
        return path.startswith(head) and (fnmatch.fnmatchcase(path[len(head):], tail)
                                          or any(fnmatch.fnmatchcase(path[len(head):].split('/', i)[-1], tail)
                                                 for i in range(1, path.count('/') + 1)))
    return fnmatch.fnmatchcase(path, pattern) and path.count('/') == pattern.count('/')


def doc_files(files):
    return sorted(f for f in files if f.endswith('.md') and any(glob_match(f, g) for g in DOC_GLOBS))


def c_string(literals):
    """The value of adjacent C string literals"""
    out = []
    for literal in STRING_LITERAL_RE.findall(literals):
        out.append(re.sub(r'\\(.)', lambda m: C_ESCAPES.get(m.group(1), m.group(1)), literal))
    return ''.join(out)


def strip_fences(text):
    """Lines with fenced code blanked, numbering kept"""
    out, fenced = [], False
    for line in text.split('\n'):
        if FENCE_RE.match(line):
            fenced = not fenced
            out.append('')
        else:
            out.append('' if fenced else line)
    return out


def load_allowlist(root, findings):
    entries = {}
    path = os.path.join(root, ALLOWLIST)
    if not os.path.isfile(path):
        return entries
    for n, raw in enumerate(read(root, ALLOWLIST).split('\n'), start=1):
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        entry, _, reason = line.partition('#')
        entry = entry.strip()
        if not reason.strip():
            findings.append(Finding('ALLOWLIST_NO_REASON', ALLOWLIST, n, '%s has no "# reason"' % entry))
            continue
        file, sep, token = entry.partition('::')
        entries[(file, token) if sep else ('', entry)] = n
    return entries


def allowed(allow, path, token, used):
    for key in ((path, token), ('', token)):
        if key in allow:
            used.add(key)
            return True
    return False


# ---- 1. reference against the sources ---------------------------------------------------------------------------

def source_tools(root, files):
    """{tool: {'toolset': X, 'description': str or None, 'file': path}}, REGISTER_MCP_TOOL uses, problems"""
    tools, uses, problems = {}, 0, []
    for path in sorted(f for f in files if f.startswith(TOOLS_DIR + '/') and f.endswith(('.cpp', '.h'))):
        text = read(root, path)
        n = len(REGISTER_RE.findall(text))
        uses += n
        if not n or not path.endswith('.cpp'):
            continue
        names = DEFINITION_NAME_RE.findall(text)
        if len(names) != 1:
            problems.append((path, 'has REGISTER_MCP_TOOL but %d FMcpToolDefinition names' % len(names)))
            continue
        full = DEFINITION_RE.search(text)
        refs = TOOLSET_REF_RE.findall(text[text.find(names[0]):])
        tools[names[0]] = {'toolset': refs[0] if refs else None, 'file': path,
                           'description': c_string(full.group(2)) if full and full.group(1) == names[0] else None}
    # the macro's own definition (in a header outside Implementations) is not a use
    return tools, uses, problems


def declared_toolsets(root):
    text = read(root, TOOLSETS_HEADER)
    decls = {ident: (name, c_string(desc)) for ident, name, desc in TOOLSET_DECL_RE.findall(text)}
    order = re.findall(r'&(\w+)', TOOLSET_ALL_RE.search(text).group(1)) if TOOLSET_ALL_RE.search(text) else list(decls)
    return decls, order


def check_reference(root, files, ref, findings):
    def stale(message, path=REFERENCE_JSON):
        findings.append(Finding('REF_STALE', path, 0, message + '; run Scripts/generate_tool_reference.py'))

    tools, uses, problems = source_tools(root, files)
    for path, message in problems:
        findings.append(Finding('REF_STALE', path, 0, message))
    decls, order = declared_toolsets(root)
    ref_tools = {t['name']: t for t in ref['tools']}

    for name in sorted(set(tools) - set(ref_tools)):
        stale('%s is registered (%s) but not in the reference' % (name, tools[name]['file']))
    for name in sorted(set(ref_tools) - set(tools)):
        stale('%s is in the reference but no source registers it' % name)
    if not (ref['toolCount'] == len(ref['tools']) == uses):
        stale('toolCount %d, %d tools listed, %d REGISTER_MCP_TOOL uses' % (ref['toolCount'], len(ref['tools']), uses))
    names = [t['name'] for t in ref['tools']]
    if names != sorted(names):
        stale('tools are not sorted by name')

    expected_sets = [decls[i][0] for i in order if i in decls]
    if [s['name'] for s in ref['toolsets']] != expected_sets:
        stale('toolsets %s, N2CMcpToolsets.h declares %s' % ([s['name'] for s in ref['toolsets']], expected_sets))
    by_name = {decls[i][0]: decls[i][1] for i in decls}
    for toolset in ref['toolsets']:
        if toolset['name'] in by_name and toolset['description'] != by_name[toolset['name']]:
            stale('toolset %s: description differs from N2CMcpToolsets.h' % toolset['name'])
        listed = sorted(n for n, t in ref_tools.items() if t.get('toolset') == toolset['name'])
        if sorted(toolset['tools']) != listed:
            stale('toolset %s lists %s, its tools are %s' % (toolset['name'], toolset['tools'], listed))

    guard = read(root, PLAY_SESSION_GUARD) if PLAY_SESSION_GUARD in files else ''
    note = re.search(r'DescriptionNote\s*=\s*TEXT\(((?:\s*"(?:[^"\\]|\\.)*")+)\s*\)', guard)
    note = c_string(note.group(1)) if note else None
    for name in sorted(set(tools) & set(ref_tools)):
        source, tool = tools[name], ref_tools[name]
        if (source['description'] is not None and note and not tool.get('worksDuringPlaySession', True)
                and note not in source['description']):
            # registration appends the PIE note to a tool refused during Play In Editor (FN2CMcpPlaySessionGuard)
            source = dict(source, description=('%s %s' % (source['description'].rstrip(), note)).lstrip())
        ident = source['toolset']
        declared = decls.get(ident, (None,))[0]
        if declared is None:
            findings.append(Finding('REF_STALE', source['file'], 0, '%s names no declared toolset (%s)' % (name, ident)))
        elif tool.get('toolset') != declared:
            stale('%s: toolset %s in the reference, %s in %s' % (name, tool.get('toolset'), declared, source['file']))
        if source['description'] is not None and source['description'] != tool['description']:
            stale('%s: description differs from %s' % (name, source['file']))

    if ref['defaultEnabledCount'] != sum(bool(t.get('defaultEnabled')) for t in ref['tools']):
        stale('defaultEnabledCount does not match the tools')
    if ref['unrealMcpOverlapCount'] != sum('unrealMcpCounterpart' in t for t in ref['tools']):
        stale('unrealMcpOverlapCount does not match the tools')

    md = read(root, REFERENCE_MD)
    headings = re.findall(r'^### (\S+)$', md, re.MULTILINE)
    wanted = names + [t['name'] for t in ref['toolSearchTools']]
    if sorted(headings) != sorted(wanted):
        stale('tools.generated.md sections %s do not match tools.generated.json'
              % sorted(set(headings) ^ set(wanted)), REFERENCE_MD)
    if 'registers %d MCP tools in %d toolsets' % (ref['toolCount'], len(ref['toolsets'])) not in md:
        stale('tools.generated.md does not state %d MCP tools in %d toolsets' % (ref['toolCount'], len(ref['toolsets'])),
              REFERENCE_MD)

    if SMOKE_EXPECTED in files:
        counts = json.loads(read(root, SMOKE_EXPECTED)).get('tool_counts', {})
        for engine, modes in sorted(counts.items()):
            if modes.get('default') != ref['toolCount']:
                findings.append(Finding('REF_STALE', SMOKE_EXPECTED, 0, '%s default %s, the reference has %d tools'
                                        % (engine, modes.get('default'), ref['toolCount'])))
    return len(tools)


# ---- 2-5. the docs ----------------------------------------------------------------------------------------------

def known_counts(ref):
    tools = ref['tools']
    counts = {ref['toolCount'], ref['defaultEnabledCount'], ref['unrealMcpOverlapCount'], len(ref['toolSearchTools']),
              ref['toolCount'] - ref['unrealMcpOverlapCount'], ref['defaultEnabledCount'] - ref['unrealMcpOverlapCount']}
    counts.update(len(s['tools']) for s in ref['toolsets'])
    counts.add(sum(1 for t in tools if not t.get('defaultEnabled')))
    counts.add(len(ref['toolsets']))
    return counts


def tool_sections(lines):
    """(tool, heading line index, end index) for each heading that is a tool-like name"""
    sections = []
    for i, line in enumerate(lines):
        m = HEADING_RE.match(line)
        if not m:
            continue
        level = len(m.group(1))
        end = len(lines)
        for j in range(i + 1, len(lines)):
            h = ANY_HEADING_RE.match(lines[j])
            if h and len(h.group(1)) <= level:
                end = j
                break
        sections.append((m.group(2), i, end))
    return sections


def section_arguments(lines, start, end):
    """[(argument, line index)] named by the section's **Parameters** list (top level only)"""
    found = []
    i = start
    while i < end:
        m = PARAMETERS_RE.match(lines[i])
        if not m:
            i += 1
            continue
        base = len(m.group(1))
        found += [(a, i) for a in INLINE_ARG_RE.findall(lines[i])]
        child = None
        i += 1
        while i < end:
            line = lines[i]
            if not line.strip():
                i += 1
                continue
            indent = len(line) - len(line.lstrip())
            if indent <= base:
                break
            bullet = BULLET_RE.match(line)
            if bullet and child is None:
                child = len(bullet.group(1))
            arg = BULLET_ARG_RE.match(line)
            if arg and len(arg.group(1)) == child:
                # `a`, `b` and `c` (optional): every name before the description
                found += [(name, i) for name in re.findall(r'`([A-Za-z_]\w*)`', arg.group(0))]
            i += 1
    return found


def resolve_path(token, doc, files, folders):
    """True/False for a repo path token, None when the token is not one"""
    token = token.strip().rstrip('.,;:')
    if any(c in token for c in '*<>{}[]$ |\\') or '...' in token or token.startswith(('/', 'http')):
        return None
    token = re.sub(r':\d+(?:-\d+)?$', '', token).split('#')[0].rstrip('/')
    if '/' not in token or MCP_METHOD_RE.match(token):
        return None
    pair = re.match(r'^(.+)\.(\w+)/\.(\w+)$', token)  # `Dir/File.h/.cpp`
    if pair:
        return all(resolve_path('%s.%s' % (pair.group(1), ext), doc, files, folders) is not False
                   for ext in pair.group(2, 3))
    if token.startswith(('./', '../')):
        target = os.path.normpath(os.path.join(os.path.dirname(doc), token)).replace('\\', '/')
    else:
        first = token.split('/', 1)[0]
        if first not in folders or first in ('Engine', 'Plugins'):
            return None
        target = token
    return target in files or target in folders


def check_docs(root, files, ref, allow, used, findings, stats):
    tools = {t['name']: t for t in ref['tools']}
    meta = {t['name']: t for t in ref['toolSearchTools']}
    verbs = {name.split('-', 1)[0] for name in tools}
    counts = known_counts(ref)
    folders = folders_of(files)
    sectioned = set()
    docs = [d for d in doc_files(files) if d not in GENERATED_DOCS]

    def report(code, doc, line, token, message):
        if not allowed(allow, doc, token, used):
            findings.append(Finding(code, doc, line, message, token))

    for doc in docs:
        lines = strip_fences(read(root, doc))
        for n, line in enumerate(lines, start=1):
            for token in BACKTICK_RE.findall(line):
                token = token.strip()
                bare = token.split('(', 1)[0]
                if KEBAB_RE.match(bare) and bare.split('-', 1)[0] in verbs and bare not in tools:
                    report('TOOL_UNKNOWN', doc, n, bare, '`%s` looks like a tool name but no tool is registered as %s'
                           % (bare, bare))
                exists = resolve_path(token, doc, files, folders)
                if exists is False:
                    report('PATH_MISSING', doc, n, token, '`%s` is not a tracked file or folder' % token)
            table = TABLE_TOOL_RE.match(line)
            if table:
                sectioned.add(table.group(1))  # a tool table row documents the tool, as a section does
            plain = BACKTICK_RE.sub('', line)
            for href in MD_LINK_RE.findall(plain):
                if EXTERNAL_RE.match(href) or doc.startswith(USER_PAGES):
                    continue  # wiki pages link by page name: the user-page check resolves those
                target = os.path.normpath(os.path.join(os.path.dirname(doc), href.split('#')[0])).replace('\\', '/')
                if target not in files and target not in folders:
                    report('LINK_BROKEN', doc, n, href, 'link %s resolves to nothing (%s)' % (href, target))
            for number in COUNT_RE.findall(plain):
                if int(number) not in counts and int(number) > 1:
                    report('COUNT_DRIFT', doc, n, '%s tools' % number, '"%s tools": the reference counts %s'
                           % (number, ', '.join(str(c) for c in sorted(counts))))
        for tool, start, end in tool_sections(lines):
            if tool not in tools and tool not in meta:
                if tool.split('-', 1)[0] in verbs:
                    report('TOOL_UNKNOWN', doc, start + 1, tool,
                           'heading `%s` looks like a tool name but no tool is registered as %s' % (tool, tool))
                continue
            sectioned.add(tool)
            stats['sections'] += 1
            schema = (tools.get(tool) or meta.get(tool))['inputSchema'].get('properties', {})
            for argument, i in section_arguments(lines, start, end):
                stats['arguments'] += 1
                if argument not in schema:
                    report('ARG_UNKNOWN', doc, i + 1, '%s.%s' % (tool, argument),
                           '%s has no argument `%s` (its inputSchema: %s)'
                           % (tool, argument, ', '.join(schema) or 'none'))

    listing = read(root, TOOL_LIST_DOC) if TOOL_LIST_DOC in files else ''
    named = set(BACKTICK_RE.findall(listing))
    for name in sorted(tools):
        if name not in named:
            report('TOOL_LIST_INCOMPLETE', TOOL_LIST_DOC, 0, name, '%s is registered but not named here' % name)
        if name not in sectioned:
            report('TOOL_LIST_INCOMPLETE', '', 0, name, '%s has no `### %s` section or tool table row in any doc'
                   % (name, name))
    return docs


# ---- 6. user pages ----------------------------------------------------------------------------------------------

def check_wiki(root, wiki, findings):
    """Links between the user pages: a GitHub wiki links pages by name from any folder, or by relative path"""
    spec = importlib.util.spec_from_file_location('docs_check', os.path.join(root, 'tools', 'docs_check.py'))
    docs_check = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(docs_check)
    cfg = dict(docs_check.DEFAULTS)
    cfg.update({'target': 'wiki:github', 'docsRoot': os.path.abspath(wiki), 'nav': 'none', 'summary': False,
                'pageBudget': 0, 'images': {}, 'roots': []})
    count = 0
    for f in docs_check.check_tree(docs_check.Path(root), cfg):
        if f.code in ('LINK_BROKEN', 'IMAGE_MISSING', 'DOCS_ROOT_MISSING'):
            findings.append(Finding('WIKI_' + f.code, f.path, f.line, f.message))  # repo-relative, else absolute
            count += 1
    return count


def lint(root=REPO_ROOT, wiki=None):
    findings, used = [], set()
    allow = load_allowlist(root, findings)
    files = tracked_files(root)
    ref = json.loads(read(root, REFERENCE_JSON))
    registered = check_reference(root, files, ref, findings)
    stats = {'sections': 0, 'arguments': 0}
    docs = check_docs(root, files, ref, allow, used, findings, stats)
    wiki = wiki or os.path.join(root, 'wiki')
    check_wiki(root, wiki, findings)
    for key, n in sorted(allow.items(), key=lambda kv: kv[1]):
        if key not in used:
            findings.append(Finding('ALLOWLIST_UNUSED', ALLOWLIST, n, '%s matches nothing; remove it'
                                    % ('::'.join(key) if key[0] else key[1])))
    return findings, dict(stats, tools=registered, docs=len(docs), wiki=os.path.abspath(wiki))


def main(argv=None):
    ap = argparse.ArgumentParser(prog='docs_lint', description=__doc__.split('\n\n')[0])
    ap.add_argument('--root', default=REPO_ROOT, help='the repository root (default: this checkout)')
    ap.add_argument('--wiki', help='folder of user pages to link-check (default: wiki/ in the repository)')
    ap.add_argument('--json', action='store_true', help='print the findings as one JSON object')
    args = ap.parse_args(argv)
    findings, checked = lint(os.path.abspath(args.root), args.wiki)
    if args.json:
        print(json.dumps({'findings': [f.as_dict() for f in findings], 'checked': checked}, indent=1))
    else:
        github = os.environ.get('GITHUB_ACTIONS') == 'true'
        for f in findings:
            print(str(f))
            if github and f.path and not os.path.isabs(f.path):
                print('::error file=%s,line=%d::%s %s' % (f.path, max(f.line, 1), f.code, f.message))
        print('docs_lint: %d finding(s); %d registered tools, %d docs, %d tool sections naming %d arguments, user pages '
              'in %s' % (len(findings), checked['tools'], checked['docs'], checked['sections'], checked['arguments'],
                         checked['wiki']))
    return 1 if findings else 0


if __name__ == '__main__':
    sys.exit(main())
