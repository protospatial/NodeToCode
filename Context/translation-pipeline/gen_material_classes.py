"""gen_material_classes: inventory the material expression and material asset classes of UE 5.6, 5.7 and 5.8.

Stdlib only, read-only; run with `python -I` from anywhere (card 0446, epic 0442). It reads the engine sources and
never writes under the engine folders. It needs no editor: do not run DumpMaterialExpressionsCommandlet instead.

    python -I Context/translation-pipeline/gen_material_classes.py \
        --engine 5.6=G:/UE/UE_5.6 --engine 5.7=G:/UE/UE_5.7 --engine 5.8=G:/UE/UE_5.8 [--check] [--json out.json]

Without --check it (re)writes material-classes.md next to this script. With --check it writes nothing and exits 1
when the committed page differs from what the engine trees give, or when a guard probe or a handling rule no longer
matches the engines (2 for bad arguments). Output is sorted and has no dates, so a re-run on the same trees is
byte-identical.

What it reads, per engine:
- every header under Engine/Source and Engine/Plugins (ThirdParty, Intermediate, Binaries and Content skipped) for
  `UCLASS(...) class U<Name> : public U<Base>` declarations, the way Scripts/k2_node_inventory.py (card 0246) does;
  the classes under UMaterialExpression are the expression inventory, the classes under UMaterialInterface and
  UMaterialFunctionInterface (plus UMaterialParameterCollection) the asset inventory;
- the UCLASS specifiers for the flags: abstract, deprecated, experimental (`Experimental` or
  `DevelopmentStatus="Experimental"`), new-translator (`meta=(NewMaterialTranslator)`: hidden from the palette on 5.7
  and later unless r.Material.Translator.EnableNew is set, MaterialExpressionClasses.cpp), private (`meta=(Private)`,
  hidden from the palette), substrate (a Substrate class name or MaterialExpressionSubstrate.h);
- each class's own GetInputValueType, GetOutputValueType, GetInputType and GetOutputType: the declaration in the class
  body and the out-of-line body in the module's .cpp files, whitespace and comments normalised. "Value types differ"
  means one of these is not the same text on every engine that has the class. A change in a base class, in a helper
  the body calls, or in the MCT_ bits themselves is not seen here (the guard list covers the MCT_ bits).
- the GUARDS probes below (one file and regular expression per ENGINE_MINOR_VERSION guard, with the engines the
  pattern must match), which give the guard list its file:line references.

Proposed handling (input to cards 0449-0452; the in-code table of 0452 must agree with it):
  refused  deprecated classes, and the layer-system classes in REFUSED_ROOTS: kept as a node with its class name and
           a structured UNSUPPORTED warning; properties are not read and the assets they reference are not followed
  mapped   a hand mapping: the class or its nearest ancestor is in MAPPED_ROOTS, or the class overrides GetInputs,
           GetInputsView or GetInput (dynamic inputs, which reflection over FExpressionInput properties would miss)
  generic  the reflection fallback of 0451 (FExpressionInput properties as pins, edit properties as defaults)
  base     abstract: never placed in a graph, so it needs no handling of its own
"""
import argparse
import json
import os
import re
import sys
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_MD = os.path.join(HERE, 'material-classes.md')
SKIP_DIRS = ('ThirdParty', 'Intermediate', 'Binaries', 'Content', 'DerivedDataCache', 'Saved')

UCLASS_OPEN = re.compile(r'\bUCLASS\s*\(')
CLASS_DECL = re.compile(
    r'\s*(?:(?://[^\n]*\n|/\*.*?\*/)\s*)*'                  # comments between UCLASS(...) and class
    r'class\s+(?:\w+_API\s+)?(?P<uedep>UE_DEPRECATED\((?:[^()"]|"[^"]*")*\)\s*)?(?P<name>U\w+)\s*(?:final\s*)?'
    r':\s*(?P<bases>[^{;]*)\{', re.S)
