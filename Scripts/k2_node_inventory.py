"""k2_node_inventory: list every UK2Node subclass in the engine sources and how NodeToCode would type it.

Stdlib only, read-only; run with `python -I Scripts/k2_node_inventory.py` from anywhere (card 0246). It never writes
under the engine folders.

    python -I Scripts/k2_node_inventory.py --engine 5.6=G:/UE/UE_5.6 --engine 5.7=G:/UE/UE_5.7 \
        --engine 5.8=G:/UE/UE_5.8 [--json out.json] [--markdown out.md]

For each engine it reads the headers under Engine/Source and Engine/Plugins, collects every `class U<Name> : public
U<Base>` declaration that follows a UCLASS (with the UCLASS specifiers, so Abstract and deprecated classes can be told
apart), and walks the tree under UK2Node. Each K2 class is then typed the way FN2CNodeTypeRegistry::GetNodeType types
it (card 0234): the nearest ancestor (the class itself first) whose name without the `K2Node_` prefix is one of the
registry's class-name mappings wins; with none, Event when the class or an ancestor names IK2Node_EventNodeInterface in
its base list, else Unknown. The mappings are read from Source/Private/Utils/N2CNodeTypeRegistry.cpp
(`RegisterNodeType(FName(TEXT("...")), ...)`), so the script follows the registry without an editor.

Verdict per class:
  own       the class's own name is mapped (a dedicated N2C type)
  inherited mapped only through an ancestor (for example a CallFunction subclass typed CallFunction): the type is
            right for the family but the node's own semantics travel only in its title, pins and member fields
  event     no mapping, implements IK2Node_EventNodeInterface: typed Event
  unknown   no mapping at all: typed Unknown with its class (card 0445)

Header parsing is textual: a class declared by a macro, or one whose base is a template, is missed (rare for K2
nodes). The output lists the classes per verdict, per family (the module or plugin that declares them) and the
differences between engines.
"""
import argparse
import json
import os
import re
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
REGISTRY = os.path.join(REPO, 'Source', 'Private', 'Utils', 'N2CNodeTypeRegistry.cpp')

UCLASS_DECL = re.compile(
    r'UCLASS\s*\((?P<spec>[^)]*(?:\([^)]*\)[^)]*)*)\)\s*'  # UCLASS(...) with one level of nested parentheses
    r'(?:\r?\n|\s)*(?:(?://[^\n]*\n|/\*.*?\*/)\s*)*'          # comments between UCLASS and class
    r'class\s+(?:\w+_API\s+)?(?:UE_DEPRECATED\([^)]*\)\s*)?(?P<name>U\w+)\s*(?:final\s*)?'
    r':\s*(?P<bases>[^{;]*)\{', re.S)
BASE = re.compile(r'public\s+((?:[UI])\w+)')
REGISTER = re.compile(r'RegisterNodeType\(FName\(TEXT\("(\w+)"\)\),\s*EN2CNodeType::(\w+)\)')


def read_mappings(path=REGISTRY):
    with open(path, encoding='utf-8', errors='replace') as f:
        return dict(REGISTER.findall(f.read()))


def family_of(rel):
    """The module or plugin that declares a header: Plugins/<...>/<Plugin> or Source/<Kind>/<Module>"""
    parts = rel.replace('\\', '/').split('/')
    if parts[0] == 'Plugins':
        # Plugins/[Category/...]/<Plugin>/Source/<Module>/...
        if 'Source' in parts:
            i = parts.index('Source')
            return 'Plugin ' + parts[i - 1] + ' / ' + (parts[i + 1] if i + 1 < len(parts) else '?')
        return 'Plugin ' + parts[-2]
    if parts[0] == 'Source' and len(parts) > 2:
        return parts[1] + ' / ' + parts[2]
    return parts[0]


def scan_engine(root):
    """{class name: {base, interfaces, spec, header, family}} for every UCLASS under Engine/Source and Engine/Plugins"""
    engine = os.path.join(root, 'Engine')
    classes = {}
    for top in ('Source', 'Plugins'):
        for dirpath, dirnames, filenames in os.walk(os.path.join(engine, top)):
            dirnames[:] = [d for d in dirnames if d not in ('ThirdParty', 'Intermediate', 'Binaries', 'Content')]
            for name in filenames:
                if not name.endswith('.h'):
                    continue
                path = os.path.join(dirpath, name)
                try:
                    with open(path, encoding='utf-8', errors='replace') as f:
                        text = f.read()
                except OSError:
                    continue
                if 'UCLASS' not in text:
                    continue
                rel = os.path.relpath(path, engine).replace('\\', '/')
                for m in UCLASS_DECL.finditer(text):
                    bases = BASE.findall(m.group('bases'))
                    if not bases or not bases[0].startswith('U'):
                        continue
                    classes[m.group('name')] = {
                        'base': bases[0],
                        'interfaces': [b for b in bases[1:] if b.startswith('I')],
                        'spec': ' '.join(m.group('spec').split()),
                        'header': rel,
                        'family': family_of(rel),
                    }
    return classes


def k2_classes(classes):
    children = defaultdict(list)
    for name, info in classes.items():
        children[info['base']].append(name)
    out, stack = [], ['UK2Node']
    while stack:
        cur = stack.pop()
        for child in children.get(cur, []):
            out.append(child)
            stack.append(child)
    return sorted(set(out))


def chain(name, classes):
    seq = []
    while name and name not in seq:
        seq.append(name)
        name = classes.get(name, {}).get('base')
    return seq