BASE = re.compile(r'public\s+((?:[UI])\w+)')
VALUE_FUNCS = ('GetInputValueType', 'GetOutputValueType', 'GetInputType', 'GetOutputType')
DECL = re.compile(r'[^;{}\n]*\b(' + '|'.join(VALUE_FUNCS) + r')\s*\([^)]*\)[^;{]*(?:;|\{[^{}]*\})')
DYNAMIC_INPUTS = re.compile(r'\b(?:GetInputsView|GetInputs)\s*\(\s*\)\s*(?:const\s*)?override|'
                            r'\bGetInput\s*\(\s*int32\s+\w+\s*\)\s*(?:const\s*)?override')
CPP_DEF = re.compile(r'\b(U\w+)::(' + '|'.join(VALUE_FUNCS) + r')\s*\([^)]*\)\s*(?:const\s*)?\{')
COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)

ASSET_ROOTS = ('UMaterialInterface', 'UMaterialFunctionInterface')
ASSET_EXTRA = ('UMaterialParameterCollection',)

# Proposed handling. The keys are class names; a class takes the entry of its nearest ancestor (itself first).
REFUSED_ROOTS = {
    'UMaterialExpressionMaterialAttributeLayers': 'layer stack; its layers and blends are asset kinds 0443 refuses',
    'UMaterialExpressionMaterialLayerOutput': 'output of a Material Layer function, an asset kind 0443 refuses',
}
MAPPED_ROOTS = {
    'UMaterialExpressionComment': 'comment text, untrusted (0449, 0451)',
    'UMaterialExpressionReroute': 'reroute, traced through (0449)',
    'UMaterialExpressionNamedRerouteBase': 'named reroute, traced through (0449)',
    'UMaterialExpressionComposite': 'composite, passed through (0450)',
    'UMaterialExpressionPinBase': 'composite pin base, passed through (0450)',
    'UMaterialExpressionFunctionInput': 'function input boundary (0450)',
    'UMaterialExpressionFunctionOutput': 'function output, liveness boundary (0449)',
    'UMaterialExpressionCustomOutput': 'custom output, liveness boundary (0449)',
    'UMaterialExpressionMaterialFunctionCall': 'function call followed to the depth limit (0450)',
    'UMaterialExpressionStaticSwitch': 'static switch, both branches kept (0450)',
    'UMaterialExpressionStaticBool': 'static bool feeding static switches (0450)',
    'UMaterialExpressionParameter': 'parameter name, group and default (0449)',
    'UMaterialExpressionTextureBase': 'texture reference as a soft object path (0449, 0451)',
    'UMaterialExpressionCollectionParameter': 'MPC reference as a soft object path (0451)',
    'UMaterialExpressionCustom': 'Custom HLSL, inputs, defines and includes as untrusted text (0451)',
    'UMaterialExpressionMakeMaterialAttributes': 'attribute pins by attribute name (0450)',
    'UMaterialExpressionBreakMaterialAttributes': 'attribute pins by attribute name (0450)',
    'UMaterialExpressionSetMaterialAttributes': 'attribute pins by attribute GUID (0450)',
    'UMaterialExpressionGetMaterialAttributes': 'attribute pins by attribute GUID (0450)',
    'UMaterialExpressionSubstrateSlabBSDF': 'Slab into FrontMaterial captures without a fallback warning (0451)',
}

# Material asset classes and their outcome under owner decision 0443 (applied provisionally, its recommendation).
# Nearest listed ancestor wins; `exact` entries apply to that class only (capture takes exact classes, card 0449).
ASSET_OUTCOME = [
    ('UMaterial', True, 'captured (0449)'),
    ('UMaterial', False, 'refused: a UMaterial subclass (capture takes the exact class, 0449)'),
    ('UMaterialFunction', True, 'captured (0450)'),
    ('UMaterialFunctionMaterialLayer', False, 'refused: UNSUPPORTED_ASSET_TYPE (0443, 0450)'),
    ('UMaterialFunctionMaterialLayerBlend', False, 'refused: UNSUPPORTED_ASSET_TYPE (0443, 0450)'),
    ('UMaterialFunctionInstance', False, 'refused: UNSUPPORTED_ASSET_TYPE, no graph of its own (0443, 0450)'),
    ('UMaterialFunction', False, 'refused: a UMaterialFunction subclass (capture takes the exact class, 0450)'),
    ('UMaterialInstanceConstant', False, 'read-only summary: parent capture plus overrides, if 0443 keeps it (0470)'),
    ('UMaterialInstanceDynamic', False, 'refused: UNSUPPORTED_ASSET_TYPE (a dynamic instance; UMaterialInstanceDynamic '
                                        'itself is made at run time and never saved)'),
    ('UMaterialInterface', False, 'refused: UNSUPPORTED_ASSET_TYPE'),
    ('UMaterialFunctionInterface', False, 'refused: UNSUPPORTED_ASSET_TYPE'),
    ('UMaterialParameterCollection', False, 'refused as an asset; referenced by path from CollectionParameter (0451)'),
]

# One probe per ENGINE_MINOR_VERSION guard: (title, since, [(engine-relative file, regex, engines that must match)])
GUARDS = [
    ('UMaterialInterface::GetMaterialResource(ERHIFeatureLevel) is deprecated and final and returns NULL; the '
     'EShaderPlatform overload replaces it', '5.7', [
         ('Engine/Source/Runtime/Engine/Public/Materials/MaterialInterface.h',
          r'GetMaterialResource\(ERHIFeatureLevel::Type[^)]*\)\s*(?:const\s*)?\{', ('5.6',)),
         ('Engine/Source/Runtime/Engine/Public/Materials/MaterialInterface.h',
          r'GetMaterialResource\(ERHIFeatureLevel::Type[^)]*\)\s*(?:const\s*)?final', ('5.7', '5.8')),
     ]),
    ('FMaterialResource::SetMaterial takes EShaderPlatform instead of ERHIFeatureLevel', '5.7', [
        ('Engine/Source/Runtime/Engine/Public/MaterialShared.h',
         r'void SetMaterial\(UMaterial\* InMaterial, UMaterialInstance\* InInstance, ERHIFeatureLevel', ('5.6',)),
        ('Engine/Source/Runtime/Engine/Public/MaterialShared.h',
         r'void SetMaterial\(UMaterial\* InMaterial, UMaterialInstance\* InInstance, EShaderPlatform', ('5.7', '5.8')),
    ]),
    ('Materials/MaterialParameters.h exists and EMaterialParameterType has ParameterCollection (5.6: the enum is in '
     'MaterialTypes.h, without it)', '5.7', [
         ('Engine/Source/Runtime/Engine/Public/MaterialTypes.h', r'enum class EMaterialParameterType', ('5.6',)),
         ('Engine/Source/Runtime/Engine/Public/Materials/MaterialParameters.h', r'^\s*ParameterCollection,',
          ('5.7', '5.8')),
     ]),
    ('EMaterialValueType gains MCT_ bits 32-36 (Float3x3, Float4x4, LWCMatrix, MaterialCacheABuffer, Unexposed); '
     'the type no longer fits in 32 bits', '5.7', [
         ('Engine/Source/Runtime/Engine/Public/MaterialValueType.h', r'MCT_\w+\s*=\s*1ull\s*<<\s*3[2-6]\b',
          ('5.7', '5.8')),
     ]),
    ('UMaterialExpressionScalarParameter gains ControlType, Enumeration and EnumerationIndex', '5.7', [
        ('Engine/Source/Runtime/Engine/Public/Materials/MaterialExpressionScalarParameter.h',
         r'^\s*(?:EMaterialScalarParameterControlType ControlType|TSoftObjectPtr<UObject> Enumeration;|'
         r'uint8 EnumerationIndex)', ('5.7', '5.8')),
    ]),
    ('UMaterialEditingLibrary::RecompileMaterial returns `TArray<FString>` (was void)', '5.8', [
        ('Engine/Source/Editor/MaterialEditor/Public/MaterialEditingLibrary.h',
         r'static\s+\w*\s*void RecompileMaterial\(UMaterial\* Material\)', ('5.6', '5.7')),
        ('Engine/Source/Editor/MaterialEditor/Public/MaterialEditingLibrary.h',
         r'TArray<FString> RecompileMaterial\(UMaterial\* Material\)', ('5.8',)),
    ]),
    ('UMaterialGraphSchema::PC_Void and PC_ValueType are removed', '5.8', [
        ('Engine/Source/Editor/UnrealEd/Classes/MaterialGraph/MaterialGraphSchema.h',
         r'static const FName (?:PC_Void|PC_ValueType);', ('5.6', '5.7')),
    ]),
    ('UMaterialGraph::GetUnusedMaterialExpressions exists (5.8 only)', '5.8', [
        ('Engine/Source/Editor/UnrealEd/Classes/MaterialGraph/MaterialGraph.h',
         r'static void GetUnusedMaterialExpressions\(', ('5.8',)),
    ]),
]