def resolve(name, classes, mappings):
    """(verdict, n2c type, mapped ancestor) the way FN2CNodeTypeRegistry::GetNodeType resolves the class"""
    seq = chain(name, classes)
    for i, cls in enumerate(seq):
        short = cls[1:]
        short = short[len('K2Node_'):] if short.startswith('K2Node_') else short
        if short in mappings:
            return ('own' if i == 0 else 'inherited'), mappings[short], cls
    if any('IK2Node_EventNodeInterface' in classes.get(c, {}).get('interfaces', []) for c in seq):
        return 'event', 'Event', None
    return 'unknown', 'Unknown', None


def flags(spec):
    s = spec.lower()
    out = []
    if 'abstract' in s:
        out.append('abstract')
    if 'deprecated' in s:
        out.append('deprecated')
    return out


def inventory(root, mappings):
    classes = scan_engine(root)
    rows = {}
    for name in k2_classes(classes):
        info = classes[name]
        verdict, n2c, via = resolve(name, classes, mappings)
        rows[name] = {'verdict': verdict, 'type': n2c, 'via': via, 'family': info['family'], 'header': info['header'],
                      'flags': flags(info['spec']), 'chain': chain(name, classes)[1:]}
    return rows


HEADER = '''# K2 node inventory: every UK2Node subclass in UE {versions} and the N2C type capture gives it

Generated by `Scripts/k2_node_inventory.py` (card 0246) from the engine headers and the class-name mappings in
`Source/Private/Utils/N2CNodeTypeRegistry.cpp` ({mappings} mappings); do not edit by hand. Regenerate with:

    python -I Scripts/k2_node_inventory.py --engine 5.6=<UE_5.6> --engine 5.7=<UE_5.7> --engine 5.8=<UE_5.8> \\
        --markdown Context/translation-pipeline/k2-node-inventory.md

Verdicts: **own** (the class is mapped), **inherited** (typed by its nearest mapped base class, so the node's own
meaning travels only in its title, pins and member fields), **event** (no mapping; implements
IK2Node_EventNodeInterface, typed Event), **unknown** (typed Unknown with its class name). How these feed decision
0247: [graph-type-coverage.md](graph-type-coverage.md).

'''


def to_markdown(result):
    versions = list(result['engines'])
    lines = [HEADER.format(versions=', '.join(versions), mappings=result['registry_mappings']).rstrip(), '']
    lines.append('| Engine | K2 classes | own | inherited | event | unknown | of which abstract/deprecated |')
    lines.append('|---|---|---|---|---|---|---|')
    for v in versions:
        rows = result['engines'][v]
        count = defaultdict(int)
        for r in rows.values():
            count[r['verdict']] += 1
        skipped = sum(1 for r in rows.values() if r['flags'])
        lines.append('| %s | %d | %d | %d | %d | %d | %d |' % (v, len(rows), count['own'], count['inherited'],
                                                             count['event'], count['unknown'], skipped))
    last = versions[-1]
    rows = result['engines'][last]
    lines.append('')
    lines.append('Unknown and event classes on %s by family (abstract and deprecated classes left out):' % last)
    lines.append('')
    lines.append('| Family | Verdict | Classes |')
    lines.append('|---|---|---|')
    fam = defaultdict(list)
    for name, r in rows.items():
        if r['verdict'] in ('unknown', 'event') and not r['flags']:
            fam[(r['family'], r['verdict'])].append(name[1:])
    for (family, verdict), names in sorted(fam.items()):
        lines.append('| %s | %s | %s |' % (family, verdict, ', '.join(sorted(names))))
    lines.append('')
    lines.append('Inherited-only classes on %s, by the type they get:' % last)
    lines.append('')
    lines.append('| N2C type (via) | Classes |')
    lines.append('|---|---|')
    inh = defaultdict(list)
    for name, r in rows.items():
        if r['verdict'] == 'inherited' and not r['flags']:
            inh['%s (%s)' % (r['type'], r['via'][1:])].append(name[1:])
    for key, names in sorted(inh.items()):
        lines.append('| %s | %s |' % (key, ', '.join(sorted(names))))
    if len(versions) > 1:
        lines.append('')
        lines.append('Differences between engines:')
        lines.append('')
        lines.append('| Class | ' + ' | '.join(versions) + ' |')
        lines.append('|---|' + '---|' * len(versions))
        names = set()
        for v in versions:
            names |= set(result['engines'][v])
        for name in sorted(names):
            cells = [result['engines'][v].get(name) for v in versions]
            keys = [(c['verdict'], c['type']) if c else None for c in cells]
            if len(set(keys)) > 1:
                lines.append('| %s | %s |' % (name[1:], ' | '.join(
                    ('%s %s' % (c['verdict'], c['type'])) if c else 'absent' for c in cells)))
    return '\n'.join(lines) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--engine', action='append', required=True, help='<version>=<engine root>, repeatable')
    ap.add_argument('--json', help='write the full inventory here')
    ap.add_argument('--markdown', help='write the summary tables here')
    args = ap.parse_args()
    mappings = read_mappings()
    result = {'registry_mappings': len(mappings), 'engines': {}}
    for spec in args.engine:
        version, _, root = spec.partition('=')
        if not os.path.isdir(os.path.join(root, 'Engine')):
            print('no Engine folder under %s' % root, file=sys.stderr)
            return 2
        result['engines'][version] = inventory(root, mappings)
    md = to_markdown(result)
    if args.json:
        with open(args.json, 'w', encoding='utf-8') as f:
            json.dump(result, f, indent=1, sort_keys=True)
    if args.markdown:
        with open(args.markdown, 'w', encoding='utf-8') as f:
            f.write(md)
    if not args.json and not args.markdown:
        sys.stdout.write(md)
    else:
        for v, rows in result['engines'].items():
            print('%s: %d K2 classes, %d unknown' % (v, len(rows), sum(1 for r in rows.values()
                                                                      if r['verdict'] == 'unknown')))
    return 0


if __name__ == '__main__':
    sys.exit(main())