_PLUGIN_NAME = {}


def plugin_of(root, rel_dir):
    """The name of the .uplugin in the nearest folder at or above rel_dir that has one, or None"""
    parts = rel_dir.split('/')
    for i in range(len(parts), 1, -1):
        d = '/'.join(parts[:i])
        key = (root, d)
        if key not in _PLUGIN_NAME:
            try:
                names = sorted(n for n in os.listdir(os.path.join(root, d)) if n.endswith('.uplugin'))
            except OSError:
                names = []
            _PLUGIN_NAME[key] = names[0][:-len('.uplugin')] if names else None
        if _PLUGIN_NAME[key]:
            return _PLUGIN_NAME[key]
    return None


def family_of(root, rel):
    """The module or plugin that declares a header: Plugin <Plugin> / <Module>, or <Kind> / <Module>"""
    parts = rel.split('/')[1:]  # drop Engine/
    if parts[0] == 'Plugins':
        module = parts[parts.index('Source') + 1] if 'Source' in parts[:-1] else '?'
        plugin = plugin_of(root, '/'.join(rel.split('/')[:-1])) or parts[-2]
        if module in ('Public', 'Private', 'Classes', 'Internal'):  # Source/Private: a single-module plugin
            module = plugin
        return 'Plugin ' + plugin + ' / ' + module
    if parts[0] == 'Source' and len(parts) > 2:
        return parts[1] + ' / ' + parts[2]
    return parts[0]


def uclass_decls(text):
    """(offset, specifiers, class match) for each UCLASS(...) followed by a class declaration. The specifiers may
    nest parentheses to any depth and hold string literals with parentheses (DisplayName = "A (B)")."""
    for m in UCLASS_OPEN.finditer(text):
        i, depth, in_str = m.end(), 1, False
        while i < len(text) and depth:
            c = text[i]
            if in_str:
                if c == '\\':
                    i += 1
                elif c == '"':
                    in_str = False
            elif c == '"':
                in_str = True
            elif c == '(':
                depth += 1
            elif c == ')':
                depth -= 1
            i += 1
        decl = CLASS_DECL.match(text, i)
        if decl:
            yield m.start(), text[m.end():i - 1], decl


def module_root(rel):
    """The folder holding a header's Public/Classes/Private/Internal folders (the module root)"""
    parts = rel.split('/')
    for i, p in enumerate(parts):
        if p in ('Public', 'Classes', 'Private', 'Internal'):
            return '/'.join(parts[:i])
    return '/'.join(parts[:-1])


def class_body(text, start):
    """The text of a class body whose `{` is at text[start - 1], up to its matching `}`"""
    depth, i = 1, start
    while i < len(text) and depth:
        c = text[i]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        i += 1
    return text[start:i - 1]


def norm(code):
    return ' '.join(COMMENT.sub(' ', code).split())


def walk(top, ext):
    for dirpath, dirnames, filenames in os.walk(top):
        dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS)
        for name in sorted(filenames):
            if name.endswith(ext):
                yield os.path.join(dirpath, name)


def read(path):
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            return f.read()
    except OSError:
        return ''


def scan_engine(root):
    """{class: info} for every UCLASS under Engine/Source and Engine/Plugins"""
    classes = {}
    for top in ('Source', 'Plugins'):
        for path in walk(os.path.join(root, 'Engine', top), '.h'):
            text = read(path)
            if 'UCLASS' not in text:
                continue
            rel = os.path.relpath(path, root).replace('\\', '/')
            for start, spec, m in uclass_decls(text):
                bases = BASE.findall(m.group('bases'))
                if not bases or not bases[0].startswith('U') or m.group('name') in classes:
                    continue
                body = class_body(text, m.end())
                classes[m.group('name')] = {
                    'base': bases[0],
                    'spec': ' '.join(spec.split()) + (' UE_DEPRECATED' if m.group('uedep') else ''),
                    'header': rel,
                    'line': text.count('\n', 0, start) + 1,
                    'family': family_of(root, rel),
                    'decls': sorted(norm(d.group(0)) for d in DECL.finditer(COMMENT.sub(' ', body))),
                    'dynamic_inputs': bool(DYNAMIC_INPUTS.search(COMMENT.sub(' ', body))),
                }
    return classes


def descendants(classes, roots):
    children = defaultdict(list)
    for name, info in classes.items():
        children[info['base']].append(name)
    out, stack = set(), list(roots)
    while stack:
        for child in children.get(stack.pop(), []):
            if child not in out:
                out.add(child)
                stack.append(child)
    return out


def chain(name, classes):
    seq = []
    while name and name not in seq:
        seq.append(name)
        name = classes.get(name, {}).get('base')
    return seq


def cpp_value_bodies(root, names, headers):
    """{(class, function): normalised body} for the value-type functions defined in the classes' module .cpp files"""
    out = {}
    for mod in sorted({module_root(h) for h in headers}):
        for path in walk(os.path.join(root, mod), '.cpp'):
            text = read(path)
            if 'Type(' not in text:
                continue
            for m in CPP_DEF.finditer(text):
                if m.group(1) in names:
                    out[(m.group(1), m.group(2))] = norm(m.group(0) + class_body(text, m.end()) + '}')
    return out


def flags_of(info, name):
    s = info['spec'].lower().replace(' ', '')
    out = []
    if re.search(r'(^|,)abstract(,|$)', s):
        out.append('abstract')
    if 'deprecated' in s or name.endswith('_DEPRECATED'):
        out.append('deprecated')
    if re.search(r'(^|,)experimental(,|$|\))', s) or 'developmentstatus="experimental"' in s:
        out.append('experimental')
    if 'newmaterialtranslator' in s:
        out.append('new-translator')
    if re.search(r'meta=\([^)]*\bprivate\b', s):
        out.append('private')
    if 'Substrate' in name or info['header'].endswith('/MaterialExpressionSubstrate.h'):
        out.append('substrate')
    return out


def scan(version, root):
    classes = scan_engine(root)
    expr = descendants(classes, ['UMaterialExpression'])
    assets = descendants(classes, ASSET_ROOTS) | {c for c in ASSET_ROOTS + ASSET_EXTRA if c in classes}
    assets |= descendants(classes, ASSET_EXTRA)
    bodies = cpp_value_bodies(root, expr, [classes[n]['header'] for n in expr])
    rows = {}
    for name in expr:
        info = classes[name]
        rows[name] = {
            'family': info['family'], 'header': info['header'], 'line': info['line'], 'flags': flags_of(info, name),
            'chain': chain(name, classes)[1:], 'dynamic_inputs': info['dynamic_inputs'],
            'value_types': {'decls': info['decls'],
                            'bodies': {f: bodies[(name, f)] for f in VALUE_FUNCS if (name, f) in bodies}},
        }
    asset_rows = {n: {'family': classes[n]['family'], 'header': classes[n]['header'], 'line': classes[n]['line'],
                      'flags': flags_of(classes[n], n), 'chain': chain(n, classes)[1:]} for n in assets}
    guards = []
    for title, since, probes in GUARDS:
        hits = []
        for rel, pattern, expect in probes:
            text = read(os.path.join(root, rel))
            lines = [text.count('\n', 0, m.start()) + 1 for m in re.finditer(pattern, text, re.M)]
            hits.append({'file': rel, 'lines': lines, 'expected': version in expect})
        guards.append(hits)
    return {'expressions': rows, 'assets': asset_rows, 'guards': guards}


def handling(name, per_engine):
    """(handling, reason) from the class's rows on every engine that has it"""
    any_row = next(iter(per_engine.values()))
    flags = set().union(*(set(r['flags']) for r in per_engine.values()))
    lineage = [name] + any_row['chain']
    for cls in lineage:
        if cls in REFUSED_ROOTS:
            return 'refused', REFUSED_ROOTS[cls] + ('' if cls == name else ' (via %s)' % short(cls))
    if 'deprecated' in flags:
        return 'refused', 'deprecated class'
    if 'abstract' in flags and all('abstract' in r['flags'] for r in per_engine.values()):
        return 'base', 'abstract'
    for cls in lineage:
        if cls in MAPPED_ROOTS:
            return 'mapped', MAPPED_ROOTS[cls] + ('' if cls == name else ' (via %s)' % short(cls))
    dyn = [v for v, r in per_engine.items() if r['dynamic_inputs']]
    if dyn:
        return 'mapped', 'dynamic inputs: overrides GetInputs/GetInput' + (
            '' if len(dyn) == len(per_engine) else ' (%s)' % ', '.join(dyn))
    return 'generic', ''


def short(name):
    name = name[1:]
    return name[len('MaterialExpression'):] if name.startswith('MaterialExpression') else name


def value_type_diff(per_engine):
    if len(per_engine) < 2:
        return 'n/a'
    differ = []
    for f in VALUE_FUNCS:
        seen = set()
        for r in per_engine.values():
            decl = tuple(d for d in r['value_types']['decls'] if re.search(r'\b%s\b' % f, d))
            seen.add((decl, r['value_types']['bodies'].get(f)))
        if len(seen) > 1:
            differ.append(f)
    return ', '.join(differ) if differ else 'no'


def flag_cell(per_engine, versions):
    out = []
    allf = sorted(set().union(*(set(r['flags']) for r in per_engine.values())))
    for f in allf:
        has = [v for v in versions if v in per_engine and f in per_engine[v]['flags']]
        out.append(f if len(has) == len(per_engine) else '%s (%s)' % (f, ', '.join(has)))
    return ', '.join(out)


def asset_outcome(name, row):
    lineage = [name] + row['chain']
    for i, cls in enumerate(lineage):
        for key, exact, outcome in ASSET_OUTCOME:
            if key == cls and (not exact or i == 0):
                return outcome
    return 'refused: UNSUPPORTED_ASSET_TYPE'


HEADER = '''# Material classes: expressions, assets and engine guards on UE {versions}

Generated by `Context/translation-pipeline/gen_material_classes.py` (card 0446, epic 0442) from the engine sources
only; do not edit by hand. Regenerate, or check that the engines and this page agree (exit 1 when they do not):

    python -I Context/translation-pipeline/gen_material_classes.py --engine 5.6=<UE_5.6> --engine 5.7=<UE_5.7> \\
        --engine 5.8=<UE_5.8> [--check]

The K2 half of the inventory is [k2-node-inventory.md](k2-node-inventory.md); the graph-type evidence for decision
0247 is [graph-type-coverage.md](graph-type-coverage.md) (row 22). The asset outcomes and the proposed handling apply
owner decision 0443's recommendation provisionally: when 0443 is recorded, change `ASSET_OUTCOME`, `REFUSED_ROOTS` or
`MAPPED_ROOTS` in the script and regenerate. Cards 0449-0451 implement the handling and 0452's coverage spec compares
its in-code table with this page.

Handling: **mapped** (a hand mapping in the capture: the class or its nearest ancestor is in the script's
`MAPPED_ROOTS`, or it overrides GetInputs/GetInput, so reflection over its FExpressionInput properties would miss
pins), **generic** (the reflection fallback of 0451, with a structured warning), **refused** (deprecated, or part of
the material layer system: kept as a node with its class name and an UNSUPPORTED warning; its properties are not read
and the assets it references are not followed), **base** (abstract, never placed in a graph). Flags come from the
UCLASS specifiers: **new-translator** classes (`meta=(NewMaterialTranslator)`) are hidden from the palette from 5.7
unless `r.Material.Translator.EnableNew` is set, **private** ones always. "Value types differ" names the
GetInputValueType, GetOutputValueType, GetInputType or GetOutputType overrides of the class itself whose declaration
or out-of-line body is not the same on every engine that has the class (changes in a base class are not seen).
'''


def to_markdown(result):
    versions = list(result['engines'])
    lines = [HEADER.format(versions=', '.join(versions)).rstrip(), '']
    names = sorted(set().union(*(set(e['expressions']) for e in result['engines'].values())))
    per = {n: {v: result['engines'][v]['expressions'][n] for v in versions if n in result['engines'][v]['expressions']}
           for n in names}
    rows = {n: handling(n, per[n]) for n in names}

    lines += ['## Counts', '']
    lines.append('| Engine | Expression classes | not abstract | mapped | generic | refused | new-translator | '
                 'experimental | substrate |')
    lines.append('|---|---|---|---|---|---|---|---|---|')
    for v in versions:
        here = [n for n in names if v in per[n]]
        fl = lambda f: sum(1 for n in here if f in per[n][v]['flags'])
        hd = lambda h: sum(1 for n in here if rows[n][0] == h)
        lines.append('| %s | %d | %d | %d | %d | %d | %d | %d | %d |' % (
            v, len(here), len(here) - fl('abstract'), hd('mapped'), hd('generic'), hd('refused'),
            fl('new-translator'), fl('experimental'), fl('substrate')))
    lines.append('')
    lines.append('Classes are counted from UCLASS declarations, not headers (one header can declare several, for '
                 'example MaterialExpressionSubstrate.h).')
    if len(versions) > 1:
        lines += ['', '## Changes between engines', '']
        lines.append('| Class | Module | ' + ' | '.join(versions) + ' |')
        lines.append('|---|---|' + '---|' * len(versions))
        for n in names:
            if len(per[n]) != len(versions):
                fam = next(iter(per[n].values()))['family']
                lines.append('| %s | %s | %s |' % (short(n), fam, ' | '.join(
                    'yes' if v in per[n] else '-' for v in versions)))
        fc = [n for n in names if len({tuple(r['flags']) for r in per[n].values()}) > 1]
        lines += ['', 'Flags that change between engines: %d classes (%s).' % (
            len(fc), ', '.join('%s: %s' % (short(n), flag_cell(per[n], versions)) for n in fc) or 'none')]
        vt = [n for n in names if value_type_diff(per[n]) not in ('no', 'n/a')]
        lines += ['', 'Value types that differ between engines: %d classes (%s).' % (
            len(vt), ', '.join(short(n) for n in vt) or 'none')]

    lines += ['', '## Expression classes', '']
    lines.append('| Class | Module | ' + ' | '.join(versions) + ' | Flags | Handling | Why | Value types differ |')
    lines.append('|---|---|' + '---|' * len(versions) + '---|---|---|---|')
    for n in names:
        fam = next(iter(per[n].values()))['family']
        h, why = rows[n]
        lines.append('| %s | %s | %s | %s | %s | %s | %s |' % (
            short(n), fam, ' | '.join('yes' if v in per[n] else '-' for v in versions), flag_cell(per[n], versions),
            h, why, value_type_diff(per[n])))

    lines += ['', '## Material asset classes', '']
    lines.append('Outcome per decision 0443 (its recommendation, applied provisionally) and card 0470. Capture '
                 'checks the exact class, so a subclass of a captured class is refused unless listed.')
    lines.append('')
    anames = sorted(set().union(*(set(e['assets']) for e in result['engines'].values())))
    lines.append('| Class | Base | Module | ' + ' | '.join(versions) + ' | Flags | Outcome |')
    lines.append('|---|---|---|' + '---|' * len(versions) + '---|---|')
    for n in anames:
        pe = {v: result['engines'][v]['assets'][n] for v in versions if n in result['engines'][v]['assets']}
        r = next(iter(pe.values()))
        out = 'n/a (abstract)' if 'abstract' in r['flags'] else asset_outcome(n, r)
        lines.append('| %s | %s | %s | %s | %s | %s |' % (
            n, (r['chain'] or ['UObject'])[0], r['family'],
            ' | '.join('yes' if v in pe else '-' for v in versions), flag_cell(pe, versions), out))

    lines += ['', '## ENGINE_MINOR_VERSION guards', '']
    lines.append('What material capture code must guard. Each line is found by a probe in the script (`GUARDS`), which '
                 'fails the check when the pattern stops matching where it should, or starts matching where it '
                 'should not.')
    lines.append('')
    lines.append('| Since | Change | Where (file:line per engine) |')
    lines.append('|---|---|---|')
    for gi, (title, since, probes) in enumerate(GUARDS):
        cells = []
        for pi, (rel, _, _) in enumerate(probes):
            parts = []
            for v in versions:
                hit = result['engines'][v]['guards'][gi][pi]
                if hit['lines']:
                    parts.append('%s %s' % (v, ','.join(str(x) for x in hit['lines'])))
            cells.append('%s: %s' % (rel, '; '.join(parts) or 'none'))
        lines.append('| %s | %s | %s |' % (since, title, '<br>'.join(cells)))
    return '\n'.join(lines) + '\n'


def problems(result):
    """Rules that no longer match the engines: guards, handling roots, asset outcome classes"""
    out = []
    versions = list(result['engines'])
    for gi, (title, since, probes) in enumerate(GUARDS):
        for pi, (rel, pattern, expect) in enumerate(probes):
            for v in versions:
                hit = result['engines'][v]['guards'][gi][pi]
                if bool(hit['lines']) != hit['expected']:
                    out.append('guard "%s": %s on %s %s' % (title[:60], rel, v,
                                                              'no longer matches' if hit['expected'] else 'now matches'))
    every = set().union(*(set(e['expressions']) for e in result['engines'].values()))
    for root in list(REFUSED_ROOTS) + list(MAPPED_ROOTS):
        if root not in every:
            out.append('handling root %s is not a material expression class on any engine' % root)
    assets = set().union(*(set(e['assets']) for e in result['engines'].values()))
    for key, _, _ in ASSET_OUTCOME:
        if key not in assets:
            out.append('asset outcome class %s is not a material asset class on any engine' % key)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--engine', action='append', required=True, help='<version>=<engine root>, repeatable, in order')
    ap.add_argument('--check', action='store_true', help='write nothing; exit 1 if the page or a rule is out of date')
    ap.add_argument('--json', help='also write the full per-engine inventory here')
    ap.add_argument('--out', default=OUT_MD, help='the page to write or check (default: material-classes.md here)')
    args = ap.parse_args()
    specs = []
    for spec in args.engine:
        version, _, root = spec.partition('=')
        if not os.path.isdir(os.path.join(root, 'Engine', 'Source')):
            print('no Engine/Source folder under %s' % root, file=sys.stderr)
            return 2
        specs.append((version, root))
    with ThreadPoolExecutor(max_workers=len(specs)) as pool:
        scanned = list(pool.map(lambda s: scan(*s), specs))
    result = {'engines': {v: r for (v, _), r in zip(specs, scanned)}}
    md = to_markdown(result)
    bad = problems(result)
    for p in bad:
        print('RULE: ' + p, file=sys.stderr)
    if args.json:
        with open(args.json, 'w', encoding='utf-8', newline='\n') as f:
            json.dump(result, f, indent=1, sort_keys=True)
    for v, r in result['engines'].items():
        print('%s: %d expression classes, %d asset classes' % (v, len(r['expressions']), len(r['assets'])))
    if args.check:
        try:
            with open(args.out, encoding='utf-8') as f:  # CRLF checkouts compare equal
                current = f.read()
        except OSError:
            current = ''
        if current != md:
            old, new = current.splitlines(), md.splitlines()
            old_set, new_set = set(old), set(new)
            gone = [l for l in old if l not in new_set]
            added = [l for l in new if l not in old_set]
            print('STALE: %s differs from the engine trees (%d lines only in the page, %d only in the engines)'
                  % (args.out, len(gone), len(added)), file=sys.stderr)
            for l in (['- ' + x for x in gone[:10]] + ['+ ' + x for x in added[:10]]):
                print('  ' + l[:160], file=sys.stderr)
            return 1
        return 1 if bad else 0
    with open(args.out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(md)
    print('wrote %s' % args.out)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
